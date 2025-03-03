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
 */

#pragma once

#include <atomic>
#include <fstream>
#include <thread>
#include <string>
#include <ctime>
#include <deque>
#include <filesystem>
#include "Config.hpp"
#include "api/API.hpp"
#include "api/APIClient.hpp"
#include "util/CSVParser.hpp"
#include "util/M3UParser.hpp"
#include "util/util.hpp"
#include "third_party/json.hpp"

namespace castor {
class Calendar {

    const std::string m3uPrefix = "m3u://";
    const std::string filePrefix = "file://";
    const std::string defaultFileSuffix = ".flac";
    
    const Config& mConfig;
    std::atomic<bool> mRunning = false;
    std::thread mWorker;
    std::deque<PlayItem> mItems;
    api::Client mAPIClient;
    util::M3UParser m3uParser;
    time_t mLastRefreshTime = 0;

public:

    std::function<void(const std::deque<PlayItem>& items)> calendarChangedCallback;

    Calendar(const Config& tConfig) :
        mConfig(tConfig),
        mAPIClient(mConfig),
        m3uParser()
    {}


    void start() {
        mRunning = true;
        mWorker = std::thread([this] {
            while (mRunning) {
                work();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    void stop() {
        mRunning = false;
        if (mWorker.joinable()) mWorker.join();
    }


    void load(const std::string& tURL) {
        auto parser = util::CSVParser(tURL);
        auto rows = parser.rows();
        auto now = std::time(0);
        for (const auto& row : rows) {
            if (row.size() != 3) continue;
            auto start = now + std::stoi(row[0]);
            auto end   = now + std::stoi(row[1]);
            auto url   = row[2];
            auto item = PlayItem{start, end, url};
            mItems.push_back(item);
            // log.info(Log::Red) << start << " " << end << " " << url;
        }
        if (calendarChangedCallback) calendarChangedCallback(mItems);
    }

private:

    void work() {
        auto now = std::time(0);
        if (now - mLastRefreshTime > mConfig.calendarRefreshInterval) {
            mLastRefreshTime = now;
            try {
                refresh();
            }
            catch (const std::exception& e) {
                log.error() << "Calendar refresh failed: " << e.what();
            }
        }
    }

    void refresh() {
        log.debug() << "Calendar refresh";

        auto items = fetchItems();
        if ( items != mItems) {
            mItems = std::move(items);
            log.debug(Log::Yellow) << "Calendar changed";
            // for (const auto& itm : items) {
            //     static constexpr const char* fmt = "%Y-%m-%d %H:%M:%S";
            //     log.debug() << util::timefmt(itm.start, fmt) << " - " << util::timefmt(itm.end, fmt) << " " << itm.program.showName << " " << itm.uri;
            // }
            if (calendarChangedCallback) calendarChangedCallback(mItems);
        } else {
            log.debug() << "Calendar not changed";
        }
    }
    
    std::deque<PlayItem> fetchItems() {
        std::deque<PlayItem> items;
        // m3uParser.reset();
        const auto now = std::time(0);
        const auto program = mAPIClient.getProgram(mConfig.preloadTimeFile);
        for (const auto& pr : program) {
            // log.debug() << pr.start << " - " << pr.end << " Show: " << pr.showName << ", Episode: " << pr.episodeTitle;
            if (pr.playlistId <= 0) {
                log.error() << "Calendar item '" << pr.showName << "' has no playlist id";
                continue;
            }
            const auto playlist = mAPIClient.getPlaylist(pr.playlistId);
            const auto prStart = util::parseDatetime(pr.start);
            const auto prEnd = util::parseDatetime(pr.end);
            auto itemStart = prStart;

            for (const auto& entry : playlist.entries) {
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
                            for (auto& itm : m3u) itm.program = pr;
                            items.insert(items.end(), m3u.begin(), m3u.end());
                        } else {
                            log.warn() << "Calendar found no M3U metadata - adding file as item";
                            items.push_back({itemStart, itemEnd, uri, pr});
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
                    items.push_back({itemStart, itemEnd, uri, pr});
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
};
}