#pragma once

#include <atomic>
#include <vector>
#include <iostream>
#include <string>
#include <cmath>

#include "AudioClient.hpp"
#include "SinOsc.hpp"
#include "WAVPlayer.hpp"
#include "MP3Player.hpp"
#include "SilenceDetector.hpp"

namespace lap {
class AudioEngine : public AudioClientRenderer {
    static constexpr char* const kDefaultDeviceName = "Soundcraft Signature 12 MTK: USB Audio (hw:2,0)";
    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize = 512;

    double mSampleRate;
    size_t mBufferSize;

    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning;

    AudioClient mAudioClient;
    SinOsc mOscL;
    SinOsc mOscR;
    MP3Player mPlayer;
    SilenceDetector mSilenceDet;
    
public:
    AudioEngine(double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mAudioClient(mSampleRate, mBufferSize),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mPlayer("../audio/Alternate Gate 6 Master.mp3", mSampleRate),
        mSilenceDet()
    {
        mAudioClient.setRenderer(this);
        mOscL.setFrequency(440);
        mOscR.setFrequency(525);
    }

    ~AudioEngine() override {
        
    }

    void start(const std::string& tDeviceName = kDefaultDeviceName) {
        mAudioClient.start(tDeviceName);
    }

    void stop() {
        mAudioClient.stop();
    }


    std::vector<int> inChannelMap = {6,7};
    std::vector<int> outChannelMap = {10,11};

    enum MixerMode {
        LINE,
        FILE
    };

    MixerMode mMode = MixerMode::FILE;


private:

    void renderCallback(const float* in, float* out, size_t nframes) override {

        switch (mMode) {
            case MixerMode::LINE: {
                memcpy(out, in, nframes * 2 * sizeof(float));
                break;
            }
            case MixerMode::FILE: {
                mPlayer.read(out, nframes);
                break;
            };
        }

        mSilenceDet.process(out, nframes);

        if (mSilenceDet.silenceDetected()) {
            for (auto i = 0; i < nframes; ++i) {
                float sL = mOscL.process();
                float sR = mOscR.process();
                out[i*2] = sL;
                out[i*2+1] = sR;
            }
        }
    }
};

}