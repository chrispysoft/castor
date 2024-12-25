#pragma once

#include <string>

namespace cst {

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
    bool isActive(const time_t& now) { return now >= tsStart && now <= tsEnd; };

    enum State {
        QUEUED, PLAYING, DONE
    };

    State getState(const time_t& now) const {
        if (now < tsStart) return QUEUED;
        if (now >= tsStart && now <= tsEnd) return PLAYING;
        return DONE;
    }

    time_t tsStart;
    time_t tsEnd;
};
}