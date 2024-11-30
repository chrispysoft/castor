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
    static constexpr const char* kDefaultDeviceName = "Soundcraft Signature 12 MTK: USB Audio (hw:2,0)";
    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize = 512;

    double mSampleRate;
    size_t mBufferSize;

    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning;

    AudioClient mAudioClient;
    SinOsc mOscL;
    SinOsc mOscR;
    MP3Player* mPlayer;
    std::vector<MP3Player> mFilePlayers;
    SilenceDetector mSilenceDet;
    
public:
    AudioEngine(double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mAudioClient(mSampleRate, mBufferSize),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mPlayer(nullptr),
        mSilenceDet()
    {
        mAudioClient.setRenderer(this);
        mOscL.setFrequency(432);
        mOscR.setFrequency(432 + (432.0/12.0*3));
    }

    ~AudioEngine() override {
        
    }

    void start(const std::string& tDeviceName = kDefaultDeviceName) {
        mAudioClient.start(tDeviceName);
    }

    void stop() {
        mAudioClient.stop();
    }

    void play(const std::string& tURL) {
        if (mPlayer) {
            delete mPlayer;
            mPlayer = nullptr;
        }
        try {
            auto player = new MP3Player(tURL, mSampleRate);
            mPlayer = player;
        }
        catch (...) {
            std::cout << "Failed to load '" << tURL << "'" << std::endl;
        }
    }

    void roll(double position) {
        if (mPlayer) mPlayer->roll(position);
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
                if (mPlayer) mPlayer->read(out, nframes);
                else memset(out, 0, nframes * 2 * sizeof(float));
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