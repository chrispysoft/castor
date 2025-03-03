/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

// #pragma once

// #include <thread>
// #include <atomic>
// #include <string>
// #include <string_view>
// #include <deque>
// #include <iostream>
// #include <mutex>
// #include "AudioProcessor.hpp"
// #include "StreamPlayer.hpp"
// #include "../util/Log.hpp"

// namespace castor {
// namespace audio {
// class QueuePlayer : public Player {
//     std::unique_ptr<std::thread> mWorker;
//     std::atomic<bool> mRunning;
//     std::deque<std::string> mQueue;
//     std::mutex mMutex;
//     StreamPlayer mPlayer;
    
// public:
//     QueuePlayer(double tSampleRate, const std::string tName = "") : Player(tName),
//         mWorker(nullptr),
//         mRunning(false),
//         mQueue {},
//         mPlayer(tSampleRate)
//     {
        
//     }

//     ~QueuePlayer() {
//         if (mRunning) stop();
//     }


//     void load(const std::string& tURL, double seek = 0) override {
//         log.info() << "QueuePlayer open " << tURL;
//         if (tURL.ends_with(".m3u")) {
//             log.debug() << "QueuePlayer opening m3u file " << tURL;
//             std::ifstream file(tURL);
//             if (!file.is_open()) throw std::runtime_error("Failed to open file");
//             std::string line;
//             while (getline(file, line)) {
//                 if (line.starts_with("#")) continue;
//                 util::stripM3ULine(line);
//                 mQueue.push_back(line);
//                 // log.debug() << "QueuePlayer added m3u entry " << line;
//             }
//             file.close();
//             log.debug() << "QueuePlayer added " << mQueue.size() << " m3u entries";
//         } else {
//             mQueue.push_back(tURL);
//         }

//         if (mQueue.empty()) {
//             log.error() << "QueuePlayer is empty";
//             return;
//         }

//         if (mQueue.size() == 1 && !mRunning) {
//             auto url = mQueue.front();
//             mQueue.pop_front();
//             try {
//                 mPlayer.load(url, seek);
//                 log.debug() << "QueuePlayer loaded " << url;
//             }
//             catch (const std::runtime_error& e) {
//                 log.error() << "QueuePlayer failed to load " << url << ": " << e.what();
//             }
//         }
//         else {
//             mRunning = true;
//             mWorker = std::make_unique<std::thread>([this] {
//                 while (this->mRunning) {
//                     this->work2();
//                     std::this_thread::sleep_for(std::chrono::milliseconds(100));
//                 }
//             });
//         }
//     }

//     void stop() override {
//         log.debug() << "QueuePlayer stopping...";
//         mRunning = false;
//         mQueue = {};
//         mPlayer.eject();
//         if (mWorker) {
//             if (mWorker->joinable()) mWorker->join();
//             mWorker = nullptr;
//         }
//         state = IDLE;
//         log.info() << "QueuePlayer stopped";
//     }

    
//     void process(const sam_t* in, sam_t* out, size_t framecount) override {
//         mPlayer.process(in, out, framecount);
//     }

// private:
//     void work2() {
//     //     if (mPlayer.isIdle()) {
//     //         if (mQueue.size() > 0) {
//     //             auto url = mQueue.front();
//     //             mQueue.pop_front();
//     //             if (url != mPlayer.currentURL()) {
//     //                 try {
//     //                     mPlayer.load(url);
//     //                     log.info() << "QueuePlayer loaded " << url;
//     //                 }
//     //                 catch (const std::runtime_error& e) {
//     //                     log.error() << "QueuePlayer failed to load " << url << "': " << e.what();
//     //                 }
//     //             }
//     //         }
//     //     }
//     }
// };
// }
// }