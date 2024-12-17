#pragma once

#include <curl/curl.h>
#include <iostream>
#include <string>
#include "util.hpp"

namespace cst {
class APIClient {
    const std::string mPlaylogURL;
    CURL* mCURL;

public:

    APIClient(const std::string& tPlaylogURL) :
        mPlaylogURL(tPlaylogURL)
    {
        mCURL = curl_easy_init();
        if (!mCURL) {
            throw std::runtime_error("Failed to init curl");
        }
        // curl_global_init(CURL_GLOBAL_ALL); // init Winsock
    }

    ~APIClient() {
        if (mCURL) curl_easy_cleanup(mCURL);
        // curl_global_cleanup();
    }


    void postPlaylog(const std::string tPlaylog) {
        using namespace std;
        // cout << "postPlaylog " << mPlaylogURL << " " << tPlaylog << endl;

        string playlog(tPlaylog);
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

        // cout << "postPlaylog " << mPlaylogURL << " " << playlog << endl;
        
        curl_easy_setopt(mCURL, CURLOPT_URL, mPlaylogURL.c_str());
        curl_easy_setopt(mCURL, CURLOPT_POSTFIELDS, playlog.c_str());

        curl_slist* list = NULL;
        list = curl_slist_append(list, "Content-Type: application/json");
        curl_easy_setopt(mCURL, CURLOPT_HTTPHEADER, list);

        auto res = curl_easy_perform(mCURL);
        if (res == CURLE_OK) {
            std::cout << "APIClient post success" << std::endl;
        } else {
            std::cerr << "APIClient post failed: " << curl_easy_strerror(res) << std::endl;
        }

        curl_slist_free_all(list);
    }
};
}