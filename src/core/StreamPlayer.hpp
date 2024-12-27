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
#include "../common/Log.hpp"
#include "../common/util.hpp"

namespace cst {
class StreamPlayer : public AudioProcessor {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 1024 * 512;
    static constexpr size_t kPipeBufferSize = 512;

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

    bool canPlay(const PlayItem& item) override {
        return item.uri.starts_with("http");
    }

    void load(const std::string& tURL, double seek = 0) override {
        using namespace std;

        if (mRunning) {
            log.debug() << "StreamPlayer already open";
            return;
        }

        mRunning = true;
        if (mReadThread && mReadThread->joinable()) {
            mReadThread->join();
        }

        log.info() << "StreamPlayer open " << tURL;
        
        string command = "ffmpeg -i \"" + tURL + "\" -ac " + to_string(kChannelCount) + " -ar " + to_string(int(mSampleRate)) + " -channel_layout stereo -f f32le - 2>/dev/null";
        // log.debug() << command << endl;
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            throw runtime_error("Failed to open pipe");
        }

        mReadThread = make_unique<thread>([this, pipe] {
            vector<float> rxBuf(kPipeBufferSize);
            size_t bytesRead;
            while (this->mRunning && (bytesRead = fread(rxBuf.data(), 1, kPipeBufferSize * sizeof(float), pipe)) > 0) {
               this->mRingBuffer.write(rxBuf.data(), bytesRead / sizeof(float));
                // log.debug() << "wrote " << bytesRead << " bytes";
            }
            pclose(pipe);
            mRunning = false;
            log.info() << "StreamPlayer finished";
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
        log.info() << "StreamPlayer stopped";
    }
    
    void process(const float*, float* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto samplesRead = mRingBuffer.read(tBuffer, sampleCount);
        if (samplesRead == sampleCount) {
            // log.debug() << "read " << samplesRead << " from ringbuffer";
        } else {
            // log.debug() << "0 bytes read"; 
            memset(tBuffer, 0, sampleCount * sizeof(float));
        }
    }
};
}
