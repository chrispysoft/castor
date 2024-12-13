#pragma once

#include <thread>
#include <atomic>
#include <string>
#include <string_view>
#include <queue>
#include <iostream>
#include "AudioProcessor.hpp"
#include "MP3Player.hpp"

namespace lap {
class QueuePlayer : public AudioProcessor {
    std::unique_ptr<std::thread> mWorker;
    std::atomic<bool> mRunning;
    std::queue<std::string> mQueue;
    MP3Player mPlayer;
    
public:
    QueuePlayer(double tSampleRate) :
        mWorker(nullptr),
        mRunning(false),
        mQueue {},
        mPlayer(tSampleRate)
    {
        
    }

    void push(const std::string& tURL) {
        std::cout << "QueuePlayer push " << tURL << std::endl;
        if (std::string_view(tURL).ends_with(".m3u")) {
            std::cout << "QueuePlayer opening m3u file " << tURL << std::endl;
            std::ifstream file(tURL);
            std::string line;
            while (getline(file, line)) {
                std::cout << "QueuePlayer pushing " << line << std::endl;
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
        mRunning = false;
        if (mWorker) {
            if (mWorker->joinable()) mWorker->join();
            mWorker = nullptr;
        }
        mQueue = {};
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
                        std::cout << "Loaded " << url << std::endl;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load " << url << " " << e.what() << std::endl;
                    }
                    mQueue.pop();
                }
            }
        }
    }
};
}