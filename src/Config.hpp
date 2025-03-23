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

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "util/Log.hpp"
#include "util/util.hpp"

namespace castor {
class Config {

    typedef std::unordered_map<std::string, std::string> Map;

    static constexpr const char* kLogPath = "./logs/castor.log";
    static constexpr const char* kSocketPath = "/tmp/castor.socket";
    static constexpr const char* kAudioSourcePath = "";
    static constexpr const char* kAudioPlaylistPath = "";
    static constexpr const char* kAudioFallbackPath = "";
    static constexpr const char* kAudioRecordPath = "";
    static constexpr const char* kDeviceName = "default";
    static constexpr const char* kStreamOutURL = "";
    static constexpr const char* kStreamOutMetadataURL = "";
    static constexpr const char* kStreamOutName = "";
    static constexpr const char* kStreamOutDescription = "";
    static constexpr const char* kStreamOutGenre = "";
    static constexpr const char* kStreamOutHREF = "";
    static constexpr const char* kProgramURL = "";
    static constexpr const char* kMediaURL = "";
    static constexpr const char* kPlaylogURL = "";
    static constexpr const char* kHealthURL = "";
    static constexpr const char* kClockURL = "";
    static constexpr const char* kLogLevel = "1";
    static constexpr const char* kCalendarRefreshInterval = "60";
    static constexpr const char* kCalendarCachePath = "./cache/calendar.json";
    static constexpr const char* kHealthReportInterval = "60";
    static constexpr const char* kTCPPort = "0";
    static constexpr const char* kSilenceThreshold = "-90";
    static constexpr const char* kSilenceStartDuration = "5";
    static constexpr const char* kSilenceStopDuration = "1";
    static constexpr const char* kPreloadTimeFile = "3600";
    static constexpr const char* kPreloadTimeStream = "10";
    static constexpr const char* kPreloadTimeFallback = "3600";
    static constexpr const char* kProgramFadeInTime = "1.0";
    static constexpr const char* kProgramFadeOutTime = "1.0";
    static constexpr const char* kFallbackCrossFadeTime = "5.0";

    static Map parseConfigFile(const std::string& tPath) {
        std::ifstream cfgfile(tPath);
        if (!cfgfile) {
            throw std::runtime_error("Failed to open config file");
        }
        Map cfgmap;
        std::string line;
        while (std::getline(cfgfile, line)) {
            std::istringstream iss(line);
            std::string key, value;
            if (std::getline(iss, key, '=') && std::getline(iss, value)) {
                cfgmap[key] = value;
            }
        }
        cfgfile.close();
        return cfgmap;
    }

public:

    std::string logPath;
    std::string socketPath;
    std::string audioSourcePath;
    std::string audioPlaylistPath;
    std::string audioFallbackPath;
    std::string audioRecordPath;
    std::string iDevName;
    std::string oDevName;
    std::string streamOutURL;
    std::string streamOutMetadataURL;
    std::string streamOutName;
    std::string streamOutDescription;
    std::string streamOutGenre;
    std::string streamOutHREF;
    std::string programURL;
    std::string mediaURL;
    std::string playlogURL;
    std::string healthURL;
    std::string clockURL;
    std::string calendarCachePath;
    int logLevel;
    int calendarRefreshInterval;
    int healthReportInterval;
    int tcpPort;
    int silenceThreshold;
    int silenceStartDuration;
    int silenceStopDuration;
    int preloadTimeFile;
    int preloadTimeStream;
    int preloadTimeLine = 5;
    int preloadTimeFallback;

    int sampleRate = 44100;
    int audioBufferSize = 512;
    float programFadeInTime = 1;
    float programFadeOutTime = 1;
    float fallbackCrossFadeTime = 5;
    bool realtimeRendering = true;

    static std::string get(Map& map, std::string mapKey, std::string defaultValue) {
        auto envKey = mapKey;
        std::transform(envKey.begin(), envKey.end(), envKey.begin(), [](unsigned char c){ return std::toupper(c); });
        std::string value;
        if (map.find(mapKey) != map.end()) value = map.at(mapKey);
        auto envVal = util::getEnvar(envKey);
        if (!envVal.empty()) value = envVal;
        if (value.empty()) value = defaultValue;
        return value;
    }

    Config(const std::string& tPath) {
        Map map = {};
        try {
            map = parseConfigFile(tPath);
        }
        catch (const std::exception& e) {
            log.error() << "Config failed to parse file: " << e.what();
        }

        logPath = get(map, "log_path", kLogPath);
        socketPath = get(map, "socket_path", kSocketPath);
        audioSourcePath = get(map, "audio_source_path", kAudioSourcePath);
        audioPlaylistPath = get(map, "audio_playlist_path", kAudioPlaylistPath);
        audioRecordPath = get(map, "audio_record_path", kAudioRecordPath);
        audioFallbackPath = get(map, "audio_fallback_path", kAudioFallbackPath);
        iDevName = get(map, "in_device_name", kDeviceName);
        oDevName = get(map, "out_device_name", kDeviceName);
        streamOutURL = get(map, "stream_out_url", kStreamOutURL);
        streamOutMetadataURL = get(map, "stream_out_metadata_url", kStreamOutMetadataURL);
        streamOutName = get(map, "stream_out_name", kStreamOutName);
        streamOutDescription = get(map, "stream_out_description", kStreamOutDescription);
        streamOutGenre = get(map, "stream_out_genre", kStreamOutGenre);
        streamOutHREF = get(map, "stream_out_href", kStreamOutHREF);
        programURL = get(map, "program_url", kProgramURL);
        mediaURL = get(map, "media_url", kMediaURL);
        playlogURL = get(map, "playlog_url", kPlaylogURL);
        healthURL = get(map, "health_url", kHealthURL);
        clockURL = get(map, "clock_url", kClockURL);
        calendarCachePath = get(map, "calendar_cache_path", kCalendarCachePath);
        logLevel = std::stoi(get(map, "log_level", kLogLevel));
        calendarRefreshInterval = std::stoi(get(map, "calendar_refresh_interval", kCalendarRefreshInterval));
        healthReportInterval = std::stoi(get(map, "health_report_interval", kHealthReportInterval));
        tcpPort = std::stoi(get(map, "tcp_port", kTCPPort));
        silenceThreshold = std::stoi(get(map, "silence_threshold", kSilenceThreshold));
        silenceStartDuration = std::stoi(get(map, "silence_start_duration", kSilenceStartDuration));
        silenceStopDuration = std::stoi(get(map, "silence_stop_duration", kSilenceStopDuration));
        preloadTimeFile = std::stoi(get(map, "preload_time_file", kPreloadTimeFile));
        preloadTimeStream = std::stoi(get(map, "preload_time_stream", kPreloadTimeStream));
        preloadTimeFallback = std::stoi(get(map, "preload_time_fallback", kPreloadTimeFallback));
        programFadeInTime = std::stof(get(map, "program_fade_in_time", kProgramFadeInTime));
        programFadeOutTime = std::stof(get(map, "program_fade_out_time", kProgramFadeOutTime));
        fallbackCrossFadeTime = std::stof(get(map, "fallback_cross_fade_time", kFallbackCrossFadeTime));
        
        log.info() << "Config:"
        << "\n\t logPath=" << logPath
        << "\n\t logLevel=" << logLevel
        << "\n\t socketPath=" << socketPath
        << "\n\t audioSourcePath=" << audioSourcePath
        << "\n\t audioPlaylistPath=" << audioPlaylistPath
        << "\n\t audioFallbackPath=" << audioFallbackPath
        << "\n\t audioRecordPath=" << audioRecordPath
        << "\n\t iDevName=" << iDevName
        << "\n\t oDevName=" << oDevName
        << "\n\t streamOutURL=" << streamOutURL
        << "\n\t streamOutMetadataURL=" << streamOutMetadataURL
        << "\n\t streamOutName=" << streamOutName
        << "\n\t streamOutDescription=" << streamOutDescription
        << "\n\t streamOutGenre=" << streamOutGenre
        << "\n\t streamOutHREF=" << streamOutHREF
        << "\n\t programURL=" << programURL
        << "\n\t mediaURL=" << mediaURL
        << "\n\t playlogURL=" << playlogURL
        << "\n\t healthURL=" << healthURL
        << "\n\t clockURL=" << clockURL
        << "\n\t calendarRefreshInterval=" << calendarRefreshInterval
        << "\n\t healthReportInterval=" << healthReportInterval
        << "\n\t calendarCachePath=" << calendarCachePath
        << "\n\t tcpPort=" << tcpPort
        << "\n\t silenceThreshold=" << silenceThreshold
        << "\n\t silenceStartDuration=" << silenceStartDuration
        << "\n\t silenceStopDuration=" << silenceStopDuration
        << "\n\t preloadTimeFile=" << preloadTimeFile
        << "\n\t preloadTimeStream=" << preloadTimeStream
        << "\n\t preloadTimeFallback=" << preloadTimeFallback
        << "\n\t programFadeInTime=" << programFadeInTime
        << "\n\t programFadeOutTime=" << programFadeOutTime
        << "\n\t fallbackCrossFadeTime=" << fallbackCrossFadeTime;
    }
};
}