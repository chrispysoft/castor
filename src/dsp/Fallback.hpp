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

class Fallback : public Input {
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;

    const double mSampleRate;
    const std::string mFallbackURL;
    SineOscillator mOscL;
    SineOscillator mOscR;
    size_t mBufferTime;
    std::thread mWorker;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mActive = false;
    std::deque<std::shared_ptr<StreamPlayer>> mPlayers = {};
    std::shared_ptr<StreamPlayer> mCurrPlayer = nullptr;

public:
    Fallback(double tSampleRate, const std::string& tFallbackURL, size_t tBufferTime) : Input(),
        mSampleRate(tSampleRate),
        mFallbackURL(tFallbackURL),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mBufferTime(tBufferTime)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));        
    }

    
    void run() {
        mRunning = true;
        mWorker = std::thread(&Fallback::runSync, this);
        log.debug() << "Fallback running";
    }

    void terminate() {
        log.debug() << "Fallback terminate...";
        mRunning = false;
        if (mWorker.joinable()) mWorker.join();
        for (auto player : mPlayers) player->stop();
        log.info() << "Fallback terminated";
    }


    void runSync() {
        while (mRunning) {
            if (mPlayers.empty()) {
                loadQueue();
            }

            for (auto it = mPlayers.begin(); it != mPlayers.end(); ) {
                auto player = *it;
                if (player->mBuffer.readPosition() >= player->mBuffer.capacity()) {
                    player->stop();
                    it = mPlayers.erase(it); // erase() returns next valid iterator
                } else {
                    // player->work();
                    ++it;
                }
            }

            if (!mPlayers.empty()) mCurrPlayer = mPlayers.front();
            else mCurrPlayer = nullptr;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void loadQueue() {
        log.debug(Log::Yellow) << "Fallback loading queue...";
        
        size_t maxSamples = mSampleRate * mBufferTime * 2;
        size_t sumSamples = 0;

        for (const auto& entry : std::filesystem::directory_iterator(mFallbackURL)) {
            if (!entry.is_regular_file()) continue;
            const auto& file = entry.path().string();
            try {
                auto player = std::make_shared<StreamPlayer>(mSampleRate, file);
                player->load(file);
                sumSamples += player->mBuffer.capacity();
                if (sumSamples > maxSamples) {
                    log.warn() << "Fallback buffer size reached";
                    player->stop();
                    break;
                }
                mPlayers.push_back(player);
            }
            catch (const std::exception& e) {
                log.error() << "Fallback failed to load '" << file << "': " << e.what();
            }
        }
        log.debug(Log::Yellow) << "Fallback load queue done size: " << mPlayers.size();
    }


    void start() {
        if (mActive) return;
        log.info(Log::Yellow) << "Fallback start";
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
        auto nread = 0;
        
        auto player = mCurrPlayer;
        if (player) {
            nread = player->mBuffer.read(out, nsamples);
        }

        if (nread == 0) {
            for (auto i = 0; i < nframes; ++i) {
                out[i*2]   += mOscL.process() * kGain;
                out[i*2+1] += mOscR.process() * kGain;
            }
        }
    }
};
}
}