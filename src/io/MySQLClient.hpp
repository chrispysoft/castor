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

#include <string>
#include <vector>
#include <mariadb/mysql.h>
#include "../util/Log.hpp"

namespace castor {
namespace io {

class MySQLClient {
    MYSQL* mConn;

public:

    MySQLClient() {
        log.debug() << "MySQLClient init...";
        mConn = mysql_init(nullptr);
        if (!mConn) throw std::runtime_error("mysql_init failed");
        log.debug() << "MySQLClient inited";
    }

    ~MySQLClient() {
        mysql_close(mConn);
        log.debug() << "MySQLClient closed";
    }

    void connect(const std::string& tHost, const std::string& tUser, const std::string& tPass, const std::string& tDB) {
        log.debug() << "MySQLClient connecting...";
        auto conn = mysql_real_connect(mConn, tHost.c_str(), tUser.c_str(), tPass.c_str(), tDB.c_str(), 0, nullptr, 0);
        if (!conn || conn != mConn) throw std::runtime_error("mysql_real_connect failed: " + std::string(mysql_error(mConn)));
        log.info() << "MySQLClient connected successfully";
    }

    std::vector<std::vector<std::string>> query(const std::string& tStatement) {
        log.debug() << "MySQLClient query: " << tStatement;
        auto err = mysql_query(mConn, tStatement.c_str());
        if (err) throw std::runtime_error("MySQLClient failed: " + std::string(mysql_error(mConn)));
        auto res = mysql_store_result(mConn);
        if (!res) throw std::runtime_error("MySQLClient to retrieve result: " + std::string(mysql_error(mConn)));
        // NB: usually we prepare the query first by binding all params to prevent injections.
        // Actually not needed since we're taking care of correct usage.
        auto numRows = mysql_num_rows(res);
        auto numCols = mysql_num_fields(res);
        std::vector<std::vector<std::string>> result(numRows, std::vector<std::string>(numCols, "")); // alloc once to save realloc costs
        MYSQL_ROW row;
        for (int i = 0; i < numRows; ++i) {
            row = mysql_fetch_row(res);
            if (!row) throw std::runtime_error("Expected mysql row but got nullptr");
            for (auto j = 0; j < numCols; ++j) {
                if (row[j]) result[i][j] = row[j];
            }
        }
        mysql_free_result(res);
        return result;
    }
};

}
}