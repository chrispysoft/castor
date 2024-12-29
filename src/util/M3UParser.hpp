#pragma once

#include "../api/API.hpp"

namespace cst {
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
        // log.debug() << "M3UParser opened " + uri;
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
                    items.push_back({0, duration, path});
                }
            }
            // std::cout << line << std::endl;
        }

        mMap[uri] = items;

        return items;
    }
};
}