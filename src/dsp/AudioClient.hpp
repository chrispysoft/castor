#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <portaudio.h>
#include "../util/Log.hpp"

namespace cst {
namespace audio {
class Client {
public:

    class Renderer {
        public:
            virtual void renderCallback(const float* in, float* out, size_t nframes) = 0;
            virtual ~Renderer() = default;
        };
    
    const std::string mIDevName;
    const std::string mODevName;
    const double mSampleRate;
    const size_t mBufferSize;
    PaStream* mStream;
    Renderer* mRenderer;

    Client(const std::string& tIDevName, const std::string& tODevName, double tSampleRate, size_t tBufferSize) :
        mIDevName(tIDevName),
        mODevName(tODevName),
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mStream(nullptr),
        mRenderer(nullptr)
    {
        Pa_Initialize();
        printDeviceNames();
    }

    ~Client() {
        Pa_Terminate();
    }


    void start() {
        log.debug() << "AudioClient start";
        auto numDevices = Pa_GetDeviceCount();
        auto iDevID = paNoDevice;
        auto oDevID = paNoDevice;
        const PaDeviceInfo* info;
        for (auto i = 0; i < numDevices; ++i) {
            info = Pa_GetDeviceInfo(i);
            std::string name(info->name);
            if (iDevID == paNoDevice && info->maxInputChannels > 0 && name.starts_with(mIDevName)) {
                iDevID = i;
            }
            if (oDevID == paNoDevice && info->maxOutputChannels > 0 && name.starts_with(mODevName)) {
                oDevID = i;
            }
        }

        if (iDevID == paNoDevice) {
            log.warn() << "AudioClient input device '" << mIDevName << "' not found - using default";
            iDevID = Pa_GetDefaultInputDevice();
        }
        if (oDevID == paNoDevice) {
            log.warn() << "AudioClient output device '" << mODevName << "' not found - using default";
            oDevID = Pa_GetDefaultOutputDevice();
        }

        const PaDeviceInfo* iDevInfo = Pa_GetDeviceInfo(iDevID);
        const PaDeviceInfo* oDevInfo = Pa_GetDeviceInfo(oDevID);
        if (!iDevInfo) {
            throw std::runtime_error("AudioClient failed to get input device info");
        }
        if (!oDevInfo) {
            throw std::runtime_error("AudioClient failed to get output device info");
        }

        PaStreamParameters iParams(iDevID, 2, paFloat32, iDevInfo->defaultLowInputLatency, NULL);
        PaStreamParameters oParams(oDevID, 2, paFloat32, oDevInfo->defaultLowOutputLatency, NULL);
        auto res = Pa_OpenStream(&mStream, &iParams, &oParams, mSampleRate, mBufferSize, paNoFlag, &Client::paCallback, this);
        if (res != paNoError) {
            throw std::runtime_error("AudioClient Pa_OpenStream failed with error "+std::to_string(res));
        }

        res = Pa_SetStreamFinishedCallback(mStream, &Client::paStreamFinished);
        if (res != paNoError) {
            stop();
            throw std::runtime_error("AudioClient Pa_SetStreamFinishedCallback failed with error "+std::to_string(res));
        }

        res = Pa_StartStream(mStream);
        if (res != paNoError) {
            stop();
            throw std::runtime_error("AudioClient Pa_SetStreamFinishedCallback failed with error "+std::to_string(res));
        }

        log.info() << "AudioClient opened stream with device ids " << iDevID << "," << oDevID << " sample rate " << mSampleRate << ", buffer size " << mBufferSize;
    }

    void stop() {
        if (!mStream) return;
        auto res = Pa_StopStream(mStream);
        if (res != paNoError) {
            log.debug() << "AudioClient failed to stop stream";
        }
        res = Pa_CloseStream(mStream);
        if (res != paNoError) {
            log.debug() << "AudioClient failed to close stream";
        }
        mStream = nullptr;
    }

    void setRenderer(Renderer* tRenderer) {
        mRenderer = tRenderer;
    }

    void printDeviceNames() {
        auto numDevices = Pa_GetDeviceCount();
        log.info(Log::Magenta) << "AudioClient found " << numDevices << " devices:";
        const PaDeviceInfo* info;
        for (auto i = 0; i < numDevices; ++i) {
            info = Pa_GetDeviceInfo(i);
            log.info(Log::Magenta) << "#" << std::setfill(' ') << std::setw(2) << i << " " << std::setw(2) << info->maxInputChannels << " " << std::setw(2) << info->maxOutputChannels << " " << info->name;
        }
    }

private:

    int paCallbackMethod(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags) {
        if (mRenderer) mRenderer->renderCallback(static_cast<const float*>(inputBuffer), static_cast<float*>(outputBuffer), framesPerBuffer);
        return paContinue;
    }

    void paStreamFinishedMethod() {
        log.debug() << "AudioClient stream finished";
    }

    static int paCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return static_cast<Client*>(userData)->paCallbackMethod(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }

    static void paStreamFinished(void* userData) {
        return static_cast<Client*>(userData)->paStreamFinishedMethod();
    }
};

}
}