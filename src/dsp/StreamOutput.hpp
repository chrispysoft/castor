#pragma once

#include "Recorder.hpp"
#include "../util/Log.hpp"
#include "../util/HTTPClient.hpp"

namespace cst {
namespace audio {
class StreamOutput {

    Recorder mRecorder;

public:

    StreamOutput(double tSampleRate) :
        mRecorder(tSampleRate)
    {
    }

    bool isRunning() {
        return mRecorder.isRunning();
    }

    void start(const std::string& tURL) {
        mRecorder.start(tURL);
    }

    void stop() {
        mRecorder.stop();
    }

    void updateMetadata(const std::string& tMetadata) {
        log.debug() << "StreamOutput updateMetadata " << tMetadata;
        auto url = "http://source:hackme@localhost:8000/admin/metadata?mount=/test.mp3&mode=updinfo&song="+tMetadata;
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


    void process(const float* tSamples, size_t nframes) {
        mRecorder.process(tSamples, nframes);
    }

};
}
}