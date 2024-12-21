#pragma once

#include "SinOsc.hpp"
#include "StreamPlayer.hpp"
#include "../common/Log.hpp"

namespace cst {
class Fallback {
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;

    SinOsc mOscL;
    SinOsc mOscR;
    StreamPlayer mStreamPlayer;
    bool mActive;

public:
    Fallback(double tSampleRate) :
        mOscL(tSampleRate),
        mOscR(tSampleRate),
        mStreamPlayer(tSampleRate)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));
    }

    void start() {
        if (mActive) return;
        log.warn() << "Fallback start";
        mActive = true;
        mStreamPlayer.open("http://stream.fro.at/fro-128.ogg");
    }

    void stop() {
        if (!mActive) return;
        log.info() << "Fallback stop";
        mActive = false;
        mStreamPlayer.stop();
    }

    bool isActive() {
        return mActive;
    }

    void process(const float* in, float* out, size_t nframes) {
        mStreamPlayer.process(in, out, nframes);
        
        for (auto i = 0; i < nframes; ++i) {
            out[i*2]   += mOscL.process() * kGain;
            out[i*2+1] += mOscR.process() * kGain;
        }
    }
};
}