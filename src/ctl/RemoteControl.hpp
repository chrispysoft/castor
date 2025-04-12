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

#pragma once

#include <queue>
#include <cstring>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include <vector>
#include "../util/Log.hpp"
 
namespace castor {
namespace ctl {

class RemoteControl {

    using CommandFunction = std::function<void()>;  

    std::atomic<bool> mRunning = false;
    std::string mStatusString;
    std::queue<std::string> mStatusQueue;
    std::mutex mStatusMutex;
    std::unordered_map<std::string, CommandFunction> mCommands;


public:

    RemoteControl() {

    }

    ~RemoteControl() {

    }

    bool registerCommand(const std::string& tName, CommandFunction tFunc) {
        if (mCommands.find(tName) != mCommands.end()) {
            log.error() << "RemoteControl command already exists: " << tName;
            return false;
        }
        mCommands[tName] = std::move(tFunc);
        log.debug() << "RemoteControl registered command: " << tName;
        return true;
    }


    void executeCommand(const std::string& name, const std::string& argument = "") {
        auto cmd = util::stripLF(name);
        auto it = mCommands.find(cmd);
        if (it != mCommands.end()) {
            log.info() << "RemoteControl executing command: " << cmd;
            /*std::string result = */ it->second();
            //callback(result);
        } else {
            log.error() << "RemoteControl unknown command: " << cmd;
            //callback("ERROR: Unknown command");
        }
    }

    void pushStatus(std::string tStatus) {
        std::lock_guard<std::mutex> lock(mStatusMutex);
        // mStatusQueue.push(tStatus);
        mStatusString = std::move(tStatus);
    }

    bool connected() {
        return 0;
    }
};

}
}