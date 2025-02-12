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

#include <string>
#include <vector>
#include <deque>
#include <ranges>
#include <atomic>
#include <thread>
#include <mutex>
#include "Config.hpp"
#include "Calendar.hpp"
#include "io/TCPServer.hpp"
#include "util/Log.hpp"
#include "util/util.hpp"
#include "api/APIClient.hpp"
#include "dsp/AudioClient.hpp"
#include "dsp/LinePlayer.hpp"
#include "dsp/StreamPlayer.hpp"
#include "dsp/Fallback.hpp"
#include "dsp/SilenceDetector.hpp"
#include "dsp/Recorder.hpp"
#include "dsp/StreamOutput.hpp"

namespace castor {
class Engine : public audio::Client::Renderer {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::deque<PlayItem> mScheduledItems = {};
    std::mutex mMutex;

    double mSampleRate = 44100;
    size_t mBufferSize = 1024;

    const Config& mConfig;
    Calendar mCalendar;
    io::TCPServer mTCPServer;
    api::Client mAPIClient;
    audio::Client mAudioClient;
    audio::SilenceDetector mSilenceDet;
    audio::Fallback mFallback;
    audio::Recorder mRecorder;
    audio::StreamOutput mStreamOutput;
    std::vector<audio::sam_t> mMixBuffer;
    std::deque<std::shared_ptr<audio::Player>> mPlayers = {};
    time_t mLastReportTime = 0;
    time_t mReportInterval = 10;
    
public:
    Engine(const Config& tConfig) :
        mConfig(tConfig),
        mCalendar(mConfig),
        mTCPServer(mConfig.tcpPort),
        mAPIClient(mConfig),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mSampleRate, mBufferSize),
        mSilenceDet(mConfig.silenceThreshold, mConfig.silenceStartDuration, mConfig.silenceStopDuration),
        mFallback(mSampleRate, mConfig.audioFallbackPath, mConfig.fallbackCacheTime),
        mRecorder(mSampleRate),
        mStreamOutput(mSampleRate),
        mMixBuffer(mBufferSize * 2)
    {
        mPlayers.push_back(std::make_shared<audio::StreamPlayer>(mSampleRate, "Player 1"));
        mPlayers.push_back(std::make_shared<audio::StreamPlayer>(mSampleRate, "Player 2"));
        mPlayers.push_back(std::make_shared<audio::LinePlayer>(mSampleRate, "Line 1"));

        for (auto& player : mPlayers) {
            player->playItemDidStartCallback = [this](auto item) { this->playItemDidStart(item); };
        }

        mAudioClient.setRenderer(this);
    }

    void start() {
        log.debug() << "Engine starting...";
        mRunning = true;
        mCalendar.start();
        mAudioClient.start();
        mFallback.run();
        for (auto& player : mPlayers) player->run();
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                this->work();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        try {
            if (!mConfig.streamOutURL.empty()) mStreamOutput.start(mConfig.streamOutURL);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to start output stream: " << e.what();
        }

        try {
            mTCPServer.start();
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to start socket server: " << e.what();
        }
        log.info() << "Engine started";
    }

    void stop() {
        log.debug() << "Engine stopping...";
        mRunning = false;
        if (mWorker && mWorker->joinable()) mWorker->join();
        mTCPServer.stop();
        mCalendar.stop();
        mRecorder.stop();
        mFallback.terminate();
        mStreamOutput.stop();
        for (const auto& player : mPlayers) player->terminate();
        mAudioClient.stop();
        log.info() << "Engine stopped";
    }

    void work() {
        if (mSilenceDet.silenceDetected()) {
            mFallback.start();
        } else {
            mFallback.stop();
        }

        // std::lock_guard lock(mMutex);
        const auto items = mCalendar.items();
        for (auto& item : items) {
            if (item.isInScheduleTime()) {
                if (util::contains(mScheduledItems, item)) continue;
                
                auto sources = mPlayers | std::ranges::views::filter([&](auto v){ return v->canPlay(item); });
                if (sources.empty()) {
                    log.error() << "No processor registered for uri " << item.uri;
                    mScheduledItems.push_back(item);
                    continue;
                }
                auto freeSources = sources | std::ranges::views::filter([&](auto v){ return v->getState() == audio::Player::State::IDLE; });
                if (!freeSources.empty()) {
                    auto source = freeSources.front();
                    source->schedule(item);
                    mScheduledItems.push_back(item);
                }
            }
        }

        auto now = std::time(0);
        if (now - mLastReportTime > mReportInterval) {
            mLastReportTime = now;
            postHealth();
        }

        if (mTCPServer.connected()) {
            updateStatus();
        }
    }


    api::Program mCurrProgram = {};

    void playItemDidStart(const std::shared_ptr<PlayItem>& item) {
        log.info() << "Engine playItemDidStart";

        if (mStreamOutput.isRunning() && !mConfig.streamOutMetadataURL.empty()) {
            // log.debug() << "Engine updating stream metadata";
            try {
                auto songName = (item) ? item->program.showName : "";
                std::replace(songName.begin(), songName.end(), ' ', '+');
                mStreamOutput.updateMetadata(mConfig.streamOutMetadataURL, songName);
            }
            catch (const std::exception& e) {
                log.error() << "Engine failed to update stream metadata: " << e.what();
            }
        }
    
        if (mCurrProgram != item->program) {
            mCurrProgram = item->program;
            log.info() << "Engine program changed to " << mCurrProgram.showName;

            if (mConfig.audioRecordPath.size() > 0) {
                mRecorder.stop();

                if (mCurrProgram.showId > 1) {
                    auto recURL = mConfig.audioRecordPath + "/" + util::utcFmt() + "_" + mCurrProgram.showName + ".mp3";
                    try {
                        std::unordered_map<std::string, std::string> metadata = {{"artist", item->program.showName }, {"title", item->program.episodeTitle}};
                        mRecorder.start(recURL, metadata);
                    }
                    catch (const std::runtime_error& e) {
                        log.error() << "Engine failed to start recorder for url: " << recURL << " " << e.what();
                    }
                }
            }
        }
        try {
            mAPIClient.postPlaylog({*item});
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post playlog: " << e.what();
        }
    }

    void postHealth() {
        try {
            // nlohmann::json j = mCalendar.items();
            // std::stringstream s;
            // s << j;
            mAPIClient.postHealth({true, util::currTimeFmtMs(), ":)"});
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post health: " << e.what();
        }
    }

    void updateStatus() {
        std::ostringstream statusSS;
        statusSS << "\x1b[5A";
        statusSS << '\n';
        for (auto player : mPlayers) statusSS << std::left << std::setfill(' ') << std::setw(16) << player->name << ' ';
        statusSS << '\n';
        for (auto player : mPlayers) statusSS << std::left << std::setfill(' ') << std::setw(16) << player->stateStr() << ' ';
        statusSS << '\n';
        for (auto player : mPlayers) statusSS << std::left << std::setfill(' ') << std::setw(16) << std::fixed << std::setprecision(2) << player->volume << ' ';
        statusSS << '\n';
        for (auto player : mPlayers) statusSS << std::left << std::setfill(' ') << std::setw(16) << std::fixed << std::setprecision(2) << player->rms << ' ';
        statusSS << '\n';
        mTCPServer.statusString = statusSS.str();
        // log.debug() << statusSS.str();
    }
    

    void renderCallback(const audio::sam_t* in,  audio::sam_t* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(audio::sam_t));

        for (auto& source : mPlayers) {
            if (!source->isActive()) continue;
            source->process(in, mMixBuffer.data(), nframes);
            for (auto i = 0; i < mMixBuffer.size(); ++i) {
                out[i] += mMixBuffer[i] * source->volume;
            }
        }

        mSilenceDet.process(out, nframes);
        if (mFallback.isActive()) {
            mFallback.process(in, out, nframes);
        }

        if (mRecorder.isRunning()) {
            mRecorder.process(out, nframes);
        }

        if (mStreamOutput.isRunning()) {
            mStreamOutput.process(out, nframes);
        }
    }

};

}