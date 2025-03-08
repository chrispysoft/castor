/*
 *  Copyright (C) 2024-2025 Christoph Pastl
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
#include "dsp/FilePlayer.hpp"
#include "dsp/StreamPlayer.hpp"
#include "dsp/Fallback.hpp"
#include "dsp/SilenceDetector.hpp"
#include "dsp/Recorder.hpp"
#include "dsp/StreamOutput.hpp"
#include "util/dispatch.hpp"

namespace castor {

class PlayerFactory {
    // std::mutex mMutex;
    float mSampleRate;
    time_t mPreloadTimeFile;
    time_t mPreloadTimeStream;
    time_t mPreloadTimeLine;
    
public:
    PlayerFactory(const Config& tConfig) :
        mSampleRate(tConfig.sampleRate),
        mPreloadTimeFile(tConfig.preloadTimeFile),
        mPreloadTimeStream(tConfig.preloadTimeStream),
        mPreloadTimeLine(tConfig.preloadTimeLine)
    {}

    std::shared_ptr<audio::Player> createPlayer(const PlayItem& tPlayItem) {
        auto uri = tPlayItem.uri;
        auto name = uri.substr(uri.rfind('/')+1);
        // std::lock_guard<std::mutex> lock(mMutex);
        // log.debug(Log::Magenta) << "PlayerFactory createPlayer " << name;
        if (uri.starts_with("line"))
            return std::make_shared<audio::LinePlayer>(mSampleRate, name, mPreloadTimeLine);
        else if (uri.starts_with("http"))
            return std::make_shared<audio::StreamPlayer>(mSampleRate, name, mPreloadTimeStream);
        else
            return std::make_shared<audio::FilePlayer>(mSampleRate, name, mPreloadTimeFile);
    }

    void returnPlayer(std::shared_ptr<audio::Player> tPlayer) {
        // std::lock_guard<std::mutex> lock(mMutex);
    }
};


class Engine : public audio::Client::Renderer {

    const Config& mConfig;
    Calendar mCalendar;
    io::TCPServer mTCPServer;
    api::Client mAPIClient;
    audio::Client mAudioClient;
    audio::SilenceDetector mSilenceDet;
    audio::Fallback mFallback;
    audio::Recorder mRecorder;
    audio::StreamOutput mStreamOutput;
    std::deque<std::shared_ptr<audio::Player>> mPlayers = {};
    std::unique_ptr<PlayerFactory> mPlayerFactory = nullptr;
    std::shared_ptr<audio::Player> mActivePlayer = nullptr;
    std::map<PlayItem, std::shared_ptr<audio::Player>> mPlayerMap;
    std::thread mWorker;
    std::thread mLoadThread;
    std::atomic<bool> mRunning = false;
    util::Timer mReportTimer;
    util::Timer mTCPUpdateTimer;
    api::Program mCurrProgram = {};
    // std::queue<PlayItem> mScheduleItems = {};
    // std::mutex mScheduleItemsMutex;
    // std::condition_variable mScheduleItemsCV;
    std::mutex mPlayersMutex;
    dispatch_queue mScheduleQueue;
    dispatch_queue mAPIReportQueue;
    
    
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
        mReportTimer(mConfig.healthReportInterval),
        mTCPUpdateTimer(1),
        mScheduleQueue("schedule queue", 1),
        mAPIReportQueue("report queue", 1)
    {
        mCalendar.calendarChangedCallback = [this](const auto& items) { this->calendarChanged(items); };
        mAudioClient.setRenderer(this);
        mPlayerFactory = std::make_unique<PlayerFactory>(mConfig);
    }

    void parseArgs(std::unordered_map<std::string,std::string> tArgs) {
        try {
            auto calFile = tArgs["--calendar"];
            if (!calFile.empty()) mCalendar.load(calFile);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to load calendar test file: " << e.what();
        }
    }

    void start() {
        log.debug() << "Engine starting...";
        mRunning = true;
        mCalendar.start();
        mAudioClient.start(mConfig.realtimeRendering);
        mFallback.run();
        mWorker = std::thread([this] {
            while (this->mRunning) {
                this->work();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        mLoadThread = std::thread(&Engine::load, this);
        // mRenderThread = std::thread(&Engine::render, this);

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
        if (mLoadThread.joinable()) mLoadThread.join();
        for (auto player : mPlayers) player->stop();
        mPlayers.clear();
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

        for (auto player : mPlayers) {
            if (player->isPlaying() && player != mActivePlayer) {
                // std::lock_guard<std::mutex> lock(mPlayersMutex);
                mActivePlayer = player;
            }
        }

        if (mReportTimer.query()) {
            mAPIReportQueue.dispatch([this] {
                postHealth();
            });
        }

        if (mTCPServer.connected() && mTCPUpdateTimer.query()) {
            updateStatus();
        }

        mScheduleQueue.dispatch([this] {
            cleanup();
        });
    }

    void calendarChanged(const std::deque<PlayItem>& playItems) {
        for (const auto& item : playItems) {
            if (item.end < std::time(0)) continue;
            mScheduleQueue.dispatch([item=item, this] {
                schedule(item);
            });
        }
    }


    void schedule(const PlayItem& item) {
        // log.debug(Log::Yellow) << "Engine schedule " << item.start;

        if (mPlayerMap.contains(item)) return;
        
        auto player = mPlayerFactory->createPlayer(item);
        // player->playItemDidStartCallback = [this](auto item) { this->playItemDidStart(item); };
        player->schedule(item);
        
        // std::lock_guard<std::mutex> lock(mPlayersMutex);
        mPlayerMap[item] = player;
        mPlayers.push_back(player);
    }

    void cleanup() {
        while (!mPlayers.empty() && mPlayers.front()->isFinished()) {
            mPlayers.front()->stop();
            // std::lock_guard<std::mutex> lock(mPlayersMutex);
            mPlayerMap.erase(mPlayers.front()->playItem);
            mPlayers.pop_front();
        }
    }


    void load() {
        while (mRunning) {
            
            //std::unique_lock<std::mutex> lock(mPlayersMutex);
            auto players = mPlayers;
            //lock.unlock();

            for (auto player : players) {
                if (player && player->needsLoad()) {
                    player->tryLoad();
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void playItemDidStart(PlayItem item) {
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
        
        mAPIReportQueue.dispatch([this, item=item] {
            postPlaylog(item);
        });
    }
    

    // realtime thread or called by manual render function

    void renderCallback(const audio::sam_t* in,  audio::sam_t* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(audio::sam_t));

        auto player = mActivePlayer;
        if (player) {
            // std::cout << player->name << " ";
            player->process(in, out, nframes);
        }
        // std::cout << std::endl;

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


    std::ostringstream strstr;

    void updateStatus() {
        strstr.flush();
        // strstr << "\x1b[5A";
        strstr << "____________________________________________________________________________________________________________\n";
        strstr << "RMS: " << std::fixed << std::setprecision(2) << util::linearDB(mSilenceDet.currentRMS()) << " dB\n";
        strstr << "Fallback: " << (mFallback.isActive() ? "ACTIVE" : "INACTIVE") << '\n';
        strstr << "Player queue (" << mPlayers.size() << " items):\n";

        audio::Player::getStatusHeader(strstr);
        for (auto player : mPlayers) {
            if (player) player->getStatus(strstr);
        }
        strstr << std::endl;

        mTCPServer.pushStatus(strstr.str());
        // log.debug() << statusSS.str();
    }
};

}