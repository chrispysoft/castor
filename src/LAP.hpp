#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>
#include <csignal>

#include "Config.hpp"
#include "AudioEngine.hpp"
#include "SocketServer.hpp"
#include "Controller.hpp"
#include "APIClient.hpp"
#include "util.hpp"

namespace lap {
class LAP {

    time_t mStartTime;
    std::atomic<bool> mRunning;
    std::unique_ptr<std::thread> mWorker = nullptr;
    Config mConfig;
    AudioEngine mEngine;
    SocketServer mSocket;
    Controller mController;
    APIClient mAPIClient;
    

public:
    LAP() :
        mStartTime(std::time(0)),
        mRunning(false),
        mConfig(),
        mEngine(mConfig.iDevName, mConfig.oDevName),
        mSocket(mConfig.socketPath),
        mController(),
        mAPIClient()
    {
        std::signal(SIGINT,  handlesig);
        std::signal(SIGTERM, handlesig);

        mSocket.rxHandler = [this](const char* buffer, size_t size, auto txCallback) {
            this->mController.parse(buffer, size, txCallback);
        };

        mEngine.registerControlCommands(&mController);
        mEngine.setAPIClient(&mAPIClient);
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
            //std::string testCmd = "mixer.select 2 true\n";
            //this->mController.parse(testCmd, [](auto) {});
            //testCmd = "in_stream_0.url https://stream.fro.at/fro128.mp3\n";
            //testCmd = "in_queue_0.push ::/home/fro/code/lap/audio/test.m3u\n";
            //for (int i = 0; i < 5; ++i) {
                //testCmd = "in_queue_0.push ::/home/fro/code/lap/audio/A maj.mp3\n";
                //this->mController.parse(testCmd.c_str(), testCmd.size(), [](auto response) {});
            //}
            //testCmd = "in_queue_0.push ::/home/fro/code/lap/audio/Alternate Gate 6 Master.mp3\n";
            //this->mController.parse(testCmd, [](auto) {});
            while (this->mRunning.load()) {
                this->mEngine.work();
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