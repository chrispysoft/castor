#pragma once

#include <curl/curl.h>
#include <iostream>
#include <string>
#include <vector>
#include "../common/Log.hpp"
#include "json.hpp"

namespace cst {
class HTTPClient {
    CURL* mCURL;

public:

    struct Result {
        int code;
        std::string response;
    };

    HTTPClient() {
        mCURL = curl_easy_init();
        if (!mCURL) {
            throw std::runtime_error("HTTPClient failed to init curl");
        }
    }

    ~HTTPClient() {
        if (mCURL) curl_easy_cleanup(mCURL);
    }

    Result get(std::string tURL, std::vector<std::string> tHeaders = {}) {
        using namespace std;
        using json = nlohmann::json;

        Result rResult;
        std::vector<char> rxBuf;
        curl_easy_setopt(mCURL, CURLOPT_URL, tURL.c_str());
        curl_easy_setopt(mCURL, CURLOPT_WRITEDATA, static_cast<void*>(&rxBuf));
        curl_easy_setopt(mCURL, CURLOPT_WRITEFUNCTION, &HTTPClient::writeCallback);
        
        curl_slist* list = NULL;
        if (tHeaders.size() >= 1) {
            for (auto header : tHeaders) {
                list = curl_slist_append(list, header.c_str());
            }
            curl_easy_setopt(mCURL, CURLOPT_HTTPHEADER, list);
        }

        auto res = curl_easy_perform(mCURL);

        if (list) {
            curl_slist_free_all(list);
        }

        if (res == CURLE_OK) {
            log.debug() << "HTTPClient get success " << tURL;
            rResult.response = string(rxBuf.begin(), rxBuf.end());
            rResult.code = 0;
        } else {
            log.error() << "HTTPClient get failed: " << curl_easy_strerror(res) << " " << tURL;
            rResult.response = string(curl_easy_strerror(res));
            rResult.code = -1;
        }

        return rResult;
    }

    static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        auto realsize = size * nmemb;
        auto buffer = static_cast<std::vector<char>*>(userp);
        auto insertpos = buffer->size();
        auto newsize = buffer->size() + realsize;

        buffer->resize(newsize);
        if(buffer->size() != newsize) {
            log.error() << "HTTPClient writeCallback error: not enough memory";
            return 0;
        }

        memcpy(&buffer->at(insertpos), contents, realsize);
        return realsize;
    }
};
}