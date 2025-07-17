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
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Lesser General Public License.
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
#include "io/WebService.hpp"
#include "io/SMTPSender.hpp"
#include "api/APIClient.hpp"
#include "dsp/AudioClient.hpp"
#include "dsp/LinePlayer.hpp"
#include "dsp/FilePlayer.hpp"
#include "dsp/StreamPlayer.hpp"
#include "dsp/FallbackPremix.hpp"
#include "dsp/SilenceDetector.hpp"
#include "dsp/Recorder.hpp"
#include "dsp/StreamOutput.hpp"
#include "dsp/StreamProvider.hpp"
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

    const Config mConfig;
    const audio::AudioStreamFormat mClientFormat;
    std::unique_ptr<Calendar> mCalendar;
    std::unique_ptr<io::SMTPSender> mSMTPSender;
    std::unique_ptr<api::Client> mAPIClient;
    std::unique_ptr<PlayerFactory> mPlayerFactory;
    ctl::Parameters mParameters;
    ctl::Status mStatus;
    std::unique_ptr<io::WebService> mWebService;
    audio::Client mAudioClient;
    audio::SilenceDetector mSilenceDet;
    audio::SilenceDetector mInputMeter;
    audio::FallbackPremix mFallback;
    audio::Recorder mScheduleRecorder;
    audio::Recorder mBlockRecorder;
    audio::StreamOutput mStreamOutput;
    audio::StreamProvider mStreamProvider;
    std::atomic<bool> mRunning = false;
    std::thread mScheduleThread;
    std::thread mLoadThread;
    std::atomic<std::deque<std::shared_ptr<audio::Player>>*> mPlayers{};
    std::deque<std::shared_ptr<audio::Player>> mPlayersBuf1, mPlayersBuf2;
    
    std::shared_ptr<api::Program> mCurrProgram = nullptr;
    util::ManualTimer mEjectTimer;
    util::AsyncTimer mReportTimer;
    util::AsyncAlignedTimer mBlockRecordTimer;
    util::TaskQueue mPlayerModifyQueue;
    util::TaskQueue mItemChangeQueue;
    util::TaskQueue mParamChangeQueue;
    util::TaskQueue mReportQueue;
    util::TaskQueue mMailSendQueue;
    // audio::Player* mPlayerPtrs[3];
    // std::atomic<size_t> mPlayerPtrIdx;
    time_t mStartTime;
    float mOutputGainLog = 0.0f;
    std::atomic<float> mOutputGainLin = 1.0f;
    
    
public:
    Engine(Config tConfig) :
        mConfig(tConfig),
        mClientFormat(mConfig.sampleRate, mConfig.samplesPerFrame, 2),
        mCalendar(std::make_unique<Calendar>(mConfig)),
        mAPIClient(std::make_unique<api::Client>(mConfig)),
        mSMTPSender(std::make_unique<io::SMTPSender>()),
        mParameters(mConfig.parametersPath),
        mWebService(std::make_unique<io::WebService>(mConfig.webControlHost, mConfig.webControlPort, mConfig.webControlAuthUser, mConfig.webControlAuthPass, mConfig.webControlAuthToken, mConfig.webControlStatic, mConfig.webControlAudioStream, mParameters, mStatus)),
        mPlayerFactory(std::make_unique<PlayerFactory>(mClientFormat, mConfig)),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mConfig.sampleRate, mConfig.samplesPerFrame),
        mSilenceDet(mClientFormat, mConfig.silenceThreshold, mConfig.silenceStartDuration, mConfig.silenceStopDuration),
        mInputMeter(mClientFormat, 0, 0, 0),
        mFallback(mClientFormat, mConfig.audioFallbackPath, mConfig.preloadTimeFallback, mConfig.fallbackCrossFadeTime, mConfig.fallbackShuffle, mConfig.fallbackSineSynth),
        mScheduleRecorder(mClientFormat, mConfig.recordScheduleBitRate),
        mBlockRecorder(mClientFormat, mConfig.recordBlockBitRate),
        mStreamOutput(mClientFormat, mConfig.streamOutBitRate),
        mStreamProvider(mClientFormat, 128000),
        mEjectTimer(1),
        mReportTimer(mConfig.healthReportInterval),
        mBlockRecordTimer(mConfig.recordBlockDuration),
        mStartTime(std::time(0))
    {
        mCalendar->calendarChangedCallback = [this](const auto& items) { onCalendarChanged(items); };
        mSilenceDet.silenceChangedCallback = [this](const auto& silence) { onSilenceChanged(silence); };
        mFallback.startCallback = [this](auto itm) { onPlayerStart(itm); };
        mReportTimer.callback = [this] { onReportStatus(); };
        mBlockRecordTimer.callback = [this] { onRecordBlockChanged(); };
        mParameters.onParametersChanged = [this] { onParametersChanged(); };
        mParameters.publish();
        mAudioClient.setRenderer(this);
        mScheduleRecorder.logName = "Schedule Recorder";
        mBlockRecorder.logName = "Block Recorder";
        mWebService->audioStreamBuffer = &mStreamProvider.mRingBufferO;
    }

    void parseArgs(const std::unordered_map<std::string,std::string>& tArgs) {
        try {
            auto calFile = tArgs.at("--calendar");
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
        if (mConfig.healthURL.size()) {
            mReportTimer.start();
        }
        if (mConfig.recordBlockPath.size()) {
            mBlockRecordTimer.start();
        }
        if (mConfig.webControlAudioStream) {
            mStreamProvider.start();
        }

        try {
            if (!mConfig.streamOutURL.empty()) mStreamOutput.start(mConfig.streamOutURL);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to start output stream: " << e.what();
        }

        log.info() << "Engine started";

        mWebService->start();
    }

    void stop() {
        log.debug() << "Engine stopping...";
        mRunning = false;
        mWebService->stop();
        mCalendar->stop();
        mReportTimer.stop();
        if (mScheduleThread.joinable()) mScheduleThread.join();
        if (mLoadThread.joinable()) mLoadThread.join();
        mScheduleRecorder.stop();
        mBlockRecorder.stop();
        mFallback.terminate();
        for (const auto& player : mPlayersBuf1) player->stop();
        for (const auto& player : mPlayersBuf2) player->stop();
        mStreamOutput.stop();
        mStreamProvider.stop();
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
                mPlayerModifyQueue.async([this] {
                    cleanPlayers();
                });
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
            players.pop_front();
        }
        setPlayers(players);
    }

    void schedulePlayers(std::vector<std::shared_ptr<PlayItem>> tScheduleItems) {
        log.debug() << "Engine schedulePlayers";

        auto itemsEqual = [](const std::shared_ptr<PlayItem>& lhs, const std::shared_ptr<PlayItem>& rhs) {
            return lhs && rhs && *lhs == *rhs;
        };

        auto oldPlayers = getPlayers();
        std::deque<std::shared_ptr<audio::Player>> newPlayers;

        // push existing players matching new play items
        for (const auto& item : tScheduleItems) {
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
            auto it = std::find_if(tScheduleItems.begin(), tScheduleItems.end(), [&](const auto& itm) { return itemsEqual(itm, player->playItem); });
            if (it == tScheduleItems.end()) {
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
        mMailSendQueue.async([this, silence=tSilence] {
            sendSilenceNotificationMail(silence);
        });
    }
    
    void onCalendarChanged(const std::vector<std::shared_ptr<PlayItem>>& tItems) {
        log.debug() << "Engine onCalendarChanged";
        mPlayerModifyQueue.async([this, items=tItems] {
            schedulePlayers(items);
        });
    }

    void onPlayerStart(std::shared_ptr<PlayItem> tItem) {
        log.debug() << "Engine onPlayerStart";
        if (!tItem) {
            log.error() << "Engine playerStartCallback item is null";
            return;
        }
        mItemChangeQueue.async([this, item=tItem] {
            playItemChanged(item);
        });
    }

    void onReportStatus() {
        log.debug() << "Engine onReportStatus";
        mReportQueue.async([this] {
            postStatus();
        });
    }

    void onRecordBlockChanged() {
        log.debug() << "Engine onRecordBlockChanged";
        mBlockRecorder.stop();
        auto recURL = mConfig.audioRecordPath + "/" + mConfig.recordBlockPath + "/" + util::fileTimestamp() + "." + mConfig.recordBlockFormat;
        try {
            mBlockRecorder.start(recURL);
        }
        catch (const std::runtime_error& e) {
            log.error() << "Engine failed to start block recorder: " << e.what();
        }
    }

    void onParametersChanged() {
        mParamChangeQueue.async([this] {
            auto params = mParameters.get();
            auto outGainLog = params.outputGain.load();
            if (outGainLog != mOutputGainLog) {
                mOutputGainLog = outGainLog;
                mOutputGainLin = util::dbLinear(mOutputGainLog);
                log.info() << "Engine output gain changed to " << mOutputGainLog << " dB / " << mOutputGainLin << " linear";
            }
        });
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

        mReportQueue.async([this, item=tItem] {
            postPlaylog(item);
        });
    }

    void programChanged() {
        log.info(Log::Cyan) << "Program changed to '" << mCurrProgram->showName << "'";

        if (mCurrProgram && mConfig.recordSchedulePath.size()) {
            mScheduleRecorder.stop();

            if (mCurrProgram->showId > 1) {
                auto recURL = mConfig.audioRecordPath + "/" + mConfig.recordSchedulePath + "/" + util::fileTimestamp() + "_" + mCurrProgram->showName + "." + mConfig.recordScheduleFormat;
                try {
                    std::unordered_map<std::string, std::string> metadata = {}; // {{"artist", item->program->showName }, {"title", item->program->episodeTitle}};
                    mScheduleRecorder.start(recURL, metadata);
                }
                catch (const std::runtime_error& e) {
                    log.error() << "Engine failed to start recorder for url: " << recURL << " " << e.what();
                }
            }
        }
    }

    void updateWebService() {
        auto players = getPlayers();
        mStatus.rmsLinIn = mInputMeter.currentRMS();
        mStatus.rmsLinOut = mSilenceDet.currentRMS();
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

    // send mail notification on silence

    void sendSilenceNotificationMail(bool tSilence) {
        if (mConfig.smtpURL.empty()) return;
        std::string action = tSilence ? "started" : "stopped";
        std::string subject = "Castor Silence Notification (" + action + ")";
        std::string body = "Silence " + action + " at " + util::currTimeFmtMs() + "\n\n";
        try {
            mSMTPSender->send(mConfig.smtpURL, mConfig.smtpUser, mConfig.smtpPass, mConfig.smtpSenderName, mConfig.smtpSenderAddress, mConfig.smtpRecipients, subject, body);
            log.info() << "Engine sent mail notification to: " << mConfig.smtpRecipients;
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to send mail: " << e.what();
        }
    }
    

    // realtime thread or called by manual render function
    
    void renderCallback(const audio::sam_t* in,  audio::sam_t* out, size_t nframes) override {
        memset(out, 0, nframes * mClientFormat.channelCount * sizeof(audio::sam_t));

        mInputMeter.process(in, nframes);

        auto players = getPlayers();
        for (const auto& player : players) {
            if (player && player->isPlaying()) {
                player->process(in, out, nframes);
                break;
            }
        }

        mSilenceDet.process(out, nframes);
        mFallback.process(in, out, nframes);

        if (mOutputGainLin != 1.0f) {
            for (auto i = 0; i < nframes; ++i) {
                auto iL = i * mClientFormat.channelCount;
                auto iR = iL + 1;
                out[iL] *= mOutputGainLin;
                out[iR] *= mOutputGainLin;
            }
        }

        if (mScheduleRecorder.isRunning()) {
            mScheduleRecorder.process(out, nframes);
        }

        if (mBlockRecorder.isRunning()) {
            mBlockRecorder.process(out, nframes);
        }

        if (mStreamOutput.isRunning()) {
            mStreamOutput.process(out, nframes);
        }

        if (mStreamProvider.isRunning()) {
            mStreamProvider.process(out, nframes);
        }
    }

};

}
