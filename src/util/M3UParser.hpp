#pragma once

#include "../api/API.hpp"

namespace cst {
class M3UParser {
public:

    std::unordered_map<size_t, std::vector<PlayItem>> mMap = {};

    void reset() {
        mMap.clear();
    }

    std::vector<PlayItem> parse(const std::string& url, const time_t& startTime = 0, const time_t& endTime = 0) {
        auto hash = std::hash<std::string>{}(std::string(url + std::to_string(startTime) + std::to_string(endTime)));
        auto it = mMap.find(hash);
        if (it != mMap.end()) {
            return it->second;
        } else {
            auto items = _parse(url, startTime, endTime);
            mMap[hash] = items;
            return items;
        }
    }

    std::vector<PlayItem> _parse(const std::string& url, const time_t& startTime = 0, const time_t& endTime = 0) {
        // std::cout << "_parse " << uri << std::endl;
        using namespace std;
        std::vector<PlayItem> items;
        ifstream file(url);
        if (!file.is_open()) {
            throw runtime_error("Failed to open file " + url);
        }
        // log.debug() << "M3UParser opened " + uri;
        auto itmStart = startTime;
        string line;
        while (getline(file, line)) {
            if (line.starts_with("#EXTINF:")) {
                auto metadata = util::splitBy(line, ':').second;
                auto metainfo = util::splitBy(metadata, ',');
                int duration = stoi(metainfo.first);
                if (duration <= 0) {
                    throw runtime_error("Invalid duration " + to_string(duration));
                }
                auto artist = metainfo.second;
                string path;
                if (getline(file, path)) {
                    util::stripM3ULine(path);
                    auto itmEnd = itmStart + duration;
                    if (endTime == 0 || itmEnd <= endTime) {
                        items.push_back({itmStart, itmEnd, path});
                        itmStart = itmEnd;
                    } else {
                        log.info() << "M3U item exceeds end time - cropping";
                        items.push_back({itmStart, endTime, path});
                        break;
                    }
                }
            }
            // std::cout << line << std::endl;
        }

        return items;
    }
};
}