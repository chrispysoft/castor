#pragma once

#include <string>
#include "API.hpp"
#include "HTTPClient.hpp"
#include "Log.hpp"
#include "util.hpp"

namespace cst {
class APIClient {
    static constexpr const char* kProgramURL = "http://localhost/steering/api/v1/program/playout/";
    static constexpr const char* kPlaylistURL = "http://localhost/tank/api/v1/playlists/";
    static constexpr const char* kPlaylistToken = "engine:1234";

    const std::vector<std::string> mAuthHeaders = {
        "Authorization: Bearer " + std::string(kPlaylistToken),
        "content-type: application/json"
    };

    const Config& mConfig;

public:

    APIClient(const Config& tConfig) :
        mConfig(tConfig)
    {
        
    }

    ~APIClient() {
    }

    std::vector<api::Program> getProgram(time_t duration = 3600) {
        auto url = std::string(kProgramURL);
        if (duration > 0) {
            auto now = std::time(nullptr);
            auto end = now + duration;
            auto endfmt = util::utcFmt(end);
            url += "?end="+endfmt;
        }

        log.debug() << "APIClient getProgram " << url;
        
        auto res = HTTPClient().get(url);
        if (res.code == 200) {
            std::stringstream ss(res.response);
            nlohmann::json j;
            ss >> j;
            auto p = j.template get<std::vector<api::Program>>();
            return p;
        } else {
            return {};
        }
    }

    api::Playlist getPlaylist(int showID) {
        auto url = kPlaylistURL + std::to_string(showID);
        auto res = HTTPClient().get(url, mAuthHeaders);
        log.debug() << "APIClient getPlaylist " << url;
        if (res.code == 200) {
            std::stringstream ss(res.response);
            nlohmann::json j;
            ss >> j;
            auto p = j.template get<api::Playlist>();
            return p;
        } else {
            return {};
        }
    }

    void postPlaylog(const std::string& tPlaylog) {
        const auto& url = mConfig.playlogURL;
        log.debug() << "APIClient postPlaylog " << url << " " << tPlaylog;

        auto res = HTTPClient().post(url, tPlaylog);
        if (res.code == 0) {
            
        } else {
            
        }
    }
};
}