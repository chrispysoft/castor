#pragma once

#include "SinOsc.hpp"
#include "QueuePlayer.hpp"
#include "StreamPlayer.hpp"
#include "../common/Log.hpp"

namespace cst {
class Fallback {
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;

    SinOsc mOscL;
    SinOsc mOscR;
    QueuePlayer mQueuePlayer;
    StreamPlayer mStreamPlayer;
    std::string mFallbackURL;
    bool mActive;

public:
    Fallback(double tSampleRate, const std::string tFallbackURL) :
        mOscL(tSampleRate),
        mOscR(tSampleRate),
        mQueuePlayer(tSampleRate),
        mStreamPlayer(tSampleRate),
        mFallbackURL(tFallbackURL)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));
    }

    void start() {
        if (mActive) return;
        log.warn() << "Fallback start";
        mActive = true;
        // if (mFallbackURL.starts_with("http")) {
        //     mStreamPlayer.load(mFallbackURL);
        // } else {
        //     mQueuePlayer.load(mFallbackURL);
        // }
    }

    void stop() {
        if (!mActive) return;
        log.warn() << "Fallback stop";
        mActive = false;
        // mStreamPlayer.stop();
        // mQueuePlayer.clear();
    }

    bool isActive() {
        return mActive;
    }

    void process(const float* in, float* out, size_t nframes) {
        // mStreamPlayer.process(in, out, nframes);
        // mQueuePlayer.process(in, out, nframes);
        
        for (auto i = 0; i < nframes; ++i) {
            out[i*2]   += mOscL.process() * kGain;
            out[i*2+1] += mOscR.process() * kGain;
        }
    }
};
}