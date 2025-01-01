#include <string>
#include <vector>
#include <deque>
#include <ranges>
#include <atomic>
#include <thread>
#include <mutex>
#include "Config.hpp"
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
    std::deque<PlayItem> mItemsToSchedule = {};
    std::deque<PlayItem> mScheduleItems = {};
    std::mutex mMutex;

    double mSampleRate = 44100;
    size_t mBufferSize = 1024;

    const Config& mConfig;
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

        for (auto& proc : mPlayers) {
            proc->playItemDidStartCallback = [this](auto item) { this->playItemDidStart(item); };
        }

        mAudioClient.setRenderer(this);
    }

    void start() {
        log.debug() << "Engine starting...";
        mRunning = true;
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
        log.info() << "Engine started";
    }

    void stop() {
        log.debug() << "Engine stopping...";
        mRunning = false;
        mRecorder.stop();
        for (const auto& source : mPlayers) source->stop();
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

        time_t now = std::time(0);
        for (const auto& source : mPlayers) {
            source->work();
        }

        // std::lock_guard lock(mMutex);
        if (mItemsToSchedule.size() > 0) {
            auto item = mItemsToSchedule.front();
            mItemsToSchedule.pop_front();

            if (std::find(mScheduleItems.begin(), mScheduleItems.end(), item) != mScheduleItems.end()) {
                return;
            }

            now = std::time(0);
            if (now - item.lastTry >= item.retryInterval) {
                try {
                    auto sources = mPlayers | std::ranges::views::filter([&](auto v){ return v->canPlay(item); });
                    if (sources.empty()) throw std::runtime_error("Engine could not find processor for uri " + item.uri);
                    auto freeSources = sources | std::ranges::views::filter([&](auto v){ return v->getState() == audio::Player::State::IDLE; });
                    if (!freeSources.empty()) {
                        load(item);
                        mScheduleItems.push_back(item);
                    } else {
                        mItemsToSchedule.push_front(item);
                    }
                }
                catch (const std::exception& e) {
                    log.error() << "Engine failed to load item: " << e.what();
                    item.lastTry = now;
                    // mItemsToSchedule.push_front(item);
                }
            }
        }

        now = std::time(0);
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

    void schedule(const PlayItem& item) {
        // std::lock_guard lock(mMutex);
        mItemsToSchedule.push_back(item);
    }

    void load(const PlayItem& item) {
        log.info(Log::Magenta) << "Engine load " << item.uri;
        auto it = std::find_if(mPlayers.begin(), mPlayers.end(), [&](auto p) { return p->accepts(item); });
        if (it == mPlayers.end()) {
            log.error() << "Could not find available player for item " << item.uri;
            return;
        }
        auto source = *it;
        source->schedule(item);
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