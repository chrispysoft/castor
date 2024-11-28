#include <atomic>
#include <vector>
#include <iostream>
#include <string>
#include <cmath>
#include <portaudio.h>
#include "SinOsc.hpp"
#include "WAVPlayer.hpp"
#include "MP3Player.hpp"
#include "SilenceDetector.hpp"

namespace lap {
class AudioEngine {

    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize = 512;

    double mSampleRate;
    size_t mBufferSize;

    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning;

    PaStream* mStream;
    SinOsc mOscL;
    SinOsc mOscR;
    MP3Player mPlayer;
    SilenceDetector mSilenceDet;

public:
    AudioEngine(double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mStream(nullptr),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mPlayer("../audio/Alternate Gate 6 Master.mp3", mSampleRate),
        mSilenceDet()
    {
        mOscL.setFrequency(440);
        mOscR.setFrequency(525);
        Pa_Initialize();
    }

    ~AudioEngine() {
        Pa_Terminate();
    }


    bool open(const PaDeviceIndex& inDeviceIdx, const PaDeviceIndex& outDeviceIdx) {
        PaStreamParameters inputParameters;
        PaStreamParameters outputParameters;

        inputParameters.device = inDeviceIdx;
        outputParameters.device = outDeviceIdx;

        if (outputParameters.device == paNoDevice) {
            return false;
        }

        const PaDeviceInfo* pInfo = Pa_GetDeviceInfo(inDeviceIdx);
        if (pInfo != 0) {
            printf("Output device name: '%s'\r", pInfo->name);
        }

        inputParameters.channelCount = 2;
        inputParameters.sampleFormat = paFloat32;
        inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
        inputParameters.hostApiSpecificStreamInfo = NULL;

        outputParameters.channelCount = 2;
        outputParameters.sampleFormat = paFloat32;
        outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = NULL;

        auto res = Pa_OpenStream(&mStream, &inputParameters, &outputParameters, mSampleRate, mBufferSize, paNoFlag, &AudioEngine::paCallback, this);

        if (res != paNoError) {
            return false;
        }

        res = Pa_SetStreamFinishedCallback(mStream, &AudioEngine::paStreamFinished);

        if (res != paNoError) {
            close();
            return false;
        }

        return true;
    }

    bool close() {
        if (!mStream) return false;

        auto res = Pa_CloseStream(mStream);
        mStream = nullptr;

        return res == paNoError;
    }

    bool start() {
        if (!mStream) return false;

        auto res = Pa_StartStream(mStream);

        return res == paNoError;
    }

    bool stop() {
        if (!mStream) return false;

        auto res = Pa_StopStream(mStream);

        return res == paNoError;
    }


    std::vector<int> inChannelMap = {6,7};
    std::vector<int> outChannelMap = {10,11};

    enum MixerMode {
        LINE,
        FILE
    };

    MixerMode mMode = MixerMode::FILE;


private:

    int paCallbackMethod(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags) {

        (void) timeInfo;
        (void) statusFlags;

        auto in = static_cast<const float*>(inputBuffer);
        auto out = static_cast<float*>(outputBuffer);

        switch (mMode) {
            case MixerMode::LINE: {
                memcpy(out, in, framesPerBuffer * 2 * sizeof(float));
                break;
            }
            case MixerMode::FILE: {
                mPlayer.read(out, framesPerBuffer);
                break;
            };
        }

        mSilenceDet.process(out, framesPerBuffer);

        if (mSilenceDet.silenceDetected()) {
            for (auto i = 0; i < framesPerBuffer; ++i) {
                float sL = mOscL.process();
                float sR = mOscR.process();
                out[i*2] = sL;
                out[i*2+1] = sR;
            }
        }

        return paContinue;
    }

    void paStreamFinishedMethod() {
        std::cout << "Stream finished" << std::endl;
    }

    static int paCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return static_cast<AudioEngine*>(userData)->paCallbackMethod(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }

    static void paStreamFinished(void* userData) {
        return static_cast<AudioEngine*>(userData)->paStreamFinishedMethod();
    }

    
public:

    int getDeviceID(const std::string& name) {
        using namespace std;
        auto numDevices = Pa_GetDeviceCount();
        cout << numDevices << " devices" << endl;
        const PaDeviceInfo *deviceInfo;
        int deviceID = -1;
        for (int i=0; i<numDevices; i++ ) {
            deviceInfo = Pa_GetDeviceInfo(i);
            const auto& devName = string(deviceInfo->name);
            cout << "#" << i << " " << deviceInfo->maxInputChannels << " " << deviceInfo->maxOutputChannels << " " << devName << endl;
            if (devName == name) {
                return i;
            }
        }
        return -1;
    }

    void connect() {
        using namespace std;

        static const string deviceName = "Soundcraft Signature 12 MTK: USB Audio (hw:2,0)";
        auto deviceID = getDeviceID(deviceName);
        if (deviceID == -1) deviceID = Pa_GetDefaultOutputDevice();
        std::cout << "Using deviceID " << deviceID << std::endl;
        
        if (open(deviceID, deviceID)) {
            start();
        }
    }

    void disconnect() {
        stop();
        close();
    }
};

}