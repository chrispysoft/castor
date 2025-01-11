#pragma once

#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <ctime>
#include <deque>
#include <filesystem>

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
    api::Client mAPIClient;
    time_t mLastRefreshTime = 0;
    time_t mRefreshInterval = 60;
    std::deque<PlayItem> mItems;
    M3UParser m3uParser;

    const std::string m3uPrefix = "m3u://";
    const std::string filePrefix = "file://";
    const std::string defaultFileSuffix = ".flac";

public:

    Calendar(const Config& tConfig) :
        mConfig(tConfig),
        mAPIClient(tConfig),
        m3uParser()
    {}

    std::deque<PlayItem> items() {
        const auto items = std::deque(mItems.begin(), mItems.end());
        return items;
    }

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
        //     mItems.push_back({ now +  5, now +  60, "/Users/chris/Music/Musik/Psychedelic Chillout.mp3" });
        //     mItems.push_back({ now +  20, now + 40, "http://stream.fro.at/fro-128.ogg" });
        //     mItems.push_back({ now +  40, now + 60, "http://stream.fro.at/oggst3.ogg" });
        //     mItems.push_back({ now +  5, now + 10, "/Users/chris/Music/Audio-Test-Files/Key/A maj.mp3" });
        //     mItems.push_back({ now + 10, now + 15, "/Users/chris/Music/Audio-Test-Files/Key/D maj.mp3" });
        //     mItems.push_back({ now + 15, now + 20, "/Users/chris/Music/Audio-Test-Files/Key/G maj.mp3" });
        //     mItems.push_back({ now + 20, now + 25, "/Users/chris/Music/Audio-Test-Files/Key/C maj.mp3" });
        //     mItems.push_back({ now + 25, now + 30, "/Users/chris/Music/Audio-Test-Files/Key/F maj.mp3" });
        //     mItems.push_back({ now + 30, now + 35, "/Users/chris/Music/Audio-Test-Files/Key/A# maj.mp3" });
        //     mItems.push_back({ now + 35, now + 40, "/Users/chris/Music/Audio-Test-Files/Key/D# maj.mp3" });
        //     mItems.push_back({ now + 40, now + 45, "/Users/chris/Music/Audio-Test-Files/Key/G# maj.mp3" });
        //     mItems.push_back({ now + 45, now + 50, "/Users/chris/Music/Audio-Test-Files/Key/C# maj.mp3" });
        //     mItems.push_back({ now + 50, now + 55, "/Users/chris/Music/Audio-Test-Files/Key/F# maj.mp3" });
        //     mItems.push_back({ now + 55, now + 60, "/Users/chris/Music/Audio-Test-Files/Key/B maj.mp3" });
        //     mItems.push_back({ now + 60, now + 65, "/Users/chris/Music/Audio-Test-Files/Key/E maj.mp3" });
        // }
    }
    
    std::deque<PlayItem> fetchItems() {
        std::deque<PlayItem> items;
        // m3uParser.reset();
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

    void refresh() {
        log.debug() << "Calendar refresh";

        auto items = fetchItems();
        if ( items != mItems) {
            mItems = items;
            log.info() << "Calendar changed";
            for (const auto& itm : items) {
                static constexpr const char* fmt = "%Y-%m-%d %H:%M:%S";
                log.debug() << util::timefmt(itm.start, fmt) << " - " << util::timefmt(itm.end, fmt) << " " << itm.program.showName << " " << itm.uri;
            }
        } else {
            log.debug() << "Calendar not changed";
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