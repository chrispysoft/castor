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

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <filesystem>
#include <vector>
#include <json.hpp>
#include "Config.hpp"
#include "api/API.hpp"
#include "api/APIClient.hpp"
#include "util/CSVParser.hpp"
#include "util/M3UParser.hpp"
#include "util/util.hpp"

namespace castor {
class Calendar {

    const std::string m3uPrefix = "m3u://";
    const std::string filePrefix = "file://";
    const std::string defaultFileSuffix = ".flac";
    
    const time_t mStartupTime;
    const Config& mConfig;
    std::atomic<bool> mRunning = false;
    std::thread mWorker;
    std::mutex mItemsMutex;
    std::mutex mWorkMutex;
    std::condition_variable mWorkCV;
    std::vector<std::shared_ptr<PlayItem>> mItems;
    api::Client mAPIClient;
    util::M3UParser m3uParser;

public:

    std::function<void(const std::vector<std::shared_ptr<PlayItem>>& items)> calendarChangedCallback;

    Calendar(const Config& tConfig) :
        mStartupTime(std::time(0)),
        mConfig(tConfig),
        mAPIClient(mConfig),
        m3uParser()
    {}


    void start() {
        if (mConfig.programURL.empty()) {
            log.warn() << "Calendar can't start - missing config";
            return;
        }

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

        auto items = fetchItems();

        if (!std::ranges::equal(items, mItems, [](const auto& a, const auto& b) { return *a == *b; })) {
            log.info(Log::Yellow) << "Calendar changed";
            storeItems(items);
        } else {
            log.debug() << "Calendar not changed";
        }
    }

    void storeItems(const std::vector<std::shared_ptr<PlayItem>>& tItems) {
        std::lock_guard<std::mutex> lock(mItemsMutex);
        mItems = std::move(tItems);
        if (calendarChangedCallback) {
            calendarChangedCallback(mItems);
        }
    }
    
    std::vector<std::shared_ptr<PlayItem>> fetchItems() {
        std::vector<std::shared_ptr<PlayItem>> items;
        // m3uParser.reset();
        const auto now = std::time(0);
        const auto program = mAPIClient.getProgram(mConfig.preloadTimeFile);
        for (const auto& pr : program) {
            // log.debug() << pr.start << " - " << pr.end << " Show: " << pr.showName << ", Episode: " << pr.episodeTitle;
            if (pr->mediaId <= 0) {
                log.error() << "Calendar item '" << pr->showName << "' has no media id";
                continue;
            }
            const auto media = mAPIClient.getMedia(pr->mediaId);
            const auto prStart = util::parseDatetime(pr->start);
            const auto prEnd = util::parseDatetime(pr->end);
            auto itemStart = prStart;

            for (const auto& entry : media->entries) {
                // log.debug() << entry.uri;
                auto entryDuration = (entry.duration > 0) ? entry.duration : prEnd - itemStart;
                auto itemEnd = itemStart + entryDuration;
                if (itemEnd == itemStart) {
                    itemEnd = prEnd;
                }
                if (itemEnd < now) {
                    itemStart = itemEnd;
                    continue;
                }
                
                if (entry.uri.starts_with(m3uPrefix)) {
                    auto uri = mConfig.audioPlaylistPath + entry.uri.substr(m3uPrefix.size());
                    try {
                        // log.debug() << "Calendar parsing m3u " << uri;
                        auto m3u = m3uParser.parse(uri, itemStart, itemEnd);
                        if (!m3u.empty()) {
                            // auto prPtr = std::make_shared<api::Program>(pr);
                            // for (auto& itm : m3u) itm.program = prPtr;
                            // items.insert(items.end(), m3u.begin(), m3u.end());
                            auto maxEnd = std::time(0) + mConfig.preloadTimeFile;
                            for (const auto& itm : m3u) {
                                if (itm->end <= maxEnd) {
                                    itm->program = pr;
                                    items.emplace_back(itm);
                                }
                            }
                        } else {
                            log.warn() << "Calendar found no M3U metadata - adding file as item";
                            items.emplace_back(std::make_shared<PlayItem>(itemStart, itemEnd, uri, pr));
                        }
                    } catch (const std::exception& e) {
                        log.error() << "Calendar error reading M3U: " << e.what();
                    }
                } else {
                    auto uri = entry.uri;
                    if (uri.starts_with(filePrefix)) {
                        uri = mConfig.audioSourcePath + "/" + entry.uri.substr(filePrefix.size());
                        if (std::filesystem::path(uri).extension().empty()) {
                            log.warn() << "Calendar item '" << uri << "' has no file extension - adding default " << defaultFileSuffix;
                            uri += defaultFileSuffix;
                        }
                    }
                    else if (uri.empty()) { // assume default location
                        uri = mConfig.audioSourcePath + "/" + std::to_string(pr->showId) + "/" + std::to_string(pr->mediaId) + defaultFileSuffix;
                        log.debug() << "Calendar generated file url '" << uri << "'";
                    }
                    items.emplace_back(std::make_shared<PlayItem>(itemStart, itemEnd, uri, pr));
                }
                itemStart += entryDuration;
            }

            // if (!std::filesystem::exists(uri)) {
            //     log.error() << "Calendar item '" << uri << "' does not exist";
            //     continue;
            // }
        }
        return items;
    }

    void serialize() {
        nlohmann::json j = mItems;
        std::ofstream f(mConfig.calendarCachePath);
        if (!f.is_open()) throw std::runtime_error("Failed to open output file");
        f << j;
    }

    void deserialize() {
        std::ifstream f(mConfig.calendarCachePath);
        if (!f.is_open()) throw std::runtime_error("Failed to open input file");
        nlohmann::json j;
        f >> j;
        mItems = j;
    }
};
}
