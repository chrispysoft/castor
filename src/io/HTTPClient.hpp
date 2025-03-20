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

#include <iostream>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "../util/Log.hpp"
#include "../third_party/json.hpp"

namespace castor {
namespace io {
class HTTPClient {
    CURL* mCURL;
    struct curl_slist* mPostHeaders = NULL;

public:

    struct Result {
        long code;
        std::string response;
    };

    HTTPClient() {
        mCURL = curl_easy_init();
        if (!mCURL) {
            throw std::runtime_error("HTTPClient failed to init curl");
        }
        mPostHeaders = curl_slist_append(mPostHeaders, "Content-Type: application/json");
    }

    ~HTTPClient() {
        curl_slist_free_all(mPostHeaders);
        if (mCURL) curl_easy_cleanup(mCURL);
    }

    Result get(const std::string& tURL) {
        using namespace std;
        using json = nlohmann::json;

        Result rResult;
        std::vector<char> rxBuf;
        curl_easy_setopt(mCURL, CURLOPT_URL, tURL.c_str());
        curl_easy_setopt(mCURL, CURLOPT_WRITEDATA, static_cast<void*>(&rxBuf));
        curl_easy_setopt(mCURL, CURLOPT_WRITEFUNCTION, &HTTPClient::writeCallback);
        
        // if (!tBearerToken.empty()) {
        //     curl_easy_setopt(mCURL, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
        //     curl_easy_setopt(mCURL, CURLOPT_XOAUTH2_BEARER, tBearerToken.c_str());
        // }

        auto res = curl_easy_perform(mCURL);
        if (res == CURLE_OK) {
            curl_easy_getinfo(mCURL, CURLINFO_RESPONSE_CODE, &rResult.code);
            rResult.response = string(rxBuf.begin(), rxBuf.end());
            // log.debug() << "HTTPClient get status " << rResult.code << " " << tURL;
        } else {
            rResult.code = -1;
            rResult.response = string(curl_easy_strerror(res));
        }

        curl_easy_reset(mCURL);

        return rResult;
    }

    Result post(const std::string& tURL, const std::string& tJSON) {
        using namespace std;
        using json = nlohmann::json;

        Result rResult;

        curl_easy_setopt(mCURL, CURLOPT_URL, tURL.c_str());
        curl_easy_setopt(mCURL, CURLOPT_POSTFIELDS, tJSON.c_str());
        curl_easy_setopt(mCURL, CURLOPT_HTTPHEADER, mPostHeaders);

        auto res = curl_easy_perform(mCURL);
        if (res == CURLE_OK) {
            curl_easy_getinfo(mCURL, CURLINFO_RESPONSE_CODE, &rResult.code);
            // log.debug() << "HTTPClient post status " << rResult.code << " " << tURL;
        } else {
            rResult.code = -1;
            rResult.response = string(curl_easy_strerror(res));
        }

        curl_easy_reset(mCURL);

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
}