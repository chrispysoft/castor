#pragma once

#include <string>
#include "../util/Log.hpp"

namespace cst {
namespace audio {

class Input {
public:

    const std::string name;
    
    Input(const std::string& name = "") :
        name(name)
    {}
    
    virtual ~Input() = default;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
};


class Player : public Input {
public:

    Player(const std::string& name = "") : Input(name) {}

    bool isActive(const time_t& now = time(0)) { return state == PLAY; }

    bool accepts(const PlayItem& item) {
        return canPlay(item) && state == IDLE;
    }

    enum State {
        IDLE, LOAD, CUE, PLAY
    };

    State state = IDLE;

    State getState(const time_t& now = std::time(0)) const {
        return state;
    }

    std::shared_ptr<PlayItem> playItem = nullptr;

    void play() {
        state = PLAY;
    }

    virtual void stop() {}

    virtual bool canPlay(const PlayItem& item) = 0;

    virtual void load(const std::string& url, double position = 0) = 0;

    void schedule(const PlayItem& item) {
        playItem = std::make_shared<PlayItem>(item);
        auto pos = std::time(0) - item.start;
        if (pos < 0) pos = 0;
        state = LOAD;
        load(item.uri, pos);
        state = CUE;
    }


    std::atomic<bool> isFading = false;
    float volume = 0;

    void setVolume(float vol, bool exp) {
        volume = exp ? vol*vol : vol;
    }

    void fadeIn() {
        fade(true, playItem->fadeInTime);
    }

    void fadeOut() {
        fade(false, playItem->fadeOutTime);
    }

    void fade(bool increase, time_t duration) {
        isFading = true;
        log.debug() << name << " fade start";
        std::thread([increase, duration, this] {
            auto niters = duration * 100;
            float incr = 1.0 / 100.0 / duration;
            if (!increase) incr *= -1;
            float vol = this->volume;
            for (auto i = 0; i < niters; ++i) {
                vol += incr;
                this->setVolume(vol, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                // log.debug() << "AudioProcessor " << name << " fade vol " << this->volume;
            }
            isFading = false;
            log.debug() << name << " fade done";
        }).detach();
    }


    std::function<void(const std::shared_ptr<PlayItem>& item)> playItemDidStartCallback;

    void work() {
        if (!playItem) return;
        time_t now = std::time(0);
        if (now >= playItem->start && now <= playItem->end && state == CUE) {
            log.info(Log::Magenta) << name << " PLAY";
            play();
            log.info(Log::Magenta) << name << " FADE IN";
            fadeIn();
            if (playItemDidStartCallback) playItemDidStartCallback(playItem);
        }
        else if (now >= playItem->end - playItem->fadeOutTime && now < playItem->end && state == PLAY && !isFading) {
            log.info(Log::Magenta) << name << " FADE OUT";
            fadeOut();
        }
        else if (now >= playItem->end + playItem->ejectTime && state != IDLE) {
            log.info(Log::Magenta) << name << " STOP";
            stop();
            state = IDLE;
        }
    }

    void getStatus(std::stringstream& ss) {
        ss << name << " " << state << " " << std::setfill(' ') << std::setw(2) << volume << "    ";
    }
};
}
}