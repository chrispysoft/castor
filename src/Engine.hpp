#include <string>
#include <vector>
#include <deque>
#include <ranges>
#include <atomic>
#include <thread>
#include <mutex>
#include "Config.hpp"
#include "Calendar.hpp"
#include "util/Log.hpp"
#include "util/util.hpp"
#include "api/APIClient.hpp"
#include "dsp/AudioClient.hpp"
#include "dsp/LinePlayer.hpp"
#include "dsp/MP3Player.hpp"
#include "dsp/QueuePlayer.hpp"
#include "dsp/Fallback.hpp"
#include "dsp/SilenceDetector.hpp"
#include "dsp/Recorder.hpp"
#include "dsp/StreamOutput.hpp"

namespace cst {
class Engine : public audio::Client::Renderer {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::deque<PlayItem> mScheduledItems = {};
    std::mutex mMutex;

    double mSampleRate = 44100;
    size_t mBufferSize = 1024;

    const Config& mConfig;
    Calendar mCalendar;
    api::Client mAPIClient;
    audio::Client mAudioClient;
    audio::SilenceDetector mSilenceDet;
    audio::Fallback mFallback;
    audio::Recorder mRecorder;
    audio::StreamOutput mStreamOutput;
    std::vector<float> mMixBuffer;
    std::deque<std::shared_ptr<audio::Player>> mPlayers = {};
    time_t mLastReportTime = 0;
    time_t mReportInterval = 300;
    
public:
    Engine(const Config& tConfig) :
        mConfig(tConfig),
        mCalendar(mConfig),
        mAPIClient(mConfig),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mSampleRate, mBufferSize),
        mSilenceDet(),
        mFallback(mSampleRate, mConfig.audioFallbackPath),
        mRecorder(mSampleRate),
        mStreamOutput(mSampleRate),
        mMixBuffer(mBufferSize * 2)
    {
        mPlayers.push_back(std::make_shared<audio::MP3Player>(mSampleRate, "Player 1"));
        mPlayers.push_back(std::make_shared<audio::MP3Player>(mSampleRate, "Player 2"));
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
        try {
            if (!mConfig.audioRecordPath.empty()) mRecorder.start(mConfig.audioRecordPath + "/test.mp3");
        }
        catch (const std::runtime_error& e) {
            log.error() << "Engine failed to start recorder: " << e.what();
        }
        try {
            if (!mConfig.streamOutURL.empty()) mStreamOutput.start(mConfig.streamOutURL);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to start output stream: " << e.what();
        }
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                this->work();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        for (auto& player : mPlayers) player->run();
        log.info() << "Engine started";
    }

    void stop() {
        log.debug() << "Engine stopping...";
        mRunning = false;
        mCalendar.stop();
        mRecorder.stop();
        for (const auto& player : mPlayers) player->terminate();
        mFallback.stop();
        mStreamOutput.stop();
        mAudioClient.stop();
        if (mWorker && mWorker->joinable()) mWorker->join();
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
        for (auto item : items) {
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
    }

    void playItemDidStart(const std::shared_ptr<PlayItem>& item) {
        try {
            mAPIClient.postPlaylog(*item);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post playlog: " << e.what();
        }
    }

    void postHealth() {
        try {
            Health health {true, util::currTimeFmtMs(), ":)"};
            mAPIClient.postHealth(health);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post health: " << e.what();
        }
    }

    void renderCallback(const float* in, float* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(float));

        for (auto source : mPlayers) {
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