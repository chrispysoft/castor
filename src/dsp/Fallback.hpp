#pragma once

#include "SineOscillator.hpp"
#include "QueuePlayer.hpp"
#include "../util/Log.hpp"

namespace cst {
namespace audio {
class Fallback : public Input {
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;

    SineOscillator mOscL;
    SineOscillator mOscR;
    QueuePlayer mQueuePlayer;
    std::string mFallbackURL;
    bool mActive;

public:
    Fallback(double tSampleRate, const std::string tFallbackURL) : Input(),
        mOscL(tSampleRate),
        mOscR(tSampleRate),
        mQueuePlayer(tSampleRate),
        mFallbackURL(tFallbackURL)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));
    }

    void start() {
        if (mActive) return;
        log.info(Log::Yellow) << "Fallback start";
        mActive = true;
        // mQueuePlayer.load(mFallbackURL);
    }

    void stop() {
        if (!mActive) return;
        log.info(Log::Yellow) << "Fallback stop";
        mActive = false;
        // mQueuePlayer.clear();
    }

    bool isActive() {
        return mActive;
    }

    void process(const float* in, float* out, size_t nframes) {
        // mQueuePlayer.process(in, out, nframes);
        
        for (auto i = 0; i < nframes; ++i) {
            out[i*2]   += mOscL.process() * kGain;
            out[i*2+1] += mOscR.process() * kGain;
        }
    }
};
}
}