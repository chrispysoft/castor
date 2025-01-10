#pragma once

#include "AudioProcessor.hpp"
#include "../util/Log.hpp"
#include "../util/util.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}
#include <iostream>
#include <string>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace cst {
namespace audio {
class CodecWriter {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kFrameBufferSize = 16384;
    static constexpr int64_t kBitRate = 192000;

    const double mSampleRate;
    const std::string mURL;
    std::vector<float> mFrameBuffer;
    std::atomic<bool> mCancelled = false;
    std::mutex mMutex;
    std::condition_variable mCV;

    AVChannelLayout mChannelLayout;
    AVCodecContext* mCodecCtx = nullptr;
    AVFormatContext* mFormatCtx = nullptr;
    AVStream* mStream = nullptr;
    AVPacket* mPacket = nullptr;
    AVFrame* mFrame = nullptr;
    SwrContext* mSwrCtx = nullptr;
    
public:

    CodecWriter(double tSampleRate, const std::string& tURL) :
        mSampleRate(tSampleRate),
        mURL(tURL),
        mFrameBuffer(kFrameBufferSize)
    {
        // av_log_set_level(AV_LOG_TRACE);

        av_channel_layout_default(&mChannelLayout, kChannelCount);
        
        auto codec = avcodec_find_encoder(AV_CODEC_ID_VORBIS);
        if (!codec) {
            throw std::runtime_error("Recorder encoder not found");
        }

        mCodecCtx = avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            throw std::runtime_error("Failed to allocate codec context");
        }

        if (avformat_alloc_output_context2(&mFormatCtx, nullptr, nullptr, mURL.c_str()) < 0) {
            throw std::runtime_error("Failed to allocate output context");
        }

        mStream = avformat_new_stream(mFormatCtx, nullptr);
        if (!mStream) {
            throw std::runtime_error("Failed to create new stream");
        }

        mCodecCtx->ch_layout = mChannelLayout;
        mCodecCtx->bit_rate = kBitRate;
        mCodecCtx->sample_rate = mSampleRate;
        mCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        mStream->time_base = {1, static_cast<int>(mSampleRate)};

        if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open codec");
        }

        if (avcodec_parameters_from_context(mStream->codecpar, mCodecCtx) < 0) {
            throw std::runtime_error("Failed to copy codec parameters to stream");
        }

        if (!(mFormatCtx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&mFormatCtx->pb, tURL.c_str(), AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(mFormatCtx, nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }

        mSwrCtx = swr_alloc();
        swr_alloc_set_opts2(&mSwrCtx, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate, &mCodecCtx->ch_layout, AV_SAMPLE_FMT_FLT, mSampleRate, 0, nullptr);
        if (!mSwrCtx || swr_init(mSwrCtx) < 0) {
            throw std::runtime_error("Failed to initialize resampler");
        }

        mPacket = av_packet_alloc();
        if (!mPacket) {
            throw std::runtime_error("Failed to allocate packet");
        }

        mFrame = av_frame_alloc();
        if (!mFrame) {
            throw std::runtime_error("Failed to allocate frame");
        }
        mFrame->nb_samples = mCodecCtx->frame_size;
        mFrame->format = mCodecCtx->sample_fmt;
        mFrame->ch_layout = mCodecCtx->ch_layout;

        if (av_frame_get_buffer(mFrame, 0) < 0) {
            throw std::runtime_error("Failed to allocate frame buffer");
        }

        log.info() << "AudioCodecWriter inited with sample rate " << mSampleRate << " url: " << mURL;
    }

    ~CodecWriter() {
        log.debug() << "AudioCodecWriter deinit...";
        av_frame_free(&mFrame);
        av_packet_free(&mPacket);
        swr_free(&mSwrCtx);
        avcodec_free_context(&mCodecCtx);
        if (!(mFormatCtx->oformat->flags & AVFMT_NOFILE) && mFormatCtx->pb) avio_closep(&mFormatCtx->pb);
        avformat_close_input(&mFormatCtx);
        avformat_free_context(mFormatCtx);
        log.debug() << "AudioCodecWriter deinited";
    }


    void write(util::RingBuffer<float>& tBuffer) {
        log.debug() << "AudioCodecWriter write...";

        auto samplesPerFrame = mCodecCtx->frame_size * kChannelCount;
        auto framesWritten = 0;

        while (!mCancelled) {
            auto availableSamples = tBuffer.size();
            if (availableSamples < samplesPerFrame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            tBuffer.read(mFrameBuffer.data(), samplesPerFrame);
            // log.debug() << "AudioCodecWriter read " << samplesPerFrame << " samples";

            float* src = const_cast<float*>(mFrameBuffer.data());
            uint8_t* dst = mFrame->data[0];

            if (swr_convert(mSwrCtx, &dst, mFrame->nb_samples, (const uint8_t**) &src, mCodecCtx->frame_size) < 0) {
                log.error() << "Error converting samples";
                break;
            }
            // log.debug() << "AudioCodecWriter converted";

            mFrame->pts = av_rescale_q(framesWritten, {1, mCodecCtx->sample_rate}, mStream->time_base);
            framesWritten += mCodecCtx->frame_size;

            if (avcodec_send_frame(mCodecCtx, mFrame) < 0) {
                log.error() << "Failed to send frame to encoder";
                break;
            }

            while (avcodec_receive_packet(mCodecCtx, mPacket) == 0) {
                mPacket->stream_index = mStream->index;
                if (av_interleaved_write_frame(mFormatCtx, mPacket) < 0) {
                    // throw std::runtime_error("Error writing packet");
                    log.error() << "Error writing packet";
                    break;
                }
                
                av_packet_unref(mPacket);
            }
        }

        av_write_trailer(mFormatCtx);
        
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.notify_all();
        }
        log.info() << "AudioCodecWriter wrote << " << framesWritten << " frames";
    }

    void cancel() {
        if (mCancelled) return;
        log.debug() << "AudioCodecWriter cancel...";
        mCancelled = true;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock);
        }
        log.info() << "AudioCodecWriter cancelled";
    }
};
}
}