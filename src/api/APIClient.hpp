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

#include <string>
#include "API.hpp"
#include "../io/HTTPClient.hpp"
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
            "Authorization: Bearer " + mConfig.playlistToken,
            "content-type: application/json"
        }
    {}


    std::vector<api::Program> getProgram(time_t duration = 0) {
        auto url = mConfig.programURL + "?includeVirtual=true";
        if (duration > 0) {
            auto now = std::time(nullptr);
            auto end = now + duration;
            auto endfmt = util::utcFmt(end);
            url += "&end="+endfmt;
        }

        log.debug() << "APIClient getProgram " << url;
        
        auto res = mHTTPClientProgram.get(url);
        if (res.code != 200) {
            throw std::runtime_error("APIClient getProgram failed: "+std::to_string(res.code)+" "+res.response);
        }

        nlohmann::json j = nlohmann::json::parse(res.response);
        return j.get<std::vector<api::Program>>();
    }

    api::Playlist getPlaylist(int showID) {
        auto url = mConfig.playlistURL + std::to_string(showID);
        log.debug() << "APIClient getPlaylist " << url;

        auto res = mHTTPClientProgram.get(url, mConfig.playlistToken);
        if (res.code != 200) {
            throw std::runtime_error("APIClient getPlaylist failed: " + std::to_string(res.code) + " " + res.response);
        }

        nlohmann::json j = nlohmann::json::parse(res.response);
        return j.get<api::Playlist>();
    }

    void postPlaylog(const PlayLog& item) {
        const auto& url = mConfig.playlogURL;
        log.debug() << "APIClient postPlaylog " << url;

        nlohmann::json j = item;
        auto jstr = j.dump();

        auto res = mHTTPClientPlaylog.post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postPlaylog failed: "+std::to_string(res.code)+" "+res.response);
        }
    }

    void postHealth(const Health& health) {
        const auto& url = mConfig.healthURL;
        log.debug() << "APIClient postHealth " << url;

        nlohmann::json j = health;
        auto jstr = j.dump();

        auto res = mHTTPClientPlaylog.post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postHealth failed: "+std::to_string(res.code)+" "+res.response);
        }
    }
};
}
}