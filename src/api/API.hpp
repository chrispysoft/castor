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

#include <ranges>
#include <json.hpp>
#include "../dsp/CodecBase.hpp"

namespace castor {
namespace api {

struct Program {
    int timeslotId = -1;
    int showId = -1;
    int mediaId = -1;
    std::string id;
    std::string start;
    std::string end;
    std::string showName;
    std::string episodeTitle;

    bool operator==(const Program& other) const {
        return other.timeslotId == this->timeslotId && other.showId == this->showId && other.mediaId == this->mediaId && other.id == this->id;
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
        j.at("mediaId").get_to(t.mediaId);
    }
    catch (...) {
        try {
            j.at("schedule").at("defaultMediaId").get_to(t.mediaId);
        }
        catch (...) {
            try {
                j.at("show").at("defaultMediaId").get_to(t.mediaId);
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
        {"mediaId", p.mediaId},
        {"id", p.id},
        {"start", p.start},
        {"end", p.end}
    };
}


struct Media {
    struct Entry {
        std::string uri;
        int duration;
    };
    int id;
    std::vector<Entry> entries;
};

void from_json(const nlohmann::json& j, Media::Entry& e) {
    j.at("uri").get_to(e.uri);
    try {
        j.at("duration").get_to(e.duration);
    }
    catch (...) {
        e.duration = 0;
    }
}

void from_json(const nlohmann::json& j, Media& m) {
    j.at("id").get_to(m.id);
    j.at("entries").get_to(m.entries);
}

}

struct PlayItem {
    std::time_t start;
    std::time_t end;
    std::string uri;
    std::shared_ptr<api::Program> program = nullptr;
    std::unique_ptr<audio::Metadata> metadata = nullptr;

    bool operator==(const PlayItem& item) const {
        return item.start == this->start && item.end == this->end && item.uri == this->uri;
    }

    bool operator<(const PlayItem& item) const {
        return this->start < item.start;
    }
};

void to_json(nlohmann::json& j, const PlayItem& p) {
    j = nlohmann::json {
        {"start", p.start},
        {"end", p.end},
        {"uri", p.uri}
    };
}

void to_json(nlohmann::json& j, const std::shared_ptr<PlayItem>& p) {
    j = *p;
}

void to_json(nlohmann::json& j, const std::vector<std::shared_ptr<PlayItem>>& v) {
    j = std::vector<nlohmann::json>(v.begin(), v.end());
}

void from_json(const nlohmann::json& j, PlayItem& p) {
    j.at("start").get_to(p.start);
    j.at("end").get_to(p.end);
    j.at("uri").get_to(p.uri);
}

void from_json(const nlohmann::json& j, std::vector<std::shared_ptr<PlayItem>>& v) {
    v.clear();
    v.reserve(j.size());
    for (const auto& item_json : j) {
        v.emplace_back(std::make_shared<PlayItem>(item_json.get<PlayItem>()));
    }
}



struct PlayLog {
    std::string trackStart;
    std::string trackArtist;
    std::string trackAlbum;
    std::string trackTitle;
    std::string showName;
    std::string timeslotId;
    float trackDuration;
    int trackType = 0;
    int trackNum = 1;
    int mediaId = -1;
    int showId = -1;
    int logSource = 1;
    

    PlayLog(const PlayItem& p) :
        trackStart(util::utcFmt(p.start)),
        trackDuration(p.end - p.start)
    {
        const auto& meta = p.metadata;
        if (meta) {
            trackTitle = meta->get("title");
            trackArtist = meta->get("artist");
            trackAlbum = meta->get("album");
        }
        auto program = p.program;
        if (program) {
            showId = program->showId;
            showName = program->showName;
            mediaId = program->mediaId;
            timeslotId = std::to_string(program->timeslotId);
        }
    }
};

void to_json(nlohmann::json& j, const PlayLog& p) {
    j = nlohmann::json {
        {"trackStart", p.trackStart},
        {"trackArtist", p.trackArtist},
        {"trackAlbum", p.trackAlbum},
        {"trackTitle", p.trackTitle},
        {"trackDuration", p.trackDuration},
        {"trackType", p.trackType},
        {"trackNum", p.trackNum},
        {"mediaId", p.mediaId},
        {"timeslotId", p.timeslotId},
        {"showId", p.showId},
        {"showName", p.showName},
        {"logSource", p.logSource}
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