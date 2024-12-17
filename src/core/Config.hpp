#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include "util.hpp"

namespace cst {
class Config {

    static constexpr const char* kSocketPath = "/tmp/cst_socket";
    static constexpr const char* kDeviceName = "default";

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

    Config() :
        socketPath(util::getEnvar("SOCKET_PATH")),
        iDevName(util::getEnvar("AURA_ENGINE_INPUT_DEVICE")),
        oDevName(util::getEnvar("AURA_ENGINE_OUTPUT_DEVICE"))
    {
        if (socketPath == "") socketPath = kSocketPath;
        if (iDevName == "") iDevName = kDeviceName;
        if (oDevName == "") oDevName = kDeviceName;
        std::cout << "Config: socketPath=" << socketPath << ", iDevName=" << iDevName << ", oDevName=" << oDevName << std::endl;
    }

    Config(const std::unordered_map<std::string, std::string> tMap) :
        socketPath(tMap.at("socket_path")),
        iDevName(tMap.at("in_device_name")),
        oDevName(tMap.at("out_device_name"))
    {

    }

    Config(const std::string& tPath) {
        try {
            auto map = parseConfigFile(tPath);
            socketPath = map.at("socket_path");
            iDevName = map.at("in_device_name");
            oDevName = map.at("out_device_name");
        }
        catch (const std::exception& e) {
            std::cerr << e.what() << std::endl;
        }

        auto envSocketPath = util::getEnvar("SOCKET_PATH");
        auto envIDevName = util::getEnvar("AURA_ENGINE_INPUT_DEVICE");
        auto envODevName = util::getEnvar("AURA_ENGINE_OUTPUT_DEVICE");

        if (envSocketPath != "") socketPath = envSocketPath;
        if (envIDevName != "") iDevName = envIDevName;
        if (envODevName != "") oDevName = envODevName;

        if (socketPath == "") socketPath = kSocketPath;
        if (iDevName == "") iDevName = kDeviceName;
        if (oDevName == "") oDevName = kDeviceName;
        std::cout << "Config: socketPath=" << socketPath << ", iDevName=" << iDevName << ", oDevName=" << oDevName << std::endl;
    }
};
}