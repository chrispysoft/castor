/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <atomic>
#include <fstream>
#include <vector>
#include <stdexcept>
#include "CodecWriter.hpp"
#include "audio.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {
class Recorder {

    static constexpr size_t kRingBufferSize = 65536;

    const AudioStreamFormat& mClientFormat;
    const int mBitRate;
    util::RingBuffer<sam_t> mRingBuffer;
    std::unique_ptr<CodecWriter> mWriter = nullptr;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning = false;

public:

    std::string logName = "Recorder";

    Recorder(const AudioStreamFormat& tClientFormat, int tBitRate) :
        mClientFormat(tClientFormat),
        mBitRate(tBitRate),
        mRingBuffer(kRingBufferSize)
    {}

    void start(const std::string tURL, const std::unordered_map<std::string, std::string>& tMetadata = {}) {
        log.debug(Log::Magenta) << logName << " start...";
        if (mRunning) {
            log.debug() << logName << " already running";
            return;
        }

        mWriter = std::make_unique<CodecWriter>(mClientFormat, mBitRate, tURL, tMetadata);
        mRunning = true;

        log.info(Log::Magenta) << logName << " started";

        mWorker = std::make_unique<std::thread>([this] {
            try {
                mWriter->write(mRingBuffer);
            }
            catch (const std::exception& e) {
                log.error() << logName << " error: " << e.what();
            }
            mRunning = false;
            log.info() << logName << " finished";
        });
    }

    void stop() {
        if (!mRunning) {
            return;
        }
        log.debug() << logName << " stopping...";
        mRunning = false;
        if (mWriter) mWriter->cancel();
        if (mWorker && mWorker->joinable()) mWorker->join();
        mWriter = nullptr;
        mWorker = nullptr;
        mRingBuffer.flush();
        log.info(Log::Magenta) << logName << " stopped";
    }

    bool isRunning() {
        return mRunning;
    }


    void process(const sam_t* in, size_t nframes) {
        auto nsamples = nframes * mClientFormat.channelCount;
        mRingBuffer.write(in, nsamples);
    }
};
}
}