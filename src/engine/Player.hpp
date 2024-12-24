#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>
#include "../common/Config.hpp"
#include "../common/Log.hpp"
#include "../common/util.hpp"
#include "../core/AudioClient.hpp"
#include "../core/LinePlayer.hpp"
#include "../core/MP3Player.hpp"
#include "../core/QueuePlayer.hpp"
#include "../core/StreamPlayer.hpp"
#include "../core/AudioClient.hpp"
#include "../core/SinOsc.hpp"

namespace cst {
class Player : public AudioClientRenderer {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    double mSampleRate = 44100;
    size_t mBufferSize = 1024;
    const Config& mConfig;
    AudioClient mAudioClient;
    SinOsc mOsc;
    std::vector<std::shared_ptr<AudioProcessor>> mSources = {};

    std::mutex mMutex;
    std::deque<PlayItem> mItemsToSchedule = {};
    std::deque<PlayItem> mScheduleItems = {};
    
public:
    Player(const Config& tConfig) :
        mConfig(tConfig),
        mOsc(mSampleRate),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mSampleRate, mBufferSize)
    {
        mAudioClient.setRenderer(this);
        mOsc.setFrequency(400);
    }

    void run() {
        mRunning = true;
        mAudioClient.start();
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
        mAudioClient.stop();
        mRunning = false;
        log.debug() << "Player terminated";
    }

    void work() {
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
            if (item.uri.ends_with(".m3u")) {
                auto source = std::make_shared<QueuePlayer>(mSampleRate);
                source->push(item.uri);
                source->tsStart = item.start;
                source->tsEnd = item.end;
                mSources.push_back(source);
            } else {
                auto source = std::make_shared<MP3Player>(mSampleRate);
                auto pos = std::time(0) - item.start;
                source->load(item.uri, pos);
                source->tsStart = item.start;
                source->tsEnd = item.end;
                mSources.push_back(source);
            }
        }
    }

    void renderCallback(const float* in, float* out, size_t nframes) override {
        memset(out, 0, nframes * 2 * sizeof(float));

        time_t now = std::time(nullptr);
        for (const auto& source : mSources) {
            if (source->isActive(now)) source->process(in, out, nframes);
        }

        // for (auto i = 0; i < nframes; ++i) {
        //     auto s = mOsc.process() * 0.25;
        //     out[i*2]   += s;
        //     out[i*2+1] += s;
        // }
    }

};

}