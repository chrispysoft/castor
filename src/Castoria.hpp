#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>

#include "Calendar.hpp"
#include "Engine.hpp"
#include "Config.hpp"
#include "util/Log.hpp"
#include "util/util.hpp"

namespace cst {
class Castoria {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    Config mConfig;
    Calendar mCalendar;
    Engine mEngine;
    
    Castoria() :
        mConfig("./config/config.txt"),
        mCalendar(mConfig),
        mEngine(mConfig)
    {
        log.setFilePath(mConfig.logPath);
        mCalendar.activeItemChangeHandler = [this](const std::vector<PlayItem>& items) {
            log.debug() << "Castoria activeItemChangeHandler";
            for (const auto& item : items) {
                this->mEngine.schedule(item);
            }
        };

        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);
        std::signal(SIGPIPE, handlesig);
    }

    static void handlesig(int sig) {
        log.warn() << "Castoria received signal " << sig;
        if (sig == SIGPIPE) {
            log.error() << "Broken pipe";
        } else {
            instance().terminate();
        }
    }

public:
    static Castoria& instance() {
        static Castoria instance;
        return instance;
    }

    void run() {
        mRunning = true;
        mCalendar.start();
        mEngine.start();
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        mWorker->join();
    }

    void terminate() {
        log.debug() << "Castoria terminating...";
        mRunning = false;
        mEngine.stop();
        mCalendar.stop();
        log.info() << "Castoria terminated";
    }
};



}