#pragma once

#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <ctime>

#include "../common/Config.hpp"
#include "../common/APIClient.hpp"
#include "../common/util.hpp"

namespace cst {

struct PlayItem {
    std::time_t start;
    std::time_t end;
    std::string uri;

    std::time_t scheduleStart() const { return start - 5; }
    std::time_t scheduleEnd() const { return end - 5; }

    bool operator==(const PlayItem& item) const {
        return item.start == this->start && item.end == this->end && item.uri == this->uri;
    }
};

class M3UParser {
public:

    std::unordered_map<std::string, std::vector<PlayItem>> mMap = {};

    void reset() {
        mMap.clear();
    }

    std::vector<PlayItem> parse(const std::string& uri, time_t startOffset = 0) {
        auto it = mMap.find(uri);
        if (it != mMap.end()) {
            return it->second;
        } else {
            auto items = _parse(uri);
            mMap[uri] = items;
            return items;
        }
    }

    std::vector<PlayItem> _parse(const std::string& uri) {
        // std::cout << "_parse " << uri << std::endl;
        using namespace std;
        std::vector<PlayItem> items;
        ifstream file(uri);
        if (!file.is_open()) {
            throw runtime_error("Failed to open file " + uri);
        }
        string line;
        while (getline(file, line)) {
            if (line.starts_with("#EXTINF:")) {
                auto metadata = util::splitBy(line, ':').second;
                auto metainfo = util::splitBy(metadata, ',');
                int duration = stoi(metainfo.first);
                auto artist = metainfo.second;
                string path;
                if (getline(file, path)) {
                    items.push_back({0, duration, path});
                }
            }
            // std::cout << line << std::endl;
        }

        mMap[uri] = items;

        return items;
    }
};


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

    const std::string mPlaylistURI = "/Volumes/Playlists/aura";
    const std::string mStoreURI = "/Volumes/Sendungsarch/aura";
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
            refresh();
        }

        std::vector<PlayItem> activeItems(0);
        for (const auto& item : mItems) {
            if (now >= item.start && now <= item.end) {
                activeItems.push_back(item);
            }
        }

        if (mActiveItems != activeItems) {
            mActiveItems.assign(activeItems.begin(), activeItems.end());
            log.debug() << "Calendar active items changed";
            if (activeItemChangeHandler) activeItemChangeHandler(mActiveItems);
        }
    }
    

    void refresh() {
        std::vector<PlayItem> items;
        m3uParser.reset();
        const auto program = mAPIClient.getProgram();
        for (const auto& pr : program) {
            // std::cout << pr.start << " - " << pr.end << " Show: " << pr.showName << ", Episode: " << pr.episodeTitle << std::endl;
            const auto startTS = util::parseDatetime(pr.start);
            const auto endTS = util::parseDatetime(pr.end);
            const auto playlist = mAPIClient.getPlaylist(pr.playlistId);
            auto tmpStartTS = startTS;

            for (const auto& entry : playlist.entries) {
                const auto itmStart = tmpStartTS;
                const auto itemEnd = itmStart + entry.duration;
                
                if (entry.uri.starts_with(m3uPrefix)) {
                    try {
                        auto uri = mPlaylistURI + entry.uri.substr(m3uPrefix.size());
                        auto m3u = m3uParser.parse(uri);
                        for (auto& m : m3u) {
                            m.start += tmpStartTS;
                            m.end += tmpStartTS;
                            tmpStartTS = m.end;
                        }
                        items.insert(items.end(), m3u.begin(), m3u.end());
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed read m3u file: " << e.what() << std::endl;
                    }
                } else {
                    auto uri = (entry.uri.starts_with(filePrefix)) ? mStoreURI + entry.uri.substr(filePrefix.size()) : entry.uri;
                    PlayItem itm = { itmStart, itemEnd, entry.uri };
                    items.push_back(itm);
                }
                tmpStartTS = itemEnd;
            }
        }

        if (items.empty()) {
            auto now = std::time(nullptr);
            items.push_back({ now +  1, now + 5, "/Users/chris/Music/Audio-Test-Files/Key/A maj.mp3" });
            items.push_back({ now +  5, now + 10, "/Users/chris/Music/Audio-Test-Files/Key/D maj.mp3" });
            items.push_back({ now + 10, now + 30, "http://stream.fro.at/fro-128.ogg" });
            items.push_back({ now + 30, now + 40, "/Users/chris/Music/Audio-Test-Files/Key/E maj.mp3" });
        }

        for (const auto& itm : items) {
            std::cout << itm.start << " - " << itm.end << " " << itm.uri << std::endl;
        }

        if (mItems != items) {
            mItems.assign(items.begin(), items.end());
            // this->notifyChange();
            // std::cout << "Items changed" << std::endl;
        } else {
            // std::cout << "Items not changed" << std::endl;
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