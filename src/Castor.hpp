/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#include <condition_variable>
#include <csignal>
#include <mutex>
#include <string>
#include "Engine.hpp"
#include "Config.hpp"
#include "util/util.hpp"
#include "util/ArgumentParser.hpp"
#include "util/Log.hpp"

namespace castor {
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
        log.setLevel(mConfig.logLevel);

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

    void run(int argc, char* argv[]) {
        mRunning = true;
        mEngine.start();
        mEngine.parseArgs(util::ArgumentParser(argc, argv).args());
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock, [&]{ return !mRunning; });
        }
    }

    void terminate() {
        log.info() << "Castor terminating...";
        mEngine.stop();
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mRunning = false;
            mCV.notify_all();
        }
        log.info() << "Castor terminated";
    }
};

}