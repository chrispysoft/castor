#pragma once

#include <string>

namespace lap {

class AudioSource {
public:

    virtual ~AudioSource() = default;
    virtual void open(const std::string& tURL) = 0;
    virtual void roll(double tPosition) = 0;
    virtual void clear() = 0;
    virtual void process(const float* in, float* out, size_t nframes) = 0;
    
};
}