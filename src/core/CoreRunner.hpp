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

namespace cst {
class CoreRunner {

    std::atomic<bool> mRunning = false;
    std::unique_ptr<std::thread> mWorker = nullptr;
    Config mConfig;
    AudioEngine mEngine;
    SocketServer mSocket;
    Controller mController;
    APIClient mAPIClient;
    

public:
    CoreRunner() :
        mConfig("../config/config.txt"),
        mEngine(mConfig.iDevName, mConfig.oDevName),
        mSocket(mConfig.socketPath),
        mController(),
        mAPIClient(mConfig.playlogURL)
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
            //testCmd = "aura_engine_line_in_0.set_track_metadata {\"show_name\": \"Arbeit Quo Vadis\", \"show_id\": 9, \"timeslot_id\": \"c2bf5e70-cf07-4176-a2e2-c81bef82fa0\", \"playlist_id\": 1865, \"playlist_item\": \"2.0\", \"track_type\": 2, \"track_start\": \"2024/12/17 20:50:10\", \"track_duration\": 3590, \"track_title\": \"\", \"track_album\": \"\", \"track_artist\": \"\"}\n";
            //testCmd = "in_queue_0.push annotate:show_name="BlauCrowd FM",show_id="16",timeslot_id="111a9c3b-6f48-4eef-9aa4-a1fb52662b40",playlist_id="2743",playlist_item="1.0",track_type="0",track_start="",track_duration="10",track_title="title",track_album="",track_artist="artist":"
            //this->mController.parse(testCmd, [](auto) {});
            //testCmd = "in_stream_0.url https://stream.fro.at/fro128.mp3\n";
            //testCmd = "in_queue_0.push ::audio/test.m3u\n";
            //for (int i = 0; i < 5; ++i) {
                //testCmd = "in_queue_0.push ::audio/A maj.mp3\n";
                //this->mController.parse(testCmd.c_str(), testCmd.size(), [](auto response) {});
            //}
            //testCmd = "in_queue_0.push ::audio/Alternate Gate 6 Master.mp3\n";
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
        mRunning = false;
        mSocket.stop();
        mEngine.stop();
        std::cout << "CoreRunner terminated" << std::endl;
    }

};

}