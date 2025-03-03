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
        mBufferSize(util::nextMultiple(mSampleRate * kChannelCount * kBufferTimeHint, 16))
    {
        preloadTime = 10;
    }
    
    ~StreamPlayer() {
        // log.debug() << "StreamPlayer " << name << " dealloc...";
        // if (state != IDLE) stop();
        // log.debug() << "StreamPlayer " << name << " dealloced";
    }

    void load(const std::string& tURL, double tSeek = 0) override {
        log.info() << "StreamPlayer load " << tURL;
        // eject();
        mBuffer.resize(mBufferSize, true);

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(mSampleRate, tURL);
        mLoadWorker = std::thread([this] {
            mReader->read(mBuffer);
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
        mBuffer.resize(0, false);
        if (mLoadWorker.joinable()) mLoadWorker.join();
        mReader = nullptr;
        
        log.debug() << "StreamPlayer stopped";
    }

};
}
}