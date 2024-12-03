#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <condition_variable>
#include "AudioProcessor.hpp"
#include "util.hpp"

namespace lap {
class StreamPlayer : public AudioProcessor {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 1024 * 1024;
    static constexpr size_t kPipeBufferSize = 4096;

    const double mSampleRate;
    util::RingBuffer<float> mRingBuffer;
    std::unique_ptr<std::thread> mReadThread = nullptr;
    std::atomic<bool> mRunning = false;

public:
    StreamPlayer(double tSampleRate, size_t tRingBufferSize = kRingBufferSize) :
        mSampleRate(tSampleRate),
        mRingBuffer(tRingBufferSize)
    {

    }

    ~StreamPlayer() override {
        stop();
    }

    void open(const std::string& tURL) {
        using namespace std;

        if (mRunning) {
            cout << "StreamPlayer already open" << endl;
            return;
        }

        cout << "StreamPlayer open " << tURL << endl;
        
        string command = "ffmpeg -i \"" + tURL + "\" -ac " + to_string(kChannelCount) + " -ar " + to_string(int(mSampleRate)) + " -channel_layout stereo -f f32le - 2>/dev/null";
        // cout << command << endl;
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw runtime_error("Failed to open pipe");
        }

        mRunning = true;
        if (mReadThread && mReadThread->joinable()) {
            mReadThread->join();
        }
        mReadThread = make_unique<thread>([this, pipe] {
            vector<float> rxBuf(kPipeBufferSize);
            size_t bytesRead;
            while (this->mRunning && (bytesRead = fread(rxBuf.data(), 1, kPipeBufferSize * sizeof(float), pipe)) > 0) {
               this->mRingBuffer.write(rxBuf.data(), bytesRead / sizeof(float));
                // cout << "wrote " << bytesRead << " bytes" << endl;
            }
            pclose(pipe);
            mRunning = false;
            cout << "StreamPlayer finished" << endl;
        });
    }

    void start() {
        
    }

    void stop() {
        mRunning = false;
        if (mReadThread && mReadThread->joinable()) {
            mReadThread->join();
        }
        mReadThread = nullptr;
        std::cout << "StreamPlayer stopped" << std::endl;
    }
    
    void process(const float*, float* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto samplesRead = mRingBuffer.read(tBuffer, sampleCount);
        if (samplesRead == sampleCount) {
            // std::cout << "read " << samplesRead << " from ringbuffer" << std::endl;
        } else {
            // std::cout << "0 bytes read" << std::endl; 
            memset(tBuffer, 0, sampleCount * sizeof(float));
        }
    }
};
}
