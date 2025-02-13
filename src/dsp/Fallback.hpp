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

#include <filesystem>
#include <thread>
#include <future>
#include "SineOscillator.hpp"
#include "QueuePlayer.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {

class Buflet {
    const double mSampleRate;
    const std::string mURL;
    std::unique_ptr<CodecReader> mReader;
    const size_t mSize;
    util::RingBuffer<sam_t>& mBuffer;

    enum State { IDLE, LOADING, DONE } state = IDLE;

public:
    Buflet(double tSampleRate, const std::string& tURL, util::RingBuffer<sam_t>& tBuffer) :
        mSampleRate(tSampleRate),
        mURL(tURL),
        mReader(std::make_unique<CodecReader>(mSampleRate, mURL)),
        mSize(mReader->sampleCount()),
        mBuffer(tBuffer)
    {}

    size_t size() {
        return mSize;
    }

    void load() {
        state = LOADING;
        mReader->read(mBuffer);
        mReader = nullptr;
        state = DONE;
    }
};


class Fallback : public Input {
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;

    const double mSampleRate;
    SineOscillator mOscL;
    SineOscillator mOscR;
    std::string mFallbackURL;
    size_t mBufferTime;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning = false;
    std::deque<std::unique_ptr<Buflet>> mQueueItems = {};
    util::RingBuffer<sam_t> mBuffer;
    bool mActive;

public:
    Fallback(double tSampleRate, const std::string& tFallbackURL, size_t tBufferTime) : Input(),
        mSampleRate(tSampleRate),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mFallbackURL(tFallbackURL),
        mBufferTime(tBufferTime),
        mBuffer(mSampleRate * 2 * mBufferTime)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));        
    }

    
    void run() {
        mRunning = true;
        mWorker = std::make_unique<std::thread>([this] {
            log.info(Log::Yellow) << "Fallback loading queue...";
            for (const auto& entry : std::filesystem::directory_iterator(mFallbackURL)) {
                if (!entry.is_regular_file()) continue;
                const auto& file = entry.path().string();
                try {
                    auto buflet = std::make_unique<Buflet>(mSampleRate, file, mBuffer);
                    while (mRunning && mBuffer.remaining() < buflet->size()) {
                        // log.debug() << Waiting for buffer space...";
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    if (!mRunning) return;
                    buflet->load();
                    mQueueItems.push_back(std::move(buflet));
                }
                catch (const std::exception& e) {
                    log.error() << "Fallback failed to load '" << file << "': " << e.what();
                }
            }
            log.info(Log::Yellow) << "Fallback load queue done size: " << mQueueItems.size();
        });
    }

    void terminate() {
        log.debug() << "Fallback terminate...";
        mRunning = false;
        if (mWorker->joinable()) mWorker->join();
        log.info() << "Fallback terminated";
    }

    void start() {
        if (mActive) return;
        log.info(Log::Yellow) << "Fallback start";
        mBuffer.resetHead();
        mActive = true;
    }

    void stop() {
        if (!mActive) return;
        log.info(Log::Yellow) << "Fallback stop";
        mActive = false;
    }

    bool isActive() {
        return mActive;
    }

    void process(const sam_t* in, sam_t* out, size_t nframes) {
        auto nsamples = nframes * 2;
        auto nread = mBuffer.read(out, nsamples);

        if (nread < nsamples) {
            for (auto i = 0; i < nframes; ++i) {
                out[i*2]   += mOscL.process() * kGain;
                out[i*2+1] += mOscR.process() * kGain;
            }
        }
    }
};
}
}