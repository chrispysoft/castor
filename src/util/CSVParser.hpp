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

#include <fstream>
#include <string>
#include <vector>
#include "Log.hpp"

namespace castor {
namespace util {

class CSVParser {
    std::vector<std::vector<std::string>> mRows;
public:
    const auto& rows() { return mRows; }
    
    CSVParser(const std::string& tURL) {
        log.debug() << "CSVParser open " << tURL;
        std::ifstream file(tURL);
        if (!file) throw std::runtime_error("Failed to open " + tURL);
        std::string line;
        while (std::getline(file, line)) {
            std::istringstream iss(line);
            std::vector<std::string> cells;
            std::string cell;
            while (std::getline(iss, cell, ',')) cells.emplace_back(cell);
            mRows.emplace_back(cells);
        }
        file.close();
        log.debug() << "CSVParser closed " << tURL;
    }
};

}
}