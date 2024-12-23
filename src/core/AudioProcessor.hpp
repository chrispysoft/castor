#pragma once

#include <string>

namespace cst {

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
    bool isActive(const time_t& now) { return now >= tsStart && now <= tsEnd; };
    time_t tsStart;
    time_t tsEnd;
};
}