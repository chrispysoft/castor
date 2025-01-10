#pragma once

#include <deque>
#include <unordered_map>
#include "../api/API.hpp"
#include "../dsp/CodecReader.hpp"

namespace cst {
class M3UParser {
public:

    std::unordered_map<size_t, std::deque<PlayItem>> mMap = {};

    void reset() {
        mMap.clear();
    }

    std::deque<PlayItem> parse(const std::string& url, const time_t& startTime = 0, const time_t& endTime = 0) {
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

    std::deque<PlayItem> _parse(const std::string& url, const time_t& startTime = 0, const time_t& endTime = 0) {
        // std::cout << "_parse " << uri << std::endl;
        using namespace std;
        std::deque<PlayItem> items;
        ifstream file(url);
        if (!file.is_open()) {
            throw runtime_error("Failed to open file " + url);
        }
        // log.debug() << "M3UParser opened " + uri;
        auto itmStart = startTime;
        string line;
        getline(file, line);
        if (line.starts_with("#EXTM3U")) {
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
        } else {
            string path;
            while (getline(file, path)) {
                util::stripM3ULine(path);
                try {
                    auto reader = audio::CodecReader(44100, path);
                    int duration = round(reader.duration());
                    if (duration <= 0) {
                        throw std::runtime_error("Could not get duration");
                    }

                    auto itmEnd = itmStart + duration;
                    if (endTime == 0 || itmEnd <= endTime) {
                        items.push_back({itmStart, itmEnd, path});
                        itmStart = itmEnd;
                    } else {
                        log.warn() << "M3U item exceeds end time - cropping";
                        items.push_back({itmStart, endTime, path});
                        break;
                    }
                }
                catch (const std::exception& e) {
                    log.error() << "M3UParser failed to get metadata: " << e.what();
                }
            }
        }

        return items;
    }
};
}