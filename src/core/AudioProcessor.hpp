#pragma once

#include <string>

namespace lap {

class AudioProcessor {
public:
    virtual ~AudioProcessor() = default;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
};
}