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

#include <memory>
#include <string>
#include <vector>
#include "API.hpp"
#include "../io/HTTPClient.hpp"
#include "../util/M3UParser.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace api {
class Client {

    const Config& mConfig;
    const std::vector<std::string> mAuthHeaders;
    io::HTTPClient mHTTPClientProgram;
    io::HTTPClient mHTTPClientPlaylog;

public:
    Client(const Config& tConfig) :
        mConfig(tConfig),
        mAuthHeaders {
            "content-type: application/json"
        }
    {}

    std::vector<std::shared_ptr<api::Program>> getProgram(time_t duration = 0) {
        auto url = mConfig.programURL + "?includeVirtual=true";
        if (duration > 0) {
            auto now = std::time(nullptr);
            auto end = now + duration;
            auto endfmt = util::utcFmt(end);
            url += "&end=" + endfmt;
        }

        log.debug() << "APIClient getProgram " << url;
        
        auto res = mHTTPClientProgram.get(url);
        if (res.code != 200) {
            throw std::runtime_error("APIClient getProgram failed: " + res.response + " (" + std::to_string(res.code) + ")");
        }

        nlohmann::json j = nlohmann::json::parse(res.response);
        auto programs = j.get<std::vector<api::Program>>();
        std::vector<std::shared_ptr<api::Program>> programPtrs;
        programPtrs.reserve(programs.size());
        for (const auto& program : programs) {
            programPtrs.emplace_back(std::make_shared<api::Program>(program));
        }
        return programPtrs;
    }

    std::shared_ptr<api::Media> getMedia(int showID) {
        auto url = mConfig.mediaURL + std::to_string(showID) + "/";
        log.debug() << "APIClient getMedia " << url;

        auto res = mHTTPClientProgram.get(url);
        if (res.code != 200) {
            throw std::runtime_error("APIClient getMedia failed: " + res.response + " (" + std::to_string(res.code) + ")");
        }

        nlohmann::json j = nlohmann::json::parse(res.response);
        return std::make_shared<api::Media>(j.get<api::Media>());
    }

    void postPlaylog(const PlayLog& item) {
        const auto& url = mConfig.playlogURL;
        auto debug = log.debug();
        debug << "APIClient postPlaylog " << url;

        nlohmann::json j = item;
        auto jstr = j.dump();
        debug << jstr;

        auto res = mHTTPClientPlaylog.post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postPlaylog failed: " + res.response + " (" + std::to_string(res.code) + ")");
        }
    }

    void postHealth(const Health& health) {
        const auto& url = mConfig.healthURL;
        auto debug = log.debug();
        debug << "APIClient postHealth " << url;

        nlohmann::json j = health;
        auto jstr = j.dump();
        debug << jstr;

        auto res = mHTTPClientPlaylog.post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postHealth failed: " + res.response + " (" + std::to_string(res.code) + ")");
        }
    }

    const std::string m3uPrefix = "m3u://";
    const std::string defaultFileSuffix = ".flac";
    util::M3UParser mM3uParser;

    std::vector<std::shared_ptr<PlayItem>> fetchItems() {
        std::vector<std::shared_ptr<PlayItem>> items;
        // m3uParser.reset();
        const auto now = std::time(0);
        const auto program = getProgram(mConfig.preloadTimeFile);
        for (const auto& pr : program) {
            // log.debug() << pr.start << " - " << pr.end << " Show: " << pr.showName << ", Episode: " << pr.episodeTitle;
            if (pr->mediaId <= 0) {
                log.error() << "Calendar item '" << pr->showName << "' has no media id";
                continue;
            }
            const auto media = getMedia(pr->mediaId);
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
                        auto m3u = mM3uParser.parse(uri, itemStart, itemEnd);
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
                    if (std::all_of(uri.begin(), uri.end(), ::isdigit)) {
                        uri = mConfig.audioSourcePath + "/" + std::to_string(pr->showId) + "/" + uri + defaultFileSuffix;
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
};
}
}