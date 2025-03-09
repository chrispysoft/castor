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

#include <cmath>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include "../api/API.hpp"
#include "../dsp/CodecReader.hpp"
#include "../util/util.hpp"

namespace castor {
namespace util {

class M3UParser {
public:

    std::unordered_map<size_t, std::vector<std::shared_ptr<PlayItem>>> mMap = {};

    void reset() {
        mMap.clear();
    }

    std::vector<std::shared_ptr<PlayItem>> parse(const std::string& url, const time_t& startTime = 0, const time_t& endTime = 0) {
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

    std::vector<std::shared_ptr<PlayItem>> _parse(const std::string& url, const time_t& startTime = 0, const time_t& endTime = 0) {
        using namespace std;

        auto getDuration = [](const string& path) {
            auto reader = audio::CodecReader(44100, path);
            int duration = ceil(reader.duration());
            if (duration <= 0) {
                throw std::runtime_error("M3UParser could not get duration");
            }
            return duration;
        };

        std::vector<std::shared_ptr<PlayItem>> items;
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
                    auto duration = stoi(metainfo.first);
                    auto artist = metainfo.second;
                    string path;
                    if (getline(file, path)) {
                        util::stripM3ULine(path);
                        if (duration <= 0) {
                            log.warn() << "M3UParser found invalid duration - using CodecReader...";
                            duration = getDuration(path);
                        }
                        auto itmEnd = itmStart + duration;
                        if (endTime == 0 || itmEnd <= endTime) {
                            items.emplace_back(std::make_shared<PlayItem>(itmStart, itmEnd, path));
                            itmStart = itmEnd;
                        } else {
                            log.debug() << "M3U item exceeds end time - adapting";
                            items.emplace_back(std::make_shared<PlayItem>(itmStart, endTime, path));
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
                    int duration = getDuration(path);
                    auto itmEnd = itmStart + duration;
                    if (endTime == 0 || itmEnd <= endTime) {
                        items.emplace_back(std::make_shared<PlayItem>(itmStart, itmEnd, path));
                        itmStart = itmEnd;
                    } else {
                        log.debug() << "M3U item exceeds end time - adapting";
                        items.emplace_back(std::make_shared<PlayItem>(itmStart, endTime, path));
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
}