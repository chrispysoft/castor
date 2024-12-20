#pragma once

#include <atomic>
#include <vector>
#include <iostream>
#include <string>
#include <cmath>

#include "Config.hpp"
#include "Controller.hpp"
#include "AudioClient.hpp"
#include "Mixer.hpp"
#include "Fallback.hpp"
#include "SilenceDetector.hpp"
#include "ShowManager.hpp"
#include "Recorder.hpp"
#include "StreamOutput.hpp"
#include "APIClient.hpp"
#include "Log.hpp"
#include "util.hpp"

namespace cst {
class AudioEngine : public AudioClientRenderer {
    static constexpr const char* kDefaultDeviceName = "default";
    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize =  1024;

    double mSampleRate;
    size_t mBufferSize;

    const Config& mConfig;
    AudioClient mAudioClient;
    Mixer mMixer;
    Fallback mFallback;
    SilenceDetector mSilenceDet;
    ShowManager mShowManager;
    Recorder mRecorder;
    StreamOutput mStreamOutput;
    util::Timer mUptimer;
    
public:
    AudioEngine(const Config& tConfig, double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mConfig(tConfig),
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mAudioClient(mConfig.iDevName, mConfig.oDevName, mSampleRate, mBufferSize),
        mMixer(mSampleRate, mBufferSize),
        mFallback(mSampleRate),
        mSilenceDet(),
        mShowManager(),
        mRecorder(mSampleRate),
        mStreamOutput(mSampleRate),
        mUptimer()
    {
        mAudioClient.setRenderer(this);
        mMixer.setShowManager(&mShowManager);
    }

    ~AudioEngine() override {
        
    }

    void registerControlCommands(Controller* tController) {

        tController->registerCommand("", "uptime", [this](auto, auto callback) {
            auto uptime = util::timestr(this->mUptimer.get());
            callback(uptime);
        });

        tController->registerCommand("aura_engine", "status", [this](auto, auto callback) {
            auto uptime = util::timestr(this->mUptimer.get());
            auto fallback = util::boolstr(this->mSilenceDet.silenceDetected());
            auto status = "{ \"uptime\": \"" + uptime + "\", \"is_fallback\": " + fallback + " }";
            callback(status);
        });

        tController->registerCommand("aura_engine", "version", [this](auto, auto callback) {
            auto version = "{ \"core\": \"0.0.1\", \"liquidsoap\": \"-1\" }";
            callback(version);
        });

        mMixer.registerControlCommands(tController);
    }

    void setAPIClient(APIClient* tAPIClient) {
        mShowManager.setAPIClient(tAPIClient);
    }

    void start() {
        mAudioClient.start();

        if (mConfig.streamOutURL != "") {
            std::string icecastURL = "icecast://source:stroemer1@" + mConfig.streamOutURL + ":8000/castoria.ogg";
            mStreamOutput.start(icecastURL);
        }
        // try {
        //     mRecorder.start("audio/test.mp3");
        // }
        // catch (const std::exception& e) {
        //     log.debug() << "Start record failed: " << e.what();
        // }
        // catch (...) {
        //     log.debug() << "Start record failed due to other error";
        // }
    }

    void stop() {
        mAudioClient.stop();
    }

    void work() {
        if (mSilenceDet.silenceDetected()) {
            mFallback.start();
        } else {
            mFallback.stop();
        }
    }


private:

    void renderCallback(const float* in, float* out, size_t nframes) override {

        mMixer.process(in, out, nframes);
        mSilenceDet.process(out, nframes);

        if (mFallback.isActive()) {
            mFallback.process(in, out, nframes);
        }

        if (mRecorder.isRunning()) {
            mRecorder.process(out, nframes);
        }

        if (mStreamOutput.isRunning()) {
            mStreamOutput.process(out, nframes);
        }
    }
};

}