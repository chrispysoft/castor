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

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
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
    std::vector<std::shared_ptr<AudioProcessor>> mProcessors = {};
    
public:
    Player(const Config& tConfig) :
        mConfig(tConfig),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mSampleRate, mBufferSize),
        mSilenceDet(),
        mFallback(mSampleRate, mConfig.audioFallbackPath),
        mRecorder(mSampleRate),
        mStreamOutput(mSampleRate),
        mMixBuffer(mBufferSize * 2)
    {
        mProcessors.push_back(std::make_shared<QueuePlayer>(mSampleRate));
        mProcessors.push_back(std::make_shared<QueuePlayer>(mSampleRate));
        mProcessors.push_back(std::make_shared<StreamPlayer>(mSampleRate));
        mProcessors.push_back(std::make_shared<StreamPlayer>(mSampleRate));
        mProcessors.push_back(std::make_shared<LinePlayer>(mSampleRate));
        mAudioClient.setRenderer(this);
    }

    void run() {
        mRunning = true;
        mAudioClient.start();
        try {
            if (!mConfig.audioRecordPath.empty()) mRecorder.start(mConfig.audioRecordPath + "/test.mp3");
        }
        catch (const std::runtime_error& e) {
            log.error() << "Failed to start recorder: " << e.what();
        }
        try {
            if (!mConfig.streamOutURL.empty()) mStreamOutput.start(mConfig.streamOutURL);
        }
        catch (const std::exception& e) {
            log.error() << "Failed to start output stream: " << e.what();
        }
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

        // time_t now = std::time(0);
        // for (const auto& source : mProcessors) {
        //     auto state = source->getState(now);
        //     switch (state) {
        //         case AudioProcessor::State::CUE: {
        //             if (now >= source->tsStart && now <= ) {
        //                 source->play();
        //             }
        //         }
        //         case AudioProcessor::State::PLAY: {
        //             break;
        //         }
        //         default: break;
        //     }
        // }

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
                out[i] += mMixBuffer[i];
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