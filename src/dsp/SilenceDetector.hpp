/*
 *  Copyright (C) 2024-2025 Christoph Pastl
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
#include <ctime>
#include <atomic>
#include <limits>
#include <thread>
#include "../util/Log.hpp"

namespace castor {
namespace audio {
class SilenceDetector {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kBufferSize = 65536;
    
    const float mThresholdLin; // linear (avoids log10 in compute thread)
    const time_t mStartDuration;
    const time_t mStopDuration;
    size_t mBufferReadIdx = 0;
    std::atomic<size_t> mBufferWriteIdx = 0;
    std::atomic<time_t> mSilenceStart = 0;
    std::atomic<time_t> mSilenceStop = 0;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mSilence = false;
    std::atomic<float> mCurrRMS = 0;
    std::thread mWorker;
    std::mutex mMutex;
    std::condition_variable mCV;
    std::vector<sam_t> mBuffer;
    
public:
    SilenceDetector(float tThreshold, time_t tStartDuration, time_t tStopDuration) :
        mThresholdLin(util::dbLinear(tThreshold)),
        mStartDuration(tStartDuration),
        mStopDuration(tStopDuration),
        mBuffer(kBufferSize)
    {
        mRunning = true;
        mWorker = std::thread(&SilenceDetector::work, this);
    }

    ~SilenceDetector() {
        mRunning.exchange(false, std::memory_order_release);
        mCV.notify_all();
        if (mWorker.joinable()) mWorker.join();
    }
    
    bool silenceDetected() const {
        return mSilence;
    }

    float currentRMS() const {
        return mCurrRMS;
    }

    
    void work() {
        const auto halfSz = mBuffer.size() / 2;

        while (mRunning) {
            // wait until first or second part is written
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCV.wait(lock, [&] {
                    auto writeIdx = mBufferWriteIdx.load(std::memory_order_acquire);
                    return  (writeIdx == 0 && mBufferReadIdx == halfSz) ||
                            (writeIdx == halfSz && mBufferReadIdx == 0) ||
                            !mRunning.load(std::memory_order_acquire);
                });
            }
            if (!mRunning) return;

            // read safe area without locking
            float sqSum = 0;
            for (auto i = mBufferReadIdx; i < mBufferReadIdx + halfSz; ++i) sqSum += mBuffer[i] * mBuffer[i];
            mBufferReadIdx += halfSz;
            if (mBufferReadIdx >= mBuffer.size()) mBufferReadIdx = 0;

            // time for "expensive" operations
            sqSum /= halfSz;
            mCurrRMS = sqSum > 0 ? sqrt(sqSum) : 0;

            calcSilence();

            // log.debug() << "SilenceDetector work done " << util::linearDB(mCurrRMS);
        }
    }

    void calcSilence() {
        auto now = std::time(0);
        bool silence = mCurrRMS < mThresholdLin;
        if (silence) {
            if (mSilenceStart == 0) {
                mSilenceStart = now;
            } else {
                if (now - mSilenceStart > mStartDuration) {
                    mSilenceStop = 0;
                    mSilence = true;
                }
            }
        } else {
            if (mSilenceStop == 0) {
                mSilenceStop = now;
            } else {
                if (now - mSilenceStop > mStopDuration) {
                    mSilenceStart = 0;
                    mSilence = false;
                }
            }
        }
    }

    
    void process(const sam_t* in, size_t nframes) {
        auto nsamples = nframes * kChannelCount;
        memcpy(mBuffer.data() + mBufferWriteIdx, in, nsamples * sizeof(sam_t));

        auto newIdx = mBufferWriteIdx + nsamples;
        if (newIdx >= mBuffer.size()) newIdx = 0;
        // log.debug() << newIdx;

        mBufferWriteIdx.store(newIdx, std::memory_order_release);
        mCV.notify_one();
    }

};
}
}
