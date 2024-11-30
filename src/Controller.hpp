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

    time_t mStartTime;

public:

    typedef std::function<void(const std::string& command)> CommandHandler;
    CommandHandler commandHandler;


    void registerCommand(const std::string& namespaze, const std::string& command, std::function<void(const std::string&, SendHandler)> callback) {
        commands[namespaze+"."+command] = callback;
    }


    Controller() {
        mStartTime = std::time(0);

        static const std::string endToken = "\nEND\r\n";
        commands["aura_engine.version"] = [&](auto param, SendHandler sendHandler) {
            sendHandler("{ \"core\": \"0.0.1\", \"liquidsoap\": \"-1\" }" + endToken);
        };
        commands["aura_engine.status"] = [&](auto param, SendHandler sendHandler) {
            sendHandler("{ \"uptime\": \"0d 00h 01m 11s\", \"is_fallback\": true }" + endToken);
        };
        commands["uptime"] = [&](auto param, SendHandler sendHandler) {
            auto uptime = std::time(0) - this->mStartTime;
            sendHandler("0d 00h 01m 11s" + endToken);
        };
        commands["mixer.inputs"] = [&](auto param, SendHandler sendHandler) {
            //commandCallback()
            sendHandler("in_queue_0.2 in_queue_1.2 in_stream_0.2 in_stream_1.2 aura_engine_line_in_0" + endToken);
        };
        commands["mixer.outputs"] = [&](auto param, SendHandler sendHandler) {
            //commandCallback()
            sendHandler("{ \"stream\": [], \"line\": [ \"aura_engine_line_out_0\" ] }" + endToken);
        };
        commands["mixer.volume"] = [&](auto param, SendHandler sendHandler) {
            //commandCallback()
            sendHandler("ready=false selected=false single=false volume=100% remaining=inf" + endToken);
        };
        commands["mixer.status"] = [&](auto param, SendHandler sendHandler) {
            sendHandler("ready=false selected=false single=false volume=100% remaining=inf" + endToken);
        };
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
            it->second(param, tSendHandler);
        } else {
            std::cout << "Controller: unknown command '" << input << "'" << std::endl;
        }
    }

};
}