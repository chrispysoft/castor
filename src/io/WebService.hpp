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

#include <string>
#include <httplib.h>
#include <json.hpp>
#include "../ctl/Parameters.hpp"
#include "../ctl/Status.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace io {

class WebService {

    using Handler = httplib::Server::Handler;
    using Request = httplib::Request;
    using Response = httplib::Response;
    using JSON = nlohmann::json;

    httplib::Server mServer;
    const std::string mHost;
    const int mPort;
    const std::string mStaticPath;
    ctl::Parameters& mParameters;
    ctl::Status& mStatus;

public:
    WebService(const std::string& tHost, int tPort, const std::string& tStaticPath, ctl::Parameters& tParameters, ctl::Status& tStatus) :
        mServer(),
        mHost(tHost),
        mPort(tPort),
        mStaticPath(tStaticPath),
        mParameters(tParameters),
        mStatus(tStatus)
    {
        mServer.set_error_handler([](const Request&, Response& res) {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
        });
    }

    typedef void(WebService::*InterceptionHandler)(const Request& req, Response& res);

    // cat /dev/urandom | tr -dc 'a-zA-Z0-9' | fold -w 256 | head -n 1
    const std::string mToken = "";

    httplib::Server::Handler intercept(InterceptionHandler handler) {
        return [this, handler](const Request& req, Response& res) {
            auto auth = req.get_header_value("Authorization");
            if (mToken.empty() || auth == "Bearer " + mToken) (this->*handler)(req, res);
            else res.status = 401;
        };
    }

    void start() {
        log.debug() << "WebService starting...";
        
        mServer.Get ("/status", intercept(&WebService::getStatus));
        mServer.Get ("/parameters", intercept(&WebService::getParameters));
        mServer.Post("/parameters", intercept(&WebService::postParameters));
        
        if (!mServer.set_mount_point("/", mStaticPath)) {
            log.error() << "Failed to set mount point " << mStaticPath;
        }
        
        log.info() << "WebService listening on " << mHost << ":" << mPort << " (static: " << mStaticPath << ")";
        mServer.listen(mHost, mPort);
    }

    void stop() {
        log.debug() << "WebService stopping...";
        mServer.stop();
        log.info() << "WebService stopped";
    }

    void getStatus(const Request& req, Response& res) {
        auto j = nlohmann::json(mStatus);
        res.set_content(j.dump(), "text/json");
    }

    void getParameters(const Request& req, Response& res) {
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
};

}
}