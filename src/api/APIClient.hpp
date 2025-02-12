/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
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

public:

    Client(const Config& tConfig) :
        mConfig(tConfig),
        mAuthHeaders {
            "Authorization: Bearer " + mConfig.playlistToken,
            "content-type: application/json"
        }
    {
        
    }

    ~Client() {
    }

    std::vector<api::Program> getProgram(time_t duration = 3600) {
        auto url = std::string(mConfig.programURL);
        if (duration > 0) {
            auto now = std::time(nullptr);
            auto end = now + duration;
            auto endfmt = util::utcFmt(end);
            url += "?includeVirtual=true&end="+endfmt;
        }

        log.debug() << "APIClient getProgram " << url;
        
        auto res = io::HTTPClient().get(url);
        if (res.code == 200) {
            std::stringstream ss(res.response);
            nlohmann::json j;
            ss >> j;
            auto p = j.template get<std::vector<api::Program>>();
            return p;
        } else {
            throw std::runtime_error("APIClient getProgram status code "+std::to_string(res.code));
        }
    }

    api::Playlist getPlaylist(int showID) {
        auto url = mConfig.playlistURL + std::to_string(showID);
        auto res = io::HTTPClient().get(url, mAuthHeaders);
        log.debug() << "APIClient getPlaylist " << url;
        if (res.code == 200) {
            std::stringstream ss(res.response);
            nlohmann::json j;
            ss >> j;
            auto p = j.template get<api::Playlist>();
            return p;
        } else {
            throw std::runtime_error("APIClient getPlaylist status code "+std::to_string(res.code));
        }
    }

    void postPlaylog(const PlayLog& item) {
        const auto& url = mConfig.playlogURL;

        nlohmann::json j = item;
        std::stringstream ss;
        ss << j;
        auto jstr = ss.str();

        log.debug() << "APIClient postPlaylog " << url << " " << jstr << " ";
        auto res = io::HTTPClient().post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postPlaylog status code "+std::to_string(res.code));
        }
    }

    void postHealth(const Health& health) {
        const auto& url = mConfig.healthURL;

        nlohmann::json j = health;
        std::stringstream ss;
        ss << j;
        auto jstr = ss.str();

        log.debug() << "APIClient postHealth " << url << " " << jstr << " ";
        auto res = io::HTTPClient().post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postHealth status code "+std::to_string(res.code));
        }
    }
};
}
}