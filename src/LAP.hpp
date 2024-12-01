#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>

#include "AudioEngine.hpp"
#include "SocketServer.hpp"
#include "Controller.hpp"
#include "util.hpp"

namespace lap {
class LAP {

    const std::string kSocketPath = "/tmp/lap_socket";
    
    time_t mStartTime;
    std::atomic<bool> mRunning;
    std::unique_ptr<std::thread> mWorker = nullptr;
    AudioEngine mEngine;
    SocketServer mSocket;
    Controller mController;
    

public:
    LAP() :
        mStartTime(std::time(0)),
        mRunning(false),
        mEngine(),
        mSocket(kSocketPath),
        mController()
    {
        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);

        mSocket.rxHandler = [this](const char* buffer, size_t size, auto txCallback) {
            this->mController.parse(buffer, size, txCallback);
        };

        mEngine.registerControlCommands(&mController);
    }

    static LAP& instance() {
        static LAP instance;
        return instance;
    }

    static void handlesig(int sig) {
        std::cout << "Received signal " << sig << std::endl;
        instance().stop();
    }

    void run() {
        mRunning.store(true);
        mSocket.start();
        mEngine.start();
        mWorker = std::make_unique<std::thread>([this] {
            const std::string testCmd = "in_stream_0.push ::https://stream.fro.at/fro128.mp3\n";
            this->mController.parse(testCmd.c_str(), testCmd.size(), [](auto response) {});
            while (this->mRunning.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        mWorker->join();
    }

    void stop() {
        std::cout << "STOPPING..." << std::endl;
        mSocket.stop();
        mEngine.stop();
        mRunning.store(false);
        std::cout << "STOPPED" << std::endl;
    }

};

}