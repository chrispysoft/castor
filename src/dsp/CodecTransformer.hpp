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
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Lesser General Public License.
 */

#pragma once

#include "CodecBase.hpp"

namespace castor {
namespace audio {

class CodecTransformer : public CodecBase {

    static constexpr size_t kFrameBufferSize = 16384;
    
    AVDictionary* mOptions = nullptr;
    AVDictionary* mMetadata = nullptr;
    AVCodecContext* mCodecCtx = nullptr;
    AVFormatContext* mFormatCtx = nullptr;
    AVStream* mStream = nullptr;
    AVPacket* mPacket = nullptr;
    AVFrame* mFrame = nullptr;
    SwrContext* mSwrCtx = nullptr;
    std::vector<uint8_t> mOutputBuffer;
    util::RingBuffer<uint8_t>& mOutputRingBuffer;
    
public:
    
    static int WritePacket(void* tOpaque, uint8_t* tBuf, int tSize) {
        auto self = static_cast<CodecTransformer*>(tOpaque);
        self->mOutputRingBuffer.write(tBuf, tSize);
        return tSize;
    }
    
    CodecTransformer(const AudioStreamFormat& tClientFormat, int tBitRate, util::RingBuffer<uint8_t>& tRingBuffer) :
        CodecBase(tClientFormat, kFrameBufferSize, ""),
        mOutputBuffer(65536),
        mOutputRingBuffer(tRingBuffer)
    {
        auto codecFormat = AV_CODEC_ID_MP3;

        auto codec = avcodec_find_encoder(codecFormat);
        if (!codec) {
            throw std::runtime_error("Failed to find encoder");
        }

        mCodecCtx = avcodec_alloc_context3(codec);
        if (!mCodecCtx) {
            throw std::runtime_error("Failed to allocate codec context");
        }
        mCodecCtx->ch_layout = mChannelLayout;
        mCodecCtx->bit_rate = tBitRate;
        mCodecCtx->sample_rate = mClientFormat.sampleRate;
        mCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;

        mFormatCtx = avformat_alloc_context();
        if (!mFormatCtx) {
            throw std::runtime_error("Failed to allocate output context");
        }

        mStream = avformat_new_stream(mFormatCtx, nullptr);
        if (!mStream) {
            throw std::runtime_error("Failed to create new stream");
        }
        mStream->time_base = {1, mClientFormat.sampleRate};
        
        if (avcodec_open2(mCodecCtx, codec, nullptr) < 0) {
            throw std::runtime_error("Failed to open codec");
        }

        if (avcodec_parameters_from_context(mStream->codecpar, mCodecCtx) < 0) {
            throw std::runtime_error("Failed to copy codec parameters to stream");
        }
        
        AVIOContext* avio_ctx = avio_alloc_context(mOutputBuffer.data(), (int) mOutputBuffer.size(), 1, this, nullptr, WritePacket, nullptr);
        mFormatCtx->pb = avio_ctx;
        mFormatCtx->oformat = av_guess_format("mp3", NULL, NULL);
        mFormatCtx->flags |= AVFMT_FLAG_CUSTOM_IO;

        if (avformat_write_header(mFormatCtx, nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }

        mSwrCtx = swr_alloc();
        swr_alloc_set_opts2(&mSwrCtx, &mCodecCtx->ch_layout, mCodecCtx->sample_fmt, mCodecCtx->sample_rate, &mCodecCtx->ch_layout, AV_SAMPLE_FMT_FLT, mClientFormat.sampleRate, 0, nullptr);
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

        log.info() << "CodecTransformer inited with sample rate: " << mClientFormat.sampleRate << ", bit rate: " << tBitRate << ", url: " << mURL;
    }

    ~CodecTransformer() {
        log.debug() << "CodecTransformer deinit...";
        av_frame_free(&mFrame);
        av_packet_free(&mPacket);
        swr_free(&mSwrCtx);
        avcodec_free_context(&mCodecCtx);
        avformat_free_context(mFormatCtx);
        av_channel_layout_uninit(&mChannelLayout);
        log.debug() << "CodecTransformer deinited";
    }


    void write(util::RingBuffer<sam_t>& tBuffer) {
        log.debug() << "CodecTransformer write...";

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

        auto samplesPerFrame = mCodecCtx->frame_size * mClientFormat.channelCount;
        auto framesWritten = 0;

        while (!mCancelled) {
            auto availableSamples = tBuffer.size();
            if (availableSamples < samplesPerFrame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            tBuffer.read(mFrameBuffer.data(), samplesPerFrame);
            // log.debug() << "CodecTransformer read " << samplesPerFrame << " samples";

            const auto* src = mFrameBuffer.data();
            if (swr_convert(mSwrCtx, mFrame->data, mFrame->nb_samples, (const uint8_t**) &src, mCodecCtx->frame_size) < 0) {
                log.error() << "CodecTransformer swr_convert failed";
                break;
            }
            // log.debug() << "CodecTransformer converted";

            mFrame->pts = av_rescale_q(framesWritten, {1, mCodecCtx->sample_rate}, mStream->time_base);
            framesWritten += mCodecCtx->frame_size;

            try {
                writeFrame(mFrame);
            }
            catch (const std::exception& e) {
                log.error() << "CodecTransformer writeFrame data failed: " << e.what();
                break;
            }
        }

        try {
            writeFrame(nullptr);
        }
        catch (const std::exception& e) {
            log.error() << "CodecTransformer writeFrame null (flush) failed: " << e.what();
        }

        av_write_trailer(mFormatCtx);

        log.info() << "CodecTransformer wrote " << framesWritten << " frames";
    }
};
}
}
