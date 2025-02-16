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
#include <string>
#include <thread>
#include "AudioProcessor.hpp"
#include "CodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {
class StreamPlayer : public Player {

    static constexpr size_t kChannelCount = 2;
    const size_t kRingBufferSize = util::align16byte(44100 * 2 * 60);

    const double mSampleRate;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mSampleCount = 0;
    std::string mCurrURL = "";
    std::thread mLoadWorker;
    std::unique_ptr<CodecReader> mReader = nullptr;
    util::RingBuffer<sam_t> mRingBuffer;

public:
    StreamPlayer(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {}
    
    ~StreamPlayer() {
        log.debug() << "StreamPlayer " << name << " dealloc...";
        if (state != IDLE) stop();
        log.debug() << "StreamPlayer " << name << " dealloced";
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "StreamPlayer load " << tURL << " position " << seek;
        // eject();

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(mSampleRate, tURL, seek);

        auto sampleCount = mReader->sampleCount();
        if (sampleCount > 0) {
            mSampleCount = sampleCount;
            mRingBuffer.resize(mSampleCount * 2);
        }

        if (mLoadWorker.joinable()) mLoadWorker.join();
        mLoadWorker = std::thread([this] {
            mReader->read(mRingBuffer);
            mReader = nullptr;
        });
    }

    void stop() override {
        log.debug() << "StreamPlayer stop...";
        scheduling = false;
        if (mReader) mReader->cancel();
        if (mLoadWorker.joinable()) mLoadWorker.join();
        state = IDLE;
        mReader = nullptr;
        mReadPos = 0;
        mCurrURL = "";
        mRingBuffer.flush();
        log.debug() << "StreamPlayer stopped";
    }

    
    void process(const sam_t*, sam_t* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        if (sampleCount <= mRingBuffer.size()) {
            auto samplesRead = mRingBuffer.read(tBuffer, sampleCount);
            mReadPos += samplesRead;
            if (samplesRead == sampleCount) {
                // log.debug() << "read " << samplesRead << " from ringbuffer";
            } else {
                // log.debug() << "0 bytes read"; 
                memset(tBuffer, 0, sampleCount * sizeof(sam_t));
            }
        } else {
            memset(tBuffer, 0, sampleCount * sizeof(sam_t));
        }
        // calcRMS(tBuffer, sampleCount);
    }
};
}
}