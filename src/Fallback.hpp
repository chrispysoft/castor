#pragma once

#include "SinOsc.hpp"
#include "StreamPlayer.hpp"

namespace lap {
class Fallback {
    static constexpr double kGain = 0.125;
    
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
        mOscL.setFrequency(1000);
        mOscR.setFrequency(1000 * (5.0 / 4.0));
    }

    void start() {
        if (mActive) return;
        std::cout << "Fallback start" << std::endl;
        mActive = true;
        mStreamPlayer.open("http://stream.fro.at/fro128.mp3");
    }

    void stop() {
        if (!mActive) return;
        std::cout << "Fallback stop" << std::endl;
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