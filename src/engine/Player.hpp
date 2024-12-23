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

    std::vector<PlayItem> mScheduleItems = {};

    void schedule(const PlayItem& item) {
        try {
            _schedule(item);
        }
        catch (const std::exception& e) {
            log.error() << "Player failed to schedule item: " << e.what();
        }
    }

    void _schedule(const PlayItem& item) {
        if (std::find(mScheduleItems.begin(), mScheduleItems.end(), item) != mScheduleItems.end()) {
            return;
        }
        mScheduleItems.push_back(item);
        log.warn() << "Player schedule " << item.uri;
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
            auto source = std::make_shared<MP3Player>(mSampleRate);
            source->load(item.uri);
            source->tsStart = item.start;
            source->tsEnd = item.end;
            mSources.push_back(source);
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