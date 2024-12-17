#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <portaudio.h>

namespace cst {

class AudioClientRenderer {
public:
    virtual void renderCallback(const float* in, float* out, size_t nframes) = 0;
    virtual ~AudioClientRenderer() = default;
};

class AudioClient {
    
    const std::string mIDevName;
    const std::string mODevName;
    const double mSampleRate;
    const size_t mBufferSize;
    PaStream* mStream;
    AudioClientRenderer* mRenderer;

public:

    AudioClient(const std::string& tIDevName, const std::string& tODevName, double tSampleRate, size_t tBufferSize) :
        mIDevName(tIDevName),
        mODevName(tODevName),
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


    void start() {
        auto iDevID = getDeviceID(mIDevName);
        if (iDevID == -1) {
            std::cout << "AudioClient in device '" << mIDevName << "' not found - using default" << std::endl;
            iDevID = Pa_GetDefaultOutputDevice();
        }
        auto oDevID = getDeviceID(mODevName);
        if (oDevID == -1) {
            std::cout << "AudioClient out device '" << mODevName << "' not found - using default" << std::endl;
            oDevID = Pa_GetDefaultOutputDevice();
        }
        std::cout << "AudioClient starting PortAudio stream with device ids " << iDevID << "," << oDevID << " sample rate " << mSampleRate << ", buffer size " << mBufferSize  << std::endl;
        
        if (openStream(iDevID, oDevID)) {
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
        if (idx >= names.size()) idx = -1;
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
        if (!pInfo) {
            return false;
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
        std::cout << "AudioClient Portaudio stream finished" << std::endl;
    }

    static int paCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return static_cast<AudioClient*>(userData)->paCallbackMethod(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }

    static void paStreamFinished(void* userData) {
        return static_cast<AudioClient*>(userData)->paStreamFinishedMethod();
    }
};

}