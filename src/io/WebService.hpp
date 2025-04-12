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

#include <atomic>
#include <mutex>
#include <string>
#include <sstream>
#include <httplib.h>
#include <json.hpp>
#include "../ctl/Parameters.hpp"
#include "../ctl/Status.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace io {

class WebService {
public:
    struct AuthConf {
        std::string user;
        std::string pass;
        std::string token;
    };

    struct StaticContentToken {
        static constexpr size_t kTokenLength = 64;
        static constexpr time_t kTokenTimeout = 60;
        const time_t created;
        const std::string bearer;
        const std::string json;

        static std::string generateToken(size_t length = kTokenLength) {
            const std::string characters = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            std::mt19937 rng(static_cast<unsigned int>(std::time(nullptr)));
            std::uniform_int_distribution<> dist(0, characters.size() - 1);
            std::string token;
            for (int i = 0; i < length; ++i) token += characters[dist(rng)];
            return token;
        }

        static std::string generateJSON(const std::string& token, time_t created) {
            nlohmann::json j = {
                {"token", token},
                {"created", created},
                {"timeout", kTokenTimeout}
            };
            return j.dump();
        }

        StaticContentToken() :
            created(std::time(0)),
            bearer(generateToken()),
            json(generateJSON(bearer, created))
        {}

        bool validate(const std::string& auth) const {
            auto timeValid = std::time(0) - created <= kTokenTimeout;
            return timeValid && auth == "Bearer " + bearer;
        }
    };

private:
    static constexpr time_t kClientConnectedTimeout = 1;

    using Handler = httplib::Server::Handler;
    using Request = httplib::Request;
    using Response = httplib::Response;
    using JSON = nlohmann::json;

    httplib::Server mServer;
    const std::string mHost;
    const int mPort;
    const std::string mStaticPath;
    const AuthConf mAuthConf;
    std::string mStaticContent;
    std::unique_ptr<StaticContentToken> mStaticToken = nullptr;
    ctl::Parameters& mParameters;
    ctl::Status& mStatus;
    std::atomic<time_t> mLastClientRequest = 0;
    std::mutex mInterceptionMutex;
    bool mServeStatic = true;

    typedef void(WebService::*InterceptionHandler)(const Request& req, Response& res);

public:
    WebService(const std::string& tHost, int tPort, const std::string& tStaticPath, const std::string& tAuthUser, const std::string& tAuthPass, const std::string& tAuthToken, ctl::Parameters& tParameters, ctl::Status& tStatus) :
        mServer(),
        mHost(tHost),
        mPort(tPort),
        mStaticPath(tStaticPath),
        mStaticContent(),
        mAuthConf(AuthConf{tAuthUser, tAuthPass, tAuthToken}),
        mParameters(tParameters),
        mStatus(tStatus)
    {}


    httplib::Server::Handler interceptAPI(InterceptionHandler handler) {
        return [this, handler](const Request& req, Response& res) {
            std::lock_guard<std::mutex> lock(mInterceptionMutex);

            auto auth = req.get_header_value("Authorization");

            log.debug() << "WebService interceptAPI from " << req.remote_addr << " " << req.path << " auth: " << auth;
            mLastClientRequest = std::time(0);

            if (mStaticToken && mStaticToken->validate(auth)) (this->*handler)(req, res);
            else {
                res.set_content("Unauthorized", "text/plain");
                res.status = 401;
            }
        };
    }

    httplib::Server::Handler interceptStatic(InterceptionHandler handler) {
        return [this, handler](const Request& req, Response& res) {
            std::lock_guard<std::mutex> lock(mInterceptionMutex);

            log.debug() << "WebService interceptStatic from " << req.remote_addr << " " << req.path;

            auto it = req.headers.find("Authorization");
            if (it != req.headers.end()) {
                log.debug() << "WebService auth header: " << it->first << " " << it->second;
                if (it->second == ("Basic " + httplib::detail::base64_encode(mAuthConf.user + ":" + mAuthConf.pass))) {
                    (this->*handler)(req, res);
                    return;
                }
            }

            res.set_header("WWW-Authenticate", "Basic realm=\"Authenticate\"");
            res.set_content("Unauthorized", "text/plain");
            res.status = 401;
        };
    }

    bool isClientConnected() {
        return std::time(0) - mLastClientRequest <= kClientConnectedTimeout;
    }

    void start() {
        log.debug() << "WebService starting...";

        mServer.Options(R"(.*)", [](const Request&, Response& res) {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
        });

        //mServer.set_error_handler(std::bind(&WebService::errorHandler, this, std::placeholders::_1, std::placeholders::_2));

        mServer.Get ("/status", interceptAPI(&WebService::getStatus));
        mServer.Get ("/parameters", interceptAPI(&WebService::getParameters));
        mServer.Post("/parameters", interceptAPI(&WebService::postParameters));
        if (mServeStatic) {
            loadStaticContent();
            mServer.Get("/token", interceptStatic(&WebService::getStaticToken));
            mServer.Get("/", interceptStatic(&WebService::getStatic));
        }
        
        log.info() << "WebService listening on " << mHost << ":" << mPort << " (static: " << mStaticPath << ")";
        mServer.listen(mHost, mPort);
    }

    void stop() {
        log.debug() << "WebService stopping...";
        mServer.stop();
        log.info() << "WebService stopped";
    }

private:

    void errorHandler(const Request& req, Response& res) {
        log.error() << "WebService error: " << req.path;
        res.set_content("Not Found", "text/plain");
        res.status = 404;
    }

    void getStatus(const Request& req, Response& res) {
        log.debug() << "WebService get status";
        auto j = nlohmann::json(mStatus);
        res.set_content(j.dump(), "text/json");
    }

    void getParameters(const Request& req, Response& res) {
        log.debug() << "WebService get parameters";
        auto j = nlohmann::json(mParameters.get());
        res.set_content(j.dump(), "text/json");
    }

    void postParameters(const Request& req, Response& res) {
        log.debug() << "WebService post parameters";
        try {
            auto j = nlohmann::json::parse(req.body);
            mParameters.set(j);
        }
        catch (const std::exception& e) {
            log.error() << "WebService json parse error: " << e.what();
        }
        getParameters(req, res);
    }

    void getStatic(const Request& req, Response& res) {
        log.debug() << "WebService get static";
        res.set_content(mStaticContent, "text/html");
    }

    void getStaticToken(const Request& req, Response& res) {
        log.debug() << "WebService get static token";
        mStaticToken = std::make_unique<StaticContentToken>();
        res.set_content(mStaticToken->json, "text/html");
    }


    void loadStaticContent() {
        try {
            auto indexFile = mStaticPath + "/index.html";
            mStaticContent = util::readRawFile(indexFile);
            log.info() << "WebService loaded static content from: " << indexFile;
        }
        catch (const std::exception& e) {
            log.error() << "WebService failed to load static content: " << e.what();
            mStaticContent = "<html><body><h1>Static content not found</h1></body></html>";
        }
    }
};

}
}