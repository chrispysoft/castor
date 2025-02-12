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

#include <limits>
#include "audio.hpp"

namespace castor {
namespace audio {

template <typename T>
inline float rms(const T* tBlock, size_t tNumSamples, size_t tSampleMax = 1) {
    float rms = 0;
    float divisor = (typeid(T) == typeid(float) || typeid(T) == typeid(double)) ? 1.0 : std::numeric_limits<T>::max();
    for (auto i = 0; i < tNumSamples; ++i) {
        float sam = static_cast<float>(tBlock[i]) / divisor;
        rms += sam * sam;
    }
    if (rms == 0) return 0;
    rms = sqrt(rms / tNumSamples);
    return rms;
}

class RMS {
    const size_t mSize;
    const size_t mChannelCount;
    std::vector<float> mValues;
    size_t mIdx = 0;
    float mRMS = -std::numeric_limits<float>::infinity();

public:

    RMS(size_t tSize, size_t tChannelCount) :
        mSize(tSize),
        mChannelCount(tChannelCount),
        mValues(mSize, 0.0f)
    {}

    float process(const sam_t* in, size_t nframes) {
        mValues[mIdx] = rms(in, nframes * mChannelCount);
        // for (auto val : mValues) std::cout << val << " ";
        // std::cout << "\n";
        if (++mIdx >= mSize) {
            float avg = 0;
            for (const auto& val : mValues) {
                avg += val;
            }
            avg /= mSize;
            if (avg == 0) mRMS = -INFINITY;
            else mRMS = 20 * log10(avg);
            mIdx = 0;
        }
        return mRMS;
    }
};
}
}