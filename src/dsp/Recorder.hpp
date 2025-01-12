#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <atomic>
#include "CodecWriter.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace cst {
namespace audio {
class Recorder {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 65536;

    const double mSampleRate;
    util::RingBuffer<float> mRingBuffer;
    std::unique_ptr<CodecWriter> mWriter = nullptr;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning = false;

public:

    Recorder(double tSampleRate) :
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {
        
    }

    void start(const std::string tURL) {
        log.info(Log::Magenta) << "Recorder start";
        if (mRunning) {
            log.debug() << "Recorder already running";
            return;
        }
        
        mWriter = std::make_unique<CodecWriter>(mSampleRate, tURL);
        mWorker = std::make_unique<std::thread>([this] {
            log.debug() << "Recorder worker started";
            mRunning = true;
            try {
                mWriter->write(mRingBuffer);
            }
            catch (const std::exception& e) {
                log.error() << "Recorder error: " << e.what();
            }
            mRunning = false;
            log.debug() << "Recorder worker finished";
        });
    }

    void stop() {
        if (!mRunning) {
            return;
        }
        log.debug() << "Recorder stopping...";
        mRunning = false;
        if (mWriter) mWriter->cancel();
        if (mWorker && mWorker->joinable()) mWorker->join();
        mWriter = nullptr;
        mWorker = nullptr;
        mRingBuffer.flush();
        log.info(Log::Magenta) << "Recorder stopped";
    }

    bool isRunning() {
        return mRunning;
    }


    void process(const float* in, size_t nframes) {
        auto nsamples = nframes * kChannelCount;
        // if (mRingBuffer.size() + nsamples > kRingBufferSize) {
        //     log.warn() << "Recorder ring buffer overflow";
        //     mRingBuffer.flush();
        //     return;
        // }
        mRingBuffer.write(in, nsamples);
    }
};
}
}