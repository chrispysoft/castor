#pragma once

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include "AudioProcessor.hpp"
#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "../common/Log.hpp"

namespace cst {
class MP3Player : public AudioProcessor {

    static constexpr size_t kChannelCount = 2;

    const double mSampleRate;
    std::atomic<size_t> mReadPos = 0;
    std::vector<float> mSamples;
    std::string mCurrURL = "";
    double mDuration;
    std::mutex mMutex;
    std::condition_variable mCondition;
    std::atomic<bool> mLoading = false;
    

public:
    MP3Player(double tSampleRate) :
        mSampleRate(tSampleRate),
        mReadPos(0),
        mSamples(0)
    {
        
    }

    ~MP3Player() override {
        
    }
    
    std::string currentURL() {
        return mCurrURL;
    }

    void load(const std::string& tURL, double seek = 0) {
        log.info() << "MP3Player loading... " << tURL;
        eject();
        mLoading = true;
        try {
            _load(tURL, seek);
            mLoading = false;
            mCondition.notify_one();
        }
        catch (const std::runtime_error& e) {
            mLoading = false;
            mCondition.notify_one();
            throw e;
        }
    }

    void _load(const std::string& tURL) {
        // open input file
        AVFormatContext* formatCtx = nullptr;
        if (avformat_open_input(&formatCtx, tURL.c_str(), nullptr, nullptr) < 0) {
            throw std::runtime_error("Could not open input file.");
        }

        // find the best stream
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not find stream information.");
        }

        int streamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (streamIndex < 0) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not find audio stream.");
        }

        AVStream* audioStream = formatCtx->streams[streamIndex];
        AVCodecParameters* codecParams = audioStream->codecpar;

        // find decoder
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Unsupported codec.");
        }

        // allocate codec context
        AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not allocate codec context.");
        }

        if (avcodec_parameters_to_context(codecCtx, codecParams) < 0) {
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not fill codec context.");
        }

        // open codec
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not open codec.");
        }

        // convert
        SwrContext* swrCtx = swr_alloc();
        if (!swrCtx) {
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not allocate resampler context.");
        }

        char inChLayoutDesc[128];
        int sts = av_channel_layout_describe(&codecCtx->ch_layout, inChLayoutDesc, sizeof(inChLayoutDesc));
        if (sts < 0) {
            swr_free(&swrCtx);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not load input channel layout description");
        }

        av_opt_set(swrCtx, "in_chlayout", inChLayoutDesc, 0);
        av_opt_set(swrCtx, "out_chlayout", "stereo", 0);
        //av_opt_set_int(swrCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        av_opt_set_int(swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", mSampleRate, 0);
        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        if (swr_init(swrCtx) < 0) {
            swr_free(&swrCtx);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not initialize resampler.");
        }

        // prepare to read packets and decode
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        if (!packet || !frame) {
            av_packet_free(&packet);
            av_frame_free(&frame);
            swr_free(&swrCtx);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not allocate packet or frame.");
        }

        if (seek > 0) {
            auto ts = seek * AV_TIME_BASE;
            av_seek_frame(formatCtx, -1, ts, 0);
        }

        while (av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == streamIndex) {
                if (avcodec_send_packet(codecCtx, packet) >= 0) {
                    while (avcodec_receive_frame(codecCtx, frame) >= 0) {
                        // Resample frame data to float format
                        int nbChannels = codecCtx->ch_layout.nb_channels; //av_get_channel_layout_nb_channels(codecCtx->channel_layout);
                        int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                        std::vector<float> resampledData(outSamples * nbChannels);

                        uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(resampledData.data()) };
                        int convertedSamples = swr_convert(
                            swrCtx,
                            outData,                // Output buffer
                            outSamples,             // Max output samples
                            (const uint8_t**)frame->data, // Input buffer
                            frame->nb_samples       // Number of input samples
                        );

                        if (convertedSamples < 0) {
                            av_packet_unref(packet);
                            av_frame_unref(frame);
                            throw std::runtime_error("Error during resampling.");
                        }
                        
                        mSamples.insert(mSamples.end(), resampledData.begin(), resampledData.begin() + convertedSamples * nbChannels);
                    }
                }
            }
            av_packet_unref(packet);
        }

        // Clean up
        av_packet_free(&packet);
        av_frame_free(&frame);
        swr_free(&swrCtx);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);

        mDuration = mSamples.size() / kChannelCount / mSampleRate;
        mCurrURL = tURL;
        log.debug() << "MP3Player loaded " << mSamples.size() << " samples" << " with duration " << mDuration;
    }

    void eject() {
        std::lock_guard lock(mMutex);
        mSamples.clear();
        mReadPos = 0;
        mCurrURL = "";
        // log.info() << "MP3Player ejected";
    }

    void roll(double pos) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCondition.wait(lock, [this] { return !this->mLoading; });
        log.debug() << "MP3Player rolling to " << pos;
        size_t idx = round(pos * mSampleRate * kChannelCount);
        if (idx < mSamples.size()) {
            mReadPos = idx;
        } else {
            log.error() << "MP3Player roll position exceeds duration";
        }
    }

    bool isIdle() {
        return mSamples.size() == 0 || mReadPos >= mSamples.size();
    }

    
    void process(const float*, float* tBuffer, size_t tFrameCount) override {
        auto sampleCount = tFrameCount * kChannelCount;
        auto byteSize = sampleCount * sizeof(float);
        
        if (!mLoading && mReadPos < mSamples.size()) {
            auto ncopyable = std::min(sampleCount, mSamples.size() - mReadPos);
            memcpy(tBuffer, mSamples.data() + mReadPos, ncopyable * sizeof(float));
            mReadPos += sampleCount;
            if (ncopyable < sampleCount) {
                memset(tBuffer+ncopyable, 0, (sampleCount-ncopyable) * sizeof(float));
            }
        } else {
            memset(tBuffer, 0, byteSize);
        }
    }
};
}
