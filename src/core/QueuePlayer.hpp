#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <string_view>
#include <queue>
#include <iostream>
#include <mutex>
#include "AudioProcessor.hpp"
#include "MP3Player.hpp"
#include "Log.hpp"

namespace cst {
class QueuePlayer : public AudioProcessor {
    std::unique_ptr<std::thread> mWorker;
    std::atomic<bool> mRunning;
    std::queue<std::string> mQueue;
    std::mutex mMutex;
    MP3Player mPlayer;
    
public:
    QueuePlayer(double tSampleRate) :
        mWorker(nullptr),
        mRunning(false),
        mQueue {},
        mPlayer(tSampleRate)
    {
        
    }

    ~QueuePlayer() override {
        clear();
    }

    void push(const std::string& tURL) {
        std::lock_guard lock(mMutex);
        log.debug() << "QueuePlayer push " << tURL;
        if (std::string_view(tURL).ends_with(".m3u")) {
            log.debug() << "QueuePlayer opening m3u file " << tURL;
            std::ifstream file(tURL);
            std::string line;
            while (getline(file, line)) {
                log.debug() << "QueuePlayer pushing m3u entry " << line;
                mQueue.push(line);
            }
            file.close();
        } else {
            mQueue.push(tURL);
        }
        
        if (mWorker == nullptr) {
            mRunning = true;
            mWorker = std::make_unique<std::thread>([this] {
                while (this->mRunning) {
                    this->work();
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
    }


    void roll(double pos) {
        mPlayer.roll(pos);
    }

    void clear() {
        std::lock_guard lock(mMutex);
        mRunning = false;
        if (mWorker) {
            if (mWorker->joinable()) mWorker->join();
            mWorker = nullptr;
        }
        mQueue = {};
        mPlayer.eject();
    }

    
    void process(const float* in, float* out, size_t framecount) override {
        mPlayer.process(in, out, framecount);
    }

private:
    void work() {
        if (mQueue.size() > 0) {
            if (mPlayer.isIdle()) {
                auto url = mQueue.front();
                if (url != mPlayer.currentURL()) {
                    try {
                        mPlayer.load(url);
                        log.info() << "QueuePlayer loaded " << url;
                    }
                    catch (const std::exception& e) {
                        log.error() << "QueuePlayer failed to load '" << url << "': " << e.what();
                    }
                    mQueue.pop();
                }
            }
        }
    }
};
}