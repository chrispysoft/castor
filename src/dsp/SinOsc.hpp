#pragma once

#include <numbers>
#include <cmath>

class SinOsc {
    
    double mSampleRate;
    double mOmega = 0.0;
    double mDeltaOmega = 0.0;
    
public:
    
    SinOsc(double sampleRate = 44100.0) :
        mSampleRate(sampleRate)
    {}

    void setFrequency(double frequency) {
        mDeltaOmega = frequency / mSampleRate;
    }
    
    void reset() {
        mOmega = 0;
    }

    double process() {
        const double sample = std::sin(mOmega * (M_PI * 2.0));
        mOmega += mDeltaOmega;

        if (mOmega >= 1.0) { mOmega -= 1.0; }
        return sample;
    }
};
