#include <atomic>
#include <vector>
#include <iostream>
#include <fstream>
#include <thread>
#include <csignal>
#include <cassert>
#include <cstring>
#include <cmath>
#include <portaudio.h>
#include "SinOsc.hpp"
#include "WAVPlayer.hpp"
#include "MP3Player.hpp"

namespace lap {
class Engine {

    static constexpr double kDefaultSampleRate = 44100;
    static constexpr size_t kDefaultBufferSize = 512;

    double mSampleRate;
    size_t mBufferSize;

    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning;

    PaStream *mStream;
    SinOsc mOscL;
    SinOsc mOscR;
    MP3Player mPlayer;

public:
    Engine(double tSampleRate = kDefaultSampleRate, size_t tBufferSize = kDefaultBufferSize) :
        mSampleRate(tSampleRate),
        mBufferSize(tBufferSize),
        mStream(nullptr),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mPlayer("../audio/Alternate Gate 6 Master.mp3", mSampleRate)
    {
        mOscL.setFrequency(440);
        mOscR.setFrequency(525);
        Pa_Initialize();

        //std::signal(SIGINT,  handlesig);
        //std::signal(SIGTERM, handlesig);
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

        auto res = Pa_OpenStream(&mStream, &inputParameters, &outputParameters, mSampleRate, mBufferSize, paNoFlag, &Engine::paCallback, this);

        if (res != paNoError) {
            return false;
        }

        res = Pa_SetStreamFinishedCallback(mStream, &Engine::paStreamFinished);

        if (res != paNoError) {
            Pa_CloseStream(mStream);
            mStream = nullptr;
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


private:

    int paCallbackMethod(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags) {

        (void) timeInfo;
        (void) statusFlags;

        float *in = (float*)inputBuffer;
        float *out = (float*)outputBuffer;

        mPlayer.read(out, framesPerBuffer);

        // for (auto i = 0; i < framesPerBuffer; ++i) {
        //     if (true) {
        //         float sL = mOscL.process();
        //         float sR = mOscR.process();
        //         //out[i*2] = sL;
        //         //out[i*2+1] = sR;

        //         mPlayer.read(out, 1);

        //     }
        //     else {
        //         *in++;
        //         *in++;
        //         *out++ = *in++;
        //         *out++ = *in++;
        //         *out++ = 0;
        //         *out++ = 0;
        //     }
        // }

        return paContinue;

    }

    void paStreamFinishedMethod() {
        std::cout << "Stream finished" << std::endl;
    }

    static int paCallback(const void* inputBuffer, void* outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void* userData) {
        return static_cast<Engine*>(userData)->paCallbackMethod(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }

    static void paStreamFinished(void* userData) {
        return static_cast<Engine*>(userData)->paStreamFinishedMethod();
    }

    
public:

    static Engine& instance() {
        static Engine instance;
        return instance;
    }

    static void handlesig(int sig) {
        std::cout << "Received signal " << sig << std::endl;
        instance().stop();
    }

    void run() {
        mRunning = true;
        test();
        // worker = std::make_unique<std::thread>([this] {
        // while (this->mRunning)
        //   this->work();
        // });
        // worker->join();
    }

    // void stop() {
    //     std::cout << "STOPPING..." << std::endl;
    //     mRunning = false;
    //     std::cout << "STOPPED" << std::endl;
    // }


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


    int test() {
        using namespace std;

        static const string deviceName = "Soundcraft Signature 12 MTK: USB Audio (hw:2,0)";
        auto deviceID = getDeviceID(deviceName);
        if (deviceID == -1) deviceID = Pa_GetDefaultOutputDevice();
        std::cout << "Using deviceID " << deviceID << std::endl;
        
        if (open(deviceID, deviceID)) {
            if (start()) {
                static constexpr double sleepSec = 1;
                static constexpr unsigned int sleepTime = sleepSec * 1000;
                while (mRunning) {
                    Pa_Sleep(sleepTime);
                }
                stop();
            }
            close();
        }

        return paNoError;
    }
};

}