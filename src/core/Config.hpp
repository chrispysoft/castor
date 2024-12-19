#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "Log.hpp"
#include "util.hpp"

namespace cst {
class Config {

    static constexpr const char* kSocketPath = "/tmp/cst_socket";
    static constexpr const char* kDeviceName = "default";
    static constexpr const char* kPlaylogURL = "http://localhost/playlog";

    static std::unordered_map<std::string, std::string> parseConfigFile(const std::string& tPath) {
        std::ifstream cfgfile(tPath);
        if (!cfgfile) {
            throw std::runtime_error("Failed to open config file");
        }
        std::unordered_map<std::string, std::string> cfgmap;
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

    std::string socketPath;
    std::string iDevName;
    std::string oDevName;
    std::string playlogURL;

    Config(const std::string& tPath) {
        try {
            auto map = parseConfigFile(tPath);
            if (map.find("socket_path") != map.end()) socketPath = map.at("socket_path");
            if (map.find("in_device_name") != map.end()) iDevName = map.at("in_device_name");
            if (map.find("out_device_name") != map.end()) oDevName = map.at("out_device_name");
            if (map.find("playlog_url") != map.end()) playlogURL = map.at("playlog_url");
        }
        catch (const std::exception& e) {
            log.error() << "Config failed to parse file: " << e.what();
        }

        auto envSocketPath = util::getEnvar("SOCKET_PATH");
        auto envIDevName = util::getEnvar("AURA_ENGINE_INPUT_DEVICE");
        auto envODevName = util::getEnvar("AURA_ENGINE_OUTPUT_DEVICE");
        auto envPlaylogURL = util::getEnvar("AURA_ENGINE_API_URL_PLAYLOG");

        if (envSocketPath != "") socketPath = envSocketPath;
        if (envIDevName != "") iDevName = envIDevName;
        if (envODevName != "") oDevName = envODevName;
        if (envPlaylogURL != "") playlogURL = envPlaylogURL;

        if (socketPath == "") socketPath = kSocketPath;
        if (iDevName == "") iDevName = kDeviceName;
        if (oDevName == "") oDevName = kDeviceName;
        if (playlogURL == "") playlogURL = kPlaylogURL;
        log.info() << "Config: socketPath=" << socketPath << ", iDevName=" << iDevName << ", oDevName=" << oDevName << ", playlogURL=" << playlogURL;
    }
};
}