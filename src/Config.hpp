#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "util/Log.hpp"
#include "util/util.hpp"

namespace cst {
class Config {

    typedef std::unordered_map<std::string, std::string> Map;

    static constexpr const char* kLogPath = "./logs/cst.log";
    static constexpr const char* kSocketPath = "/tmp/cst_socket";
    static constexpr const char* kAudioSourcePath = "";
    static constexpr const char* kAudioPlaylistPath = "";
    static constexpr const char* kAudioFallbackPath = "";
    static constexpr const char* kAudioRecordPath = "";
    static constexpr const char* kDeviceName = "default";
    static constexpr const char* kStreamOutURL = "";
    static constexpr const char* kProgramURL = "http://localhost/program";
    static constexpr const char* kPlaylistURL = "http://localhost/playlist";
    static constexpr const char* kPlaylogURL = "http://localhost/playlog";
    static constexpr const char* kHealthURL = "http://localhost/health";
    static constexpr const char* kClockURL = "http://localhost/clock";
    static constexpr const char* kPlaylistToken = "castor:id";
    static constexpr const char* kTCPPort = "0";

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
    std::string programURL;
    std::string playlistURL;
    std::string playlogURL;
    std::string healthURL;
    std::string clockURL;
    std::string playlistToken;
    int tcpPort;

    static std::string get(Map& map, std::string mapKey, std::string envKey, std::string defaultValue) {
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

        logPath = get(map, "log_path", "LOG_PATH", kLogPath);
        socketPath = get(map, "socket_path", "SOCKET_PATH", kSocketPath);
        audioSourcePath = get(map, "audio_source_path", "AUDIO_SOURCE_PATH", kAudioSourcePath);
        audioPlaylistPath = get(map, "audio_playlist_path", "AUDIO_PLAYLIST_PATH", kAudioPlaylistPath);
        audioRecordPath = get(map, "audio_record_path", "AUDIO_RECORD_PATH", kAudioRecordPath);
        audioFallbackPath = get(map, "audio_fallback_path", "AUDIO_FALLBACK_PATH", kAudioFallbackPath);
        iDevName = get(map, "in_device_name", "INPUT_DEVICE", kDeviceName);
        oDevName = get(map, "out_device_name", "OUTPUT_DEVICE", kDeviceName);
        streamOutURL = get(map, "stream_out_url", "STREAM_OUTPUT_URL", kStreamOutURL);
        programURL = get(map, "program_url", "PROGRAM_URL", kProgramURL);
        playlistURL = get(map, "playlist_url", "PLAYLIST_URL", kPlaylistURL);
        playlogURL = get(map, "playlog_url", "PLAYLOG_URL", kPlaylogURL);
        healthURL = get(map, "health_url", "HEALTH_URL", kHealthURL);
        clockURL = get(map, "clock_url", "CLOCK_URL", kClockURL);
        playlistToken = get(map, "playlist_token", "PLAYLIST_TOKEN", kPlaylistToken);
        tcpPort = std::stoi(get(map, "tcp_port", "TCP_PORT", kTCPPort));

        log.info() << "Config:"
        << "\n\t logPath=" << logPath 
        << "\n\t socketPath=" << socketPath 
        << "\n\t audioSourcePath=" << audioSourcePath
        << "\n\t audioPlaylistPath=" << audioPlaylistPath
        << "\n\t audioFallbackPath=" << audioFallbackPath
        << "\n\t audioRecordPath=" << audioRecordPath
        << "\n\t iDevName=" << iDevName 
        << "\n\t oDevName=" << oDevName 
        << "\n\t streamOutURL=" << streamOutURL 
        << "\n\t programURL=" << programURL
        << "\n\t playlistURL=" << playlistURL
        << "\n\t playlogURL=" << playlogURL
        << "\n\t healthURL=" << healthURL
        << "\n\t clockURL=" << clockURL
        << "\n\t playlistToken=<hidden>"
        << "\n\t tcpPort=" << tcpPort;
    }
};
}