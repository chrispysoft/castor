#pragma once

#include <vector>
#include <string>
#include <atomic>
#include "Controller.hpp"
#include "AudioProcessor.hpp"
#include "QueuePlayer.hpp"
#include "StreamPlayer.hpp"
#include "LinePlayer.hpp"
#include "util.hpp"

namespace lap {
class Input {
protected:
    const std::string mNamespace;
    AudioProcessor& mSource;
    std::atomic<bool> mSelected;
    std::atomic<bool> mReady;
    std::atomic<float> mVolume;

public:

    Input(const std::string tNamespace, AudioProcessor& tSource) :
        mNamespace(tNamespace),
        mSource(tSource)
    {

    }

    virtual void registerControlCommands(Controller* tController) = 0;

    std::string getNamespace() {
        return mNamespace;
    }

    bool getSelected() {
        return mSelected;
    }

    void setSelected(bool tSelected) {
        mSelected = tSelected;
        mVolume = mSelected ? 1 : 0;
    }

    float getVolume() {
        return mVolume;
    }

    void setVolume(float tVolume) {
        mVolume = tVolume;
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

class QueueInput : public Input {
    QueuePlayer& mQueuePlayer;
public:

    QueueInput(const std::string tNamespace, QueuePlayer& tSource) :
        Input(tNamespace, tSource),
        mQueuePlayer(tSource)
    {}

    void registerControlCommands(Controller* tController) override {
        tController->registerCommand(mNamespace, "push", [this](auto args, auto callback) {
            const auto url = util::extractUrl(args);
            this->mQueuePlayer.push(url);
            callback("OK");
        });

        tController->registerCommand(mNamespace, "roll", [this](auto args, auto callback) {
            auto pos = std::stod(args);
            this->mQueuePlayer.roll(pos);
            callback("OK");
        });

        tController->registerCommand(mNamespace, "clear", [this](auto, auto callback) {
            this->mQueuePlayer.clear();
            callback("OK");
        });

        tController->registerCommand(mNamespace, "status", [this](auto, auto callback) {
            callback("OK");
        });
    }
};

class StreamInput : public Input {
    StreamPlayer& mStreamPlayer;
public:

    StreamInput(const std::string tNamespace, StreamPlayer& tSource) :
        Input(tNamespace, tSource),
        mStreamPlayer(tSource)
    {}

    void registerControlCommands(Controller* tController) override {
        
        tController->registerCommand(mNamespace, "url", [this](auto url, auto callback) {
            this->mStreamPlayer.open(url);
            callback("OK " + url);
        });

        tController->registerCommand(mNamespace, "start", [this](auto, auto callback) {
            this->mReady = true;
            this->setSelected(true);
            callback("connected");
        });
        
        tController->registerCommand(mNamespace, "stop", [this](auto, auto callback) {
            this->mReady = false;
            callback("OK");
        });

        tController->registerCommand(mNamespace, "status", [this](auto, auto callback) {
            callback("connected");
            //auto status = this->getStatusString();
            //callback(status);
        });
    }
};

class LineInput : public Input {
    LinePlayer& mLinePlayer;
public:

    LineInput(const std::string tNamespace, LinePlayer& tSource) :
        Input(tNamespace, tSource),
        mLinePlayer(tSource)
    {}

    void registerControlCommands(Controller* tController) override {
        tController->registerCommand(mNamespace, "set_track_metadata", [this](auto, auto callback) {
            callback("OK");
        });
    }
};
}