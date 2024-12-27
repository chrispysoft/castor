#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <string_view>
#include <deque>
#include <iostream>
#include <mutex>
#include "AudioProcessor.hpp"
#include "MP3Player.hpp"
#include "../common/Log.hpp"

namespace cst {
class QueuePlayer : public AudioProcessor {
    std::unique_ptr<std::thread> mWorker;
    std::atomic<bool> mRunning;
    std::deque<std::string> mQueue;
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

    bool canPlay(const PlayItem& item) override {
        return mPlayer.canPlay(item);
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "QueuePlayer open " << tURL;
        if (tURL.ends_with(".m3u")) {
            log.debug() << "QueuePlayer opening m3u file " << tURL;
            std::ifstream file(tURL);
            std::string line;
            while (getline(file, line)) {
                if (line.starts_with("#")) continue;
                util::stripM3ULine(line);
                mQueue.push_back(line);
                // log.debug() << "QueuePlayer added m3u entry " << line;
            }
            file.close();
            log.debug() << "QueuePlayer added " << mQueue.size() << " m3u entries";
        } else {
            mQueue.push_back(tURL);
        }

        if (mQueue.empty()) {
            log.error() << "QueuePlayer is empty";
            return;
        }

        if (mQueue.size() == 1 && !mRunning) {
            auto url = mQueue.front();
            mQueue.pop_front();
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
        log.debug() << "QueuePlayer clear...";
        mRunning = false;
        mQueue = {};
        mPlayer.eject();
        if (mWorker) {
            if (mWorker->joinable()) mWorker->join();
            mWorker = nullptr;
        }
        log.info() << "QueuePlayer cleared";
    }

    void stop() override {
        clear();
        log.info() << "QueuePlayer stopped";
    }

    
    void process(const float* in, float* out, size_t framecount) override {
        mPlayer.process(in, out, framecount);
    }

private:
    void work() {
        if (mPlayer.isIdle()) {
            if (mQueue.size() > 0) {
                auto url = mQueue.front();
                mQueue.pop_front();
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