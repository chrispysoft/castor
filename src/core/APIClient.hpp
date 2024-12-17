#pragma once

#include <curl/curl.h>
#include <iostream>
#include <string>
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
        
        curl_easy_setopt(mCURL, CURLOPT_URL, mPlaylogURL.c_str());
        curl_easy_setopt(mCURL, CURLOPT_POSTFIELDS, tPlaylog.c_str());

        auto res = curl_easy_perform(mCURL);
        if (res == CURLE_OK) {
            std::cout << "APIClient post success" << std::endl;
        } else {
            std::cerr << "APIClient post failed: " << curl_easy_strerror(res) << std::endl;
        }
    }
};
}