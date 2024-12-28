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
#include "../util/Log.hpp"

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
    std::atomic<bool> mCancelled = false;
    

public:
    MP3Player(double tSampleRate) :
        mSampleRate(tSampleRate),
        mReadPos(0),
        mSamples(0)
    {
        av_log_set_level(AV_LOG_ERROR);
    }
    
    ~MP3Player() override {
        if (state != IDLE) stop();
    }
    
    std::string currentURL() {
        return mCurrURL;
    }

    bool canPlay(const PlayItem& item) override {
        return item.uri.starts_with("/") || item.uri.starts_with("./");
    }

    void load(const std::string& tURL, double seek = 0) override {
        log.info() << "MP3Player load " << tURL << " position " << seek;
        eject();
        state = LOAD;
        mLoading = true;
        try {
            _load(tURL, seek);
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

    void _load(const std::string& tURL, double seek = 0) {
        mCancelled = false;
        AVDictionary *options = NULL;
        av_dict_set(&options, "timeout", "5000000", 0); // 5 seconds
        av_dict_set(&options, "buffer_size", "65536", 0); // 64KB buffer
        av_dict_set(&options, "reconnect", "1", 0); // Enable reconnection
        av_dict_set(&options, "reconnect_at_eof", "1", 0); // Reconnect at EOF
        //av_dict_set(&options, "reconnect_streamed", "1", 0); // For live streams
        av_dict_set(&options, "reconnect_delay_max", "2", 0); // Max delay 2s
        av_dict_set(&options, "fflags", "+discardcorrupt+genpts", 0);

        // open input file
        log.debug() << "MP3Player open file...";
        AVFormatContext* formatCtx = nullptr;
        if (avformat_open_input(&formatCtx, tURL.c_str(), nullptr, &options) < 0) {
            throw std::runtime_error("Could not open input file.");
        }

        // find the best stream
        log.debug() << "MP3Player find stream info...";
        if (avformat_find_stream_info(formatCtx, nullptr) < 0) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not find stream information.");
        }

        log.debug() << "MP3Player find best stream...";
        int streamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (streamIndex < 0) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not find audio stream.");
        }

        AVStream* audioStream = formatCtx->streams[streamIndex];
        AVCodecParameters* codecParams = audioStream->codecpar;

        // find decoder
        log.debug() << "MP3Player find decoder...";
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Unsupported codec.");
        }

        // allocate codec context
        log.debug() << "MP3Player alloc codec context...";
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
        log.debug() << "MP3Player open codec...";
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not open codec.");
        }

        // convert
        log.debug() << "MP3Player alloc resampler...";
        SwrContext* swrCtx = swr_alloc();
        if (!swrCtx) {
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not allocate resampler context.");
        }

        log.debug() << "MP3Player get channel layout...";
        char inChLayoutDesc[128];
        int sts = av_channel_layout_describe(&codecCtx->ch_layout, inChLayoutDesc, sizeof(inChLayoutDesc));
        if (sts < 0) {
            swr_free(&swrCtx);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not load input channel layout description");
        }

        log.debug() << "MP3Player set av options...";
        av_opt_set(swrCtx, "in_chlayout", inChLayoutDesc, 0);
        av_opt_set(swrCtx, "out_chlayout", "stereo", 0);
        av_opt_set_int(swrCtx, "in_sample_rate", codecCtx->sample_rate, 0);
        av_opt_set_int(swrCtx, "out_sample_rate", mSampleRate, 0);
        av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", codecCtx->sample_fmt, 0);
        av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        log.debug() << "MP3Player init resampler...";
        if (swr_init(swrCtx) < 0) {
            swr_free(&swrCtx);
            avcodec_free_context(&codecCtx);
            avformat_close_input(&formatCtx);
            throw std::runtime_error("Could not init resampler.");
        }

        // prepare to read packets and decode
        log.debug() << "MP3Player alloc packet and frame...";
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

        if (mCancelled) return;

        if (seek > 0) {
            auto ts = seek * AV_TIME_BASE;
            log.debug() << "MP3Player seek frame " << std::to_string(ts);
            av_seek_frame(formatCtx, -1, ts, 0);
        }

        if (mCancelled) return;

        size_t requiredSamples = formatCtx->duration / AV_TIME_BASE * codecCtx->sample_rate * kChannelCount;
        log.debug() << "MP3Player alloc playback buffer size " << std::to_string(requiredSamples);
        // mSamples.resize(requiredSamples + 16384, 0.0f);
        auto insertPos = 0;

        log.debug() << "MP3Player enter read loop...";

        std::vector<float> tmpBuf(0);
        
        while (!mCancelled && av_read_frame(formatCtx, packet) >= 0) {
            if (packet->stream_index == streamIndex) {
                if (avcodec_send_packet(codecCtx, packet) >= 0) {
                    while (!mCancelled && avcodec_receive_frame(codecCtx, frame) >= 0) {
                        int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
                        tmpBuf.resize(outSamples * kChannelCount, 0.0f);
                        uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(tmpBuf.data()) };
                        int convertedFrames = swr_convert(swrCtx, outData, outSamples, const_cast<const uint8_t**>(frame->data), frame->nb_samples);
                        if (convertedFrames < 0) {
                            av_packet_unref(packet);
                            av_frame_unref(frame);
                            throw std::runtime_error("Error during resampling.");
                        }
                        mSamples.insert(mSamples.end(), tmpBuf.begin(), tmpBuf.begin() + convertedFrames * kChannelCount);

                        insertPos += convertedFrames * kChannelCount;
                    }
                }
            }
            av_packet_unref(packet);
        }

        av_packet_free(&packet);
        av_frame_free(&frame);
        swr_free(&swrCtx);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);

        mDuration = mSamples.size() / kChannelCount / mSampleRate;
        mCurrURL = tURL;
        log.info() << "MP3Player loaded " << mSamples.size() << " samples " << mDuration << " sec.";
    }

    void stop() override {
        eject();
    }

    void eject() {
        log.debug() << "MP3Player eject...";
        mCancelled = true;
        //std::lock_guard lock(mMutex);
        mSamples.clear();
        mReadPos = 0;
        mCurrURL = "";
        state = IDLE;
        log.info() << "MP3Player ejected";
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
