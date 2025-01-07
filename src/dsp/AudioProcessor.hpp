#pragma once

#include <string>
#include <iomanip>
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


class Runner {
    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;

public:
    void run() {
        mRunning = true;
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                this->work();
            }
        });
    }

    void terminate() {
        mRunning = false;
        if (mWorker && mWorker->joinable()) mWorker->join();
    }

    void sleep(time_t millis) {
        std::this_thread::sleep_for(std::chrono::milliseconds(millis));
    }

    virtual void work() = 0;
};


class Player : public Input, public Runner {
public:

    Player(const std::string& name = "") : Input(name), Runner() {}

    bool isActive(const time_t& now = time(0)) { return state == PLAY; }

    enum State {
        IDLE, LOAD, CUE, PLAY
    };

    State state = IDLE;

    State getState(const time_t& now = std::time(0)) const {
        return state;
    }

    const char* stateStr() {
        static constexpr const char* Idle = "IDLE";
        static constexpr const char* Load = "LOAD";
        static constexpr const char* Cue  = "CUE";
        static constexpr const char* Play = "PLAY";
        switch (state) {
            case IDLE: return Idle;
            case LOAD: return Load;
            case CUE:  return Cue;
            case PLAY: return Play;
        }
    }

    void play() {
        state = PLAY;
    }

    virtual void stop() {}

    virtual bool canPlay(const PlayItem& item) = 0;

    virtual void load(const std::string& url, double position = 0) = 0;

    std::shared_ptr<PlayItem> playItem = nullptr;
    std::unique_ptr<std::thread> schedulingThread = nullptr;

    void schedule(const PlayItem& item) {
        playItem = std::make_shared<PlayItem>(item);
        state = LOAD;
        if (schedulingThread && schedulingThread->joinable()) schedulingThread->join();
        schedulingThread = std::make_unique<std::thread>([this] {
            while (playItem && playItem->isInScheduleTime() && state != CUE) {
                auto pos = std::time(0) - playItem->start;
                if (pos < 0) pos = 0;
                try {
                    auto uri = playItem->uri;
                    load(uri, pos);
                    state = CUE;
                }
                catch (std::exception& e) {
                    log.error() << "AudioProcessor failed to load '" << playItem->uri << "': " << e.what();
                    std::this_thread::sleep_for(std::chrono::seconds(playItem->retryInterval));
                }
            }
        });
    }

    ~Player() {
        playItem = nullptr;
        if (schedulingThread && schedulingThread->joinable()) schedulingThread->join();
    }


    std::atomic<bool> isFading = false;
    float volume = 0;

    void setVolume(float vol, bool exp) {
        if (vol < 0) {
            log.error() << "AudioProcessor " << name << " volume < 0";
            vol = 0;
        } else if (vol > 1) {
            log.error() << "AudioProcessor " << name << " volume > 1";
            vol = 1;
        }
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
            float vol = 0;
            if (!increase) {
                incr *= -1;
                vol = 1;
            }
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

    void work() override {
        if (playItem) {
            auto item = *playItem;
            time_t now = std::time(0);
            
            if (now >= item.start && now <= item.end && state == CUE) {
                log.info(Log::Magenta) << name << " PLAY";
                play();
                log.info(Log::Magenta) << name << " FADE IN";
                fadeIn();
                if (playItemDidStartCallback) playItemDidStartCallback(std::make_shared<PlayItem>(item));
            }
            else if (now >= item.end - item.fadeOutTime && now < item.end && state == PLAY && !isFading) {
                log.info(Log::Magenta) << name << " FADE OUT";
                fadeOut();
            }
            else if (now >= item.end + item.ejectTime && state != IDLE) {
                log.info(Log::Magenta) << name << " STOP";
                stop();
                state = IDLE;
            }

            sleep(10);
        } else {
            sleep(100);
        }
    }
};
}
}