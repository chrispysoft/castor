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
#include <mutex>
#include <thread>
#include <filesystem>
#include <vector>
#include <json.hpp>
#include "Config.hpp"
#include "api/API.hpp"
#include "api/APIClientYARM.hpp"
#include "util/CSVParser.hpp"
#include "util/util.hpp"

namespace castor {
class Calendar {

    const std::string m3uPrefix = "m3u://";
    const std::string defaultFileSuffix = ".flac";
    
    const time_t mStartupTime;
    const Config& mConfig;
    std::atomic<bool> mRunning = false;
    std::thread mWorker;
    std::mutex mItemsMutex;
    std::mutex mWorkMutex;
    std::condition_variable mWorkCV;
    std::vector<std::shared_ptr<PlayItem>> mItems;
    api::ClientYARM mAPIClient;

public:

    std::function<void(const std::vector<std::shared_ptr<PlayItem>>& items)> calendarChangedCallback;

    Calendar(const Config& tConfig) :
        mStartupTime(std::time(0)),
        mConfig(tConfig),
        mAPIClient(mConfig)
    {}


    void start() {
        // if (mConfig.programURL.empty()) {
        //     log.warn() << "Calendar can't start - missing config";
        //     return;
        // }

        if (mRunning.exchange(true)) return;
        mWorker = std::thread([this] {
            while (mRunning) {
                try {
                    refresh();
                }
                catch (const std::exception& e) {
                    log.error() << "Calendar refresh failed: " << e.what();
                }
                auto refreshTime = std::chrono::seconds(mConfig.calendarRefreshInterval);
                std::unique_lock<std::mutex> lock(mWorkMutex);
                mWorkCV.wait_for(lock, refreshTime, [this] { return !mRunning.load(std::memory_order_acquire); });
            }
        });
        log.debug() << "Calendar started";
    }

    void stop() {
        log.debug() << "Calendar stopping...";
        if (!mRunning.exchange(false)) return;
        mRunning.store(false, std::memory_order_release);
        mWorkCV.notify_all();
        if (mWorker.joinable()) mWorker.join();
        log.debug() << "Calendar stopped";
    }

    void load(std::string tURL) {
        auto program = std::make_shared<api::Program>(1, 2, 3, "id", "", "", "Test Show", "Test Episode");
        auto parser = util::CSVParser(tURL);
        auto rows = parser.rows();
        auto items = std::vector<std::shared_ptr<PlayItem>>();
        for (const auto& row : rows) {
            if (row.size() != 3) continue;
            auto start = mStartupTime + std::stoi(row[0]);
            auto end   = mStartupTime + std::stoi(row[1]);
            auto url   = row[2];
            items.emplace_back(std::make_shared<PlayItem>(start, end, url, program));
            // log.info(Log::Red) << start << " " << end << " " << url;
        }
        storeItems(items);
    }

private:

    void refresh() {
        log.debug() << "Calendar refresh";

        std::vector<std::shared_ptr<PlayItem>> items;
        try {
            items = mAPIClient.fetchItems();
        }
        catch (const std::exception& e) {
            log.error() << "Calendar failed to fetch items from API: " << e.what();
            try {
                deserialize(items);
                log.info() << "Calendar loaded cached data";
            }
            catch (const std::exception& e) {
                throw std::runtime_error(std::string("Failed to deserialize cached data: ") + e.what());
            }
        }

        storeItems(items);
    }

    void storeItems(const std::vector<std::shared_ptr<PlayItem>>& tItems) {
        std::lock_guard<std::mutex> lock(mItemsMutex);
        if (std::ranges::equal(tItems, mItems, [](const auto& a, const auto& b) { return *a == *b; })) {
            log.debug() << "Calendar not changed";
            return;
        }
        log.info(Log::Yellow) << "Calendar changed";

        mItems = std::move(tItems);
        if (calendarChangedCallback) calendarChangedCallback(mItems);
        try {
            serialize(mItems);
        } catch (const std::exception& e) {
            log.error() << "Calendar failed to serialize items: " << e.what();
        }
    }

    void serialize(const std::vector<std::shared_ptr<PlayItem>>& tItems) const {
        nlohmann::json j = tItems;
        std::ofstream f(mConfig.calendarCachePath);
        if (!f.is_open()) throw std::runtime_error("Failed to open output file");
        f << j;
    }

    void deserialize(std::vector<std::shared_ptr<PlayItem>>& tItems) const {
        std::ifstream f(mConfig.calendarCachePath);
        if (!f.is_open()) throw std::runtime_error("Failed to open input file");
        nlohmann::json j;
        f >> j;
        tItems = j;
    }
};
}
