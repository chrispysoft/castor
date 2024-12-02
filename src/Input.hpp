#pragma once

#include <vector>
#include <string>
#include <atomic>
#include "Controller.hpp"
#include "AudioSource.hpp"
#include "MP3Player.hpp"
#include "StreamPlayer.hpp"
#include "util.hpp"

namespace lap {
class Input {
    const std::string mNamespace;
    AudioSource& mSource;
    std::atomic<bool> mSelected;
    std::atomic<bool> mReady;
    std::atomic<float> mVolume;

public:

    Input(const std::string tNamespace, AudioSource& tSource) :
        mNamespace(tNamespace),
        mSource(tSource)
    {

    }

    void registerControlCommands(Controller* tController) {
        tController->registerCommand(mNamespace, "push", [&](auto args, auto callback) {
            const auto url = util::extractUrl(args);
            this->push(url);
            callback("OK");
        });

        tController->registerCommand(mNamespace, "url", [&](auto args, auto callback) {
            this->push(args);
            callback("OK");
        });

        tController->registerCommand(mNamespace, "roll", [this](auto args, auto callback) {
            auto pos = std::stod(args);
            this->roll(pos);
            callback("OK");
        });

        tController->registerCommand(mNamespace, "clear", [this](auto, auto callback) {
            this->clear();
            callback("OK");
        });

        tController->registerCommand(mNamespace, "stop", [this](auto, auto callback) {
            this->clear();
            callback("OK");
        });

        tController->registerCommand(mNamespace, "start", [this](auto, auto callback) {
            this->mReady = true;
            callback("OK");
        });

        tController->registerCommand(mNamespace, "status", [this](auto, auto callback) {
            callback("connected");
        });

        tController->registerCommand(mNamespace, "set_track_metadata", [this](auto, auto callback) {
            callback("OK");
        });
    }

    std::string getNamespace() {
        return mNamespace;
    }

    bool getSelected() {
        return mSelected;
    }

    void setSelected(bool tSelected) {
        mSelected = tSelected;
    }

    float getVolume() {
        return mVolume;
    }

    void setVolume(float tVolume) {
        mVolume = tVolume;
    }

    void push(const std::string& tURL) {
        mSelected = true;
        mVolume = 1;
        mSource.open(tURL);
    }

    void roll(double tPos) {
        mSource.roll(tPos);
    }

    void clear() {
        mSource.clear();
    }

    std::string getStatusString() {
        auto readyStr = util::boolstr(mReady);
        auto selStr = util::boolstr(mSelected);
        auto volStr = std::to_string(static_cast<int>(getVolume() * 100));
        return "ready="+readyStr+" selected="+selStr+" single=false volume="+volStr+"% remaining=inf";
    }

    void process(const float* in, float* out, size_t nframes) const {
        mSource.process(in, out, nframes);
    }
};
}