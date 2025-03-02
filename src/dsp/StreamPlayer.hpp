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
    static constexpr size_t kBufferTimeHint = 15;
    
    const double mSampleRate;
    const size_t mBufferSize;
    std::thread mLoadWorker;
    std::unique_ptr<CodecReader> mReader = nullptr;

public:
    StreamPlayer(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate),
        mBufferSize(util::nextMultiple(mSampleRate * kChannelCount * kBufferTimeHint, 2048))
    {}
    
    ~StreamPlayer() {
        // log.debug() << "StreamPlayer " << name << " dealloc...";
        // if (state != IDLE) stop();
        // log.debug() << "StreamPlayer " << name << " dealloced";
    }

    void load(const std::string& tURL, double tSeek = 0) override {
        log.info() << "StreamPlayer load " << tURL;
        // eject();

        mBuffer.resize(mBufferSize, true);

        //if (mLoadWorker.joinable()) mLoadWorker.join();
        
        mLoadWorker = std::thread([this, url=tURL, seek=tSeek] {
            if (mReader) mReader->cancel();
            mReader = std::make_unique<CodecReader>(mSampleRate, url, seek);
            mReader->read(mBuffer);
            mReader = nullptr;
        });
    }

    void play() override {
        mBuffer.align();
        Player::play();
    }

    void stop() override {
        log.debug() << "StreamPlayer stop...";
        Player::stop();
        if (mReader) mReader->cancel();
        if (mLoadWorker.joinable()) mLoadWorker.join();
        mReader = nullptr;
        // mBuffer.reset();
        log.debug() << "StreamPlayer stopped";
    }

    std::vector<sam_t> mMixBuffer = std::vector<sam_t>(2048);

    void process(const sam_t*, sam_t* tBuffer, size_t tFrameCount) override {
        // if (volume == 0) return; // render cycle might start when vol is still 0

        auto sampleCount = tFrameCount * kChannelCount;
        auto samplesRead = mBuffer.read(mMixBuffer.data(), sampleCount);
        auto samplesLeft = sampleCount - samplesRead;
        if (samplesLeft > 0) {
            memset(mMixBuffer.data() + samplesRead, 0, samplesLeft * sizeof(sam_t));
        }

        if (volume == 1) {
            memcpy(tBuffer, mMixBuffer.data(), sampleCount * sizeof(sam_t));
        } else {
            for (auto i = 0; i < sampleCount; ++i) {
                float s = static_cast<float>(mMixBuffer[i]) * volume;
                if      (s > std::numeric_limits<sam_t>::max()) s = std::numeric_limits<sam_t>::max();
                else if (s < std::numeric_limits<sam_t>::min()) s = std::numeric_limits<sam_t>::min();
                tBuffer[i] = static_cast<sam_t>(s); 
            }
        }

        // calcRMS(tBuffer, sampleCount);
    }
};
}
}