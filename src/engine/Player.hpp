#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>
#include "../common/Config.hpp"
#include "../common/Log.hpp"
#include "../common/util.hpp"

namespace cst {
class Player {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    const Config& mConfig;
    
public:
    Player(const Config& tConfig) :
        mConfig(tConfig)
    {

    }

    void run() {
        mRunning = true;
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
        });
        mWorker->join();
    }

    void terminate() {
        log.debug() << "Player terminating...";
        mRunning = false;
        log.debug() << "Player terminated";
    }

    void play(const PlayItem& item) {
        log.info() << "Player play " << item.uri;
    }

};

}