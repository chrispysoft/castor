#pragma once

#include <atomic>
#include <vector>
#include <iostream>
#include <string>
#include <cmath>

#include "Controller.hpp"
#include "AudioClient.hpp"
#include "Mixer.hpp"
#include "Fallback.hpp"
#include "SilenceDetector.hpp"
#include "Recorder.hpp"
#include "APIClient.hpp"

namespace lap {
class AudioEngine : public AudioClientRenderer {
    static constexpr const char* kDefaultDeviceName = "default";
    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize = 512;

    double mSampleRate;
    size_t mBufferSize;

    AudioClient mAudioClient;
    Mixer mMixer;
    Fallback mFallback;
    SilenceDetector mSilenceDet;
    Recorder mRecorder;
    
public:
    AudioEngine(const std::string& tIDevName = kDefaultDeviceName, const std::string& tODevName = kDefaultDeviceName, double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mAudioClient(tIDevName, tODevName, mSampleRate, mBufferSize),
        mMixer(mSampleRate, mBufferSize),
        mFallback(mSampleRate),
        mSilenceDet(),
        mRecorder(mSampleRate)
    {
        mAudioClient.setRenderer(this);
    }

    ~AudioEngine() override {
        
    }

    void registerControlCommands(Controller* tController) {

        tController->registerCommand("", "uptime", [&](auto args, auto callback) {
            const std::string uptime = "0d 00h 01m 11s";
            callback(uptime);
        });

        tController->registerCommand("aura_engine", "status", [&](auto args, auto callback) {
            const std::string uptime = "0d 00h 01m 11s";
            const std::string isFallback = this->mSilenceDet.silenceDetected() ? "true" : "false";
            const auto status = "{ \"uptime\": \"" + uptime + "\", \"is_fallback\": " + isFallback + " }";
            callback(status);
        });

        tController->registerCommand("aura_engine", "version", [&](auto args, auto callback) {
            auto version = "{ \"core\": \"0.0.1\", \"liquidsoap\": \"-1\" }";
            callback(version);
        });

        mMixer.registerControlCommands(tController);
    }

    void setAPIClient(APIClient* tAPIClient) {
        mMixer.setAPIClient(tAPIClient);
    }

    void start() {
        mAudioClient.start();
        // try {
        //     mRecorder.start("/home/fro/code/lap/audio/test.mp3");
        // }
        // catch (const std::exception& e) {
        //     std::cout << "Start record failed: " << e.what() << std::endl;
        // }
        // catch (...) {
        //     std::cout << "Start record failed due to other error" << std::endl;
        // }
    }

    void stop() {
        mAudioClient.stop();
    }


private:

    void renderCallback(const float* in, float* out, size_t nframes) override {

        mMixer.process(in, out, nframes);
        mSilenceDet.process(out, nframes);

        if (mSilenceDet.silenceDetected()) {
            mFallback.process(in, out, nframes);
        }

        if (mRecorder.isRunning()) {
            mRecorder.process(out, nframes);
        }
    }
};

}