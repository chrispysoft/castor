#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>
#include <mutex>
#include "Config.hpp"
#include "util/Log.hpp"
#include "util/util.hpp"
#include "api/APIClient.hpp"
#include "dsp/AudioClient.hpp"
#include "dsp/LinePlayer.hpp"
#include "dsp/MP3Player.hpp"
#include "dsp/QueuePlayer.hpp"
#include "dsp/StreamPlayer.hpp"
#include "dsp/Fallback.hpp"
#include "dsp/SilenceDetector.hpp"
#include "dsp/Recorder.hpp"
#include "dsp/StreamOutput.hpp"

namespace cst {
class Engine : public AudioClientRenderer {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::deque<PlayItem> mItemsToSchedule = {};
    std::deque<PlayItem> mScheduleItems = {};
    std::mutex mMutex;

    double mSampleRate = 44100;
    size_t mBufferSize = 1024;

    const Config& mConfig;
    APIClient mAPIClient;
    AudioClient mAudioClient;
    SilenceDetector mSilenceDet;
    Fallback mFallback;
    Recorder mRecorder;
    StreamOutput mStreamOutput;
    std::vector<float> mMixBuffer;
    std::vector<std::shared_ptr<AudioProcessor>> mProcessors = {};
    
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
        mProcessors.push_back(std::make_shared<QueuePlayer>(mSampleRate, "File 1"));
        mProcessors.push_back(std::make_shared<QueuePlayer>(mSampleRate, "File 2"));
        mProcessors.push_back(std::make_shared<StreamPlayer>(mSampleRate, "Stream 1"));
        mProcessors.push_back(std::make_shared<StreamPlayer>(mSampleRate, "Stream 2"));
        mProcessors.push_back(std::make_shared<LinePlayer>(mSampleRate, "Line 1"));
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
        for (const auto& source : mProcessors) source->stop();
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
        for (const auto& source : mProcessors) {
            if (now >= source->tsStart && now <= source->tsEnd && source->state == AudioProcessor::State::CUE) {
                log.info(Log::Magenta) << "Engine setting " << source->name << " to PLAY";
                source->play();
                source->fadeIn();
                playingItemChanged(*source->playItem);
            }
            else if (now >= source->tsEnd - source->fadeOutTime && now < source->tsEnd && source->state == AudioProcessor::State::PLAY && !source->isFading()) {
                log.info(Log::Magenta) << "Engine fading out " << source->name;
                source->fadeOut();
            }
            else if (now >= source->tsEnd + 3 && source->state != AudioProcessor::State::IDLE) {
                log.info(Log::Magenta) << "Engine setting " << source->name << " to IDLE";
                source->stop();
                source->state = AudioProcessor::State::IDLE;
            }
        }

        // std::lock_guard lock(mMutex);
        if (mItemsToSchedule.size() > 0) {
            auto item = mItemsToSchedule.front();
            mItemsToSchedule.pop_front();

            if (std::find(mScheduleItems.begin(), mScheduleItems.end(), item) != mScheduleItems.end()) {
                return;
            }

            auto now = std::time(0);
            if (now - item.lastTry >= item.retryInterval) {
                try {
                    load(item);
                    mScheduleItems.push_back(item);
                }
                catch (const std::exception& e) {
                    log.error() << "Engine failed to load item";
                    item.lastTry = now;
                    // mItemsToSchedule.push_front(item);
                }
            }
        }
    }

    void playingItemChanged(const PlayItem& playItem) {
        try {
            mAPIClient.postPlaylog(playItem);
        }
        catch (const std::exception& e) {
            log.error() << "Engine failed to post playlog: " << e.what();
        }
    }

    void schedule(const PlayItem& item) {
        // std::lock_guard lock(mMutex);
        mItemsToSchedule.push_back(item);
    }

    void load(const PlayItem& item) {
        log.info(Log::Magenta) << "Engine load " << item.uri;
        auto it = std::find_if(mProcessors.begin(), mProcessors.end(), [&](std::shared_ptr<AudioProcessor>& p) { return p->accepts(item); });
        if (it == mProcessors.end()) {
            log.error() << "Could not find available player for item " << item.uri;
            return;
        }
        auto source = *it;
        source->schedule(item);
    }

    void renderCallback(const float* in, float* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(float));

        for (auto source : mProcessors) {
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