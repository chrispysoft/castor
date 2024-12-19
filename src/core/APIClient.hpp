#pragma once

#include <curl/curl.h>
#include <iostream>
#include <string>
#include "Log.hpp"
#include "util.hpp"

namespace cst {
class APIClient {
    const std::string mPlaylogURL;
    CURL* mCURL;

public:

    APIClient(const std::string& tPlaylogURL) :
        mPlaylogURL(tPlaylogURL)
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


    void postPlaylog(const std::string tPlaylog) {
        using namespace std;
        log.debug() << "APIClient postPlaylog " << mPlaylogURL << " " << tPlaylog;

        curl_easy_setopt(mCURL, CURLOPT_URL, mPlaylogURL.c_str());
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