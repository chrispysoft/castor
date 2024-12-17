#pragma once

#include <iostream>
#include <ctime>
#include <atomic>

namespace cst {
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

    void process(const float* in, size_t nframes) {
        const auto nsamples = nframes * kChannelCount;
        float rms = 0;
        for (auto i = 0; i < nsamples; ++i) {
            rms += abs(in[i]);
        }
        rms /= nsamples;
        rms = 20 * log10(rms);
        // std::cout << rms << std::endl;
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