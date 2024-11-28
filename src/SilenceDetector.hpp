#include <iostream>
#include <ctime>

namespace lap {
class SilenceDetector {

    static constexpr float kDefaultThreshold = -90;
    static constexpr size_t kDefaultChannelCount = 2;

    const float mThreshold;
    const size_t mChannelCount;
    const std::time_t mSilenceDuration = 3;
    std::time_t mSilenceStart = 0;
    

public:
    SilenceDetector(float tThreshold = kDefaultThreshold, size_t tChannelCount = kDefaultChannelCount) :
        mThreshold(tThreshold),
        mChannelCount(tChannelCount)
    {
        
    }

    ~SilenceDetector() {
        
    }


    bool silenceDetected() {
        if (mSilenceStart == 0) return false;
        auto now = std::time(0);
        auto duration = now - mSilenceStart;
        return duration > mSilenceDuration;
    }

    

    void process(const float* tSamples, size_t tSampleCount) {
        float rms = 0;
        for (auto i = 0; i < tSampleCount; ++i) {
            rms += abs(tSamples[i]);
        }
        rms /= tSampleCount;
        rms = 20 * log10(rms);
        // std::cout << rms << std::endl;
        auto silent = rms <= mThreshold;

        if (silent) {
            auto now = std::time(0);
            if (mSilenceStart == 0) {
                mSilenceStart = now;
            }
        } else {
            mSilenceStart = 0;
        }
    }

};
}