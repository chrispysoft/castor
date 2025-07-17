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
#include <thread>
#include "AudioProcessor.hpp"
#include "CodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {

template <typename T>
class FileBuffer : public SourceBuffer<T> {
protected:
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mWritePos = 0;
    size_t mCapacity = 0;
    std::vector<T> mBuffer;

public:
    size_t readPosition() override { return mReadPos; }
    size_t writePosition() override { return mWritePos; }
    size_t capacity() override { return mCapacity; }

    float memorySizeMiB() override {
        static constexpr float kibi = 1024.0f;
        static constexpr float mibi = kibi * kibi;
        float bytesz = mCapacity * sizeof(T);
        return bytesz / mibi;
    }

    void resize(size_t tCapacity) override {
        mReadPos = 0;
        mWritePos = 0;
        auto pagesize = sysconf(_SC_PAGE_SIZE);
        auto bufsize = util::nextMultiple(tCapacity, pagesize / sizeof(sam_t));
        mBuffer.resize(bufsize);
        mCapacity = tCapacity;
    }

    size_t write(const T* tData, size_t tLen) override {
        auto writable = std::min(tLen, mCapacity - mWritePos);
        if (writable == 0) return 0;
        memcpy(&mBuffer[mWritePos], tData, writable * sizeof(T));
        mWritePos += writable;
        return writable;
    }

    size_t read(T* tData, size_t tLen) override {
        auto readable = std::min(tLen, mWritePos - mReadPos);
        if (readable == 0) return 0;
        memcpy(tData, &mBuffer[mReadPos], readable * sizeof(T));
        mReadPos += readable;

        return readable;
    }
};

class FilePlayer : public Player {

    FileBuffer<sam_t> mFileBuffer;
    std::unique_ptr<CodecReader> mReader = nullptr;

public:
    FilePlayer(const AudioStreamFormat& tClientFormat, const std::string tName = "", time_t tPreloadTime = 0, float tFadeInTime = 0, float tFadeOutTime = 0) :
        Player(tClientFormat, tName, tPreloadTime, tFadeInTime, tFadeOutTime)
    {
        category = "FILE";
        mBuffer = &mFileBuffer;
    }
    
    ~FilePlayer() {
        log.debug() << "FilePlayer " << name << " dealloc...";
        if (state != IDLE) stop();
        log.debug() << "FilePlayer " << name << " dealloced";
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "FilePlayer load " << tURL << " position " << seek;
        // eject();

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(clientFormat, tURL, seek);

        if (playItem) playItem->metadata = mReader->metadata();

        auto sampleCount = mReader->sampleCount();
        mFileBuffer.resize(sampleCount);
        mReader->read(mFileBuffer);
        mReader = nullptr;

        log.debug() << "FilePlayer load done " << tURL;
    }

    void stop() override {
        log.debug() << "FilePlayer " << name << " stop...";
        Player::stop();
        if (mReader) mReader->cancel();
        mReader = nullptr;
        // mBuffer.reset();
        log.debug() << "FilePlayer " << name << " stopped";
    }
};
}
}