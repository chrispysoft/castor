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
    std::atomic<size_t> mCapacity = 0;
    std::atomic<size_t> mWritePos = 0;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<bool> mOverwrite = false;
    std::vector<T> mBuffer = {};

public:
    size_t readPosition() { return mReadPos; }
    size_t writePosition() { return mWritePos; }
    size_t capacity() { return mCapacity; }

    float memorySizeMB() {
        static constexpr float denum = 1024;
        float bytesz = mCapacity * sizeof(T);
        float mb = bytesz / denum / denum;
        return mb;
    }

    size_t remaining() {
        return mCapacity - mWritePos;
    }

    void resize(size_t tCapacity, bool tOverwrite) {
        mBuffer.resize(tCapacity);
        mCapacity = tCapacity;
        mOverwrite = tOverwrite;
    }

    size_t write(const T* tData, size_t tLen) {
        if (mWritePos + tLen > mCapacity) {
            if (mOverwrite) {
                mWritePos = 0;
                if (tLen > mCapacity) return 0;
            }
            else return 0;
        }

        T* dst = &mBuffer[mWritePos];
        memcpy(dst, tData, tLen * sizeof(T));

        mWritePos += tLen;

        return tLen;
    }

    size_t read(T* tData, size_t tLen) {
        if (mReadPos + tLen > mCapacity) {
            if (mOverwrite) {
                mReadPos = 0;
                if (tLen > mCapacity) return 0;
            }
            else return 0;
        }

        T* src = &mBuffer[mReadPos];
        memcpy(tData, src, tLen * sizeof(T));

        mReadPos += tLen;

        return tLen;
    }

    void flush() {
        // std::unique_lock<std::mutex> lock(mMutex);
        mWritePos = 0;
        mReadPos = 0;
        // memset(mBuffer.data(), 0, mBuffer.size() * sizeof(T));
        mBuffer = {};
        //mCV.notify_all();
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

    ~Player() {
        scheduling = false;
        if (schedulingThread.joinable()) schedulingThread.join();
    }

    bool isPlaying() const {
        return state == PLAY;
    }

    bool isFinished() const {
        return std::time(0) > (playItem.end + playItem.ejectTime + 1);
    }

    float readProgress() {
        return static_cast<float>(mBuffer.readPosition()) / static_cast<float>(mBuffer.capacity());
    }

    float writeProgress() {
        return static_cast<float>(mBuffer.writePosition()) / static_cast<float>(mBuffer.capacity());
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
            case CUED: return "CUED";
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
    std::function<void(const PlayItem& playItem)> playItemDidStartCallback;

    virtual void schedule(const PlayItem& item) {
        playItem = std::move(item);

        if (schedulingThread.joinable()) schedulingThread.join();
        schedulingThread = std::thread([this] {
            state = WAIT;
            while (scheduling && playItem.isPriorSchedulingTime()) {
                util::sleepCancellable(1, scheduling);
            }

            state = LOAD;
            while (scheduling && playItem.isInScheduleTime() && state != CUED) {
                time_t pos = std::max(0l, std::time(0) - static_cast<time_t>(playItem.start));
                try {
                    load(playItem.uri, pos);
                    state = CUED;
                }
                catch (const std::exception& e) {
                    state = FAIL;
                    log.error() << "AudioProcessor failed to load '" << playItem.uri << "': " << e.what();
                    util::sleepCancellable(playItem.retryInterval, scheduling);
                }
            }
        });
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