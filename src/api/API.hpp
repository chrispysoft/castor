/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
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

#include "../third_party/json.hpp"

namespace castor {
namespace api {

struct Program {
    int timeslotId = -1;
    int showId = -1;
    int playlistId = -1;
    std::string id;
    std::string start;
    std::string end;
    std::string showName;
    std::string episodeTitle;

    bool operator==(const Program& other) const {
        return other.timeslotId == this->timeslotId && other.showId == this->showId && other.playlistId == this->playlistId && other.id == this->id;
    }
};

void from_json(const nlohmann::json& j, Program& t) {
    j.at("showId").get_to(t.showId);
    j.at("id").get_to(t.id);
    j.at("start").get_to(t.start);
    j.at("end").get_to(t.end);
    j.at("show").at("name").get_to(t.showName);

    try {
        j.at("timeslotId").get_to(t.timeslotId);
        j.at("episode").at("title").get_to(t.episodeTitle);
    }
    catch (...) {

    }
    
    try {
        j.at("playlistId").get_to(t.playlistId);
    }
    catch (...) {
        try {
            j.at("schedule").at("defaultPlaylistId").get_to(t.playlistId);
        }
        catch (...) {
            try {
                j.at("show").at("defaultPlaylistId").get_to(t.playlistId);
            }
            catch (...) {}
        }
    }
}

void to_json(nlohmann::json& j, const Program& p) {
    j = nlohmann::json {
        {"showName", p.showName},
        {"episodeTitle", p.episodeTitle},
        {"timeslotId", p.timeslotId},
        {"showId", p.showId},
        {"playlistId", p.playlistId},
        {"id", p.id},
        {"start", p.start},
        {"end", p.end}
    };
}


struct Playlist {
    struct Entry {
        std::string uri;
        int duration;
    };
    int id;
    std::vector<Entry> entries;
};

void from_json(const nlohmann::json& j, Playlist::Entry& e) {
    j.at("uri").get_to(e.uri);
    try {
        j.at("duration").get_to(e.duration);
    }
    catch (...) {
        e.duration = 0;
    }
}

void from_json(const nlohmann::json& j, Playlist& p) {
    j.at("id").get_to(p.id);
    j.at("entries").get_to(p.entries);
}

}

struct PlayItem {
    std::time_t start;
    std::time_t end;
    std::string uri;
    api::Program program = {};
    float fadeInTime = 1;
    float fadeOutTime = 1;

    bool operator==(const PlayItem& item) const {
        return item.start == this->start && item.end == this->end && item.uri == this->uri;
    }
};

void to_json(nlohmann::json& j, const PlayItem& p) {
    j = nlohmann::json {
        {"start", p.start},
        {"end", p.end},
        //{"uri", p.uri},
        {"program", p.program}
    };
}


struct PlayLog {
    std::string trackStart;
    std::time_t trackDuration;
    int playlistId;
    int showId;
    std::string showName;
    std::string timeslotId;

    PlayLog(const PlayItem& p) :
        trackStart(util::utcFmt(p.start)),
        trackDuration(p.end - p.start),
        playlistId(p.program.playlistId),
        showId(p.program.showId),
        showName(p.program.showName),
        timeslotId(std::to_string(p.program.timeslotId))
    {}
};

void to_json(nlohmann::json& j, const PlayLog& p) {
    j = nlohmann::json {
        {"trackStart", p.trackStart},
        {"trackDuration", p.trackDuration},
        {"playlistId", p.playlistId},
        {"showId", p.showId},
        {"showName", p.showName},
        {"timeslotId", p.timeslotId}
    };
}

struct Health {
    bool isHealthy;
    std::string logTime;
    std::string details;
};

void to_json(nlohmann::json& j, const Health& h) {
    j = nlohmann::json {
        {"isHealthy", h.isHealthy},
        {"logTime", h.logTime},
        {"details", h.details}
    };
}


}