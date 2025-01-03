#pragma once

#include <optional>
#include "../third_party/json.hpp"

namespace cst {
namespace api {

using json = nlohmann::json;

struct Program {
    int timeslotId = -1;
    int showId = -1;
    int playlistId = -1;
    std::string id;
    std::string start;
    std::string end;
    std::string showName;
    std::string episodeTitle;
};


void from_json(const json& j, Program& t) {
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


struct Playlist {
    struct Entry {
        std::string uri;
        int duration;
    };
    int id;
    std::vector<Entry> entries;
};

void from_json(const json& j, Playlist::Entry& e) {
    j.at("uri").get_to(e.uri);
    try {
        j.at("duration").get_to(e.duration);
    }
    catch (...) {
        e.duration = 0;
    }
}

void from_json(const json& j, Playlist& p) {
    j.at("id").get_to(p.id);
    j.at("entries").get_to(p.entries);
}

}

struct PlayItem {
    std::time_t start;
    std::time_t end;
    std::string uri;
    api::Program program = {};
    std::time_t lastTry = 0;
    std::time_t retryInterval = 5;
    std::time_t loadTime = 30;
    std::time_t fadeInTime = 1;
    std::time_t fadeOutTime = 3;
    std::time_t ejectTime = 1;

    std::time_t scheduleStart() const { return start - loadTime; }
    std::time_t scheduleEnd() const { return end - 5; }

    bool isInScheduleTime() const {
        auto now = std::time(0);
        return now >= scheduleStart() && now <= scheduleEnd();
    }

    bool operator==(const PlayItem& item) const {
        return item.start == this->start && item.end == this->end && item.uri == this->uri;
    }
};

void to_json(nlohmann::json& j, const PlayItem& p) {
    j = nlohmann::json {
        {"trackStart", util::utcFmt(p.start)},
        {"trackDuration", p.end - p.start},
        {"timeslotId", std::to_string(p.program.timeslotId)},
        {"playlistId", p.program.playlistId},
        {"showId", p.program.showId},
        {"showName", p.program.showName}
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