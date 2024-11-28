#pragma once

#include <atomic>
#include <vector>
#include <iostream>
#include <string>
#include <bits/stdc++.h>
#include <portaudio.h>

namespace lap {

class AudioClientRenderer {
public:
    virtual void renderCallback(const float* in, float* out, size_t size) = 0;
    virtual ~AudioClientRenderer() = default;
};

class AudioClient {

    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize = 512;

    double mSampleRate;
    size_t mBufferSize;

    PaStream* mStream;
    AudioClientRenderer* mRenderer;
    

public:

    AudioClient(double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mStream(nullptr),
        mRenderer(nullptr)
    {
        Pa_Initialize();
    }

    ~AudioClient() {
        Pa_Terminate();
    }


    void start(const std::string& tDeviceName) {
        auto deviceID = getDeviceID(tDeviceName);
        if (deviceID == -1) {
            std::cout << "Device '" << tDeviceName << "' not found - using default" << std::endl;
            deviceID = Pa_GetDefaultOutputDevice();
        }
        std::cout << "Starting audio stream with device id " << deviceID << ", sample rate " << mSampleRate << ", buffer size " << mBufferSize  << std::endl;
        
        if (openStream(deviceID, deviceID)) {
            startStream();
        }
    }

    void stop() {
        stopStream();
        closeStream();
    }

    void setRenderer(AudioClientRenderer* tRenderer) {
        mRenderer = tRenderer;
    }

private:
    
    std::vector<std::string> getDeviceNames() {
        
        using namespace std;
        auto numDevices = Pa_GetDeviceCount();
        cout << "Found " << numDevices << " devices:" << endl;

        vector<string> deviceNames(numDevices);

        const PaDeviceInfo* info;
        int deviceID = -1;
        for (auto i = 0; i < numDevices; i++ ) {
            info = Pa_GetDeviceInfo(i);
            deviceNames[i] = string(info->name);
            cout << "#" << i << " " << info->maxInputChannels << " " << info->maxOutputChannels << " " << deviceNames[i] << endl;
        }
        return deviceNames;
    }

    int getDeviceID(const std::string& tDeviceName) {
        const auto names = getDeviceNames();
        auto it = std::find(names.begin(), names.end(), tDeviceName);
        int idx = it - names.begin();
        return idx;
    }

    bool openStream(const PaDeviceIndex& inDeviceIdx, const PaDeviceIndex& outDeviceIdx) {
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

        auto res = Pa_OpenStream(&mStream, &inputParameters, &outputParameters, mSampleRate, mBufferSize, paNoFlag, &AudioClient::paCallback, this);

        if (res != paNoError) {
            return false;
        }

        res = Pa_SetStreamFinishedCallback(mStream, &AudioClient::paStreamFinished);

        if (res != paNoError) {
            closeStream();
            return false;
        }

        return true;
    }

    bool closeStream() {
        if (!mStream) return false;
        auto res = Pa_CloseStream(mStream);
        mStream = nullptr;
        return res == paNoError;
    }

    bool startStream() {
        if (!mStream) return false;
        auto res = Pa_StartStream(mStream);
        return res == paNoError;
    }

    bool stopStream() {
        if (!mStream) return false;
        auto res = Pa_StopStream(mStream);
        return res == paNoError;
    }

    int paCallbackMethod(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags) {
        if (mRenderer) mRenderer->renderCallback(static_cast<const float*>(inputBuffer), static_cast<float*>(outputBuffer), framesPerBuffer);
        return paContinue;
    }

    void paStreamFinishedMethod() {
        std::cout << "Stream finished" << std::endl;
    }

    static int paCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return static_cast<AudioClient*>(userData)->paCallbackMethod(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }

    static void paStreamFinished(void* userData) {
        return static_cast<AudioClient*>(userData)->paStreamFinishedMethod();
    }
};

}