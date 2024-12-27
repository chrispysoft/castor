#pragma once

#include <iostream>
#include <string>
#include "AudioProcessor.hpp"

namespace cst {
class LinePlayer : public AudioProcessor {

    static constexpr size_t kChannelCount = 2;
    const double mSampleRate;
    

public:
    LinePlayer(double tSampleRate) :
        mSampleRate(tSampleRate)
    {
        
    }

    ~LinePlayer() override {
        
    }

    void load(const std::string& tURL, double seek = 0) override {}

    bool canPlay(const PlayItem& item) override {
        return item.uri.starts_with("line");
    }

    void process(const float* tInBuffer, float* tOutBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto byteSize = sampleCount * sizeof(float);
        memcpy(tOutBuffer, tInBuffer, byteSize);
    }
};
}
