/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <portaudio.h>
#include "audio.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {
class Client {
public:

    class Renderer {
        public:
        virtual void renderCallback(const sam_t* in, sam_t* out, size_t nframes) = 0;
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


    void start(bool tRealtime = false) {
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

        PaStreamParameters iParams(iDevID, 2, paInt16, iDevInfo->defaultLowInputLatency, NULL);
        PaStreamParameters oParams(oDevID, 2, paInt16, oDevInfo->defaultLowOutputLatency, NULL);

        PaStreamCallback* streamCallback = tRealtime ? &Client::paCallback : NULL;

        auto res = Pa_OpenStream(&mStream, &iParams, &oParams, mSampleRate, mBufferSize, paNoFlag, streamCallback, this);
        if (res != paNoError) {
            throw std::runtime_error("AudioClient Pa_OpenStream failed with error "+std::to_string(res)+" ("+Pa_GetErrorText(res)+")");
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

    bool readyToRender(size_t nframes) {
        if (!mStream) return false;
        auto availableIn  = Pa_GetStreamReadAvailable(mStream);
        auto availableOut = Pa_GetStreamWriteAvailable(mStream);
        return availableIn >= nframes && availableOut >= nframes;
    }

    void render(sam_t* in, const sam_t* out, size_t nframes) {
        Pa_ReadStream(mStream, in, nframes);
        Pa_WriteStream(mStream, out, nframes);
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
        if (mRenderer) mRenderer->renderCallback(static_cast<const sam_t*>(inputBuffer), static_cast<sam_t*>(outputBuffer), framesPerBuffer);
        return paContinue;
    }

    void paStreamFinishedMethod() {
        log.info() << "AudioClient stream finished";
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