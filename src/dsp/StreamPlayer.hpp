/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <atomic>
#include <bit>
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
    std::atomic<bool> mCancelled = false;
    std::atomic<size_t> mWritePos = 0;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mSize = 0;
    size_t mCapacity = 0;
    size_t mCapacityMask = 0;
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

    void resize(size_t tCapacity) override {
        assert(tCapacity == 0 || std::has_single_bit(tCapacity)); // allow 0 or pow2s only
        mReadPos = 0;
        mWritePos = 0;
        mSize = 0;
        mBuffer.resize(tCapacity);
        mCapacity = tCapacity;
        mCapacityMask = mCapacity > 0 ? mCapacity - 1 : 0;
        mCancelled = false;
    }
    
    void cancel() {
        mCancelled.store(true, std::memory_order_release);
        mCV.notify_all();
    }

    size_t write(const T* tData, size_t tLen) override {
        if (tLen > mCapacity) return 0;

        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock, [&]{ return mSize.load(std::memory_order_acquire) + tLen < mCapacity || mCancelled.load(std::memory_order_acquire); });
        }

        if (mCancelled) return 0;

        auto writable = std::min(tLen, mCapacity - mWritePos);
        memcpy(&mBuffer[mWritePos], tData, writable * sizeof(T));

        auto overlap = tLen - writable;
        if (overlap > 0) {
            log.warn() << "Unexpected buffer data overlap in write";
            memcpy(&mBuffer[0], tData + writable, overlap * sizeof(T));
        }

        mWritePos.store((mWritePos.load(std::memory_order_relaxed) + tLen) & mCapacityMask, std::memory_order_relaxed);
        mSize.store(mSize.load(std::memory_order_relaxed) + tLen, std::memory_order_release);
        return tLen;
    }

    size_t read(T* tData, size_t tLen) override {
        if (!tData || tLen == 0) return 0;

        auto available = mSize.load(std::memory_order_relaxed);
        if (tLen > available) return 0;

        auto readable = std::min(tLen, mCapacity - mReadPos);
        memcpy(tData, &mBuffer[mReadPos], readable * sizeof(T));

        auto overlap = tLen - readable;
        if (overlap > 0) {
            log.warn() << "Unexpected buffer data overlap in read";
            memcpy(tData + readable, &mBuffer[0], overlap * sizeof(T));
        }

        mReadPos.store((mReadPos.load(std::memory_order_relaxed) + tLen) & mCapacityMask, std::memory_order_relaxed);
        mSize.store(mSize.load(std::memory_order_relaxed) - tLen, std::memory_order_release);

        mCV.notify_one();

        return tLen;
    }
};

class StreamPlayer : public Player {

    static constexpr size_t kStreamBufferSize = 65536 * 4 * 2; // use pow2s rather than durations for optimzed % (≈ 6 sec @ 44.1k, 2 ch)

    const size_t mBufferSize;
    std::thread mLoadWorker;
    std::unique_ptr<CodecReader> mReader = nullptr;

    StreamBuffer<sam_t> mStreamBuffer;

public:
    StreamPlayer(const AudioStreamFormat& tClientFormat, const std::string tName = "", time_t tPreloadTime = 0, float tFadeInTime = 0, float tFadeOutTime = 0) :
        Player(tClientFormat, tName, tPreloadTime, tFadeInTime, tFadeOutTime),
        mBufferSize(tClientFormat.channelCount * kStreamBufferSize)
    {
        mStreamBuffer.resize(mBufferSize);
        category = "STRM";
        mBuffer = &mStreamBuffer;
    }
    
    ~StreamPlayer() {
        log.debug() << "StreamPlayer " << name << " dealloc...";
        if (state != IDLE) stop();
        log.debug() << "StreamPlayer " << name << " dealloced";
    }

    void load(const std::string& tURL, double tSeek = 0) override {
        log.info() << "StreamPlayer load " << tURL;
        // eject();

        if (mReader) mReader->cancel();
        mReader = std::make_unique<CodecReader>(clientFormat, tURL);
        
        if (playItem) playItem->metadata = mReader->metadata();

        mLoadWorker = std::thread([this] {
            mReader->read(mStreamBuffer);
        });
    }

    void play() override {
        // mStreamBuffer.align();
        // log.debug() << "StreamPlayer buffer pos write / read / capacity: " << mStreamBuffer.writePosition() << " / " << mStreamBuffer.readPosition() << " / " << mStreamBuffer.capacity();
        Player::play();
    }

    void stop() override {
        log.debug() << "StreamPlayer " << name << " stop...";
        Player::stop();
        mStreamBuffer.cancel();
        if (mReader) mReader->cancel();
        if (mLoadWorker.joinable()) mLoadWorker.join();
        mReader = nullptr;
        
        log.debug() << "StreamPlayer " << name << " stopped";
    }

};
}
}