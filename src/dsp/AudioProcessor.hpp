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

#include <string>
#include <iomanip>
#include <chrono>
#include <ctime>
#include "audio.hpp"
#include "RMS.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {

class Input {
public:

    const std::string name;
    
    Input(const std::string name = "") :
        name(name)
    {}
    
    virtual ~Input() = default;
    virtual void process(const sam_t* in, sam_t* out, size_t nframes) = 0;
};


template <typename T>
class PlayBuffer {
    std::atomic<size_t> mWritePos = 0;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mSize = 0;
    size_t mCapacity = 0;
    bool mOverwrite = false;
    std::vector<T> mBuffer;
    std::mutex mMutex;
    std::condition_variable mCV;

public:
    size_t readPosition() { return mReadPos; }
    size_t writePosition() { return mWritePos; }
    size_t capacity() { return mCapacity; }

    float memorySizeMB() {
        static constexpr float kibi = 1024.0f;
        static constexpr float mibi = kibi * kibi;
        float bytesz = mCapacity * sizeof(T);
        return bytesz / mibi;
    }

    void resize(size_t tCapacity, bool tOverwrite) {
        std::unique_lock<std::mutex> lock(mMutex);
        mBuffer = std::vector<sam_t>(tCapacity);
        mOverwrite = tOverwrite;
        mReadPos = 0;
        mWritePos = 0;
        mSize = 0;
        mCapacity = tCapacity;
        mCV.notify_all();
    }

    size_t write(const T* tData, size_t tLen) {
        if (!tData || tLen == 0) return 0;
        if (tLen > mCapacity) return 0;

        if (mOverwrite) {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock, [&]{ return mSize + tLen <= mCapacity || mCapacity == 0; });
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

    size_t read(T* tData, size_t tLen) {
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

        mReadPos.store((mReadPos + tLen) % mCapacity, std::memory_order_relaxed);
        mSize -= tLen;

        // mCV.notify_all();

        return tLen;
    }

    void reset() {
        // mSize = 0;
        mCapacity = 0;
    }
};


class BufferedSource {
public:
    PlayBuffer<sam_t> mBuffer;
};


class Player : public Input, public BufferedSource {
public:

    Player(const std::string& name = "") :
        Input(name),
        BufferedSource()
    {}

    bool isPlaying() const {
        return state == PLAY;
    }

    bool isFinished() const {
        return std::time(0) > (playItem.end + playItem.ejectTime + 1);
    }

    float readProgress() {
        auto capacity = static_cast<float>(mBuffer.capacity());
        if (capacity == 0) return 0;
        return static_cast<float>(mBuffer.readPosition()) / capacity;
    }

    float writeProgress() {
        auto capacity = static_cast<float>(mBuffer.capacity());
        if (capacity == 0) return 0;
        return static_cast<float>(mBuffer.writePosition()) / capacity;
    }

    enum State {
        IDLE, WAIT, LOAD, CUED, PLAY, FAIL
    };

    std::atomic<State> state = IDLE;

    State getState(const time_t& now = std::time(0)) const {
        return state;
    }

    const char* stateStr() {
        switch (state) {
            case IDLE: return "IDLE";
            case WAIT: return "WAIT";
            case LOAD: return "LOAD";
            case CUED: return "CUE ";
            case PLAY: return "PLAY";
            case FAIL: return "FAIL";
            default: throw std::runtime_error("Unknown default");
        }
    }

    void play() {
        state = PLAY;
    }

    virtual void stop() {
        state = IDLE;
        if (fadeThread.joinable()) fadeThread.join();
    }

    virtual void load(const std::string& url, double position = 0) = 0;

    PlayItem playItem = {};
    std::thread schedulingThread;
    std::atomic<bool> scheduling = true;
    std::atomic<bool> isLoaded = false;
    std::function<void(const PlayItem& playItem)> playItemDidStartCallback;

    virtual void schedule(const PlayItem& item) {
        playItem = std::move(item);
        state = WAIT;
    }


    bool needsLoad() {
        return !isLoaded && playItem.isInScheduleTime();
    }


    void tryLoad() {
        state = LOAD;
        time_t pos = std::max(0l, std::time(0) - static_cast<time_t>(playItem.start));
        try {
            load(playItem.uri, pos);
            state = CUED;
            isLoaded = true;
        }
        catch (const std::exception& e) {
            state = FAIL;
            log.error() << "AudioProcessor failed to load '" << playItem.uri << "': " << e.what();
        }
    }


    std::atomic<bool> isFading = false;
    std::atomic<float> volume = 0;

    void setVolume(float vol, bool exp) {
        if (vol < 0) {
            log.debug() << "AudioProcessor " << name << " volume < 0";
            vol = 0;
        } else if (vol > 1) {
            log.debug() << "AudioProcessor " << name << " volume > 1";
            vol = 1;
        }
        volume = exp ? vol*vol : vol;
        // log.debug() << "AudioProcessor " << name << " setVolume " << volume;
    }

    void fadeIn() {
        fade(true, playItem.fadeInTime);
    }

    void fadeOut() {
        fade(false, playItem.fadeOutTime);
    }

    std::thread fadeThread;

    void fade(bool increase, double duration) {
        if (isFading) {
            log.error() << "Player is already fading";
            return;
        }
        isFading = true;
        // log.debug() << name << " fade start " << duration << " sec.";
        if (fadeThread.joinable()) fadeThread.join();
        fadeThread = std::thread([increase, duration, this] {
            auto niters = static_cast<int>(duration * 100);
            float incr = 1.0f / niters;
            if (!increase) incr *= -1;
            float vol = increase ? 0.0f : 1.0f;
            for (auto i = 0; i < niters; ++i) {
                setVolume(vol, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                vol += incr;
                // log.debug() << "AudioProcessor " << name << " fade vol " << this->volume;
            }
            setVolume(increase ? 1 : 0, false);
            isFading = false;
            // log.debug() << name << " fade done";
        });
    }


    std::atomic<float> rms = -INFINITY;
    RMS mRMS = RMS(1, 2);

    void calcRMS(const sam_t* buffer, size_t sampleCount) {
        rms = mRMS.process(buffer, sampleCount);
    }


    void update() {
        auto now = std::time(0);
        
        if (now >= playItem.start && now <= playItem.end && state == CUED) {
            log.info(Log::Magenta) << name << " PLAY";
            play();
            log.info(Log::Magenta) << name << " FADE IN";
            fadeIn();
            if (playItemDidStartCallback) playItemDidStartCallback(playItem);
        }
        else if (now >= playItem.end - playItem.fadeOutTime && now < playItem.end && state == PLAY && !isFading) {
            log.info(Log::Magenta) << name << " FADE OUT";
            fadeOut();
        }
        else if (now >= playItem.end && state != IDLE) {
            log.info(Log::Magenta) << name << " STOP";
            stop();
        }
  
    }
};
}
}