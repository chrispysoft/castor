#pragma once

#include "AudioSource.hpp"
#include <iostream>
#include <string>

namespace lap {
class LinePlayer : public AudioSource {

    static constexpr size_t kChannelCount = 2;
    const double mSampleRate;
    

public:
    LinePlayer(double tSampleRate) :
        mSampleRate(tSampleRate)
    {
        
    }

    ~LinePlayer() override {
        
    }

    void open(const std::string& tURL) override {
        
    }

    void roll(double pos) override {
        
    }

    void clear() override {
        
    }

    
    void process(const float* tInBuffer, float* tOutBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto byteSize = sampleCount * sizeof(float);
        //memcpy(tOutBuffer, tInBuffer, byteSize);
    }
};
}
