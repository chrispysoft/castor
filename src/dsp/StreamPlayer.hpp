/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "AudioProcessor.hpp"
#include "CodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace cst {
namespace audio {
class StreamPlayer : public Player {

    static size_t align16byte(size_t val) {
        if (val & (16-1)) {
            return val + (16 - (val & (16-1)));
        }
        return val;
    }

    static constexpr size_t kChannelCount = 2;
    const size_t kRingBufferSize = align16byte(44100 * 2 * 60 * 10);

    const double mSampleRate;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mSampleCount = 0;
    std::string mCurrURL = "";
    double mDuration;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mLoading = false;
    std::unique_ptr<CodecReader> mReader = nullptr;
    util::RingBuffer<sam_t> mRingBuffer;
    

public:
    StreamPlayer(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {

    }
    
    ~StreamPlayer() {
        if (state != IDLE) stop();
    }
    
    std::string currentURL() {
        return mCurrURL;
    }

    bool canPlay(const PlayItem& item) override {
        return item.uri.starts_with("http") || item.uri.starts_with("/") || item.uri.starts_with("./");
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "StreamPlayer load " << tURL << " position " << seek;
        // eject();
        state = LOAD;
        mLoading = true;
        try {
            if (mReader) mReader->cancel();
            mReader = std::make_unique<CodecReader>(mSampleRate, tURL, seek);
            mSampleCount = mReader->sampleCount();
            std::thread([this] {
                this->mReader->read(this->mRingBuffer);
                this->mReader = nullptr;
            }).detach();
            state = CUE;
            mLoading = false;
            mCondition.notify_one();
        }
        catch (const std::runtime_error& e) {
            eject();
            mLoading = false;
            mCondition.notify_one();
            throw e;
        }
    }

    void stop() override {
        eject();
    }

    void eject() {
        log.debug() << "StreamPlayer eject...";
        state = IDLE;
        if (mReader) mReader->cancel();
        mReader = nullptr;
        //std::lock_guard lock(mMutex);
        mReadPos = 0;
        mCurrURL = "";
        mRingBuffer.flush();
        log.info() << "StreamPlayer ejected";
    }


    bool isIdle() {
        // log.debug() << mReadPos << " " << mSampleCount;
        return state == IDLE || mReadPos >= mSampleCount;
    }

    
    void process(const sam_t*, sam_t* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        if (sampleCount <= mRingBuffer.size()) {
            auto samplesRead = mRingBuffer.read(tBuffer, sampleCount);
            mReadPos += samplesRead;
            if (samplesRead == sampleCount) {
                // log.debug() << "read " << samplesRead << " from ringbuffer";
            } else {
                // log.debug() << "0 bytes read"; 
                memset(tBuffer, 0, sampleCount * sizeof(sam_t));
            }
        } else {
            memset(tBuffer, 0, sampleCount * sizeof(sam_t));
        }
        calcRMS(tBuffer, sampleCount);
    }
};
}
}