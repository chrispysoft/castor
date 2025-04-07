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

#include <iostream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <sstream>
#include <json.hpp>
#include "../util/Log.hpp"

namespace castor {
namespace ctl {

struct ParameterTree {
    float inputGain = 0.0f;
    float outputGain = 0.0f;
};

void from_json(const nlohmann::json& j, ParameterTree& t) {
    j.at("inputGain").get_to(t.inputGain);
    j.at("outputGain").get_to(t.outputGain);
}

void to_json(nlohmann::json& j, const ParameterTree& t) {
    j = nlohmann::json{
        {"inputGain", t.inputGain},
        {"outputGain", t.outputGain},
    };
}

bool validate(const ParameterTree& t) {
    if (t.inputGain < -24 || t.inputGain > 24) return false;
    if (t.outputGain < -24 || t.outputGain > 24) return false;
    return true;
}


class Parameters {
    const std::string mParametersPath;;
    ParameterTree mParameterTree;

public:
    
    Parameters(const std::string& tParametersPath) :
        mParametersPath(tParametersPath)
    {
        load();
    }

    ~Parameters() {
        save();
    }

    const ParameterTree& get() const {
        return mParameterTree;
    }

    void set(const nlohmann::json& j) {
        try {
            ParameterTree p = j;
            if (!validate(p)) throw std::runtime_error("Parameters validation failed");
            mParameterTree = p;
            log.debug() << "Parameters set done";
            save();
        }
        catch (const std::exception& e) {
            log.error() << "Parameters set failed: " << e.what();
        }
    }

private:

    void load() {
        try {
            nlohmann::json j;
            std::ifstream(mParametersPath) >> j;
            mParameterTree = j;
            log.info() << "Parameters load done";
        }
        catch (const std::exception& e) {
            log.error() << "Parameters load failed: " << e.what();
            mParameterTree = ParameterTree();
        }
    }

    void save() {
        try {
            auto j = nlohmann::json(mParameterTree);
            std::ofstream(mParametersPath) << j.dump(2);
            log.debug() << "Parameters save done";
        }
        catch (const std::exception& e) {
            log.error() << "Parameters save failed: " << e.what();
        }
    }
};

}
}
