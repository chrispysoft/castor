/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <atomic>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <csignal>
#include "Engine.hpp"
#include "Config.hpp"
#include "util/Log.hpp"

namespace cst {
class Castor {

    bool mRunning = false;
    std::mutex mMutex;
    std::condition_variable mCV;
    Config mConfig;
    Engine mEngine;
    
    Castor() :
        mConfig("./config/config.txt"),
        mEngine(mConfig)
    {
        log.setFilePath(mConfig.logPath);

        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);
        std::signal(SIGPIPE, handlesig);
    }

    static void handlesig(int sig) {
        log.warn() << "Received signal " << sig;
        if (sig == SIGPIPE) {
            log.error() << "Broken pipe";
        } else {
            instance().terminate();
        }
    }

public:
    static Castor& instance() {
        static Castor instance;
        return instance;
    }

    void run() {
        mRunning = true;
        mEngine.start();
        std::unique_lock<std::mutex> lock(mMutex);
        while (mRunning) {
            mCV.wait(lock);
        }
    }

    void terminate() {
        log.debug() << "Castor terminating...";
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mRunning = false;
        }
        mCV.notify_all();
        mEngine.stop();
        log.info() << "Castor terminated";
    }
};

}