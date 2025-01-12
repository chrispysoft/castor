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
    bool mWriting = false;

    AVChannelLayout mChannelLayout;
    AVDictionary* mOptions = nullptr;
    AVDictionary* mMetadata = nullptr;
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
        av_log_set_level(AV_LOG_INFO);

        av_channel_layout_default(&mChannelLayout, kChannelCount);

        av_dict_set(&mOptions, "timeout", "5000000", 0); // 5 seconds
        av_dict_set(&mOptions, "buffer_size", "65536", 0); // 64 kB
        av_dict_set(&mOptions, "reconnect", "1", 0);
        av_dict_set(&mOptions, "reconnect_at_eof", "1", 0);
        av_dict_set(&mOptions, "reconnect_streamed", "1", 0);
        av_dict_set(&mOptions, "reconnect_delay_max", "2", 0);
        av_dict_set(&mOptions, "fflags", "+discardcorrupt+genpts", 0);

        av_dict_set(&mOptions, "content_type", "audio/mpeg", 0);
        av_dict_set(&mOptions, "user_agent", "castor/0.0.5", 0);
        av_dict_set(&mOptions, "ice_name", "My Awesome Stream", 0);
        av_dict_set(&mOptions, "ice_description", "Castor", 0);
        av_dict_set(&mOptions, "ice_genre", "Live Stream", 0);
        av_dict_set(&mOptions, "ice_url", "https://crispybits.app", 0);

        av_dict_set(&mMetadata, "title", "Track Title", 0);
        av_dict_set(&mMetadata, "artist", "Track Artist", 0);
        av_dict_set(&mMetadata, "album", "Track Album", 0);
        av_dict_set(&mMetadata, "comments", "Created with castor", 0);
        
        auto codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
        if (!codec) {
            throw std::runtime_error("Recorder encoder not found");
        }

        mCodecCtx = avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            throw std::runtime_error("Failed to allocate codec context");
        }
        mCodecCtx->ch_layout = mChannelLayout;
        mCodecCtx->bit_rate = kBitRate;
        mCodecCtx->sample_rate = mSampleRate;
        mCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;

        if (avformat_alloc_output_context2(&mFormatCtx, nullptr, nullptr, mURL.c_str()) < 0) {
            throw std::runtime_error("Failed to allocate output context");
        }
        mFormatCtx->metadata = mMetadata; // freed by libavformat in avformat_free_context()

        mStream = avformat_new_stream(mFormatCtx, nullptr);
        if (!mStream) {
            throw std::runtime_error("Failed to create new stream");
        }
        mStream->time_base = {1, static_cast<int>(mSampleRate)};
        
        if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open codec");
        }

        if (avcodec_parameters_from_context(mStream->codecpar, mCodecCtx) < 0) {
            throw std::runtime_error("Failed to copy codec parameters to stream");
        }

        if (!(mFormatCtx->oformat->flags & AVFMT_NOFILE)) {
            log.debug() << "CodecWriter AVFMT_NOFILE";
            if (avio_open2(&mFormatCtx->pb, tURL.c_str(), AVIO_FLAG_WRITE, nullptr, &mOptions) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(mFormatCtx, nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }

        mSwrCtx = swr_alloc();
        swr_alloc_set_opts2(&mSwrCtx, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate, &mCodecCtx->ch_layout, AV_SAMPLE_FMT_FLT, mSampleRate, 0, nullptr);
        if (!mSwrCtx) {
            throw std::runtime_error("swr_alloc failed");
        }
        if (swr_init(mSwrCtx) < 0) {
            throw std::runtime_error("swr_init failed");
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
        av_dict_free(&mOptions);
        // mMetadata already freed by avformat_free_context
        av_channel_layout_uninit(&mChannelLayout);
        log.debug() << "AudioCodecWriter deinited";
    }


    void write(util::RingBuffer<float>& tBuffer) {
        log.debug() << "AudioCodecWriter write...";

        mWriting = true;

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

            const float* src = mFrameBuffer.data();
            if (swr_convert(mSwrCtx, mFrame->data, mFrame->nb_samples, (const uint8_t**) &src, mCodecCtx->frame_size) < 0) {
                log.error() << "AudioCodecWriter swr_convert failed";
                break;
            }
            // log.debug() << "AudioCodecWriter converted";

            mFrame->pts = av_rescale_q(framesWritten, {1, mCodecCtx->sample_rate}, mStream->time_base);
            framesWritten += mCodecCtx->frame_size;

            if (avcodec_send_frame(mCodecCtx, mFrame) < 0) {
                log.error() << "AudioCodecWriter avcodec_send_frame failed";
                break;
            }

            while (avcodec_receive_packet(mCodecCtx, mPacket) == 0) {
                mPacket->stream_index = mStream->index;
                if (av_interleaved_write_frame(mFormatCtx, mPacket) < 0) {
                    // throw std::runtime_error("Error writing packet");
                    log.error() << "AudioCodecWriter av_interleaved_write_frame failed";
                    goto finalize;
                }
                
                av_packet_unref(mPacket);
            }
        }

        finalize:
        av_write_trailer(mFormatCtx);
        
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mWriting = false;
            mCV.notify_all();
        }
        log.info() << "AudioCodecWriter wrote " << framesWritten << " frames";
    }

    void cancel() {
        if (mCancelled || !mWriting) return;
        log.debug() << "AudioCodecWriter cancel...";
        mCancelled = true;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock, [this] { return !mWriting; });
        }
        log.info() << "AudioCodecWriter cancelled";
    }
};
}
}