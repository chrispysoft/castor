/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <atomic>
#include <fstream>
#include <vector>
#include <stdexcept>
#include "CodecWriter.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {
class Recorder {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 65536;

    const double mSampleRate;
    util::RingBuffer<sam_t> mRingBuffer;
    std::unique_ptr<CodecWriter> mWriter = nullptr;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning = false;

public:
    Recorder(double tSampleRate) :
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {}

    void start(const std::string tURL, const std::unordered_map<std::string, std::string>& tMetadata = {}) {
        log.info(Log::Magenta) << "Recorder start";
        if (mRunning) {
            log.debug() << "Recorder already running";
            return;
        }

        mWriter = std::make_unique<CodecWriter>(mSampleRate, tURL, tMetadata);
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


    void process(const sam_t* in, size_t nframes) {
        auto nsamples = nframes * kChannelCount;
        mRingBuffer.write(in, nsamples);
    }
};
}
}