#pragma once

#include <optional>
#include "json.hpp"

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
}