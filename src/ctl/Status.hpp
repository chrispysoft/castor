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

#include <json.hpp>

namespace castor {
namespace ctl {

struct Status {
    std::string status;
    std::string message;
    time_t startTime = 0;
    time_t endTime = 0;
    time_t currTime = 0;
    int elapsed = 0;
    int remaining = 0;
    float rmsLinIn = 0.0f;
    float rmsLinOut = 0.0f;
    bool fallbackActive = false;
    nlohmann::json players;
};

void from_json(const nlohmann::json& j, Status& s) {
    j.at("rmsLinIn").get_to(s.rmsLinIn);
    j.at("rmsLinOut").get_to(s.rmsLinOut);
    j.at("fallbackActive").get_to(s.fallbackActive);
    j.at("players").get_to(s.players);
}

void to_json(nlohmann::json& j, const Status& s) {
    j = nlohmann::json{
        {"rmsLinIn", s.rmsLinIn},
        {"rmsLinOut", s.rmsLinOut},
        {"fallbackActive", s.fallbackActive},
        {"players", s.players}
    };
}

}
}