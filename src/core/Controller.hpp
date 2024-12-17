#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <regex>
#include <ctime>
#include "util.hpp"

namespace cst {
class Controller {
    typedef std::function<void(const std::string& command)> SendHandler;
    std::unordered_map<std::string, std::function<void(const std::string&, SendHandler)>> commands;

public:

    void registerCommand(const std::string& namespaze, const std::string& command, std::function<void(const std::string&, SendHandler)> callback) {
        auto key = (namespaze == "") ? command : namespaze+"."+command;
        commands[key] = callback;
        std::cout << "Controller: registered '" << key << "'" << std::endl;
    }

    void parse(const char* tBuffer, size_t tSize, SendHandler tSendHandler) {
        auto input = std::string(tBuffer, tSize - 1);
        parse(input, tSendHandler);
    }

    void parse(const std::string& tInput, SendHandler tSendHandler) {
        auto cmdstr = std::regex_replace(tInput, std::regex("\n+"), "");
        auto [cmd, args] = util::splitBy(cmdstr, ' ');
        auto it = commands.find(cmd);
        if (it != commands.end()) {
            std::cout << "Controller: executing '" << tInput << "'" << std::endl;
            it->second(args, [tSendHandler](auto response) {
                static constexpr const char* endToken = "\nEND\r\n";
                tSendHandler(response + endToken);
            });
        } else {
            std::cout << "Controller: unknown command '" << tInput << "'" << std::endl;
        }
    }

};
}