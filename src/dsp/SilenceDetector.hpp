#pragma once

#include <iostream>
#include <ctime>
#include <atomic>
#include <limits>
#include "../util/Log.hpp"

namespace cst {
namespace audio {
class SilenceDetector {

    static constexpr size_t kChannelCount = 2;
    
    const float mThreshold;
    const time_t mStartDuration;
    const time_t mStopDuration;
    std::atomic<time_t> mSilenceStart = 0;
    std::atomic<time_t> mSilenceStop = 0;
    
public:
    SilenceDetector(float tThreshold, time_t tStartDuration, time_t tStopDuration) :
        mThreshold(tThreshold),
        mStartDuration(tStartDuration),
        mStopDuration(tStopDuration)
    {
        
    }

    ~SilenceDetector() {
        
    }

    void process(const sam_t* in, size_t nframes) {
        float rms = util::rms_dB(in, nframes * kChannelCount);
        bool silence = rms <= mThreshold;
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

};
}
}