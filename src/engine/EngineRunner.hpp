#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>

#include "Calendar.hpp"
#include "../common/Config.hpp"
#include "../common/Log.hpp"
#include "../common/util.hpp"

namespace cst {
class EngineRunner {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    Config mConfig;
    Calendar mCalendar;
    
public:
    EngineRunner() :
        mConfig("../config/config.txt"),
        mCalendar(mConfig)
    {

    }

    void run() {
        mRunning = true;
        mCalendar.start();
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
        });
        mCalendar.activeItemChangeHandler = [this](const PlayItem& item) {
            log.debug() << "EngineRunner activeItemChangeHandler " << item.uri;
        };
        mWorker->join();
    }

    void terminate() {
        log.debug() << "EngineRunner terminating...";
        mCalendar.stop();
        mRunning = false;
        log.debug() << "EngineRunner terminated";
    }

};

}