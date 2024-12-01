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

namespace lap {
class AudioEngine : public AudioClientRenderer {
    static constexpr const char* kDefaultDeviceName = "Soundcraft Signature 12 MTK: USB Audio (hw:2,0)";
    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize =  4096;

    double mSampleRate;
    size_t mBufferSize;

    AudioClient mAudioClient;
    Mixer mMixer;
    Fallback mFallback;
    SilenceDetector mSilenceDet;
    
public:
    AudioEngine(double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mAudioClient(mSampleRate, mBufferSize),
        mMixer(mSampleRate, mBufferSize),
        mFallback(mSampleRate),
        mSilenceDet()
    {
        mAudioClient.setRenderer(this);
    }

    ~AudioEngine() override {
        
    }

    void registerControlCommands(Controller* tController) {
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

    void start(const std::string& tDeviceName = kDefaultDeviceName) {
        mAudioClient.start(tDeviceName);
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
    }
};

}