#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>

#include "AudioEngine.hpp"
#include "SocketServer.hpp"

namespace lap {
class LAP {

    const std::string kSocketPath = "/tmp/lap_socket";
    
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning;
    AudioEngine mEngine;
    SocketServer mSocket;

public:
    LAP() :
        mRunning(false),
        mEngine(),
        mSocket(kSocketPath)
    {
        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);
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