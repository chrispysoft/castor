#pragma once

#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <ctime>

#include "Config.hpp"
#include "api/API.hpp"
#include "api/APIClient.hpp"
#include "util/M3UParser.hpp"
#include "util/util.hpp"
#include "third_party/json.hpp"

namespace cst {
class Calendar {
    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    const Config& mConfig;
    APIClient mAPIClient;
    time_t mLastRefreshTime = 0;
    time_t mRefreshInterval = 60;
    std::vector<PlayItem> mItems;
    std::vector<PlayItem> mActiveItems;
    M3UParser m3uParser;

    const std::string m3uPrefix = "m3u://";
    const std::string filePrefix = "file://";

public:

    std::function<void(const std::vector<PlayItem>& items)> activeItemChangeHandler;

    Calendar(const Config& tConfig) :
        mConfig(tConfig),
        mAPIClient(tConfig),
        m3uParser()
    {}

    void work() {
        auto now = std::time(0);
        if (now - mLastRefreshTime > mRefreshInterval) {
            mLastRefreshTime = now;
            try {
                refresh();
            }
            catch (const std::exception& e) {
                log.error() << "Calendar refresh failed: " << e.what();
            }
        }

        // if (mItems.empty()) {
        //     mItems.push_back({ now +  5, now +  20, "./audio/Alternate Gate 6 Master.mp3" });
        //     mItems.push_back({ now +  20, now + 40, "http://stream.fro.at/fro-128.ogg" });
        //     mItems.push_back({ now +  40, now + 60, "http://stream.fro.at/oggst3.ogg" });
        //     // mItems.push_back({ now + 60, now + 75, "/Users/chris/Music/Audio-Test-Files/Key/E maj.mp3" });
        //     // mItems.push_back({ now + 15, now + 20, "/Users/chris/Music/Audio-Test-Files/Key/C maj.mp3" });
        //     // mItems.push_back({ now + 20, now + 60, "http://stream.fro.at/fro-128.ogg" });
        //     // mItems.push_back({ now + 60, now + 65, "/Users/chris/Music/Audio-Test-Files/Key/A maj.mp3" });
        // }

        std::vector<PlayItem> activeItems(0);
        for (const auto& item : mItems) {
            if (now >= item.scheduleStart() && now <= item.scheduleEnd()) {
                activeItems.push_back(item);
            }
        }

        if (mActiveItems != activeItems) {
            log.info() << "Calendar active items changed";
            mActiveItems.assign(activeItems.begin(), activeItems.end());
            if (activeItemChangeHandler) activeItemChangeHandler(mActiveItems);
        }
    }
    

    void refresh() {
        log.info() << "Calendar refresh";
        std::vector<PlayItem> items;
        m3uParser.reset();
        const auto now = std::time(0);
        const auto program = mAPIClient.getProgram();
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
                auto itemEnd = itemStart + entry.duration;
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
                        auto m3u = m3uParser.parse(uri);
                        if (!m3u.empty()) {
                            for (auto& m : m3u) {
                                m.start += itemStart;
                                m.end += itemStart;
                                m.program = pr;
                                itemStart = m.end;
                            }
                            items.insert(items.end(), m3u.begin(), m3u.end());
                        } else {
                            log.warn() << "Calendar found no M3U metadata - adding file as item";
                            PlayItem itm = { itemStart, itemEnd, uri, pr };
                            items.push_back(itm);
                        }
                    }
                    catch (const std::exception& e) {
                        log.error() << "Calendar error reading M3U: " << e.what();
                    }
                }
                else {
                    auto uri = (entry.uri.starts_with(filePrefix)) ? mConfig.audioSourcePath + entry.uri.substr(filePrefix.size()) : entry.uri;
                    PlayItem itm = { itemStart, itemEnd, entry.uri, pr };
                    items.push_back(itm);
                }
                itemStart = itemEnd;
            }
        }

        for (const auto& itm : items) {
            auto tm1 = *std::localtime(&itm.start);
            auto tm2 = *std::localtime(&itm.end);
            static constexpr const char* fmt = "%Y-%m-%d %H:%M:%S";
            log.debug() << std::put_time(&tm1, fmt) << " - " << std::put_time(&tm2, fmt) << " " << itm.program.showName << " " << itm.uri;
        }

        if (mItems != items) {
            mItems = items;
            log.info() << "Calendar changed";
        } else {
            log.info() << "Calendar not changed";
        }
    }

    void start() {
        mRunning = true;
        mWorker = std::make_unique<std::thread>([this] {
            while (this->mRunning) {
                this->work();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    void stop() {
        mRunning = false;
        if (mWorker->joinable()) {
            mWorker->join();
        }
    }

};
}