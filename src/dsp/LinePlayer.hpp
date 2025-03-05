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
#include "AudioProcessor.hpp"

namespace castor {
namespace audio {
class LinePlayer : public Player {

    static constexpr size_t kChannelCount = 2;
    const double mSampleRate;
    

public:
    LinePlayer(double tSampleRate, const std::string& tName = "", time_t tPreloadTime = 0) :
        Player(tName, tPreloadTime),
        mSampleRate(tSampleRate)
    {
        category = "LINE";
    }

    // ~LinePlayer() {
    //     log.debug() << "LinePlayer " << name << " dealloc...";
    //     if (state != IDLE) stop();
    //     log.debug() << "LinePlayer " << name << " dealloced";
    // }

    // void schedule(const PlayItem& item) override {
    //     playItem = std::move(item);
    //     state = CUED;
    // }

    void load(const std::string& tURL, double seek = 0) override {}

    void process(const sam_t* tInBuffer, sam_t* tOutBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto byteSize = sampleCount * sizeof(sam_t);
        memcpy(tOutBuffer, tInBuffer, byteSize);
        // calcRMS(tOutBuffer, sampleCount);
    }
};
}
}