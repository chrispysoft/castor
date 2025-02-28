/*
 *  Copyright (C) 2024-2025 Christoph Pastl (crispybits.app)
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "CodecBase.hpp"
#include "AudioProcessor.hpp"

namespace castor {
namespace audio {

class CodecReader : public CodecBase {

    static constexpr size_t kFrameBufferSize = 4096; // 128 - 2048

    size_t mSampleCount;
    double mDuration;

    AVFormatContext* mFormatCtx = nullptr;
    AVCodecContext* mCodecCtx = nullptr;
    SwrContext* mSwrCtx = nullptr;
    AVPacket* mPacket = nullptr;
    AVFrame* mFrame = nullptr;
    int mStreamIndex = -1;
    
public:
    CodecReader(double tSampleRate, const std::string& tURL, double tSeek = 0) :
        CodecBase(tSampleRate, kFrameBufferSize, tURL),
        mSampleCount(0)
    {
        AVDictionary *options = NULL;
        av_dict_set(&options, "timeout", "5000000", 0); // 5 seconds
        av_dict_set(&options, "buffer_size", "65536", 0); // 64KB buffer
        av_dict_set(&options, "reconnect", "1", 0); // enable reconnection
        // av_dict_set(&options, "reconnect_at_eof", "1", 0); // reconnect at EOF
        av_dict_set(&options, "reconnect_streamed", "1", 0); // live streams
        av_dict_set(&options, "reconnect_delay_max", "2", 0); // max delay 2s
        av_dict_set(&options, "fflags", "+discardcorrupt+genpts", 0);

        log.debug() << "CodecReader init " << mURL;
        auto res = avformat_open_input(&mFormatCtx, mURL.c_str(), nullptr, &options);
        av_dict_free(&options);
        if (res < 0) {
            throw std::runtime_error("Could not open input file: " + AVErrorString(res));
        }

        // log.debug() << "CodecReader find stream info...";
        if (avformat_find_stream_info(mFormatCtx, nullptr) < 0) {
            throw std::runtime_error("Could not find stream information.");
        }

        // log.debug() << "CodecReader find best stream...";
        mStreamIndex = av_find_best_stream(mFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (mStreamIndex < 0) {
            throw std::runtime_error("Could not find audio stream.");
        }
        
        // log.debug() << "CodecReader find decoder...";
        AVStream* audioStream = mFormatCtx->streams[mStreamIndex];
        AVCodecParameters* codecParams = audioStream->codecpar;
        const AVCodec* codec = avcodec_find_decoder(codecParams->codec_id);
        if (!codec) {
            throw std::runtime_error("Unsupported codec.");
        }

        // log.debug() << "CodecReader alloc codec context...";
        mCodecCtx = avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            throw std::runtime_error("Could not allocate codec context.");
        }

        if (avcodec_parameters_to_context(mCodecCtx, codecParams) < 0) {
            throw std::runtime_error("Could not fill codec context.");
        }

        // log.debug() << "CodecReader open codec...";
        if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            throw std::runtime_error("Could not open codec.");
        }

        // log.debug() << "CodecReader alloc resampler...";
        mSwrCtx = swr_alloc();
        swr_alloc_set_opts2(&mSwrCtx, &mChannelLayout, AV_SAMPLE_FMT_S16, mSampleRate, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate, 0, nullptr);
        if (!mSwrCtx) {
            throw std::runtime_error("swr_alloc failed");
        }
        // log.debug() << "CodecReader init resampler...";
        if (swr_init(mSwrCtx) < 0) {
            throw std::runtime_error("swr_init failed");
        }

        // log.debug() << "CodecReader alloc mPacket and frame...";
        mPacket = av_packet_alloc();
        if (!mPacket) {
            throw std::runtime_error("Could not allocate packet.");
        }
        mFrame = av_frame_alloc();
        if (!mFrame) {
            throw std::runtime_error("Could not allocate frame.");
        }

        if (mCancelled) throw std::runtime_error("Cancelled");

        if (!tURL.starts_with("http") && tSeek > 0) {
            auto ts = tSeek * AV_TIME_BASE;
            log.debug() << "CodecReader seek frame " << ts;
            av_seek_frame(mFormatCtx, -1, ts, 0);
        }

        if (mCancelled) throw std::runtime_error("Cancelled");

        if (mFormatCtx->duration > 0) {
            mDuration = mFormatCtx->duration / (double) AV_TIME_BASE - tSeek;
            mSampleCount = ceil(mDuration * mSampleRate * kChannelCount) + 1;
        }
        log.debug() << "CodecReader estimated num samples: " << mSampleCount;
    }

    ~CodecReader() {
        av_packet_free(&mPacket);
        av_frame_free(&mFrame);
        swr_free(&mSwrCtx);
        avcodec_free_context(&mCodecCtx);
        avformat_close_input(&mFormatCtx);
        av_channel_layout_uninit(&mChannelLayout);
    }

    size_t sampleCount() { return mSampleCount; }

    double duration() { return mDuration; }

    void read(PlayBuffer<sam_t>& tBuffer) {
        log.info() << "CodecReader read...";

        {
            std::unique_lock<std::mutex> lock(mMutex);
            mActive = true;
            mCV.notify_one();
        }

        while (!mCancelled && av_read_frame(mFormatCtx, mPacket) >= 0) {
            if (mPacket->stream_index != mStreamIndex) continue;
            if (avcodec_send_packet(mCodecCtx, mPacket) < 0) break;

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
                    log.error() << "CodecReader resample error";
                    break;
                }

                auto toWrite = convSamples * kChannelCount;
                auto written = tBuffer.write(mFrameBuffer.data(), toWrite);
                if ( written != toWrite) {
                    log.warn() << "CocecReader could not write all samples";
                    break;
                }
            }

            av_packet_unref(mPacket);
            av_frame_unref(mFrame);
        }

        {
            std::unique_lock<std::mutex> lock(mMutex);
            mActive = false;
            mCV.notify_one();
        }

        log.info() << "CodecReader read finished";
    }
};
}
}