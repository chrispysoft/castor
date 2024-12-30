#include <atomic>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <csignal>

#include "Calendar.hpp"
#include "Engine.hpp"
#include "Config.hpp"
#include "util/Log.hpp"
#include "util/util.hpp"

namespace cst {
class Castoria {

    bool mRunning = false;
    std::mutex mMutex;
    std::condition_variable mCV;
    Config mConfig;
    Calendar mCalendar;
    Engine mEngine;
    
    Castoria() :
        mConfig("./config/config.txt"),
        mCalendar(mConfig),
        mEngine(mConfig)
    {
        log.setFilePath(mConfig.logPath);
        mCalendar.activeItemChangeHandler = [this](auto items) {
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
        std::unique_lock<std::mutex> lock(mMutex);
        while (mRunning) {
            mCV.wait(lock);
        }
    }

    void terminate() {
        log.debug() << "Castoria terminating...";
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mRunning = false;
        }
        mCV.notify_all();
        mEngine.stop();
        mCalendar.stop();
        log.info() << "Castoria terminated";
    }
};

}