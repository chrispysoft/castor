#pragma once

#include <curl/curl.h>
#include <iostream>
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
    HTTPClient mHTTPClient = HTTPClient();
    CURL* mCURL;

public:

    APIClient(const Config& tConfig) :
        mConfig(tConfig)
    {
        mCURL = curl_easy_init();
        if (!mCURL) {
            throw std::runtime_error("Failed to init curl");
        }
        // curl_global_init(CURL_GLOBAL_ALL); // init Winsock
    }

    ~APIClient() {
        if (mCURL) curl_easy_cleanup(mCURL);
        // curl_global_cleanup();
    }

    std::vector<api::Program> getProgram() {
        auto url = std::string(kProgramURL);
        auto res = HTTPClient().get(url);
        if (res.code == 0) {
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
        if (res.code == 0) {
            std::stringstream ss(res.response);
            nlohmann::json j;
            ss >> j;
            auto p = j.template get<api::Playlist>();
            return p;
        } else {
            return {};
        }
    }

    void postPlaylog(const std::string tPlaylog) {
        using namespace std;
        const auto& url = mConfig.playlogURL;
        log.debug() << "APIClient postPlaylog " << url << " " << tPlaylog;

        curl_easy_setopt(mCURL, CURLOPT_URL, url.c_str());
        curl_easy_setopt(mCURL, CURLOPT_POSTFIELDS, tPlaylog.c_str());

        curl_slist* list = NULL;
        list = curl_slist_append(list, "Content-Type: application/json");
        curl_easy_setopt(mCURL, CURLOPT_HTTPHEADER, list);

        auto res = curl_easy_perform(mCURL);
        if (res == CURLE_OK) {
            log.debug() << "APIClient post success";
        } else {
            log.error() << "APIClient post failed: " << curl_easy_strerror(res);
        }

        curl_slist_free_all(list);
    }
};
}