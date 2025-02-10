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

#include <numbers>
#include <cmath>
#include <limits>
#include "audio.hpp"

namespace castor {
namespace audio {
class SineOscillator {

    static constexpr double k2Pi = 2 * M_PI;
    
    const double mSampleRate;
    double mOmega = 0.0;
    double mDeltaOmega = 0.0;
    
public:
    
    SineOscillator(double tSampleRate = 44100.0) :
        mSampleRate(tSampleRate)
    {}

    void setFrequency(double tFrequency) {
        mDeltaOmega = tFrequency / mSampleRate;
    }
    
    void reset() {
        mOmega = 0;
    }

    double processDbl() {
        const double sample = std::sin(mOmega * k2Pi);
        mOmega += mDeltaOmega;

        if (mOmega >= 1.0) { mOmega -= 1.0; }
        return sample;
    }

    sam_t process() {
        double sam = round(processDbl() * std::numeric_limits<sam_t>::max());
        return static_cast<sam_t>(sam);
    }
};
}
}