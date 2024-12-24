#pragma once

#include <optional>
#include "json.hpp"

namespace api {

using json = nlohmann::json;

struct Program {
    int timeslotId = -1;
    int showId = -1;
    int playlistId = -1;
    std::string id;
    std::string start;
    std::string end;
    std::string memo;
    std::string showName;
    std::string episodeTitle;
};


void from_json(const json& j, Program& t) {
    j["timeslotId"].get_to(t.timeslotId);
    j["showId"].get_to(t.showId);
    j["id"].get_to(t.id);
    j["start"].get_to(t.start);
    j["end"].get_to(t.end);
    j["timeslot"]["memo"].get_to(t.memo);
    j["show"]["name"].get_to(t.showName);
    j["episode"]["title"].get_to(t.episodeTitle);
    
    try {
        j["playlistId"].get_to(t.playlistId);
    }
    catch (...) {
        try {
            j["schedule"]["defaultPlaylistId"].get_to(t.playlistId);
        }
        catch (...) {
            try {
                j["show"]["defaultPlaylistId"].get_to(t.playlistId);
            }
            catch (...) {}
        }
    }

    if (t.playlistId == -1) {
        std::cerr << "No playlist id" << std::endl;
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
