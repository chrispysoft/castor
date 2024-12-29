#pragma once

#include <string>
#include "API.hpp"
#include "../util/HTTPClient.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace cst {
class APIClient {

    const Config& mConfig;
    const std::vector<std::string> mAuthHeaders;

public:

    APIClient(const Config& tConfig) :
        mConfig(tConfig),
        mAuthHeaders {
            "Authorization: Bearer " + mConfig.playlistToken,
            "content-type: application/json"
        }
    {
        
    }

    ~APIClient() {
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
        
        auto res = HTTPClient().get(url);
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
        auto res = HTTPClient().get(url, mAuthHeaders);
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

    void postPlaylog(const PlayItem& item) {
        const auto& url = mConfig.playlogURL;

        nlohmann::json j = item;
        std::stringstream ss;
        ss << j;
        auto jstr = ss.str();

        log.debug() << "APIClient postPlaylog " << url << " " << jstr << " ";
        auto res = HTTPClient().post(url, jstr);
        if (res.code != 210) {
            throw std::runtime_error("APIClient postPlaylog status code "+std::to_string(res.code));
        }
    }
};
}