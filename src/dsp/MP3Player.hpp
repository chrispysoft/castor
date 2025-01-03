#pragma once

#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "AudioProcessor.hpp"
#include "AudioCodecReader.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"

namespace cst {
namespace audio {
class MP3Player : public Player {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 65536 * 64;

    const double mSampleRate;
    std::atomic<size_t> mReadPos = 0;
    std::atomic<size_t> mSampleCount = 0;
    std::string mCurrURL = "";
    double mDuration;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mLoading = false;
    std::unique_ptr<AudioCodecReader> mReader = nullptr;
    util::RingBuffer<float> mRingBuffer;
    

public:
    MP3Player(double tSampleRate, const std::string tName = "") : Player(tName),
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {

    }
    
    ~MP3Player() {
        if (state != IDLE) stop();
    }
    
    std::string currentURL() {
        return mCurrURL;
    }

    bool canPlay(const PlayItem& item) override {
        return item.uri.starts_with("http") || item.uri.starts_with("/") || item.uri.starts_with("./");
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "MP3Player load " << tURL << " position " << seek;
        // eject();
        state = LOAD;
        mLoading = true;
        try {
            if (mReader) mReader->cancel();
            mReader = std::make_unique<AudioCodecReader>(mSampleRate, tURL, seek);
            mSampleCount = mReader->sampleCount();
            std::thread([this] {
                this->mReader->read(this->mRingBuffer);
                this->mReader = nullptr;
            }).detach();
            state = CUE;
            mLoading = false;
            mCondition.notify_one();
        }
        catch (const std::runtime_error& e) {
            eject();
            mLoading = false;
            mCondition.notify_one();
            throw e;
        }
    }

    void stop() override {
        eject();
    }

    void eject() {
        log.debug() << "MP3Player eject...";
        state = IDLE;
        if (mReader) mReader->cancel();
        mReader = nullptr;
        //std::lock_guard lock(mMutex);
        mReadPos = 0;
        mCurrURL = "";
        mRingBuffer.flush();
        log.info() << "MP3Player ejected";
    }


    bool isIdle() {
        // log.debug() << mReadPos << " " << mSampleCount;
        return state == IDLE || mReadPos >= mSampleCount;
    }

    
    void process(const float*, float* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        if (sampleCount <= mRingBuffer.size()) {
            auto samplesRead = mRingBuffer.read(tBuffer, sampleCount);
            mReadPos += samplesRead;
            if (samplesRead == sampleCount) {
                // log.debug() << "read " << samplesRead << " from ringbuffer";
            } else {
                // log.debug() << "0 bytes read"; 
                memset(tBuffer, 0, sampleCount * sizeof(float));
            }
        } else {
            memset(tBuffer, 0, sampleCount * sizeof(float));
        }
    }
};
}
}