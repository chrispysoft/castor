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
#include "test/CalendarMock.hpp"

namespace castor {

struct PlayerFactory {
    static std::shared_ptr<audio::Player> createPlayer(const PlayItem& tPlayItem, double tSampleRate) {
        auto name = tPlayItem.uri.substr(tPlayItem.uri.rfind('/')+1);
        log.info(Log::Magenta) << "PlayerFactory createPlayer " << name;
        if (tPlayItem.uri.starts_with("line"))
            return std::make_shared<audio::LinePlayer>(tSampleRate, name);
        else
            return std::make_shared<audio::StreamPlayer>(tSampleRate, name);
    }
};

// #define TEST_CALENDAR 1

#ifdef TEST_CALENDAR
using Calendar_t = test::CalendarMock;
#else
using Calendar_t = Calendar;
#endif

class Engine : public audio::Client::Renderer {

    const Config& mConfig;
    Calendar_t mCalendar;
    io::TCPServer mTCPServer;
    api::Client mAPIClient;
    audio::Client mAudioClient;
    audio::SilenceDetector mSilenceDet;
    audio::Fallback mFallback;
    audio::Recorder mRecorder;
    audio::StreamOutput mStreamOutput;
    std::vector<audio::sam_t> mMixBuffer;
    std::deque<std::shared_ptr<audio::Player>> mPlayers = {};
    std::thread mWorker;
    std::atomic<bool> mRunning = false;
    util::Timer mReportTimer;
    api::Program mCurrProgram = {};
    
public:
    Engine(const Config& tConfig) :
        mConfig(tConfig),
        mCalendar(mConfig),
        mTCPServer(mConfig.tcpPort),
        mAPIClient(mConfig),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mConfig.sampleRate, mConfig.audioBufferSize),
        mSilenceDet(mConfig.silenceThreshold, mConfig.silenceStartDuration, mConfig.silenceStopDuration),
        mFallback(mConfig.sampleRate, mConfig.audioFallbackPath, mConfig.preloadTimeFallback),
        mRecorder(mConfig.sampleRate),
        mStreamOutput(mConfig.sampleRate),
        mMixBuffer(mConfig.audioBufferSize * 2),
        mReportTimer(mConfig.reportInterval)
    {
        mAudioClient.setRenderer(this);
    }

    void start() {
        log.debug() << "Engine starting...";
        mRunning = true;
        mCalendar.start();
        mAudioClient.start();
        mFallback.run();
        mWorker = std::thread([this] {
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
        if (mWorker.joinable()) mWorker.join();
        for (auto player : mPlayers) if (!player->isPlaying()) player->stop();
        mTCPServer.stop();
        mCalendar.stop();
        mRecorder.stop();
        mFallback.terminate();
        mStreamOutput.stop();
        mAudioClient.stop();
        log.info() << "Engine stopped";
    }

    void work() {
        if (mSilenceDet.silenceDetected()) {
            mFallback.start();
        } else {
            mFallback.stop();
        }
        
        auto items = mCalendar.items();
        for (auto& item : items) {
            if (std::time(0) > item.end) continue;

            enum Action { NIL, EXISTS, ADD, REPLACE } action = NIL;
            
            for (auto pi = 0 ; pi < mPlayers.size(); ++pi) {
                auto player = mPlayers[pi];
                auto plItm = player->playItem;
                if (item.start >= plItm.start && item.end <= plItm.end) {
                    if (plItm == item) action = EXISTS;
                    else action = REPLACE;
                    break;
                }
            }

            switch (action) {
                case EXISTS: {
                    continue;
                }
                case REPLACE: {
                    // mPlayers.erase(0);
                    break;
                }
                default: break;
            }

            if (item.uri.starts_with("http")) item.loadTime = mConfig.preloadTimeStream;
            else if (item.uri.starts_with("line")) item.loadTime = 5;
            else item.loadTime = mConfig.preloadTimeFile;
            
            auto player = PlayerFactory::createPlayer(item, mConfig.sampleRate);
            player->playItemDidStartCallback = [this](auto item) { this->playItemDidStart(item); };
            player->schedule(item);

            mPlayers.push_back(player);
        }

        for (auto it = mPlayers.begin(); it != mPlayers.end(); ) {
            auto player = *it;
            if (player->isFinished()) {
                it = mPlayers.erase(it); // erase() returns next valid iterator
            } else {
                player->work();
                ++it;
            }
        }

        if (mReportTimer.query()) {
            postHealth();
        }

        if (mTCPServer.connected()) {
            updateStatus();
        }
    }

    void playItemDidStart(const PlayItem& item) {
        log.info() << "Engine playItemDidStart";

        if (mStreamOutput.isRunning() && !mConfig.streamOutMetadataURL.empty()) {
            // log.debug() << "Engine updating stream metadata";
            try {
                auto songName = item.program.showName;
                std::replace(songName.begin(), songName.end(), ' ', '+');
                mStreamOutput.updateMetadata(mConfig.streamOutMetadataURL, songName);
            }
            catch (const std::exception& e) {
                log.error() << "Engine failed to update stream metadata: " << e.what();
            }
        }
    
        if (mCurrProgram != item.program) {
            mCurrProgram = item.program;
            log.info() << "Engine program changed to " << mCurrProgram.showName;

            if (mConfig.audioRecordPath.size() > 0) {
                mRecorder.stop();

                if (mCurrProgram.showId > 1) {
                    auto recURL = mConfig.audioRecordPath + "/" + util::utcFmt() + "_" + mCurrProgram.showName + ".mp3";
                    try {
                        std::unordered_map<std::string, std::string> metadata = {{"artist", item.program.showName }, {"title", item.program.episodeTitle}};
                        mRecorder.start(recURL, metadata);
                    }
                    catch (const std::runtime_error& e) {
                        log.error() << "Engine failed to start recorder for url: " << recURL << " " << e.what();
                    }
                }
            }
        }
        
        postPlaylog(item);
    }

    void postPlaylog(const PlayItem& tPlayItem) {
        if (mConfig.playlogURL.empty()) return;
        try {
            mAPIClient.postPlaylog({tPlayItem});
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post playlog: " << e.what();
        }
    }

    void postHealth() {
        if (mConfig.healthURL.empty()) return;
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
        std::ostringstream strstr;
        // strstr << "\x1b[5A";
        strstr << "________________________________________________________________\n";
        strstr << "RMS: " << std::fixed << std::setprecision(2) << mSilenceDet.currentRMS() << '\n';
        strstr << "Fallback: " << (mFallback.isActive() ? "ON" : "OFF") << '\n';
        strstr << "Player queue (" << mPlayers.size() << " items):\n";

        strstr << std::left << std::setfill(' ') << std::setw(16) << "ID";
        strstr << std::right << std::setfill(' ') << std::setw(12) << "Status";
        strstr << std::right << std::setfill(' ') << std::setw(12) << "Buffered";
        strstr << std::right << std::setfill(' ') << std::setw(12) << "Played";
        strstr << std::right << std::setfill(' ') << std::setw(12) << "Gain";
        strstr << std::right << std::setfill(' ') << std::setw(12) << "Size (MiB)";
        strstr << '\n';

        for (auto player : mPlayers) {
            strstr << std::left << std::setfill(' ') << std::setw(16) << player->name.substr(0, 16);
            strstr << std::right << std::setfill(' ') << std::setw(12) << player->stateStr();
            strstr << std::right << std::setfill(' ') << std::setw(12) << std::fixed << std::setprecision(2) << player->writeProgress();
            strstr << std::right << std::setfill(' ') << std::setw(12) << std::fixed << std::setprecision(2) << player->readProgress();
            strstr << std::right << std::setfill(' ') << std::setw(12) << std::fixed << std::setprecision(2) << player->volume;
            strstr << std::right << std::setfill(' ') << std::setw(12) << std::fixed << std::setprecision(2) << player->mBuffer.memorySizeMB();
            // statusSS << std::left << std::setfill(' ') << std::setw(16) << std::fixed << std::setprecision(2) << player->rms << ' ';
            strstr << '\n';
        }
        mTCPServer.statusString = strstr.str();
        // log.debug() << statusSS.str();
    }
    

    void renderCallback(const audio::sam_t* in,  audio::sam_t* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(audio::sam_t));

        auto players = mPlayers;
        for (const auto& player : players) {
            if (!player->isPlaying()) continue;
            player->process(in, mMixBuffer.data(), nframes);
            for (auto i = 0; i < mMixBuffer.size(); ++i) {
                out[i] += mMixBuffer[i] * player->volume;
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