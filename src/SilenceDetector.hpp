#include <iostream>
#include <ctime>
#include <atomic>

namespace lap {
class SilenceDetector {

    static constexpr float kDefaultThreshold = -90;
    static constexpr time_t kDefaultSilenceDuration = 5;
    static constexpr size_t kDefaultChannelCount = 2;

    const float mThreshold;
    const size_t mChannelCount;
    const time_t mSilenceDuration;
    std::atomic<time_t> mSilenceStart = 0;
    
public:
    SilenceDetector(float tThreshold = kDefaultThreshold, time_t tSilenceDuration = kDefaultSilenceDuration, size_t tChannelCount = kDefaultChannelCount) :
        mThreshold(tThreshold),
        mSilenceDuration(tSilenceDuration),
        mChannelCount(tChannelCount),
        mSilenceStart(0)
    {
        
    }

    ~SilenceDetector() {
        
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
        return duration > mSilenceDuration;
    }

};
}