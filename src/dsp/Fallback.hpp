/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <filesystem>
#include <set>
#include <thread>
#include "SineOscillator.hpp"
#include "FilePlayer.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {

class Fallback : public Input {

    static constexpr size_t kChannelCount = 2;
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;
    static constexpr time_t kLoadRetryInterval = 10;

    const double mSampleRate;
    const size_t mFrameSize;
    const std::string mFallbackURL;
    const size_t mBufferTime;
    const float mCrossFadeTime;
    const size_t mFadeOutSampleOffset;
    const bool mSineSynth;
    SineOscillator mOscL;
    SineOscillator mOscR;
    time_t mLastLoadAttempt = 0;
    std::thread mWorker;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mActive = false;
    std::atomic<int> mActivePlayerIdxA = -1;
    std::atomic<int> mActivePlayerIdxB = -1;
    std::deque<std::shared_ptr<FilePlayer>> mPlayers{};
    std::vector<sam_t> mMixBuffer;

public:
    Fallback(double tSampleRate, size_t tFrameSize, const std::string& tFallbackURL, size_t tBufferTime, float tCrossFadeTime, bool tSineSynth) : Input(),
        mSampleRate(tSampleRate),
        mFrameSize(tFrameSize),
        mFallbackURL(tFallbackURL),
        mBufferTime(tBufferTime),
        mCrossFadeTime(tCrossFadeTime),
        mSineSynth(tSineSynth),
        mFadeOutSampleOffset(mSampleRate * kChannelCount * mCrossFadeTime),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mMixBuffer(mFrameSize * kChannelCount)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));        
    }

    bool isActive() {
        return mActive;
    }
    
    void run() {
        if (mFallbackURL.empty()) {
            log.error() << "Fallback folder not set";
            return;
        }
        if (!std::filesystem::exists(mFallbackURL)) {
            log.error() << "Fallback folder does not exist";
            return;
        }
        mRunning = true;
        mWorker = std::thread(&Fallback::runSync, this);
        log.debug() << "Fallback running";
    }

    void terminate() {
        log.debug() << "Fallback terminate...";
        mRunning = false;
        mActive = false;
        if (mWorker.joinable()) mWorker.join();
        for (const auto& player : mPlayers) player->stop();
        mPlayers.clear();
        log.info() << "Fallback terminated";
    }


    void runSync() {
        while (mRunning) {
            auto now = std::time(0);

            if (mPlayers.empty() && now >= mLastLoadAttempt + kLoadRetryInterval) {
                loadQueue();
                mLastLoadAttempt = now;
            }

            control();

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void loadQueue() {
        log.info(Log::Yellow) << "Fallback loading queue...";
        
        size_t maxSamples = mSampleRate * mBufferTime * kChannelCount;
        size_t sumSamples = 0;

        auto pushPlayer = [&](const std::string& url) {
            try {
                auto player = std::make_shared<FilePlayer>(mSampleRate, mFrameSize, url, 0, mCrossFadeTime, mCrossFadeTime);
                player->load(url);
                sumSamples += player->mBuffer->capacity();
                if (sumSamples > maxSamples) {
                    log.warn() << "Fallback buffer size reached";
                    player->stop();
                    return false; // break case
                }
                player->play();
                player->fadeIn();
                mPlayers.push_back(player);
            }
            catch (const std::exception& e) {
                log.error() << "Fallback failed to load '" << url << "': " << e.what();
            }
            return true;
        };

        bool loadNext = false;

        std::set<std::filesystem::path> sortedPaths;
        for (const auto& entry : std::filesystem::directory_iterator(mFallbackURL)) {
            if (!entry.is_regular_file()) continue;
            sortedPaths.insert(entry.path());
        }

        for (const auto& path : sortedPaths) {
            const auto& url = path.string();
            if (url.ends_with(".m3u")) {
                log.debug() << "Fallback opening m3u file " << url;
                std::ifstream file(url);
                if (!file.is_open()) throw std::runtime_error("Failed to open file");
                std::string line;
                while (getline(file, line)) {
                    if (line.starts_with("#")) continue;
                    util::stripM3ULine(line);
                    loadNext = pushPlayer(line);
                    if (!loadNext) break;
                    log.debug() << "Fallback added m3u entry " << line;
                }
                file.close();
                log.debug() << "Fallback closed m3u file " << url;
                if (!loadNext) break;
            } else {
                loadNext = pushPlayer(url);
                if (!loadNext) break;
            }
        }
        log.info(Log::Yellow) << "Fallback load queue done size: " << mPlayers.size();
    }


    void control() {
        if (mPlayers.empty()) return;

        auto shouldFadeOut = [this](const std::shared_ptr<Player>& player) {
            return player->mBuffer->readPosition() >= player->mBuffer->capacity() - mFadeOutSampleOffset;
        };

        auto isFinished = [](const std::shared_ptr<Player>& player) {
            return player->mBuffer->readPosition() >= player->mBuffer->capacity();
        };

        auto ia = -1;
        auto ib = -1;

        for (auto i = 0; i < mPlayers.size(); ++i) {
            const auto& player = mPlayers[i];
            if (isFinished(player)) continue;
            ia = i;
            if (shouldFadeOut(player)) {
                player->fadeOut();
                if (ia + 1 < mPlayers.size()) ib = ia + 1;
            }
            break;
        }
        mActivePlayerIdxA.store(ia);
        mActivePlayerIdxB.store(ib);

        if (ia == -1 && ib == -1) {
            mPlayers.clear();
        }
    }


    void start() {
        if (mActive) return;
        log.info(Log::Yellow) << "Fallback start";
        mActive = true;
        // if (mCurrPlayer) {
        //     mCurrPlayer->play();
        //     mCurrPlayer->fadeIn();
        // }
    }

    void stop() {
        if (!mActive) return;
        log.info(Log::Yellow) << "Fallback stop";
        mActive = false;
    }


    void process(const sam_t* in, sam_t* out, size_t nframes) {
        auto playerIdxA = mActivePlayerIdxA.load();
        auto playerIdxB = mActivePlayerIdxB.load();
        if (playerIdxA >= 0) {
            mPlayers[playerIdxA]->process(in, out, nframes);
            if (playerIdxB >= 0) {
                mPlayers[playerIdxB]->process(in, mMixBuffer.data(), nframes);
                for (auto i = 0; i < nframes; ++i) {
                    out[i*2+0] += mMixBuffer[i*2+0];
                    out[i*2+1] += mMixBuffer[i*2+1];
                }
            }
        }
        else if (mSineSynth) {
            for (auto i = 0; i < nframes; ++i) {
                out[i*2]   += mOscL.process() * kGain;
                out[i*2+1] += mOscR.process() * kGain;
            }
        }
    }
};
}
}