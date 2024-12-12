#pragma once

#include <vector>
#include <string>
#include "Input.hpp"
#include "Controller.hpp"
#include "QueuePlayer.hpp"
#include "StreamPlayer.hpp"
#include "LinePlayer.hpp"
#include "APIClient.hpp"

namespace lap {
class Mixer {

    static constexpr size_t kChannelCount = 2;
    const std::string mNamespace = "mixer";
    const std::vector<std::string> kSourceNames = { "in_queue_0", "in_queue_1", "in_stream_0", "in_stream_1", "aura_engine_line_in_0" };
    
public:

    const double mSampleRate;
    std::vector<Input*> mInputs;
    std::vector<float> mSumBuf;

    QueuePlayer mQueuePlayer1;
    QueuePlayer mQueuePlayer2;
    StreamPlayer mStreamPlayer1;
    StreamPlayer mStreamPlayer2;
    LinePlayer mLinePlayer1;

    QueueInput mQueueInput1;
    QueueInput mQueueInput2;
    StreamInput mStreamInput1;
    StreamInput mStreamInput2;
    LineInput mLineInput1;

    Mixer(double tSampleRate, size_t tBufferSize) :
        mSampleRate(tSampleRate),
        mSumBuf(tBufferSize * kChannelCount),
        mQueuePlayer1(mSampleRate),
        mQueuePlayer2(mSampleRate),
        mStreamPlayer1(mSampleRate),
        mStreamPlayer2(mSampleRate),
        mLinePlayer1(mSampleRate),
        mQueueInput1(kSourceNames[0], mQueuePlayer1),
        mQueueInput2(kSourceNames[1], mQueuePlayer2),
        mStreamInput1(kSourceNames[2], mStreamPlayer1),
        mStreamInput2(kSourceNames[3], mStreamPlayer2),
        mLineInput1(kSourceNames[4], mLinePlayer1),
        mInputs { &mQueueInput1, &mQueueInput2, &mStreamInput1, &mStreamInput2, &mLineInput1 }
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
        tController->registerCommand(mNamespace, "inputs", [this](auto args, auto callback) {
            const auto inputs = this->getInputs();
            callback(inputs);
        });

        tController->registerCommand(mNamespace, "outputs", [this](auto args, auto callback) {
            const auto outputs = this->getOutputs();
            callback(outputs);
        });

        tController->registerCommand(mNamespace, "select", [this](auto args, auto callback) {
            auto [chns, sels] = util::splitBy(args, ' ');
            auto chn = std::stoul(chns);
            auto sel = util::strbool(sels);
            this->mInputs[chn]->setSelected(sel);
            auto status = this->mInputs[chn]->getStatusString();
            callback(status);
        });

        tController->registerCommand(mNamespace, "volume", [this](auto args, auto callback) {
            auto [chns, vols] = util::splitBy(args, ' ');
            auto chn = std::stoul(chns);
            auto vol = std::stof(vols);
            this->mInputs[chn]->setVolume(vol);
            auto status = this->mInputs[chn]->getStatusString();
            callback(status);
        });

        tController->registerCommand(mNamespace, "status", [this](auto args, auto callback) {
            auto [chns, nil] = util::splitBy(args, ' ');
            auto chn = std::stoul(chns);
            auto status = this->mInputs[chn]->getStatusString();
            // std::cout << status << std::endl;
            callback(status);
        });

        for (auto input : mInputs) {
            input->registerControlCommands(tController);
        }
    }

    void setAPIClient(APIClient* tAPIClient) {
        tAPIClient->postPlaylog("");
    }


    std::string getInputs() {
        std::string res = "";
        for (const auto& input : mInputs) {
            res += input->getNamespace();
            if (input != mInputs.back()) res += " ";
        }
        return res;
    }

    std::string getOutputs() {
        return "{ \"stream\": [], \"line\": [ \"aura_engine_line_out_0\" ] }";
    }
};
}