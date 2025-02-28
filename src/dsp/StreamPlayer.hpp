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
    static constexpr size_t kRingBufferTimeHint = 30;
    
    const double mSampleRate;
    const size_t mRingBufferSize;
    std::thread mLoadWorker;
    std::unique_ptr<CodecReader> mReader = nullptr;

public:
    StreamPlayer(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate),
        mRingBufferSize(util::nextMultiple(mSampleRate * kChannelCount * kRingBufferTimeHint, 4096))
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
            auto alignsz = util::nextMultiple(sampleCount, 4096);
            mBuffer.resize(alignsz, false);
        } else {
            mBuffer.resize(mRingBufferSize, true);
        }

        if (mLoadWorker.joinable()) mLoadWorker.join();
        mLoadWorker = std::thread([this] {
            mReader->read(mBuffer);
            // mReader = nullptr;
        });

        if (sampleCount > 0) {
            mLoadWorker.join();
        }
    }

    void stop() override {
        log.debug() << "StreamPlayer stop...";
        Player::stop();
        scheduling = false;
        if (mReader) mReader->cancel();
        if (mLoadWorker.joinable()) mLoadWorker.join();
        mReader = nullptr;
        // mBuffer.reset();
        log.debug() << "StreamPlayer stopped";
    }

    
    void process(const sam_t*, sam_t* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto samplesRead = mBuffer.read(tBuffer, sampleCount);
        auto samplesLeft = sampleCount - samplesRead;
        if (samplesLeft > 0) {
            memset(tBuffer + samplesRead, 0, samplesLeft * sizeof(sam_t));
        }
        // calcRMS(tBuffer, sampleCount);
    }
};
}
}