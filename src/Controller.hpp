#pragma once

#include <string>
#include <unordered_map>
#include <functional>
#include <regex>
#include <ctime>

namespace lap {
class Controller {
    typedef std::function<void(const std::string& command)> SendHandler;
    std::unordered_map<std::string, std::function<void(const std::string&, SendHandler)>> commands;

public:

    void registerCommand(const std::string& namespaze, const std::string& command, std::function<void(const std::string&, SendHandler)> callback) {
        commands[namespaze+"."+command] = callback;
        std::cout << "Controller registered " << namespaze+"."+command << std::endl;
    }

    void parse(const char* tBuffer, size_t tSize, SendHandler tSendHandler) {
        auto input = std::string(tBuffer, tSize - 1);
        auto cmdstr = std::regex_replace(input, std::regex("\n+"), "");
        auto found = cmdstr.find(" ");
        std::string param = "";
        if (found != std::string::npos) {
            param = cmdstr.substr(found + 1, cmdstr.length() - found);
            cmdstr.erase(found, cmdstr.length() - 1);
        }
        auto it = commands.find(cmdstr);
        if (it != commands.end()) {
            std::cout << "Controller: executing '" << input << "'" << std::endl;
            it->second(param, [tSendHandler](auto response) {
                static constexpr const char* endToken = "\nEND\r\n";
                tSendHandler(response + endToken);
            });
        } else {
            std::cout << "Controller: unknown command '" << input << "'" << std::endl;
        }
    }

};
}