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

#pragma once

#include <atomic>
#include "Recorder.hpp"
#include "../io/HTTPClient.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {
class StreamOutput {

    std::atomic<bool> mRunning = false;
    Recorder mRecorder;
    io::HTTPClient mHTTPClient;

public:
    StreamOutput(double tSampleRate) :
        mRecorder(tSampleRate)
    {}

    bool isRunning() {
        return mRecorder.isRunning();
    }

    void start(const std::string& tURL, int tRetryInterval = 5) {
        log.debug() << "StreamOutput start " << tURL;
        mRunning = true;
        try {
            mRecorder.start(tURL);
        }
        catch (const std::exception& e) {
            log.error() << "StreamOutput failed to start: " << e.what();
        }
    }

    void stop() {
        if (!mRunning) {
            return;
        }
        log.debug() << "StreamOutput stop...";
        mRunning = false;
        mRecorder.stop();
    }

    void updateMetadata(const std::string& tURL, const std::string& tMetadata) {
        log.debug() << "StreamOutput updateMetadata " << tMetadata;
        auto url = tURL + "&mode=updinfo&song=" + tMetadata;
        auto res = mHTTPClient.get(url);
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