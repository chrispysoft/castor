#pragma once

#include <atomic>
#include "Log.hpp"
#include "util.hpp"

namespace cst {
class Recorder {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 16384;
    static constexpr size_t kPipeBufferSize = 512;

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
            log.debug() << "Recorder already running";
            return;
        }

        if (mWorker && mWorker->joinable()) {
            mWorker->join();
        }

        mRunning = true;

        log.info() << "Recorder start " << tURL;
        
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
                // log.debug() << "wrote " << bytesWritten << " bytes";

                if (bytesWritten != kPipeBufferSize * sizeof(float)) {
                    log.error() << "Error writing to pipe";
                    break;
                }

                fflush(pipe);
            }
            pclose(pipe);
            mRunning = false;
            log.info() << "Recorder finished";
        });
    }

    void stop() {
        mRunning = false;
        if (mWorker && mWorker->joinable()) {
            mWorker->join();
        }
        mWorker = nullptr;
        log.info() << "Recorder stopped";
    }


    void process(const float* tSamples, size_t nframes) {
        mRingBuffer.write(tSamples, nframes * kChannelCount);
    }

};
}