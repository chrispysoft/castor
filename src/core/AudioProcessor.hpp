#pragma once

#include <string>

namespace cst {

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
    bool isActive(const time_t& now = time(0)) { return state == PLAY; } // now >= tsStart && now <= tsEnd; };

    bool accepts(const PlayItem& item) {
        return canPlay(item) && getState() == IDLE;
    }

    virtual void stop() {}

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

    virtual bool canPlay(const PlayItem& item) = 0;
    virtual void load(const std::string& url, double position = 0) = 0;
    void schedule(const PlayItem& item) {
        tsStart = item.start;
        tsEnd = item.end;
        auto pos = std::time(0) - tsStart;
        if (pos < 0) pos = 0;
        state = LOAD;
        load(item.uri, pos);
        state = CUE;
    }
};
}