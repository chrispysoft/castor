#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>

#include "AudioEngine.hpp"
#include "SocketServer.hpp"
#include "Controller.hpp"

namespace lap {
class LAP {

    const std::string kSocketPath = "/tmp/lap_socket";
    
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning;
    AudioEngine mEngine;
    SocketServer mSocket;
    Controller mController;

public:
    LAP() :
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

        mController.commandHandler = [this](const auto& cmdstr) {
            this->mEngine.play(cmdstr);
        };
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