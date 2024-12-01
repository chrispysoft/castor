#pragma once

#include <vector>
#include <string>
#include "Input.hpp"
#include "Controller.hpp"
#include "MP3Player.hpp"
#include "StreamPlayer.hpp"

namespace lap {
class Mixer {
    const std::string mNamespace = "mixer";
    const std::vector<std::string> kSourceNames = { "in_queue_0", "in_queue_1", "in_stream_0", "in_stream_1", "aura_engine_line_in_0" };
    
public:

    const double mSampleRate;
    std::vector<Input*> mInputs;
    std::vector<float> mTmpBuffer;

    MP3Player mMP3Player1;
    MP3Player mMP3Player2;
    StreamPlayer mStreamPlayer1;
    StreamPlayer mStreamPlayer2;

    Input mMP3Input1;
    Input mMP3Input2;
    Input mStreamInput1;
    Input mStreamInput2;

    Mixer(double tSampleRate, size_t tBufferSize) :
        mSampleRate(tSampleRate),
        mTmpBuffer(tBufferSize),
        mMP3Player1(mSampleRate),
        mMP3Player2(mSampleRate),
        mStreamPlayer1(mSampleRate),
        mStreamPlayer2(mSampleRate),
        mMP3Input1(kSourceNames[0], mMP3Player1),
        mMP3Input2(kSourceNames[1], mMP3Player2),
        mStreamInput1(kSourceNames[2], mStreamPlayer1),
        mStreamInput2(kSourceNames[3], mStreamPlayer2),
        mInputs {&mMP3Input1, &mMP3Input2, &mStreamInput1, &mStreamInput2}
    {

    }

    void process(const float* in, float* out, size_t nframes) {
        memset(out, 0, nframes * sizeof(float) * 2);

        for (Input* input : mInputs) {
            if (input->selected()) {
                input->process(in, out, nframes);
                
                for (auto i = 0; i < mTmpBuffer.size(); ++i) {
                    out[i] += mTmpBuffer[i] * input->volume();
                }
            }
        }
    }

    void registerControlCommands(Controller* tController) {
        tController->registerCommand(mNamespace, "inputs", [&](auto args, auto callback) {
            const auto inputs = this->getInputs();
            callback(inputs);
        });

        tController->registerCommand(mNamespace, "outputs", [&](auto args, auto callback) {
            const auto outputs = this->getOutputs();
            callback(outputs);
        });

        tController->registerCommand(mNamespace, "select", [&](auto args, auto callback) {
            callback("OK");
        });

        tController->registerCommand(mNamespace, "volume", [&](auto args, auto callback) {
            callback("ready=false selected=false single=false volume=100% remaining=inf");
        });

        tController->registerCommand(mNamespace, "status", [&](auto args, auto callback) {
            callback("ready=false selected=false single=false volume=100% remaining=inf");
        });

        for (auto input : mInputs) {
            input->registerControlCommands(tController);
        }
    }


    std::string getInputs() {
        return "in_queue_0.2 in_queue_1.2 in_stream_0.2 in_stream_1.2 aura_engine_line_in_0";
    }

    std::string getOutputs() {
        return "{ \"stream\": [], \"line\": [ \"aura_engine_line_out_0\" ] }";
    }
};
}