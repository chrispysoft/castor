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
#include "../util/util.hpp"

namespace cst {
namespace audio {
class AudioCodecReader {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kFrameBufferSize = 4096; // 128 - 2048

    const double mSampleRate;
    size_t mSampleCount;
    double mDuration;
    std::atomic<bool> mCancelled = false;
    std::vector<float> mFrameBuffer;
    size_t mReadSamples = 0;

    AVFormatContext* mFormatCtx = nullptr;
    AVCodecContext* mCodecCtx = nullptr;
    SwrContext* mSwrCtx = nullptr;
    AVPacket* mPacket = nullptr;
    AVFrame* mFrame = nullptr;
    int mStreamIndex = -1;
    

public:
    AudioCodecReader(double tSampleRate, const std::string tURL, double tSeek = 0) :
        mSampleRate(tSampleRate),
        mSampleCount(0),
        mFrameBuffer(kFrameBufferSize)
    {
        av_log_set_level(AV_LOG_ERROR);

        AVDictionary *options = NULL;
        av_dict_set(&options, "timeout", "5000000", 0); // 5 seconds
        av_dict_set(&options, "buffer_size", "65536", 0); // 64KB buffer
        av_dict_set(&options, "reconnect", "1", 0); // enable reconnection
        // av_dict_set(&options, "reconnect_at_eof", "1", 0); // reconnect at EOF
        av_dict_set(&options, "reconnect_streamed", "1", 0); // live streams
        av_dict_set(&options, "reconnect_delay_max", "2", 0); // max delay 2s
        av_dict_set(&options, "fflags", "+discardcorrupt+genpts", 0);

        log.debug() << "AudioCodecReader open file...";
        auto res = avformat_open_input(&mFormatCtx, tURL.c_str(), nullptr, &options);
        if (res < 0) {
            throw std::runtime_error("Could not open input file: " + AVErrorString(res));
        }

        // log.debug() << "AudioCodecReader find stream info...";
        if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
            throw std::runtime_error("Could not find stream information.");
        }

        // log.debug() << "AudioCodecReader find best stream...";
        mStreamIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (mStreamIndex < 0) {
            throw std::runtime_error("Could not find audio stream.");
        }
        
        // log.debug() << "AudioCodecReader find decoder...";
        AVStream* audioStream = mFormatCtx->streams[mStreamIndex];
        AVCodecParameters* codecParams = audioStream->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            throw std::runtime_error("Unsupported codec.");
        }

        // log.debug() << "AudioCodecReader alloc codec context...";
        mCodecCtx = avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            throw std::runtime_error("Could not allocate codec context.");
        }

        if (avcodec_parameters_to_context(mCodecCtx, codecParams) < 0) {
            throw std::runtime_error("Could not fill codec context.");
        }

        // log.debug() << "AudioCodecReader open codec...";
        if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            throw std::runtime_error("Could not open codec.");
        }

        // log.debug() << "AudioCodecReader alloc resampler...";
        mSwrCtx = swr_alloc();
        if (!mSwrCtx) {
            throw std::runtime_error("Could not allocate resampler context.");
        }

        // log.debug() << "AudioCodecReader get channel layout...";
        char inChLayoutDesc[128];
        int sts = av_channel_layout_describe(&mCodecCtx->ch_layout, inChLayoutDesc, sizeof(inChLayoutDesc));
        if (sts < 0) {
            throw std::runtime_error("Could not load input channel layout description");
        }

        // log.debug() << "AudioCodecReader set av options...";
        av_opt_set(mSwrCtx, "in_chlayout", inChLayoutDesc, 0);
        av_opt_set(mSwrCtx, "out_chlayout", "stereo", 0);
        av_opt_set_int(mSwrCtx, "in_sample_rate", mCodecCtx->sample_rate, 0);
        av_opt_set_int(mSwrCtx, "out_sample_rate", mSampleRate, 0);
        av_opt_set_sample_fmt(mSwrCtx, "in_sample_fmt", mCodecCtx->sample_fmt, 0);
        av_opt_set_sample_fmt(mSwrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

        // log.debug() << "AudioCodecReader init resampler...";
        if (swr_init(mSwrCtx) < 0) {
            throw std::runtime_error("Could not init resampler.");
        }

        // log.debug() << "AudioCodecReader alloc mPacket and frame...";
        mPacket = av_packet_alloc();
        if (!mPacket) {
            throw std::runtime_error("Could not allocate packet.");
        }
        mFrame = av_frame_alloc();
        if (!mFrame) {
            throw std::runtime_error("Could not allocate frame.");
        }

        if (mCancelled) throw std::runtime_error("Cancelled");

        if (tSeek > 0) {
            auto ts = tSeek * AV_TIME_BASE;
            log.debug() << "AudioCodecReader seek frame " << std::to_string(ts);
            av_seek_frame(mFormatCtx, -1, ts, 0);
        }

        if (mCancelled) throw std::runtime_error("Cancelled");

        if (mFormatCtx->duration > 0) {
            mDuration = mFormatCtx->duration / (double) AV_TIME_BASE;
            mSampleCount = ceil(mDuration * mSampleRate * kChannelCount) + 1;
        }
        log.debug() << "AudioCodecReader sample count " << std::to_string(mSampleCount);
    }

    ~AudioCodecReader() {
        av_packet_free(&mPacket);
        av_frame_free(&mFrame);
        swr_free(&mSwrCtx);
        avcodec_free_context(&mCodecCtx);
        avformat_close_input(&mFormatCtx);
    }

    size_t sampleCount() { return mSampleCount; }

    double duration() { return mDuration; }

    void read(util::RingBuffer<float>& tBuffer) {
        log.debug() << "AudioCodecReader read...";

        while (!mCancelled && av_read_frame(mFormatCtx, mPacket) >= 0) {
            if (mPacket->stream_index == mStreamIndex) {
                if (avcodec_send_packet(mCodecCtx, mPacket) >= 0) {
                    while (!mCancelled && avcodec_receive_frame(mCodecCtx, mFrame) >= 0) {
                        auto maxSamples = swr_get_out_samples(mSwrCtx, mFrame->nb_samples);
                        auto maxBufSize = maxSamples * kChannelCount;
                        if (mFrameBuffer.size() < maxBufSize) mFrameBuffer.resize(maxBufSize);

                        uint8_t* outData[1] = { reinterpret_cast<uint8_t*>(mFrameBuffer.data()) };
                        int convSamples = swr_convert(mSwrCtx, outData, maxSamples, const_cast<const uint8_t**>(mFrame->data), mFrame->nb_samples);
                        // log.debug() << "max samples: " << maxSamples << " converted samples: " << convSamples;

                        if (convSamples < 0) {
                            av_packet_unref(mPacket);
                            av_frame_unref(mFrame);
                            throw std::runtime_error("Error during resampling.");
                        }

                        mReadSamples += convSamples * kChannelCount;
                        if (mSampleCount > 0 && mReadSamples >= mSampleCount) {
                            log.warn() << "AudioCodecReader exceeded estimated sample count";
                            break;
                        }

                        tBuffer.write(mFrameBuffer.data(), convSamples * kChannelCount);
                    }
                }
            }
            av_packet_unref(mPacket);
            av_frame_unref(mFrame);
        }

        log.info() << "AudioCodecReader read finished";
    }

    void cancel() {
        log.debug() << "AudioCodecReader cancel...";
        mCancelled = true;
        log.info() << "AudioCodecReader cancelled";
    }

    static std::string AVErrorString(int error) {
        char errbuf[256];
        av_strerror(error, errbuf, 256);
        std::string errstr(errbuf);
        return errstr;
    }
};
}
}