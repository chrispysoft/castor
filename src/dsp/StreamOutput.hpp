/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <atomic>
#include <thread>
#include "Recorder.hpp"
#include "../io/HTTPClient.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {
class StreamOutput {

    std::atomic<bool> mRunning = false;
    std::thread mStartThread;
    Recorder mRecorder;
    std::unique_ptr<io::HTTPClient> mHTTPClient;

public:
    StreamOutput(double tSampleRate) :
        mRecorder(tSampleRate),
        mHTTPClient(std::make_unique<io::HTTPClient>())
    {
        mRecorder.logName = "StreamWriter";
    }

    bool isRunning() {
        return mRecorder.isRunning();
    }

    void start(const std::string& tURL, int tRetryInterval = 5) {
        if (mRunning) return;
        log.debug() << "StreamOutput start " << tURL;
        mRunning = true;
        mStartThread = std::thread([this, url=tURL, interval=tRetryInterval] {
            while (mRunning && !mRecorder.isRunning()) {
                try {
                    mRecorder.start(url);
                }
                catch (const std::exception& e) {
                    log.error() << "StreamOutput failed to start: " << e.what() << ". Retrying in " << interval << " sec...";
                    util::sleepCancellable(interval, mRunning);
                }
            }
        });
    }

    void stop() {
        if (!mRunning) return;
        log.debug() << "StreamOutput stop...";
        mRunning = false;
        mRecorder.stop();
        if (mStartThread.joinable()) mStartThread.join();
    }

    void updateMetadata(const std::string& tURL, std::shared_ptr<PlayItem> tItem) {
        std::string songName;
        auto program = tItem->program;
        if (program) {
            songName = program->showName;
            std::replace(songName.begin(), songName.end(), ' ', '+');
        }
        log.debug() << "StreamOutput updateMetadata " << songName;
        auto url = tURL + "&mode=updinfo&song=" + songName;
        auto res = mHTTPClient->get(url);
        auto code = res.code;
        if (code != 200) {
            throw std::runtime_error("metadata http request failed with code: " + std::to_string(code));
        }
        auto xml = res.response;
        if (xml.find("<message>Metadata update successful</message>") == std::string::npos) {
            throw std::runtime_error("metadata http request failed with response: " + xml);
        }
        log.debug() << "StreamOutput updateMetadata success";
    }


    void process(const sam_t* tSamples, size_t nframes) {
        mRecorder.process(tSamples, nframes);
    }

};
}
}