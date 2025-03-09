/*
 *  Copyright (C) 2024-2025 Christoph Pastl
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
    static constexpr size_t kAudioBufferSize = 1024;

    size_t mSampleCount;
    double mDuration;

    AVFormatContext* mFormatCtx = nullptr;
    AVCodecContext* mCodecCtx = nullptr;
    SwrContext* mSwrCtx = nullptr;
    AVPacket* mPacket = nullptr;
    AVFrame* mFrame = nullptr;
    AVAudioFifo* mFIFO = nullptr;
    int mStreamIndex = -1;
    
public:
    CodecReader(double tSampleRate, const std::string& tURL, double tSeek = 0) :
        CodecBase(tSampleRate, kFrameBufferSize, tURL),
        mSampleCount(0)
    {
        av_log_set_level(AV_LOG_FATAL);

        AVDictionary *options = NULL;
        av_dict_set(&options, "timeout", "5000000", 0); // 5 seconds
        av_dict_set(&options, "buffer_size", "65536", 0); // 64KB buffer
        av_dict_set(&options, "reconnect", "1", 0); // enable reconnection
        // av_dict_set(&options, "reconnect_at_eof", "1", 0); // reconnect at EOF
        av_dict_set(&options, "reconnect_streamed", "1", 0); // live streams
        av_dict_set(&options, "reconnect_delay_max", "2", 0); // max delay 2s
        av_dict_set(&options, "fflags", "+discardcorrupt+genpts", 0);

        // log.debug() << "CodecReader init " << mURL;
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
        if (swr_alloc_set_opts2(&mSwrCtx, &mChannelLayout, AV_SAMPLE_FMT_FLT, mSampleRate, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate, 0, nullptr) < 0) {
            throw std::runtime_error("swr_alloc_set_opts failed");
        }

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

        if (!mURL.starts_with("http") && tSeek > 0) {
            auto ts = tSeek * AV_TIME_BASE;
            log.debug() << "CodecReader seek frame " << ts;
            av_seek_frame(mFormatCtx, -1, ts, 0);
        }

        if (mCancelled) throw std::runtime_error("Cancelled");

        if (mFormatCtx->duration > 0) {
            mDuration = mFormatCtx->duration / (double) AV_TIME_BASE - tSeek;
            mSampleCount = ceil(mDuration * mSampleRate * kChannelCount) + 1;
        }

        log.debug() << "CodecReader inited " << mURL << " (" << mSampleCount << " samples)";

        mFIFO = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLT, 1, kFrameBufferSize);
    }

    ~CodecReader() {
        av_audio_fifo_free(mFIFO);
        av_packet_free(&mPacket);
        av_frame_free(&mFrame);
        swr_free(&mSwrCtx);
        avcodec_free_context(&mCodecCtx);
        avformat_close_input(&mFormatCtx);
        av_channel_layout_uninit(&mChannelLayout);
    }

    size_t sampleCount() { return mSampleCount; }

    double duration() { return mDuration; }

    std::unique_ptr<Metadata> metadata() {
        return std::make_unique<Metadata>(mFormatCtx->metadata);
    }


    void read(SourceBuffer<sam_t>& tBuffer) {
        log.debug() << "CodecReader read " << mURL;

        // satisfy source buffer with constant block size
        auto outFrameSize = kAudioBufferSize * kChannelCount;

        while (!mCancelled && av_read_frame(mFormatCtx, mPacket) >= 0) {
            if (mPacket->stream_index != mStreamIndex) continue;
            if (avcodec_send_packet(mCodecCtx, mPacket) < 0) break;

            while (!mCancelled && avcodec_receive_frame(mCodecCtx, mFrame) >= 0) {
                auto maxSamples = swr_get_out_samples(mSwrCtx, mFrame->nb_samples);
                auto maxBufSize = maxSamples * kChannelCount;
                // if (mFrameBuffer.size() < maxBufSize) mFrameBuffer.resize(maxBufSize); // should not happen with sufficient buffer size

                // reading maxSamples avoids internal buffering but doesn't guarantee full block size
                uint8_t* outData[1] = { (uint8_t*) mFrameBuffer.data() };
                int convSamples = swr_convert(mSwrCtx, outData, maxSamples, (const uint8_t**) mFrame->data, mFrame->nb_samples);
                // log.debug() << "max samples: " << maxSamples << " converted samples: " << convSamples;

                if (convSamples < 0) {
                    av_packet_unref(mPacket);
                    av_frame_unref(mFrame);
                    log.error() << "CodecReader resample error";
                    break;
                }

                // use fifo to create desired chunks (and reuse frame buffer to save resources)
                auto fifoWritten = av_audio_fifo_write(mFIFO, (void**) outData, convSamples * kChannelCount);
                auto fifosz = av_audio_fifo_size(mFIFO);
                // log.debug() << fifoWritten << " written to fifo, size " << fifisz;

                while (fifosz >= outFrameSize) {
                    void* dat[1] = { reinterpret_cast<uint8_t*>(mFrameBuffer.data()) };

                    auto fifoRead = av_audio_fifo_read(mFIFO, dat, outFrameSize);
                    // log.debug() << read << " read from fifo";

                    if (fifoRead != outFrameSize) {
                        log.warn() << "CodecReader failed to read complete block from fifo";
                        break;
                    }

                    auto written = tBuffer.write(mFrameBuffer.data(), outFrameSize);
                    if (written != outFrameSize) {
                        log.warn() << "CodecReader could not write all samples to output buffer";
                        break;
                    }

                    fifosz = av_audio_fifo_size(mFIFO);
                }
            }

            av_packet_unref(mPacket);
            av_frame_unref(mFrame);
        }

        log.debug() << "CodecReader read finished " << mURL;
    }
};
}
}