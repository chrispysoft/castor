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
#include "../common/Log.hpp"

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
        log.debug() << "QueuePlayer push " << tURL;
        if (std::string_view(tURL).ends_with(".m3u")) {
            log.debug() << "QueuePlayer opening m3u file " << tURL;
            std::ifstream file(tURL);
            std::string line;
            while (getline(file, line)) {
                if (line.starts_with("#")) continue;
                //line.pop_back();
                log.debug() << "QueuePlayer pushing m3u entry " << line;
                std::lock_guard lock(mMutex);
                mQueue.push(line);
            }
            file.close();
        } else {
            std::lock_guard lock(mMutex);
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
        std::unique_lock lock(mMutex);
        mQueue = {};
        lock.unlock();
        mRunning = false;
        if (mWorker) {
            if (mWorker->joinable()) mWorker->join();
            mWorker = nullptr;
        }
        mPlayer.eject();
    }

    
    void process(const float* in, float* out, size_t framecount) override {
        mPlayer.process(in, out, framecount);
    }

private:
    void work() {
        if (mPlayer.isIdle()) {
            std::unique_lock lock(mMutex);
            auto qsize = mQueue.size();
            lock.unlock();
            if (qsize > 0) {
                lock.lock();
                auto url = mQueue.back();
                mQueue.pop();
                lock.unlock();
                if (url != mPlayer.currentURL()) {
                    try {
                        mPlayer.load(url);
                        log.info() << "QueuePlayer loaded " << url;
                    }
                    catch (const std::runtime_error& e) {
                        log.error() << "QueuePlayer failed to load " << url << "': " << e.what();
                    }
                }
            }
        }
    }
};
}