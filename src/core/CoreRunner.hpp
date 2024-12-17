#include <atomic>
#include <iostream>
#include <fstream>
#include <thread>
#include <string>

#include "Config.hpp"
#include "AudioEngine.hpp"
#include "SocketServer.hpp"
#include "Controller.hpp"
#include "APIClient.hpp"
#include "util.hpp"

namespace lap {
class CoreRunner {

    time_t mStartTime;
    std::atomic<bool> mRunning;
    std::unique_ptr<std::thread> mWorker = nullptr;
    Config mConfig;
    AudioEngine mEngine;
    SocketServer mSocket;
    Controller mController;
    APIClient mAPIClient;
    

public:
    CoreRunner() :
        mStartTime(std::time(0)),
        mRunning(false),
        mConfig("../config/config.txt"),
        mEngine(mConfig.iDevName, mConfig.oDevName),
        mSocket(mConfig.socketPath),
        mController(),
        mAPIClient()
    {
        mSocket.rxHandler = [this](const char* buffer, size_t size, auto txCallback) {
            this->mController.parse(buffer, size, txCallback);
        };
        mEngine.registerControlCommands(&mController);
        mEngine.setAPIClient(&mAPIClient);
    }

    
    void run() {
        mRunning.store(true);
        mSocket.start();
        mEngine.start();
        mWorker = std::make_unique<std::thread>([this] {
            //std::string testCmd = "mixer.select 0 true\n";
            //this->mController.parse(testCmd, [](auto) {});
            //testCmd = "aura_engine_line_in_0.set_track_metadata {\"show_name\": \"Arbeit Quo Vadis\", \"show_id\": 9, \"timeslot_id\": \"c2bf5e70-cf07-4176-a2e2-c81bef82fa0\", \"playlist_id\": 1865, \"playlist_item\": \"2.0\", \"track_type\": 2, \"track_start\": \"2024/12/11 19:00:10\", \"track_duration\": 3590, \"track_title\": \"\", \"track_album\": \"\", \"track_artist\": \"\"}\n";
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

    void terminate() {
        std::cout << "CoreRunner terminating..." << std::endl;
        mSocket.stop();
        mEngine.stop();
        mRunning = false;
        std::cout << "CoreRunner terminated" << std::endl;
    }

};

}