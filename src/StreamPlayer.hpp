#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include "AudioSource.hpp"
#include "util.hpp"

namespace lap {
class StreamPlayer : public AudioSource {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 1024 * 1024;
    static constexpr size_t kPipeBufferSize = 4096;
    static constexpr size_t kMinRenderBufferSize = 16384;


    const double mSampleRate;
    util::RingBuffer<float> mRingBuffer;

public:
    StreamPlayer(double tSampleRate, size_t tRingBufferSize = kRingBufferSize) :
        mSampleRate(tSampleRate),
        mRingBuffer(tRingBufferSize)
    {

    }

    ~StreamPlayer() override {
        
    }

    void open(const std::string& tURL) override {
        std::cout << "StreamPlayer open " << tURL << std::endl;
        
        std::string command = "ffmpeg -i \"" + tURL + "\" -ac " + std::to_string(kChannelCount) + " -ar " + std::to_string(int(mSampleRate)) + " -channel_layout stereo -f f32le - 2>/dev/null";
        // std::cout << command << std::endl;
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to open pipe to ffmpeg." << std::endl;
            return;
        }

        std::vector<float> pipeBuffer(kPipeBufferSize);
        size_t bytesRead;
        while ((bytesRead = fread(pipeBuffer.data(), 1, kPipeBufferSize * sizeof(float), pipe)) > 0) {
            mRingBuffer.write(pipeBuffer.data(), bytesRead / sizeof(float));
            // std::cout << "wrote " << bytesRead << " bytes" << std::endl;
        }

        pclose(pipe);

        std::cout << "StreamPlayer finished" << std::endl;
    }

    void roll(double) override {
        std::cout << "StreamPlayer can't roll" << std::endl;
    }

    void clear() override {
        
    }
    
    void process(const float*, float* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto byteSize = sampleCount * sizeof(float);

        if (mRingBuffer.size() < kMinRenderBufferSize) {
            // std::cout << "mRingBuffer.size " << mRingBuffer.size() << std::endl;
            memset(tBuffer, 0, byteSize);
            return;
        }
        
        size_t bytesRead = mRingBuffer.read(tBuffer, sampleCount);
        if (bytesRead) {
            // std::cout << "read " << bytesRead << " from ringbuffer" << std::endl;
            // mReadPos += sampleCount;
        } else {
            std::cout << "0 bytes read" << std::endl; 
            memset(tBuffer, 0, byteSize);
        }
    }
};
}
