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
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Lesser General Public License.
 */

#pragma once

#include <regex>
#include <string>
#include <vector>
#include "API.hpp"
#include "../io/MySQLClient.hpp"
#include "../util/M3UParser.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace castor {
namespace api {
class ClientYARM {

    const Config& mConfig;
    std::unique_ptr<io::MySQLClient> mMySQLClient;
    util::M3UParser mM3uParser;

public:
    ClientYARM(const Config& tConfig) :
        mConfig(tConfig),
        mMySQLClient(std::make_unique<io::MySQLClient>())
    {
        connect();
    }

    void connect() {
        try {
            mMySQLClient->connect(mConfig.yarmHost, mConfig.yarmUser, mConfig.yarmPass, mConfig.yarmUser);
        }
        catch (const std::exception& e) {
            log.error() << "ClientYARM failed to connect: " << e.what();
        }
    }

    
    std::vector<std::shared_ptr<PlayItem>> fetchItems() {
        auto now = std::time(0);
        auto frstr = std::to_string(now * 1000);
        auto tostr = std::to_string((now + mConfig.preloadTimeFile) * 1000);
        auto rows = mMySQLClient->query("SELECT t1, t2, PlayerValue FROM YARMProgramTable WHERE t2 >= " + frstr + " AND t2 <= " + tostr + " ORDER BY t1, t2 ASC;");
        std::vector<std::shared_ptr<PlayItem>> items;
        for (const auto& row : rows) {
            if (row.size() != 3) throw std::runtime_error("Unexpected column count");
            auto t1 = row[0];
            auto t2 = row[1];
            auto pv = row[2];
            time_t t1_ts = std::stol(t1) / 1000;
            time_t t2_ts = std::stol(t2) / 1000;
            auto url = pv.empty() ? "line://0" : "" + pv;
            if (url.ends_with("m3u")) {
                try {
                    auto m3u = mM3uParser.parse(url, t1_ts, t2_ts);
                    auto maxEnd = std::time(0) + mConfig.preloadTimeFile;
                    for (const auto& itm : m3u) {
                        if (itm->end <= maxEnd) {
                            items.emplace_back(itm);
                        }
                    }
                } catch (const std::exception& e) {
                    log.error() << "Calendar error reading M3U: " << e.what();
                }
            } else {
                items.emplace_back(std::make_shared<PlayItem>(t1_ts, t2_ts, url));
                log.debug(Log::Yellow) << util::timefmt(t1_ts, "%H:%M:%S") << " - " << util::timefmt(t2_ts, "%H:%M:%S") << " " << url;
            }
        }
        return items;
    }
};
}
}