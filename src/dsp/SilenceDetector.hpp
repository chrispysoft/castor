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
    static constexpr float kThreshold = -90;
    static constexpr float kStartDuration = 10;
    static constexpr float kStopDuration = 1;
    
    const float mThreshold;
    const time_t mStartDuration;
    const time_t mStopDuration;
    std::atomic<time_t> mSilenceStart = 0;
    std::atomic<time_t> mSilenceStop = 0;
    
public:
    SilenceDetector(float tThreshold = kThreshold, time_t tStartDuration = kStartDuration, time_t tStopDuration = kStopDuration) :
        mThreshold(tThreshold),
        mStartDuration(tStartDuration),
        mStopDuration(tStopDuration)
    {
        
    }

    ~SilenceDetector() {
        
    }

    static inline float rms_dB(const float* tBlock, size_t tNumFrames, size_t tNumChannels) {
        const auto nsamples = tNumFrames * tNumChannels;
        float rms = 0;
        for (auto i = 0; i < nsamples; ++i) {
            rms += tBlock[i] * tBlock[i];
        }
        rms = sqrt(rms / nsamples);
        float dB = (rms > 0) ? 20 * log10(rms) : -std::numeric_limits<float>::infinity();
        // log.debug() << "RMS lin: " << rms << " dB: " << dB;
        return dB;
    }

    void process(const float* in, size_t nframes) {
        float rms = rms_dB(in, nframes, kChannelCount);
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