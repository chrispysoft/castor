#pragma once

#include <atomic>
#include "util.hpp"

namespace lap {
class Recorder {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 16384;
    static constexpr size_t kPipeBufferSize = 4096;

    const double mSampleRate;
    util::RingBuffer<float> mRingBuffer;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mEnabled = false;

public:

    Recorder(double tSampleRate) :
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {
    }

    bool isRunning() {
        return mRunning;
    }

    void start(const std::string& tURL) {
        using namespace std;

        if (mRunning) {
            cout << "Recorder already running" << endl;
            return;
        }

        if (mWorker && mWorker->joinable()) {
            mWorker->join();
        }

        mRunning = true;

        cout << "Recorder open " << tURL << endl;
        
        string command = "ffmpeg -f f32le -channel_layout stereo -ac " + to_string(kChannelCount) + " -ar " + to_string(int(mSampleRate)) + " -i - -codec:a libmp3lame -q:a 2 -f mp3 - >> " + tURL + " 2>&1";

        FILE* pipe = popen(command.c_str(), "w");
        if (!pipe) {
            throw runtime_error("Failed to open pipe");
        }

        mWorker = make_unique<thread>([this, pipe] {
            vector<float> rxBuf(kPipeBufferSize);
            vector<float> txBuf(kPipeBufferSize);
            size_t bytesRead, bytesWritten;

            while (this->mRunning) {

                // bytesRead = fread(rxBuf.data(), 1, kPipeBufferSize * sizeof(float), pipe)) > 0) {

                if (this->mRingBuffer.size() < kPipeBufferSize) continue;

                this->mRingBuffer.read(txBuf.data(), kPipeBufferSize);

                bytesWritten = fwrite(txBuf.data(), 1, kPipeBufferSize * sizeof(float), pipe);
                // cout << "wrote " << bytesWritten << " bytes" << endl;

                if (bytesWritten != kPipeBufferSize * sizeof(float)) {
                    cout << "Error writing to pipe" << endl;
                    break;
                }

                fflush(pipe);
            }
            pclose(pipe);
            mRunning = false;
            cout << "Recorder finished" << endl;
        });
    }

    void stop() {
        mRunning = false;
        if (mWorker && mWorker->joinable()) {
            mWorker->join();
        }
        mWorker = nullptr;
        std::cout << "Recorder stopped" << std::endl;
    }


    void process(const float* tSamples, size_t nframes) {
        mRingBuffer.write(tSamples, nframes * kChannelCount);
    }

};
}