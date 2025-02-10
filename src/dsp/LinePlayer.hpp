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

#include <iostream>
#include <string>
#include "AudioProcessor.hpp"

namespace cst {
namespace audio {
class LinePlayer : public Player {

    static constexpr size_t kChannelCount = 2;
    const double mSampleRate;
    

public:
    LinePlayer(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate)
    {
        
    }

    ~LinePlayer() {

    }

    void load(const std::string& tURL, double seek = 0) override {}

    bool canPlay(const PlayItem& item) override {
        return item.uri.starts_with("line");
    }

    void process(const sam_t* tInBuffer, sam_t* tOutBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto byteSize = sampleCount * sizeof(sam_t);
        memcpy(tOutBuffer, tInBuffer, byteSize);
        calcRMS(tOutBuffer, sampleCount);
    }
};
}
}