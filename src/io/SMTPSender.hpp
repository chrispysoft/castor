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

#include <regex>
#include <string>
#include <curl/curl.h>
#include "../util/Log.hpp"

namespace castor {
namespace io {

class SMTPSender {
    CURL* mCURL;
    struct curl_slist* mRecipients = NULL;
    
    struct UploadStatus {
        const char* payload;
        size_t bytesLeft;
    };

    static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userp) {
        auto uploadCtx = static_cast<UploadStatus*>(userp);
        size_t max = size * nmemb;
        if (uploadCtx->bytesLeft) {
            size_t toSend = (uploadCtx->bytesLeft < max) ? uploadCtx->bytesLeft : max;
            memcpy(ptr, uploadCtx->payload, toSend);
            uploadCtx->payload += toSend;
            uploadCtx->bytesLeft -= toSend;
            return toSend;
        }
        return 0;
    }

public:

    SMTPSender() {
        mCURL = curl_easy_init();
        if (!mCURL) {
            throw std::runtime_error("SMTPSender failed to init curl");
        }
    }

    ~SMTPSender() {
        curl_slist_free_all(mRecipients);
        if (mCURL) curl_easy_cleanup(mCURL);
    }

    void send(const std::string& tURL, const std::string& tUser, const std::string& tPass, const std::string& tSenderName, const std::string& tSenderAddress, const std::string& tRecipients, const std::string& tSubject, const std::string& tBody) {
        log.debug() << "SMTPSender sending email to [" << tRecipients << "] via " << tURL;

        std::regex re(R"(\s*,\s*)");
        std::sregex_token_iterator it(tRecipients.begin(), tRecipients.end(), re, -1);
        std::sregex_token_iterator end;
        struct curl_slist* recipientsList = NULL;
        for (; it != end; ++it) if (it->str().size()) recipientsList = curl_slist_append(recipientsList, it->str().c_str());
        
        std::string payload {
            "To: " + tRecipients + "\r\n"
            "From: \"" + tSenderName + "\" <" + tSenderAddress + ">\r\n"
            "Subject: " + tSubject + "\r\n"
            "\r\n"
            + tBody + "\r\n"
        };

        UploadStatus uploadCtx{ payload.c_str(), payload.size() };

        curl_easy_setopt(mCURL, CURLOPT_URL, tURL.c_str());
        curl_easy_setopt(mCURL, CURLOPT_USERNAME, tUser.c_str());
        curl_easy_setopt(mCURL, CURLOPT_PASSWORD, tPass.c_str());
        curl_easy_setopt(mCURL, CURLOPT_MAIL_FROM, tSenderAddress.c_str());
        curl_easy_setopt(mCURL, CURLOPT_MAIL_RCPT, recipientsList);
        curl_easy_setopt(mCURL, CURLOPT_READFUNCTION, &SMTPSender::WriteCallback);
        curl_easy_setopt(mCURL, CURLOPT_READDATA, &uploadCtx);
        curl_easy_setopt(mCURL, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(mCURL, CURLOPT_USE_SSL, (long) CURLUSESSL_TRY);

        auto res = curl_easy_perform(mCURL);

        curl_easy_reset(mCURL);
        curl_slist_free_all(recipientsList);

        if (res != CURLE_OK) {
            throw std::runtime_error("SMTPSender failed to send email: " + std::string(curl_easy_strerror(res)));
        }
    }
};
}
}