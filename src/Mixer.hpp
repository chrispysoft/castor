#pragma once

#include <vector>
#include <string>
#include "Input.hpp"
#include "Controller.hpp"
#include "MP3Player.hpp"
#include "StreamPlayer.hpp"
#include "LinePlayer.hpp"

namespace lap {
class Mixer {

    static constexpr size_t kChannelCount = 2;
    const std::string mNamespace = "mixer";
    const std::vector<std::string> kSourceNames = { "in_queue_0", "in_queue_1", "in_stream_0", "in_stream_1", "aura_engine_line_in_0" };
    
public:

    const double mSampleRate;
    std::vector<Input*> mInputs;
    std::vector<float> mSumBuf;

    MP3Player mMP3Player1;
    MP3Player mMP3Player2;
    StreamPlayer mStreamPlayer1;
    StreamPlayer mStreamPlayer2;
    LinePlayer mLinePlayer1;

    Input mMP3Input1;
    Input mMP3Input2;
    Input mStreamInput1;
    Input mStreamInput2;
    Input mLineInput1;

    Mixer(double tSampleRate, size_t tBufferSize) :
        mSampleRate(tSampleRate),
        mSumBuf(tBufferSize * kChannelCount),
        mMP3Player1(mSampleRate),
        mMP3Player2(mSampleRate),
        mStreamPlayer1(mSampleRate),
        mStreamPlayer2(mSampleRate),
        mLinePlayer1(mSampleRate),
        mMP3Input1(kSourceNames[0], mMP3Player1),
        mMP3Input2(kSourceNames[1], mMP3Player2),
        mStreamInput1(kSourceNames[2], mStreamPlayer1),
        mStreamInput2(kSourceNames[3], mStreamPlayer2),
        mLineInput1(kSourceNames[4], mLinePlayer1),
        mInputs { &mMP3Input1, &mMP3Input2, &mStreamInput1, &mStreamInput2, &mLineInput1 }
    {

    }

    void process(const float* in, float* out, size_t nframes) {
        memset(out, 0, nframes * kChannelCount * sizeof(float));

        for (Input* input : mInputs) {
            if (input->getSelected()) {
                input->process(in, mSumBuf.data(), nframes);
                auto vol = input->getVolume();
                for (auto i = 0; i < mSumBuf.size(); ++i) {
                    out[i] += mSumBuf[i] * vol;
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
            auto [chns, sels] = util::splitBy(args, ' ');
            auto chn = std::stoul(chns);
            auto sel = util::strbool(sels);
            mInputs[chn]->setSelected(sel);
            auto status = mInputs[chn]->getStatusString();
            callback(status);
        });

        tController->registerCommand(mNamespace, "volume", [&](auto args, auto callback) {
            auto [chns, vols] = util::splitBy(args, ' ');
            auto chn = std::stoul(chns);
            auto vol = std::stof(vols);
            mInputs[chn]->setVolume(vol);
            auto status = mInputs[chn]->getStatusString();
            callback(status);
        });

        tController->registerCommand(mNamespace, "status", [&](auto args, auto callback) {
            auto [chns, nil] = util::splitBy(args, ' ');
            auto chn = std::stoul(chns);
            auto status = mInputs[chn]->getStatusString();
            // std::cout << status << std::endl;
            callback(status);
        });

        for (auto input : mInputs) {
            input->registerControlCommands(tController);
        }
    }


    std::string getInputs() {
        std::string res = "";
        for (const auto& input : mInputs) {
            res += input->getNamespace();
            if (input != mInputs.back()) res += " ";
        }
        return res;
        //return "in_queue_0.2 in_queue_1.2 in_stream_0.2 in_stream_1.2 aura_engine_line_in_0";
    }

    std::string getOutputs() {
        return "{ \"stream\": [], \"line\": [ \"aura_engine_line_out_0\" ] }";
    }
};
}