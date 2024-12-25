#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>
#include <mutex>
#include "../common/Config.hpp"
#include "../common/Log.hpp"
#include "../common/util.hpp"
#include "../core/AudioClient.hpp"
#include "../core/LinePlayer.hpp"
#include "../core/MP3Player.hpp"
#include "../core/QueuePlayer.hpp"
#include "../core/StreamPlayer.hpp"
#include "../core/Fallback.hpp"
#include "../core/SilenceDetector.hpp"
#include "../core/Recorder.hpp"
#include "../core/StreamOutput.hpp"

namespace cst {
class Player : public AudioClientRenderer {

    std::string mRecordURL = "";//../audio/test_recording.mp3";

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::vector<std::shared_ptr<AudioProcessor>> mSources = {};
    std::vector<std::shared_ptr<AudioProcessor>> mActiveSources = {};
    std::deque<PlayItem> mItemsToSchedule = {};
    std::deque<PlayItem> mScheduleItems = {};
    std::mutex mMutex;

    double mSampleRate = 44100;
    size_t mBufferSize = 1024;

    const Config& mConfig;
    AudioClient mAudioClient;
    SilenceDetector mSilenceDet;
    Fallback mFallback;
    Recorder mRecorder;
    StreamOutput mStreamOutput;
    std::vector<float> mMixBuffer;
    
public:
    Player(const Config& tConfig) :
        mConfig(tConfig),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mSampleRate, mBufferSize),
        mSilenceDet(),
        mFallback(mSampleRate),
        mRecorder(mSampleRate),
        mStreamOutput(mSampleRate),
        mMixBuffer(mBufferSize * 2)
    {
        mAudioClient.setRenderer(this);
    }

    void run() {
        mRunning = true;
        mAudioClient.start();
        if (!mRecordURL.empty()) mRecorder.start(mRecordURL);
        if (!mConfig.streamOutURL.empty()) mStreamOutput.start(mConfig.streamOutURL);
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                this->work();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        mWorker->join();
    }

    void terminate() {
        log.debug() << "Player terminating...";
        mRecorder.stop();
        mStreamOutput.stop();
        mAudioClient.stop();
        mRunning = false;
        log.debug() << "Player terminated";
    }

    void work() {
        if (mSilenceDet.silenceDetected()) {
            mFallback.start();
        } else {
            mFallback.stop();
        }

        std::vector<std::shared_ptr<AudioProcessor>> activeSources = {};
        time_t now = std::time(0);
        for (const auto& source : mSources) {
            auto state = source->getState(now);
            switch (state) {
                case AudioProcessor::State::QUEUED:
                case AudioProcessor::State::PLAYING: {
                    activeSources.push_back(source);
                    break;
                }
                default: break;
            }
        }

        if (mActiveSources != activeSources) {
            mActiveSources = activeSources;
            log.debug() << "Player active sources changed (" << mActiveSources.size() << ")";
        }

        std::lock_guard lock(mMutex);
        if (mItemsToSchedule.size() > 0) {
            auto item = mItemsToSchedule.back();
            mItemsToSchedule.pop_back();

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
                    log.error() << "Player failed to load item";
                    item.lastTry = now;
                    // mItemsToSchedule.push_front(item);
                }
            }
        }
    }

    void schedule(const PlayItem& item) {
        std::lock_guard lock(mMutex);
        mItemsToSchedule.push_back(item);
    }

    void load(const PlayItem& item) {
        log.warn() << "Player load " << item.uri;
        if (item.uri.starts_with("line")) {
            auto source = std::make_shared<LinePlayer>(mSampleRate);
            source->tsStart = item.start;
            source->tsEnd = item.end;
            mSources.push_back(source);
        }
        else if (item.uri.starts_with("http")) {
            auto source = std::make_shared<StreamPlayer>(mSampleRate);
            source->open(item.uri);
            source->tsStart = item.start;
            source->tsEnd = item.end;
            mSources.push_back(source);
        }
        else if (item.uri.starts_with("/")) {
            auto source = std::make_shared<QueuePlayer>(mSampleRate);
            auto pos = std::time(0) - item.start;
            source->open(item.uri, pos);
            source->tsStart = item.start;
            source->tsEnd = item.end;
            mSources.push_back(source);
        }
    }

    void renderCallback(const float* in, float* out, size_t nframes) override {
        auto nsamples = nframes * 2;
        memset(out, 0, nsamples * sizeof(float));

        const auto activeSources = mActiveSources;
        if (activeSources.size() == 1) {
            activeSources.front()->process(in, out, nframes);
        } else {
            for (auto source : activeSources) {
                source->process(in, mMixBuffer.data(), nframes);
                for (auto i = 0; i < mMixBuffer.size(); ++i) {
                    out[i] += mMixBuffer[i];
                }
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