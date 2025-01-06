#pragma once

#include <numbers>
#include <cmath>

class SineOscillator {

    static constexpr double k2Pi = 2 * M_PI;
    
    const double mSampleRate;
    double mOmega = 0.0;
    double mDeltaOmega = 0.0;
    
public:
    
    SineOscillator(double tSampleRate = 44100.0) :
        mSampleRate(tSampleRate)
    {}

    void setFrequency(double tFrequency) {
        mDeltaOmega = tFrequency / mSampleRate;
    }
    
    void reset() {
        mOmega = 0;
    }

    double process() {
        const double sample = std::sin(mOmega * k2Pi);
        mOmega += mDeltaOmega;

        if (mOmega >= 1.0) { mOmega -= 1.0; }
        return sample;
    }
};
