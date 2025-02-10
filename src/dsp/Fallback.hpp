#pragma once

#include <filesystem>
#include <thread>
#include <future>
#include "SineOscillator.hpp"
#include "QueuePlayer.hpp"
#include "../util/Log.hpp"

namespace cst {
namespace audio {

class BufletController {
    public:

    struct Buflet {
        const double mSampleRate;
        const std::string mURL;
        std::unique_ptr<CodecReader> mReader;
        const double mDuration;
        util::RingBuffer<sam_t>& mBuffer;

        enum State { IDLE, LOADING, DONE } state = IDLE;

        Buflet(double tSampleRate, const std::string& tURL, util::RingBuffer<sam_t>& tBuffer) :
            mSampleRate(tSampleRate),
            mURL(tURL),
            mReader(std::make_unique<CodecReader>(mSampleRate, mURL)),
            mDuration(mReader->duration()),
            mBuffer(tBuffer)
        {}

        void load() {
            state = LOADING;
            mReader->read(mBuffer);
            mReader = nullptr;
            state = DONE;
        }
    };

    static constexpr double kMaxDuration = 1800;
    const double mSampleRate;
    util::RingBuffer<sam_t> mBuffer;
    std::deque<std::unique_ptr<Buflet>> mQueueItems = {};
    std::deque<std::future<void>> mFuts = {};
    double mDuration = 0;

    BufletController(double tSampleRate) :
        mSampleRate(tSampleRate),
        mBuffer(mSampleRate * 2 * kMaxDuration)
    {}

    void load(const std::string& tURL) {
        log.info(Log::Yellow) << "Fallback loading queue...";
        for (const auto& entry : std::filesystem::directory_iterator(tURL)) {
            if (entry.is_regular_file()) {
                const auto file = entry.path().string();
                try {
                    auto buflet = std::make_unique<Buflet>(mSampleRate, file, mBuffer);
                    auto duration = buflet->mDuration;
                    auto total = mDuration + duration;
                    if (total > kMaxDuration) break;
                    buflet->load();
                    mDuration = total;
                    mQueueItems.push_back(std::move(buflet));
                }
                catch (const std::exception& e) {
                    log.error() << "Fallback failed to load '" << file << "': " << e.what();
                }
            }
        }
        log.info(Log::Yellow) << "Fallback load queue done size: " << mQueueItems.size();
    }


    void loadAsync(const std::string& tURL) {
        mFuts.push_back(std::async(std::launch::async, &BufletController::load, this, tURL));
    }

    size_t read(sam_t* out, size_t nsamples) {
        return mBuffer.read(out, nsamples);
    }
};


class Fallback : public Input {
    static constexpr double kGain = 1 / 128.0;
    static constexpr double kBaseFreq = 1000;

    const double mSampleRate;
    SineOscillator mOscL;
    SineOscillator mOscR;
    BufletController mBufletController;
    std::string mFallbackURL;
    bool mActive;

public:
    Fallback(double tSampleRate, const std::string tFallbackURL) : Input(),
        mSampleRate(tSampleRate),
        mOscL(mSampleRate),
        mOscR(mSampleRate),
        mBufletController(mSampleRate),
        mFallbackURL(tFallbackURL)
    {
        mOscL.setFrequency(kBaseFreq);
        mOscR.setFrequency(kBaseFreq * (5.0 / 4.0));
        std::thread([this, tFallbackURL] {
            try {
                mBufletController.load(tFallbackURL);
            }
            catch (const std::exception& e) {
                log.error() << "Fallback failed to load queue: " << e.what();
            }
        }).detach();
    }

    void start() {
        if (mActive) return;
        log.info(Log::Yellow) << "Fallback start";
        mBufletController.mBuffer.resetHead();
        mActive = true;
    }

    void stop() {
        if (!mActive) return;
        log.info(Log::Yellow) << "Fallback stop";
        mActive = false;
    }

    bool isActive() {
        return mActive;
    }

    void process(const sam_t* in, sam_t* out, size_t nframes) {
        auto nsamples = nframes * 2;
        auto nread = mBufletController.read(out, nsamples);

        if (nread < nsamples) {
            for (auto i = 0; i < nframes; ++i) {
                out[i*2]   += mOscL.process() * kGain;
                out[i*2+1] += mOscR.process() * kGain;
            }
        }
    }
};
}
}