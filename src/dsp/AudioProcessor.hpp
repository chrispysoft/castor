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
class SourceBuffer {
public:
    virtual size_t readPosition() { return 0; }
    virtual size_t writePosition() { return 0; }
    virtual size_t capacity() { return 0; }
    virtual float memorySizeMB() { return 0; }
    virtual void align() {}
    virtual void resize(size_t tCapacity, bool tOverwrite) {}
    virtual size_t write(const T* tData, size_t tLen) { return 0; }
    virtual size_t read(T* tData, size_t tLen) { return 0; }
};


class BufferedSource {
public:
    SourceBuffer<sam_t>* mBuffer;
};


class Fader {
public:
    std::vector<float> fadeInCurve;
    std::vector<float> fadeOutCurve;
    std::atomic<int> fadeInCurveIndex = -1;
    std::atomic<int> fadeOutCurveIndex = -1;

    Fader(float fadeInTime, float fadeOutTime, float sampleRate) :
        fadeInCurve(fadeInTime * sampleRate),
        fadeOutCurve(fadeOutTime * sampleRate)
    {
        generateFadeCurves();
    }

    void generateFadeCurves() {
        auto size = fadeOutCurve.size();
        float denum = size - 1;
        for (auto i = 0; i < size; ++i) {
            float vol = i / denum;
            fadeInCurve[i] = vol * vol;
        }

        size = fadeOutCurve.size();
        denum = size - 1;
        for (auto i = 0; i < fadeOutCurve.size(); ++i) {
            float vol = (denum-i) / denum;
            fadeOutCurve[i] = vol * vol;
        }
    }
};


class Player : public Input, public BufferedSource, public Fader {
public:

    Player(const std::string& name = "") :
        Input(name),
        BufferedSource(),
        Fader(2, 2, 44100)
    {
        generateFadeCurves();
    }

    ~Player() {
        if (schedulingThread.joinable()) schedulingThread.join();
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

    virtual void play() {
        state = PLAY;
    }

    virtual void stop() {
        std::lock_guard<std::mutex> lock(scheduleMutex);
        state = IDLE;
        isScheduling = false;
        scheduleCV.notify_all();
    }

    virtual void load(const std::string& url, double position = 0) = 0;

    PlayItem playItem = {};
    std::thread schedulingThread;
    std::atomic<bool> isLoaded = false;
    std::function<void(const PlayItem& playItem)> playItemDidStartCallback;
    std::mutex scheduleMutex;
    std::condition_variable scheduleCV;
    bool isScheduling = false;

    virtual void schedule(const PlayItem& item) {
        playItem = item;
        state = WAIT;
        isScheduling = true;
        schedulingThread = std::thread(&Player::waitForEvents, this);
    }

    void waitForEvents() {
        auto unlockTime1 = std::chrono::system_clock::from_time_t(playItem.start);
        auto unlockTime2 = std::chrono::system_clock::from_time_t(playItem.end - 2);
        
        {
            std::unique_lock<std::mutex> lock(scheduleMutex);
            scheduleCV.wait_until(lock, unlockTime1, [this] { return !isScheduling; });
            if (!isScheduling) return;

            log.debug(Log::Magenta) << "PLAY " << name;
            play();
            log.info(Log::Magenta) << "FADE IN " << name;
            fadeInCurveIndex = 0;

            scheduleCV.wait_until(lock, unlockTime2, [this] { return !isScheduling; });
            if (!isScheduling) return;

            log.info(Log::Magenta) << "FADE OUT " << name;
            fadeOutCurveIndex = 0;
        }
    }

    
    time_t preloadTime = 0;
    time_t loadRetryInterval = 3;
    time_t lastLoadAttempt = 0;

    bool isInLoadTime() {
        auto now = std::time(0);
        auto min = playItem.start - preloadTime;
        auto max = playItem.end - 5;
        return now >= min && now <= max;
    }

    bool needsLoad() {
        return !isLoaded && isInLoadTime() && (std::time(0) > lastLoadAttempt+loadRetryInterval);
    }

    // bool isInPlayTime() const {
    //     auto now = std::time(0);
    //     return now >= playItem.start && now < playItem.end; 
    // }


    bool isPlaying() const {
        return isLoaded && state == PLAY;
    }

    bool isFinished() const {
        return std::time(0) > (playItem.end + 1);
    }


    float readProgress() {
        auto capacity = static_cast<float>(mBuffer->capacity());
        if (capacity == 0) return 0;
        return static_cast<float>(mBuffer->readPosition()) / capacity;
    }

    float writeProgress() {
        auto capacity = static_cast<float>(mBuffer->capacity());
        if (capacity == 0) return 0;
        return static_cast<float>(mBuffer->writePosition()) / capacity;
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
            lastLoadAttempt = std::time(0);
            log.error() << "AudioProcessor failed to load '" << playItem.uri << "': " << e.what();
        }
    }
    

    // temporary fade workaround

    virtual void process(const sam_t* in, sam_t* out, size_t nframes) override {

        if (fadeInCurveIndex == -1 || fadeOutCurveIndex == -2) return; // don't process if not started fade in yet or fade out done

        auto sampleCount = nframes * 2;
        auto samplesRead = mBuffer->read(out, sampleCount);
        auto samplesLeft = sampleCount - samplesRead;

        if (fadeInCurveIndex >= 0) {
            // log.debug() << "fade in";
            for (auto i = 0; i < samplesRead/2 && fadeInCurveIndex < fadeInCurve.size(); ++i) {
                out[i*2+0] *= fadeInCurve[fadeInCurveIndex]; 
                out[i*2+1] *= fadeInCurve[fadeInCurveIndex++]; 
            }

            if (fadeInCurveIndex >= fadeInCurve.size()) fadeInCurveIndex = -2;
        }

        else if (fadeOutCurveIndex >= 0) {
            // log.debug() << "fade out";
            size_t i;
            for (i = 0; i < samplesRead/2 && fadeOutCurveIndex < fadeOutCurve.size(); ++i) {
                out[i*2+0] *= fadeOutCurve[fadeOutCurveIndex]; 
                out[i*2+1] *= fadeOutCurve[fadeOutCurveIndex++]; 
            }

            if (fadeOutCurveIndex >= fadeOutCurve.size()) {
                while (i < samplesRead/2) {
                    out[i*2+0] = 0;
                    out[i*2+1] = 0;
                    ++i;
                }
                fadeOutCurveIndex = -2;
            }
        }
    }
};
}
}