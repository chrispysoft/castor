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

#include <string>
#include <unordered_map>
#include "Log.hpp"

namespace castor {
namespace util {

class ArgumentParser {
    std::unordered_map<std::string, std::string> mArgs;
public:
    const auto& args() const { return mArgs; }
    
    ArgumentParser(int argc, char* argv[]) {
        for (auto i = 1; i+1 < argc; i += 2) {
            auto key = std::string(argv[i]);
            auto val = std::string(argv[i+1]);
            mArgs[key] = val;
        }
    }
};

}
}