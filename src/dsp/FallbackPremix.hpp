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

#include <algorithm>
#include <deque>
#include <filesystem>
#include <random>
#include <set>
#include <thread>
#include <mutex>
#include "SineOscillator.hpp"
#include "PremixPlayer.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {

    class FallbackPremix : public Input {

    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;
    static constexpr time_t kLoadRetryInterval = 5;

    const std::string mFallbackURL;
    const size_t mBufferTime;
    const float mCrossFadeTime;
    const size_t mFadeOutSampleOffset;
    const bool mShuffle;
    const bool mSineSynth;
    SineOscillator mOscL;
    SineOscillator mOscR;
    time_t mLastLoad = 0;
    std::thread mLoadThread;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mActive = false;
    std::shared_ptr<PlayItem> mCurrTrack = nullptr;
    PremixPlayer mPremixPlayer;
    std::shared_ptr<api::Program> mProgram;

public:
    std::function<void(std::shared_ptr<PlayItem> item)> startCallback = nullptr;

    FallbackPremix(const AudioStreamFormat& tClientFormat, const std::string& tFallbackURL, size_t tBufferTime, float tCrossFadeTime, bool tShuffle, bool tSineSynth) :
        Input(tClientFormat),
        mFallbackURL(tFallbackURL),
        mBufferTime(tBufferTime),
        mCrossFadeTime(tCrossFadeTime),
        mShuffle(tShuffle),
        mSineSynth(tSineSynth),
        mFadeOutSampleOffset(clientFormat.sampleRate * clientFormat.channelCount * mCrossFadeTime),
        mOscL(clientFormat.sampleRate),
        mOscR(clientFormat.sampleRate),
        mPremixPlayer(tClientFormat, "fallback", tBufferTime, 1, 0.5, mCrossFadeTime),
        mProgram(std::make_shared<api::Program>())
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));
        mPremixPlayer.startCallback = [this](auto itm) { this->onTrackStart(itm); };
        mProgram->showName = "Fallback";
    }

    void onTrackStart(std::shared_ptr<PlayItem> tItem) {
        mCurrTrack = tItem;
        if (mCurrTrack) mCurrTrack->program = mProgram;
        notifyTrackStart();
    }

    void notifyTrackStart() {
        if (startCallback && mCurrTrack) startCallback(mCurrTrack);
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
        mLoadThread = std::thread(&FallbackPremix::runLoad, this);
        log.debug() << "Fallback running";
    }

    void terminate() {
        log.debug() << "Fallback terminate...";
        mRunning = false;
        mActive = false;
        if (mLoadThread.joinable()) mLoadThread.join();
        log.info() << "Fallback terminated";
    }


    void runLoad() {
        while (mRunning) {
            if (mPremixPlayer.numTracks() == 0 && (mLastLoad == 0 || mLastLoad + kLoadRetryInterval <= std::time(0))) {
                load();
                mLastLoad = std::time(0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void load() {
        log.info(Log::Yellow) << "Fallback loading queue...";

        mPremixPlayer.eject();

        auto pushPlayer = [&](const std::string& url) {
            try {
                mPremixPlayer.load(url);
            }
            catch (int i) {
                return false;
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
        std::vector<std::filesystem::path> paths(sortedPaths.begin(), sortedPaths.end());

        if (mShuffle) {
            std::random_device rd;
            std::mt19937 rng(rd());
            std::ranges::shuffle(paths, rng);
        }

        for (const auto& path : paths) {
            if (!mRunning) return;
            const auto& url = path.string();
            if (url.ends_with(".m3u")) {
                log.debug() << "Fallback opening m3u file " << url;
                std::ifstream file(url);
                if (!file.is_open()) throw std::runtime_error("Failed to open file");
                std::string line;
                while (getline(file, line)) {
                    if (!mRunning) return;
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
        auto qsz = mPremixPlayer.numTracks();
        if (qsz > 0) log.info(Log::Yellow) << "Fallback load done (" << qsz << " tracks)";
        else log.warn() << "Fallback queue empty - reloading in " << kLoadRetryInterval << " sec...";
    }


    void start() {
        if (mActive || !mRunning) return;
        log.info(Log::Yellow) << "Fallback start";
        mPremixPlayer.fadeIn();
        mActive = true;
        notifyTrackStart();
    }

    void stop() {
        if (!mActive) return;
        log.info(Log::Yellow) << "Fallback stop";
        mPremixPlayer.fadeOut();
        // mActive = false;

        //int fadeOutMs = mPremixPlayer.fadeOutTime * 1000;
        //std::thread([this, ms=fadeOutMs] {
            //td::this_thread::sleep_for(std::chrono::milliseconds(ms));
            mActive = false;
        //}).detach(); // !
    }


    size_t process(const sam_t* in, sam_t* out, size_t nframes) override {
        auto processed = mPremixPlayer.process(in, out, nframes);
        if (processed == 0 && mActive && mSineSynth) {
            for (auto i = 0; i < nframes; ++i) {
                out[i*2]   += mOscL.process() * kGain;
                out[i*2+1] += mOscR.process() * kGain;
            }
        }
        return nframes;
    }
};

}
}