#pragma once

#include <iostream>
#include <string>
#include <regex>
#include "APIClient.hpp"
#include "util.hpp"

namespace cst {
class ShowManager {
    APIClient* mAPIClientPtr = nullptr;
    
public:

    void setAPIClient(APIClient* tAPIClient) {
        mAPIClientPtr = tAPIClient;
    }


    void setTrackMetadata(std::string tMetadata) {
        using namespace std;
        string playlog(tMetadata);
        playlog = regex_replace(playlog, regex("show_name"), "showName");
        playlog = regex_replace(playlog, regex("show_id"), "showId");
        playlog = regex_replace(playlog, regex("timeslot_id"), "timeslotId");
        playlog = regex_replace(playlog, regex("playlist_id"), "playlistId");
        playlog = regex_replace(playlog, regex("playlist_item"), "playlistItem");
        playlog = regex_replace(playlog, regex("track_type"), "trackType");
        playlog = regex_replace(playlog, regex("track_start"), "trackStart");
        playlog = regex_replace(playlog, regex("track_duration"), "trackDuration");
        playlog = regex_replace(playlog, regex("track_title"), "trackTitle");
        playlog = regex_replace(playlog, regex("track_artist"), "trackArtist");
        playlog = regex_replace(playlog, regex("track_album"), "trackAlbum");

        // cout << "setTrackMetadata " << playlog << endl;
        if (mAPIClientPtr) mAPIClientPtr->postPlaylog(playlog);
    }

    void setTrackMetadata(std::unordered_map<std::string, std::string> tMetadata) {
         //if (mAPIClientPtr) mAPIClientPtr->postPlaylog(tMetadata);
    }

};
}