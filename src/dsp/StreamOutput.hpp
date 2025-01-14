#pragma once

#include "Recorder.hpp"
#include "../util/Log.hpp"
#include "../util/HTTPClient.hpp"
#include <atomic>

namespace cst {
namespace audio {
class StreamOutput {

    std::atomic<bool> mRunning = false;
    Recorder mRecorder;

public:

    StreamOutput(double tSampleRate) :
        mRecorder(tSampleRate)
    {
    }

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
            if (tRetryInterval > 0) {
                std::thread([this, url=tURL, interval=tRetryInterval] {
                    if (!this->mRunning) return;
                    log.warn() << "StreamOutput retrying to start in " <<  interval << " seconds...";
                    std::this_thread::sleep_for(std::chrono::seconds(interval));
                    if (!this->mRunning) return;
                    log.warn() << "StreamOutput restarting...";
                    this->start(url, interval);
                }).detach();
            }
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
        auto res = HTTPClient().get(url);
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