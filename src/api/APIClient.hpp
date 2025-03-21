/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
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
            throw std::runtime_error("APIClient getProgram failed: " + std::to_string(res.code) + " " + res.response);
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
            throw std::runtime_error("APIClient getMedia failed: " + std::to_string(res.code) + " " + res.response);
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
            throw std::runtime_error("APIClient postPlaylog failed: " + std::to_string(res.code) + " " + res.response);
        }
    }

    void postHealth(const Health& health) {
        const auto& url = mConfig.healthURL;
        log.debug() << "APIClient postHealth " << url;

        nlohmann::json j = health;
        auto jstr = j.dump();

        auto res = mHTTPClientPlaylog.post(url, jstr);
        if (res.code != 204) {
            throw std::runtime_error("APIClient postHealth failed: " + std::to_string(res.code) + " " + res.response);
        }
    }
};
}
}