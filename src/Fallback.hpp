#pragma once

#include "SinOsc.hpp"

namespace lap {
class Fallback {
    SinOsc mOscL;
    SinOsc mOscR;
public:
    Fallback(double tSampleRate) :
        mOscL(tSampleRate),
        mOscR(tSampleRate)
    {
        mOscL.setFrequency(432);
        mOscR.setFrequency(432 + (432.0/12.0*3));
    }

    void process(const float* in, float* out, size_t nframes) {
        for (auto i = 0; i < nframes; ++i) {
            float sL = mOscL.process();
            float sR = mOscR.process();
            out[i*2] = sL;
            out[i*2+1] = sR;
        }
    }
};
}