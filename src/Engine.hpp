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

    std::shared_ptr<audio::Player> createPlayer(std::shared_ptr<PlayItem> tPlayItem) {
        auto uri = tPlayItem->uri;
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
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mScheduleItemsChanged = false;
    std::thread mScheduleThread;
    std::thread mLoadThread;
    std::mutex mScheduleItemsMutex;
    std::mutex mPlayersMutex;
    std::deque<std::shared_ptr<audio::Player>> mPlayers{};
    std::shared_ptr<audio::Player> mActivePlayer = nullptr;
    std::unique_ptr<PlayerFactory> mPlayerFactory = nullptr;
    std::shared_ptr<api::Program> mCurrProgram = nullptr;
    std::vector<std::shared_ptr<PlayItem>> mScheduleItems;
    util::Timer mReportTimer;
    util::Timer mTCPUpdateTimer;
    util::Timer mEjectTimer;
    dispatch_queue mAPIReportQueue;
    dispatch_queue mItemChangedQueue;
    // audio::Player* mPlayerPtrs[3];
    // std::atomic<size_t> mPlayerPtrIdx;
    
    
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
        mEjectTimer(1),
        mAPIReportQueue("report queue", 1),
        mItemChangedQueue("change queue", 1)
    {
        mCalendar.calendarChangedCallback = [this](const auto& items) { this->onCalendarChanged(items); };
        mSilenceDet.silenceChangedCallback = [this](const auto& silence) { this->onSilenceChanged(silence); };
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
        mScheduleThread = std::thread(&Engine::runSchedule, this);
        mLoadThread = std::thread(&Engine::runLoad, this);
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
            log.error() << "Engine failed to start TCP server: " << e.what();
        }
        log.info() << "Engine started";
    }

    void stop() {
        log.debug() << "Engine stopping...";
        mRunning = false;
        if (mScheduleThread.joinable()) mScheduleThread.join();
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


    // thread-safe getter and setter for player queue
    auto getPlayers() {
        std::deque<std::shared_ptr<audio::Player>> players;
        {
            std::lock_guard<std::mutex> lock(mPlayersMutex);
            players = mPlayers;
        }
        return players;
    }

    void setPlayers(const std::deque<std::shared_ptr<audio::Player>>& tPlayers) {
        std::lock_guard<std::mutex> lock(mPlayersMutex);
        mPlayers = tPlayers;
    }


    // work thread
    void runWork() {
        while (mRunning) {

            if (mSilenceDet.silenceDetected()) {
                mFallback.start();
            } else {
                mFallback.stop();
            }

            auto players = getPlayers();
            for (const auto& player : players) {
                if (player->isPlaying() && player != mActivePlayer) {
                    // log.debug() << "set active player to " << player->name;
                    mActivePlayer = player;
                    break;
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }


    // schedule thread
    void runSchedule() {
        while (mRunning) {

            if (mEjectTimer.query()) {
                cleanPlayers();
            }
            
            if (mScheduleItemsChanged) {
                mScheduleItemsChanged = false;
                refreshPlayers();
                mScheduleItems.clear();
            }

            if (mTCPServer.connected() && mTCPUpdateTimer.query()) {
                updateStatus();
            }

            if (mReportTimer.query()) {
                mAPIReportQueue.dispatch([this] {
                    postHealth();
                });
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void cleanPlayers() {
        std::lock_guard<std::mutex> lock(mPlayersMutex);
        while (!mPlayers.empty() && mPlayers.front()->isFinished()) {
            // mPlayers.front()->stop();
            mPlayers.pop_front();
        }
    }

    void refreshPlayers() {
        log.debug() << "Engine refreshPlayers";

        auto oldPlayers = getPlayers();
        std::deque<std::shared_ptr<audio::Player>> newPlayers;

        // push existing players matching new play items
        for (const auto& item : mScheduleItems) {
            if (item->end < std::time(0)) continue;

            auto it = std::find_if(oldPlayers.begin(), oldPlayers.end(), [&](const auto& plr) { return  plr->playItem == item; });
            if (it != oldPlayers.end()) {
                newPlayers.push_back(*it);
            } else {
                auto player = mPlayerFactory->createPlayer(item);
                player->startCallback = [this](auto itm) { this->onPlayerStart(itm); };
                player->schedule(item);
                newPlayers.push_back(player);
            }
        }
        
        // stop players not matching new play items
        for (const auto& player : oldPlayers) {
            auto it = std::find_if(mScheduleItems.begin(), mScheduleItems.end(), [&](const auto& itm) { return  itm == player->playItem; });
            if (it == mScheduleItems.end()) {
                player->stop();
            }
        }

        setPlayers(newPlayers);
    }


    // load thread (serial for each player)
    void runLoad() {
        while (mRunning) {
            auto players = getPlayers();
            for (const auto& player : players) {
                if (player && player->needsLoad()) {
                    player->tryLoad();
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }


    // callbacks
    
    void onCalendarChanged(const std::vector<std::shared_ptr<PlayItem>>& tItems) {
        log.debug() << "Engine onCalendarChanged";
        std::lock_guard<std::mutex> lock(mScheduleItemsMutex);
        mScheduleItems = tItems;
        mScheduleItemsChanged = true;
    }

    void onPlayerStart(std::shared_ptr<PlayItem> tItem) {
        log.debug() << "Engine onPlayerStart";
        if (!tItem) {
            log.error() << "Engine playerStartCallback item is null";
            return;
        }
        mItemChangedQueue.dispatch([item=tItem, this] {
            playItemChanged(item);
        });
    }

    void onSilenceChanged(const bool& tSilence) {
        log.debug() << "Engine onSilenceChanged " << tSilence;
        if (tSilence) mFallback.start();
        else mFallback.stop();
    }


    // playitem and program change handler

    void playItemChanged(std::shared_ptr<PlayItem> tItem) {
        if (mStreamOutput.isRunning() && !mConfig.streamOutMetadataURL.empty()) {
            // log.debug() << "Engine updating stream metadata";
            try {
                mStreamOutput.updateMetadata(mConfig.streamOutMetadataURL, tItem);
            }
            catch (const std::exception& e) {
                log.error() << "Engine failed to update stream metadata: " << e.what();
            }
        }
    
        if (mCurrProgram != tItem->program && (!mCurrProgram || !tItem->program || *mCurrProgram != *tItem->program)) {
            mCurrProgram = tItem->program;
            programChanged();
        }
    }

    void programChanged() {
        log.info(Log::Cyan) << "Program changed to '" << mCurrProgram->showName << "'";

        if (mCurrProgram && mConfig.audioRecordPath.size() > 0) {
            mRecorder.stop();

            if (mCurrProgram->showId > 1) {
                auto recURL = mConfig.audioRecordPath + "/" + util::utcFmt() + "_" + mCurrProgram->showName + ".mp3";
                try {
                    std::unordered_map<std::string, std::string> metadata = {}; // {{"artist", item->program->showName }, {"title", item->program->episodeTitle}};
                    mRecorder.start(recURL, metadata);
                }
                catch (const std::runtime_error& e) {
                    log.error() << "Engine failed to start recorder for url: " << recURL << " " << e.what();
                }
            }
        }
    }


    // temporary monitoring "UI" 
    std::ostringstream strstr;

    void updateStatus() {
        auto players = getPlayers();

        // strstr.flush();
        // strstr << "\x1b[5A";
        strstr << "____________________________________________________________________________________________________________\n";
        strstr << "RMS: " << std::fixed << std::setprecision(2) << util::linearDB(mSilenceDet.currentRMS()) << " dB\n";
        strstr << "Fallback: " << (mFallback.isActive() ? "ACTIVE" : "INACTIVE") << '\n';
        strstr << "Player queue (" << players.size() << " items):\n";

        audio::Player::getStatusHeader(strstr);
        for (auto player : players) {
            if (player) player->getStatus(strstr);
        }
        strstr << std::endl;

        mTCPServer.pushStatus(strstr.str());
        // log.debug() << statusSS.str();
    }


    // post to API

    void postPlaylog(std::shared_ptr<PlayItem> tPlayItem) {
        if (tPlayItem == nullptr || mConfig.playlogURL.empty()) return;
        try {
            mAPIClient.postPlaylog({*tPlayItem});
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
    

    // realtime thread or called by manual render function
    
    void renderCallback(const audio::sam_t* in,  audio::sam_t* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(audio::sam_t));

        auto player = mActivePlayer;
        if (player) {
            // std::cout << player->name << " ";
            player->process(in, out, nframes);
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
