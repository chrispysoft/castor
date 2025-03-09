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

#include <atomic>
#include <string>
#include <thread>
#include "AudioProcessor.hpp"
#include "CodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace audio {

template <typename T>
class StreamBuffer : public SourceBuffer<T> {
    std::atomic<size_t> mWritePos = 0;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mSize = 0;
    size_t mCapacity = 0;
    bool mOverwrite = false;
    std::vector<T> mBuffer;
    std::mutex mMutex;
    std::condition_variable mCV;

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

    void resize(size_t tCapacity, bool tOverwrite) override {
        mOverwrite = tOverwrite;
        mReadPos = 0;
        mWritePos = 0; // mOverwrite ? tCapacity / 2 : 0;
        mSize = 0;
        mBuffer.resize(tCapacity);
        std::lock_guard<std::mutex> lock(mMutex);
        mCapacity = tCapacity;
        mCV.notify_all();
    }

    size_t write(const T* tData, size_t tLen) override {
        if (!tData || tLen == 0) return 0;
        if (tLen > mCapacity) return 0;

        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock, [&]{ return mSize + tLen < mCapacity || mCapacity == 0; });
        }

        size_t freeSpace = mCapacity - mSize.load(std::memory_order_relaxed);
        if (tLen > freeSpace) {
            if (!mOverwrite) return 0;
            mReadPos.store((mReadPos + tLen) % mCapacity, std::memory_order_relaxed);
            mSize -= tLen;
        }

        auto writable = std::min(tLen, mCapacity - mWritePos);
        memcpy(&mBuffer[mWritePos], tData, writable * sizeof(T));

        auto overlap = tLen - writable;
        if (overlap > 0) {
            // log.debug() << "Expected overlap in write of " << overlap;
            memcpy(&mBuffer[0], tData + writable, overlap * sizeof(T));
        }

        mWritePos.store((mWritePos + tLen) % mCapacity, std::memory_order_relaxed);
        mSize += tLen;
        return tLen;
    }

    size_t read(T* tData, size_t tLen) override {
        if (!tData || tLen == 0) return 0;

        // std::unique_lock<std::mutex> lock(mMutex); // NB if realtime thread

        auto available = mSize.load(std::memory_order_relaxed);
        if (tLen > available) return 0;

        auto readable = std::min(tLen, mCapacity - mReadPos);
        memcpy(tData, &mBuffer[mReadPos], readable * sizeof(T));

        auto overlap = tLen - readable;
        if (overlap > 0) {
            memcpy(tData + readable, &mBuffer[0], overlap * sizeof(T));
            // log.debug() << "Unexpected overlap in read";
        }

        std::lock_guard<std::mutex> lock(mMutex);

        mReadPos.store((mReadPos + tLen) % mCapacity, std::memory_order_relaxed);
        mSize -= tLen;

        mCV.notify_all();

        return tLen;
    }
};

class StreamPlayer : public Player {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kBufferTimeHint = 15;
    
    const double mSampleRate;
    const size_t mBufferSize;
    std::thread mLoadWorker;
    std::unique_ptr<CodecReader> mReader = nullptr;

    StreamBuffer<sam_t> mStreamBuffer;

public:
    StreamPlayer(float tSampleRate, const std::string tName = "", time_t tPreloadTime = 0) :
        Player(tSampleRate, tName, tPreloadTime),
        mSampleRate(tSampleRate),
        mBufferSize(util::nextMultiple(mSampleRate * kChannelCount * kBufferTimeHint, 2048))
    {
        category = "STRM";
        mBuffer = &mStreamBuffer;
    }
    
    // ~StreamPlayer() {
    //     log.debug() << "StreamPlayer " << name << " dealloc...";
    //     if (state != IDLE) stop();
    //     log.debug() << "StreamPlayer " << name << " dealloced";
    // }

    void load(const std::string& tURL, double tSeek = 0) override {
        log.info() << "StreamPlayer load " << tURL;
        // eject();
        mStreamBuffer.resize(mBufferSize, true);

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(mSampleRate, tURL);
        
        playItem->metadata = mReader->metadata();

        mLoadWorker = std::thread([this] {
            mReader->read(mStreamBuffer);
        });
    }

    void play() override {
        // mStreamBuffer.align();
        Player::play();
    }

    void stop() override {
        log.debug() << "StreamPlayer stop...";
        Player::stop();
        if (mReader) mReader->cancel();
        mStreamBuffer.resize(0, false);
        if (mLoadWorker.joinable()) mLoadWorker.join();
        mReader = nullptr;
        
        log.debug() << "StreamPlayer stopped";
    }

};
}
}