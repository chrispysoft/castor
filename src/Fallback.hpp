#pragma once

#include "SinOsc.hpp"

namespace lap {
class Fallback {
    static constexpr double kGain = 0.25;
    
    SinOsc mOscL;
    SinOsc mOscR;
public:
    Fallback(double tSampleRate) :
        mOscL(tSampleRate),
        mOscR(tSampleRate)
    {
        mOscL.setFrequency(432);
        mOscR.setFrequency(432 * (5.0 / 4.0));
    }

    void process(const float* in, float* out, size_t nframes) {
        for (auto i = 0; i < nframes; ++i) {
            out[i*2]   = mOscL.process() * kGain;
            out[i*2+1] = mOscR.process() * kGain;
        }
    }
};
}