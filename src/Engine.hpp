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

#include <atomic>
#include <deque>
#include <optional>
#include <ranges>
#include <string>
#include <thread>
#include <vector>
#include "Config.hpp"
#include "Calendar.hpp"
#include "ctl/RemoteControl.hpp"
#include "io/TCPServer.hpp"
#include "io/WebService.hpp"
#include "api/APIClient.hpp"
#include "dsp/AudioClient.hpp"
#include "dsp/LinePlayer.hpp"
#include "dsp/FilePlayer.hpp"
#include "dsp/StreamPlayer.hpp"
#include "dsp/FallbackPremix.hpp"
#include "dsp/SilenceDetector.hpp"
#include "dsp/Recorder.hpp"
#include "dsp/StreamOutput.hpp"
#include "util/Log.hpp"
#include "util/util.hpp"

namespace castor {

class PlayerFactory {
    const audio::AudioStreamFormat& mClientFormat;
    const Config& mConfig;
    // std::mutex mMutex;
    
public:
    PlayerFactory(const audio::AudioStreamFormat& tClientFormat, const Config& tConfig) :
        mClientFormat(tClientFormat),
        mConfig(tConfig)
    {}

    std::shared_ptr<audio::Player> createPlayer(std::shared_ptr<PlayItem> tPlayItem) {
        auto uri = tPlayItem->uri;
        auto name = uri.substr(uri.rfind('/')+1);
        // std::lock_guard<std::mutex> lock(mMutex);
        // log.debug(Log::Magenta) << "PlayerFactory createPlayer " << name;
        if (uri.starts_with("line"))
            return std::make_shared<audio::LinePlayer>(mClientFormat, name, mConfig.preloadTimeLine, mConfig.programFadeInTime, mConfig.programFadeOutTime);
        else if (uri.starts_with("http"))
            return std::make_shared<audio::StreamPlayer>(mClientFormat, name, mConfig.preloadTimeStream, mConfig.programFadeInTime, mConfig.programFadeOutTime);
        else
            return std::make_shared<audio::FilePlayer>(mClientFormat, name, mConfig.preloadTimeFile, mConfig.programFadeInTime, mConfig.programFadeOutTime);
    }

    void returnPlayer(std::shared_ptr<audio::Player> tPlayer) {
        // std::lock_guard<std::mutex> lock(mMutex);
    }
};


class Engine : public audio::Client::Renderer {

    const Config& mConfig;
    const audio::AudioStreamFormat mClientFormat;
    std::unique_ptr<Calendar> mCalendar;
    std::unique_ptr<io::TCPServer> mTCPServer;
    std::unique_ptr<api::Client> mAPIClient;
    std::unique_ptr<PlayerFactory> mPlayerFactory;
    ctl::RemoteControl mRemote;
    ctl::Parameters mParameters;
    ctl::Status mStatus;
    std::unique_ptr<io::WebService> mWebService;
    audio::Client mAudioClient;
    audio::SilenceDetector mSilenceDet;
    audio::FallbackPremix mFallback;
    audio::Recorder mRecorder;
    audio::StreamOutput mStreamOutput;
    std::atomic<bool> mRunning = false;
    std::atomic<bool> mScheduleItemsChanged = false;
    std::thread mScheduleThread;
    std::thread mLoadThread;
    std::mutex mScheduleItemsMutex;
    std::mutex mPlayersMutex;
    std::atomic<std::deque<std::shared_ptr<audio::Player>>*> mPlayers{};
    std::deque<std::shared_ptr<audio::Player>> mPlayersBuf1, mPlayersBuf2;
    
    std::shared_ptr<api::Program> mCurrProgram = nullptr;
    std::vector<std::shared_ptr<PlayItem>> mScheduleItems;
    util::ManualTimer mTCPUpdateTimer;
    util::ManualTimer mEjectTimer;
    util::AsyncTimer mReportTimer;
    util::AsyncWorker<std::shared_ptr<PlayItem>> mItemChangeWorker;
    // audio::Player* mPlayerPtrs[3];
    // std::atomic<size_t> mPlayerPtrIdx;
    time_t mStartTime;
    
    
public:
    Engine(const Config& tConfig) :
        mConfig(tConfig),
        mClientFormat(mConfig.sampleRate, mConfig.samplesPerFrame, 2),
        mCalendar(std::make_unique<Calendar>(mConfig)),
        mTCPServer(std::make_unique<io::TCPServer>(mConfig.tcpPort)),
        mAPIClient(std::make_unique<api::Client>(mConfig)),
        mParameters(mConfig.parametersPath),
        mWebService(std::make_unique<io::WebService>(mConfig.webControlHost, mConfig.webControlPort, mConfig.webControlStaticPath, mParameters, mStatus)),
        mPlayerFactory(std::make_unique<PlayerFactory>(mClientFormat, mConfig)),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mConfig.sampleRate, mConfig.samplesPerFrame),
        mSilenceDet(mClientFormat, mConfig.silenceThreshold, mConfig.silenceStartDuration, mConfig.silenceStopDuration),
        mFallback(mClientFormat, mConfig.audioFallbackPath, mConfig.preloadTimeFallback, mConfig.fallbackCrossFadeTime, mConfig.fallbackShuffle, mConfig.fallbackSineSynth),
        mRecorder(mClientFormat, mConfig.recorderBitRate),
        mStreamOutput(mClientFormat, mConfig.streamOutBitRate),
        mTCPUpdateTimer(1),
        mEjectTimer(1),
        mReportTimer(mConfig.healthReportInterval),
        mStartTime(std::time(0))
    {
        mCalendar->calendarChangedCallback = [this](const auto& items) { this->onCalendarChanged(items); };
        mSilenceDet.silenceChangedCallback = [this](const auto& silence) { this->onSilenceChanged(silence); };
        mFallback.startCallback = [this](auto itm) { this->onPlayerStart(itm); };
        mReportTimer.callback = [this] { postStatus(); };
        mItemChangeWorker.callback = [this](const auto& item) { playItemChanged(item); };
        mAudioClient.setRenderer(this);
        mTCPServer->onDataReceived = [this](const auto& command) { return mRemote.executeCommand(command, ""); };
        mTCPServer->welcomeMessage = "f1: fallback start, f0: fallback stop, s: status\n";
        mRemote.registerCommand("f1\n", [this] { mFallback.start(); });
        mRemote.registerCommand("f0\n", [this] { mFallback.stop(); });
        mRemote.registerCommand("s\n", [this] { updateStatus(); });
    }

    void parseArgs(std::unordered_map<std::string,std::string> tArgs) {
        try {
            auto calFile = tArgs["--calendar"];
            if (!calFile.empty()) mCalendar->load(calFile);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to load calendar test file: " << e.what();
        }
    }

    void start() {
        log.debug() << "Engine starting...";
        mRunning = true;        
        mAudioClient.start(mConfig.realtimeRendering);
        mFallback.run();
        mCalendar->start();
        mScheduleThread = std::thread(&Engine::runSchedule, this);
        mLoadThread = std::thread(&Engine::runLoad, this);
        mReportTimer.start();
        mItemChangeWorker.start();

        try {
            if (!mConfig.streamOutURL.empty()) mStreamOutput.start(mConfig.streamOutURL);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to start output stream: " << e.what();
        }

        try {
            mTCPServer->start();
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to start TCP server: " << e.what();
        }
        log.info() << "Engine started";

        mWebService->start();
    }

    void stop() {
        log.debug() << "Engine stopping...";
        mRunning = false;
        mWebService->stop();
        mTCPServer->stop();
        mCalendar->stop();
        mReportTimer.stop();
        mItemChangeWorker.stop();
        if (mScheduleThread.joinable()) mScheduleThread.join();
        if (mLoadThread.joinable()) mLoadThread.join();
        mRecorder.stop();
        mFallback.terminate();
        for (const auto& player : mPlayersBuf1) player->stop();
        for (const auto& player : mPlayersBuf2) player->stop();
        mStreamOutput.stop();
        mAudioClient.stop();
        log.info() << "Engine stopped";
    }


    // thread-safe getter and setter for player queue
    std::deque<std::shared_ptr<audio::Player>> getPlayers() {
        auto playersPtr = mPlayers.load(std::memory_order_acquire);
        if (!playersPtr) return {};
        return *playersPtr;
    }

    void setPlayers(const std::deque<std::shared_ptr<audio::Player>>& tPlayers) {
        auto inactiveBuffer = (mPlayers.load() == &mPlayersBuf1) ? &mPlayersBuf2 : &mPlayersBuf1;
        *inactiveBuffer = tPlayers;
        mPlayers.store(inactiveBuffer, std::memory_order_release);
    }
    

    // schedule thread
    void runSchedule() {
        while (mRunning) {

            if (mEjectTimer.query()) {
                cleanPlayers();
            }
            
            if (mScheduleItemsChanged) {
                mScheduleItemsChanged = false;
                std::lock_guard<std::mutex> lock(mScheduleItemsMutex);
                refreshPlayers();
                mScheduleItems.clear();
            }

            if (mWebService->isClientConnected()) {
                updateWebService();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void cleanPlayers() {
        auto players = getPlayers();
        while (players.size() && players.front()->isFinished()) {
            players.front()->stop();
            players.pop_front();
        }
        setPlayers(players);
    }

    void refreshPlayers() {
        log.debug() << "Engine refreshPlayers";

        auto itemsEqual = [](const std::shared_ptr<PlayItem>& lhs, const std::shared_ptr<PlayItem>& rhs) {
            return lhs && rhs && *lhs == *rhs;
        };

        auto oldPlayers = getPlayers();
        std::deque<std::shared_ptr<audio::Player>> newPlayers;

        // push existing players matching new play items
        for (const auto& item : mScheduleItems) {
            if (item->end < std::time(0)) continue;

            auto it = std::find_if(oldPlayers.begin(), oldPlayers.end(), [&](const auto& plr) { return itemsEqual(plr->playItem, item); });
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
            auto it = std::find_if(mScheduleItems.begin(), mScheduleItems.end(), [&](const auto& itm) { return itemsEqual(itm, player->playItem); });
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

    void onSilenceChanged(const bool& tSilence) {
        log.debug() << "Engine onSilenceChanged " << tSilence;
        if (tSilence) mFallback.start();
        else mFallback.stop();
    }
    
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

        mItemChangeWorker.async(tItem); // playItemChanged async on same thread
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

        log.info(Log::Cyan) << "Track changed to '" << tItem->uri << "'";

        postPlaylog(tItem);
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
        // strstr.flush();
        // strstr << "\x1b[5A";
        auto players = getPlayers();
        strstr << "____________________________________________________________________________________________________________\n";
        strstr << "RMS: " << std::fixed << std::setprecision(2) << util::linearDB(mSilenceDet.currentRMS()) << " dB\n";
        strstr << "Fallback: " << (mFallback.isActive() ? "ACTIVE" : "INACTIVE") << '\n';
        strstr << "Player queue (" << players.size() << " items):\n";

        audio::Player::getStatusHeader(strstr);
        for (auto player : players) {
            if (player) player->getStatus(strstr);
        }
        strstr << std::endl;

        mTCPServer->pushStatus(strstr.str());
        // log.debug() << statusSS.str();
    }

    void updateWebService() {
        auto players = getPlayers();
        mStatus.rmsLin = mSilenceDet.currentRMS();
        nlohmann::json j = {};
        for (auto player : players) if (player) j += player->getStatusJSON();
        mStatus.players = j;
        mStatus.fallbackActive = mFallback.isActive();
    }


    // post to API

    void postPlaylog(std::shared_ptr<PlayItem> tPlayItem) {
        if (tPlayItem == nullptr || mConfig.playlogURL.empty()) return;
        try {
            mAPIClient->postPlaylog({*tPlayItem});
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post playlog: " << e.what();
        }
    }

    void postStatus() {
        if (mConfig.healthURL.empty()) return;
        try {
            auto uptime = std::time(0) - mStartTime;
            auto rms = util::linearDB(mSilenceDet.currentRMS());
            auto players = getPlayers();
            nlohmann::json j = {
                {"uptime", uptime},
                {"queue", players.size()},
                {"rms", rms},
                {"fallback", mFallback.isActive()}
            };
            
            mAPIClient->postHealth({true, util::currTimeFmtMs(), j.dump()});
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post health: " << e.what();
        }
    }
    

    // realtime thread or called by manual render function
    
    void renderCallback(const audio::sam_t* in,  audio::sam_t* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(audio::sam_t));

        auto players = getPlayers();
        for (const auto& player : players) {
            if (player && player->isPlaying()) {
                player->process(in, out, nframes);
                break;
            }
        }

        mSilenceDet.process(out, nframes);
        mFallback.process(in, out, nframes);

        if (mParameters.get().outputGain != 0) {
            float gain = util::dbLinear(mParameters.get().outputGain);
            for (auto i = 0; i < nframes; ++i) {
                out[i*2+0] *= gain;
                out[i*2+1] *= gain;
            }
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
