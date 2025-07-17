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
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Lesser General Public License.
 */

#pragma once

#include <string>
#include "AudioProcessor.hpp"

namespace castor {
namespace audio {

template <typename T>
class LineBuffer : public SourceBuffer<T> {
    const T* mBufferPtr;

public:
    size_t write(const T* tData, size_t tLen) override {
        mBufferPtr = tData;
        return tLen;
    }

    size_t read(T* tData, size_t tLen) override {
        memcpy(tData, mBufferPtr, tLen * sizeof(T));
        return tLen;
    }
};
    
class LinePlayer : public Player {

    LineBuffer<sam_t> mLineBuffer;

public:
    LinePlayer(const AudioStreamFormat& tClientFormat, const std::string& tName = "", time_t tPreloadTime = 0, float tFadeInTime = 0, float tFadeOutTime = 0) :
        Player(tClientFormat, tName, tPreloadTime, tFadeInTime, tFadeOutTime)
    {
        category = "LINE";
        mBuffer = &mLineBuffer;
    }

    ~LinePlayer() {
        log.debug() << "LinePlayer " << name << " dealloc...";
        if (state != IDLE) stop();
        log.debug() << "LinePlayer " << name << " dealloced";
    } 

    void load(const std::string& tURL, double seek = 0) override {}

    size_t process(const sam_t* tInBuffer, sam_t* tOutBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * clientFormat.channelCount;
        mLineBuffer.write(tInBuffer, sampleCount);

        return Player::process(tInBuffer, tOutBuffer, tFrameCount);
    }
};
}
}