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

namespace castor {
namespace audio {

class CodecWriter : public CodecBase {

    static constexpr size_t kFrameBufferSize = 16384;
    static constexpr int64_t kBitRate = 192000;
    
    AVDictionary* mOptions = nullptr;
    AVDictionary* mMetadata = nullptr;
    AVCodecContext* mCodecCtx = nullptr;
    AVFormatContext* mFormatCtx = nullptr;
    AVStream* mStream = nullptr;
    AVPacket* mPacket = nullptr;
    AVFrame* mFrame = nullptr;
    SwrContext* mSwrCtx = nullptr;
    
public:
    CodecWriter(double tSampleRate, const std::string& tURL, const std::unordered_map<std::string, std::string>& tMetadata = {}) :
        CodecBase(tSampleRate, kFrameBufferSize, tURL)
    {
        av_dict_set(&mOptions, "timeout", "5000000", 0); // 5 seconds
        av_dict_set(&mOptions, "buffer_size", "65536", 0); // 64 kB
        av_dict_set(&mOptions, "reconnect", "1", 0);
        av_dict_set(&mOptions, "reconnect_at_eof", "1", 0);
        av_dict_set(&mOptions, "reconnect_streamed", "1", 0);
        av_dict_set(&mOptions, "reconnect_delay_max", "2", 0);
        av_dict_set(&mOptions, "fflags", "+discardcorrupt+genpts", 0);

        av_dict_set(&mOptions, "content_type", "audio/mpeg", 0);
        av_dict_set(&mOptions, "user_agent", "ffmpeg", 0);

        for (const auto& m : tMetadata) {
            av_dict_set(&mMetadata, m.first.c_str(), m.second.c_str(), 0);
        }
        
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
            if (avio_open2(&mFormatCtx->pb, mURL.c_str(), AVIO_FLAG_WRITE, nullptr, &mOptions) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(mFormatCtx, nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }

        mSwrCtx = swr_alloc();
        swr_alloc_set_opts2(&mSwrCtx, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate, &mCodecCtx->ch_layout, AV_SAMPLE_FMT_S16, mSampleRate, 0, nullptr);
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


    void write(util::RingBuffer<sam_t>& tBuffer) {
        log.debug() << "AudioCodecWriter write...";

        auto writeFrame = [this](AVFrame* lFrame) {
            if (avcodec_send_frame(mCodecCtx, lFrame) < 0) {
                throw std::runtime_error("avcodec_send_frame failed");
            }
            while (avcodec_receive_packet(mCodecCtx, mPacket) == 0) {
                mPacket->stream_index = mStream->index;
                if (av_interleaved_write_frame(mFormatCtx, mPacket) < 0) {
                    throw std::runtime_error("av_interleaved_write_frame failed");
                }
                av_packet_unref(mPacket);
            }
        };

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

            const auto* src = mFrameBuffer.data();
            if (swr_convert(mSwrCtx, mFrame->data, mFrame->nb_samples, (const uint8_t**) &src, mCodecCtx->frame_size) < 0) {
                log.error() << "AudioCodecWriter swr_convert failed";
                break;
            }
            // log.debug() << "AudioCodecWriter converted";

            mFrame->pts = av_rescale_q(framesWritten, {1, mCodecCtx->sample_rate}, mStream->time_base);
            framesWritten += mCodecCtx->frame_size;

            try {
                writeFrame(mFrame);
            }
            catch (const std::exception& e) {
                log.error() << "AudioCodecWriter writeFrame data failed: " << e.what();
                break;
            }
        }

        try {
            writeFrame(nullptr);
        }
        catch (const std::exception& e) {
            log.error() << "AudioCodecWriter writeFrame null (flush) failed: " << e.what();
        }

        av_write_trailer(mFormatCtx);

        log.info() << "AudioCodecWriter wrote " << framesWritten << " frames";
    }
};
}
}