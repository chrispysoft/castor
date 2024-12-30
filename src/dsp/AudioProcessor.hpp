#pragma once

#include <string>
#include "../util/Log.hpp"

namespace cst {

class AudioProcessor {
public:
    AudioProcessor(const std::string& name = "") :
        name(name)
    {}
    
    virtual ~AudioProcessor() = default;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
    bool isActive(const time_t& now = time(0)) { return state == PLAY; } // now >= tsStart && now <= tsEnd; };

    std::string name;

    bool accepts(const PlayItem& item) {
        return canPlay(item) && state == IDLE;
    }

    virtual void stop() {}


    void play() {
        state = PLAY;
    }

    enum State {
        IDLE, LOAD, CUE, PLAY
    };

    State state = IDLE;

    State getState(const time_t& now = std::time(0)) const {
        return state;
        // if (now < tsStart) return QUEUED;
        // if (now >= tsStart && now <= tsEnd) return PLAYING;
        // return DONE;
    }

    time_t tsStart;
    time_t tsEnd;

    std::shared_ptr<PlayItem> playItem = nullptr;

    virtual bool canPlay(const PlayItem& item) = 0;
    virtual void load(const std::string& url, double position = 0) = 0;
    void schedule(const PlayItem& item) {
        playItem = std::make_shared<PlayItem>(item);
        tsStart = item.start;
        tsEnd = item.end;
        auto pos = std::time(0) - tsStart;
        if (pos < 0) pos = 0;
        state = LOAD;
        auto uri = item.uri;
        std::thread([this, uri, pos] {
            this->load(uri, pos);
            this->state = CUE;
        }).detach();
    }

    time_t fadeInTime = 1;
    time_t fadeOutTime = 3;
    time_t ejectTime = 1;
    std::atomic<bool> isFading = false;
    float volume = 0;

    void fadeIn() {
        fade(true, fadeInTime);
    }

    void fadeOut() {
        fade(false, fadeOutTime);
    }

    void fade(bool increase, time_t duration) {
        isFading = true;
        log.debug() << name << " fade start";
        std::thread([increase, duration, this] {
            if (increase) this->volume = 0;
            else this->volume = 1;
            auto niters = duration * 100;
            float incr = 1.0 / 100.0 / duration;
            if (!increase) incr *= -1;
            for (auto i = 0; i < niters; ++i) {
                this->volume += incr;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                // log.debug() << "AudioProcessor " << name << " fade vol " << this->volume;
            }
            isFading = false;
            log.debug() << name << " fade done";
        }).detach();
    }


    void work() {
        time_t now = std::time(0);
        if (now >= tsStart && now <= tsEnd && state == AudioProcessor::State::CUE) {
            log.info(Log::Magenta) << name << " PLAY";
            play();
            log.info(Log::Magenta) << name << " FADE IN";
            fadeIn();
            // playingItemChanged(*source->playItem);
        }
        else if (now >= tsEnd - fadeOutTime && now < tsEnd && state == PLAY && !isFading) {
            log.info(Log::Magenta) << name << " FADE OUT";
            fadeOut();
        }
        else if (now >= tsEnd + ejectTime && state != IDLE) {
            log.info(Log::Magenta) << name << " STOP";
            stop();
            state = IDLE;
        }
    }
};
}