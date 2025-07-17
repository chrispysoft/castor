/*
 *  Copyright (C) 2024-2025 Christoph Pastl
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
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Lesser General Public License.
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <json.hpp>
#include "../util/Log.hpp"

namespace castor {
namespace ctl {

struct ParameterTree {
    static constexpr float kMaxGain = 24.0f;

    std::atomic<float> inputGain{0.0f};
    std::atomic<float> outputGain{0.0f};

    ParameterTree() = default;

    ParameterTree(const ParameterTree& other) {
        inputGain.store(other.inputGain.load());
        outputGain.store(other.outputGain.load());
    }

    ParameterTree& operator=(const ParameterTree& other) {
        if (this != &other) {
            inputGain.store(other.inputGain.load());
            outputGain.store(other.outputGain.load());
        }
        return *this;
    }
};

void from_json(const nlohmann::json& j, ParameterTree& t) {
    t.inputGain.store(j.at("inputGain"));
    t.outputGain.store(j.at("outputGain"));
}

void to_json(nlohmann::json& j, const ParameterTree& t) {
    j = nlohmann::json{
        {"inputGain", t.inputGain.load()},
        {"outputGain", t.outputGain.load()}
    };
}

bool validate(const ParameterTree& t) {
    const auto& max = ParameterTree::kMaxGain;
    const auto& min = -max;
    if (t.inputGain < min || t.inputGain > max) return false;
    if (t.outputGain < min || t.outputGain > max) return false;
    return true;
}


class Parameters {
    const std::string mParametersPath;;
    ParameterTree mParameterTree;
    std::atomic<bool> mRunning = true;
    std::atomic<bool> mParametersChanged = false;
    std::mutex mNotifyMutex;
    std::condition_variable mNotifyCV;
    std::thread mNotifyThread;

public:

    std::function<void()> onParametersChanged;
    
    Parameters(const std::string& tParametersPath) :
        mParametersPath(tParametersPath),
        mNotifyThread(&Parameters::runNotify, this)
    {
        load();
    }

    ~Parameters() {
        log.debug() << "Parameters destruct...";
        mRunning.store(false, std::memory_order_release);
        mNotifyCV.notify_one();
        if (mNotifyThread.joinable()) mNotifyThread.join();
        save();
        log.debug() << "Parameters destructed";
    }

    const ParameterTree& get() const {
        return mParameterTree;
    }

    void set(const nlohmann::json& j) {
        try {
            ParameterTree p = j;
            if (!validate(p)) throw std::runtime_error("Parameters validation failed");
            mParameterTree = p;
            log.debug() << "Parameters set done";
            save();
            publish();
        }
        catch (const std::exception& e) {
            log.error() << "Parameters set failed: " << e.what();
        }
    }

    void publish() {
        mParametersChanged.store(true, std::memory_order_release);
        mNotifyCV.notify_one();
    }

private:

    void load() {
        try {
            nlohmann::json j;
            std::ifstream(mParametersPath) >> j;
            mParameterTree = j;
            log.info() << "Parameters load done";
        }
        catch (const std::exception& e) {
            log.error() << "Parameters load failed: " << e.what();
            mParameterTree = ParameterTree();
        }
    }

    void save() {
        try {
            auto j = nlohmann::json(mParameterTree);
            std::ofstream(mParametersPath) << j.dump(2);
            log.debug() << "Parameters save done";
        }
        catch (const std::exception& e) {
            log.error() << "Parameters save failed: " << e.what();
        }
    }

    void runNotify() {
        while (mRunning) {
            std::unique_lock<std::mutex> lock(mNotifyMutex);
            mNotifyCV.wait(lock, [this] { return !mRunning.load(std::memory_order_acquire) || mParametersChanged.load(std::memory_order_acquire); });
            if (!mRunning.load(std::memory_order_acquire)) return;
            if (onParametersChanged) onParametersChanged();
            mParametersChanged.store(false, std::memory_order_relaxed);
        }
    }
};

}
}
