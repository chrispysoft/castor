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

    void open(const std::string& tURL, double seek = 0) {
        log.info() << "QueuePlayer open " << tURL;
        if (tURL.ends_with(".m3u")) {
            log.debug() << "QueuePlayer opening m3u file " << tURL;
            std::ifstream file(tURL);
            std::string line;
            while (getline(file, line)) {
                if (line.starts_with("#")) continue;
                //line.pop_back();
                log.debug() << "QueuePlayer adding m3u entry " << line;
                mQueue.push(line);
            }
            file.close();
        } else {
            mQueue.push(tURL);
        }

        if (mQueue.size() == 1 && !mRunning) {
            auto url = mQueue.back();
            mQueue.pop();
            try {
                mPlayer.load(url, seek);
                log.debug() << "QueuePlayer loaded " << url;
            }
            catch (const std::runtime_error& e) {
                log.error() << "QueuePlayer failed to load " << url << ": " << e.what();
            }
        }
        else {
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
        if (mPlayer.isIdle()) {
            if (mQueue.size() > 0) {
                auto url = mQueue.back();
                mQueue.pop();
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