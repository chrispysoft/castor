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
#include <ctime>
#include <atomic>
#include <limits>
#include "RMS.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {
class SilenceDetector {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRMSCycleCount = 4;
    
    const float mThreshold;
    const time_t mStartDuration;
    const time_t mStopDuration;
    RMS mRMS;
    std::atomic<float> mCurrRMS = -std::numeric_limits<float>::infinity();
    std::atomic<time_t> mSilenceStart = 0;
    std::atomic<time_t> mSilenceStop = 0;
    
public:
    SilenceDetector(float tThreshold, time_t tStartDuration, time_t tStopDuration) :
        mThreshold(tThreshold),
        mStartDuration(tStartDuration),
        mStopDuration(tStopDuration),
        mRMS(kRMSCycleCount, kChannelCount)
    {}

    
    void process(const sam_t* in, size_t nframes) {
        mCurrRMS = mRMS.process(in, nframes);
        // std::cout << mCurrRMS << std::endl;
        bool silence = mCurrRMS <= mThreshold;
        if (silence) {
            if (mSilenceStart == 0) {
                mSilenceStart = std::time(0);
            }
        } else {
            mSilenceStart = 0;
        }
    }

    bool silenceDetected() const {
        if (mSilenceStart == 0) return false;
        auto now = std::time(0);
        auto duration = now - mSilenceStart;
        return duration > mStartDuration;
    }

    float currentRMS() {
        return mCurrRMS;
    }

};
}
}