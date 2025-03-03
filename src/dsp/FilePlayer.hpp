/*
 *  Copyright (C) 2024-2025 Christoph Pastl
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

#include <string>
#include <thread>
#include "AudioProcessor.hpp"
#include "CodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {

class FilePlayer : public Player {

    static constexpr size_t kChannelCount = 2;
    
    const double mSampleRate;
    std::unique_ptr<CodecReader> mReader = nullptr;

public:
    FilePlayer(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate)
    {
        preloadTime = 3600;
    }
    
    ~FilePlayer() {
        // log.debug() << "FilePlayer " << name << " dealloc...";
        // if (state != IDLE) stop();
        // log.debug() << "FilePlayer " << name << " dealloced";
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "FilePlayer load " << tURL << " position " << seek;
        // eject();

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(mSampleRate, tURL, seek);

        auto sampleCount = mReader->sampleCount();
        auto alignsz = util::nextMultiple(sampleCount, 2048);
        mBuffer.resize(alignsz, false);

        mReader->read(mBuffer);
        mReader = nullptr;

        log.debug() << "FilePlayer load done " << tURL;
    }

    void stop() override {
        log.debug() << "FilePlayer stop...";
        Player::stop();
        if (mReader) mReader->cancel();
        mReader = nullptr;
        // mBuffer.reset();
        log.debug() << "FilePlayer stopped";
    }
};
}
}