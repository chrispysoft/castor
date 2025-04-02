/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <string>
#include <thread>
#include <tuple>
#include <vector>
#include "AudioProcessor.hpp"
#include "CodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {

template <typename T>
class PremixBuffer : public FileBuffer<T> {
    size_t mXFadeBeginPos = INT32_MAX;
    size_t mXFadeEndPos = 0;
    size_t mFadeInCurveIdx = 0;
    size_t mFadeOutCurveIdx = 0;
    std::vector<T> mFadeInCurve;
    std::vector<T> mFadeOutCurve;

public:

    size_t write(const T* tData, size_t tLen) override {
        auto writable = std::min(tLen, this->mCapacity - this->mWritePos);
        if (writable == 0) return 0;
        if (this->mWritePos >= mXFadeBeginPos && this->mWritePos <= mXFadeEndPos - tLen) { // xfade transition
            for (auto i = 0; i < writable / 2; ++i) {
                assert(mFadeInCurveIdx+1 < mFadeInCurve.size()-1);
                assert(mFadeOutCurveIdx+1 < mFadeOutCurve.size()-1);
                auto iL = i * 2;
                auto iR = iL + 1;
                auto ch1L = this->mBuffer[this->mWritePos + iL] * mFadeOutCurve[mFadeOutCurveIdx];
                auto ch1R = this->mBuffer[this->mWritePos + iR] * mFadeOutCurve[mFadeOutCurveIdx++];
                auto ch2L = tData[iL] * mFadeInCurve[mFadeInCurveIdx];
                auto ch2R = tData[iR] * mFadeInCurve[mFadeInCurveIdx++];
                this->mBuffer[this->mWritePos + iL] = ch1L + ch2L;
                this->mBuffer[this->mWritePos + iR] = ch1R + ch2R;
            }
        } else {
            memcpy(&this->mBuffer[this->mWritePos], tData, writable * sizeof(T));
        }
        this->mWritePos += writable;
        return writable;
    }
    
    void setCrossFadeZone(size_t tXFadeBeginPos, size_t tXFadeEndPos) {
        mXFadeBeginPos = tXFadeBeginPos;
        mXFadeEndPos = tXFadeEndPos;
        this->mWritePos = mXFadeBeginPos;

        auto fadeLen = (mXFadeEndPos - mXFadeBeginPos) / 2;
        if (mFadeInCurve.size() != fadeLen) {
            mFadeInCurve.resize(fadeLen);
            float denum = fadeLen - 1;
            for (auto i = 0; i < fadeLen; ++i) {
                float vol = i / denum;
                mFadeInCurve[i] = vol * vol;
            }
        }
        if (mFadeOutCurve.size() != fadeLen) {
            mFadeOutCurve.resize(fadeLen);
            float denum = fadeLen - 1;
            for (auto i = 0; i < fadeLen; ++i) {
                float vol = (denum-i) / denum;
                mFadeOutCurve[i] = vol * vol;
            }
        }

        mFadeInCurveIdx = 0;
        mFadeOutCurveIdx = 0;
    }

    void reset() {
        this->mWritePos = 0;
        this->mReadPos = 0;
    }
};

class PremixPlayer : public Player {

    struct TrackMarker {
        size_t start;
        size_t stop;
        std::shared_ptr<PlayItem> item;
    };

    const float mCrossFadeTimeMusic;
    const float mCrossFadeTimeVoice = 1;
    const float mMaxVoiceTime = 60;
    PremixBuffer<sam_t> mPremixBuffer;
    std::unique_ptr<CodecReader> mReader = nullptr;
    std::atomic<bool> mRunning = true;
    std::thread mMonitorThread;
    std::mutex mBufferReadIdxMutex;
    std::condition_variable mBufferReadIdxCV;
    std::queue<TrackMarker> mTrackMarkers;
    double mPrevTrackDuration = 0;

public:
    PremixPlayer(const AudioStreamFormat& tClientFormat, const std::string tName = "", time_t tPreloadTime = 0, float tFadeInTime = 0, float tFadeOutTime = 0, float tCrossFadeTime = 1) :
        Player(tClientFormat, tName, tPreloadTime, tFadeInTime, tFadeOutTime),
        mCrossFadeTimeMusic(tCrossFadeTime)
    {
        auto sampleCount = clientFormat.sampleRate * clientFormat.channelCount * tPreloadTime;
        auto pagesize = sysconf(_SC_PAGE_SIZE);
        auto bufsize = util::nextMultiple(sampleCount, pagesize / sizeof(sam_t));
        log.debug() << "PremixPlayer " << name << " alloc...";
        mPremixBuffer.resize(bufsize, false);
        mBuffer = &mPremixBuffer;
        mMonitorThread = std::thread(&PremixPlayer::runMonitor, this);
        log.debug() << "PremixPlayer " << name << " alloc done";
    }
    
    ~PremixPlayer() {
        log.debug() << "PremixPlayer " << name << " dealloc...";
        mRunning = false;
        mBufferReadIdxCV.notify_all();
        if (mMonitorThread.joinable()) mMonitorThread.join();
        if (state != IDLE) stop();
        log.debug() << "PremixPlayer " << name << " dealloced";
    }

    std::function<void(std::shared_ptr<PlayItem> item)> startCallback = nullptr;

    size_t numTracks() { return mTrackMarkers.size(); }

    void eject() {
        log.info() << "PremixPlayer eject";
        mPremixBuffer.reset();
        while (mTrackMarkers.size()) mTrackMarkers.pop();
        mPrevTrackDuration = 0;
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "PremixPlayer load " << tURL << " position " << seek;

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(clientFormat, tURL, seek);

        int writePos = mPremixBuffer.writePosition();
        auto sampleCount = mReader->sampleCount();
        auto duration = round(mReader->duration());

        if (writePos + sampleCount >= mPremixBuffer.capacity()) {
            log.debug() << "Track duration exceeds buffer size";
            throw 0; // std::runtime_error("Buffer limit reached");
        }

        if (!playItem) {
            log.debug() << "PremixPlayer create play item...";
            playItem = std::make_shared<PlayItem>(0, duration, tURL);
        }

        if (playItem) playItem->metadata = mReader->metadata();

        auto xfadeOutTime = mPrevTrackDuration > mMaxVoiceTime ? mCrossFadeTimeMusic : mCrossFadeTimeVoice;
        auto xfadeInTime = duration > mMaxVoiceTime ? mCrossFadeTimeMusic : mCrossFadeTimeVoice;
        log.debug() << "PremixPlayer using crossfade times: " << xfadeOutTime << ", " << xfadeInTime;

        long xfadeOutSamples = clientFormat.sampleRate * clientFormat.channelCount * xfadeOutTime;
        long xfadeInSamples = clientFormat.sampleRate * clientFormat.channelCount * xfadeInTime;
        long xfadeBegin = static_cast<long>(writePos) - xfadeOutSamples;
        if (xfadeBegin < 0) xfadeBegin = 0;
        auto xfadeEnd = writePos + xfadeInSamples;
        mPremixBuffer.setCrossFadeZone(xfadeBegin, xfadeEnd);

        mReader->read(mPremixBuffer);
        mPrevTrackDuration = duration;
        mReader = nullptr;

        size_t trackBeg = writePos;
        size_t trackEnd = mPremixBuffer.writePosition() - 1;
        mTrackMarkers.push({trackBeg, trackEnd, playItem});

        log.debug() << "PremixPlayer load done " << tURL;
    }

    void stop() override {
        log.debug() << "PremixPlayer " << name << " stop...";
        Player::stop();
        if (mReader) mReader->cancel();
        mReader = nullptr;
        log.debug() << "PremixPlayer " << name << " stopped";
    }


    size_t process(const sam_t* in, sam_t* out, size_t nframes) override {
        auto processed = Player::process(in, out, nframes);
        mBufferReadIdxCV.notify_one();
        return processed;
    }


private:
    void runMonitor() {
        while (mRunning) {
            monitor();
        }
    }

    void monitor() {
        {
            std::unique_lock<std::mutex> lock(mBufferReadIdxMutex);
            mBufferReadIdxCV.wait(lock, [&] { return !mRunning || mTrackMarkers.size() && mTrackMarkers.front().start < mPremixBuffer.readPosition(); });
            if (!mRunning) return;
        }

        auto marker = mTrackMarkers.front();
        log.info() << "PremixPlayer passed track marker start: " << marker.start;
        if (startCallback) startCallback(marker.item);

        {
            std::unique_lock<std::mutex> lock(mBufferReadIdxMutex);
            mBufferReadIdxCV.wait(lock, [&] { return !mRunning || mTrackMarkers.size() && mTrackMarkers.front().stop <= mPremixBuffer.readPosition(); });
            if (!mRunning) return;
        }

        log.debug() << "PremixPlayer passed track marker stop: " << marker.stop;

        mTrackMarkers.pop();
    }
};
}
}