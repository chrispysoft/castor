#pragma once

#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <atomic>
#include "../common/Log.hpp"
#include "../common/util.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace cst {
class Recorder {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 16384;
    static constexpr size_t kPipeBufferSize = 512;
    static constexpr int64_t kBitRate = 192000;

    const double mSampleRate;
    util::RingBuffer<float> mRingBuffer;
    std::unique_ptr<std::thread> mWorker = nullptr;
    std::atomic<bool> mRunning = false;
    AVChannelLayout mChannelLayout = {}; 
    const AVCodec* mCodec;

public:

    Recorder(double tSampleRate) :
        mSampleRate(tSampleRate),
        mRingBuffer(kRingBufferSize)
    {
        av_channel_layout_default(&mChannelLayout, kChannelCount);
        mCodec = avcodec_find_encoder(AV_CODEC_ID_MP3);
        if (!mCodec) throw std::runtime_error("Recorder encoder not found");
    }

    void start(std::string tURL) {
        if (mRunning) {
            log.debug() << "Recorder already running";
            return;
        }
        mRunning = true;
        mWorker = std::make_unique<std::thread>([this, tURL] {
            this->record(tURL);
        });
    }

     void stop() {
        mRunning = false;
        if (mWorker && mWorker->joinable()) {
            mWorker->join();
        }
        mWorker = nullptr;
        log.info() << "Recorder stopped";
    }

    bool isRunning() {
        return mRunning;
    }

private:

    void record(const std::string& tURL) {
        log.info() << "Recorder start " << tURL;
        
        AVCodecContext* codecCtx = nullptr;
        codecCtx = avcodec_alloc_context3(mCodec);
        if (!codecCtx) {
            throw std::runtime_error("Failed to allocate codec context");
        }

        AVFormatContext* formatCtx = nullptr;
        if (avformat_alloc_output_context2(&formatCtx, nullptr, nullptr, tURL.c_str()) < 0) {
            avcodec_free_context(&codecCtx);
            throw std::runtime_error("Failed to allocate output context");
        }

        AVStream* audioStream = nullptr;
        audioStream = avformat_new_stream(formatCtx, nullptr);
        if (!audioStream) {
            throw std::runtime_error("Failed to create new stream");
        }

        codecCtx->ch_layout = mChannelLayout;
        codecCtx->bit_rate = kBitRate;
        codecCtx->sample_rate = mSampleRate;
        codecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        audioStream->time_base = {1, static_cast<int>(mSampleRate)};

        if (avcodec_open2(codecCtx, mCodec, nullptr) < 0) {
            throw std::runtime_error("Failed to open codec");
        }

        if (avcodec_parameters_from_context(audioStream->codecpar, codecCtx) < 0) {
            throw std::runtime_error("Failed to copy codec parameters to stream");
        }

        if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&formatCtx->pb, tURL.c_str(), AVIO_FLAG_WRITE) < 0) {
                throw std::runtime_error("Failed to open output file");
            }
        }

        if (avformat_write_header(formatCtx, nullptr) < 0) {
            throw std::runtime_error("Failed to write header");
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame) {
            throw std::runtime_error("Failed to allocate frame");
        }
        frame->nb_samples = codecCtx->frame_size;
        frame->format = codecCtx->sample_fmt;
        frame->ch_layout = codecCtx->ch_layout;

        if (av_frame_get_buffer(frame, 0) < 0) {
            throw std::runtime_error("Failed to allocate frame buffer");
        }

        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            throw std::runtime_error("Failed to allocate packet");
        }

        SwrContext* swr_context = nullptr;// swr_alloc();
        swr_alloc_set_opts2(&swr_context, &codecCtx->ch_layout, codecCtx->sample_fmt, codecCtx->sample_rate, &codecCtx->ch_layout, AV_SAMPLE_FMT_FLT, mSampleRate, 0, nullptr);
        if (!swr_context || swr_init(swr_context) < 0) {
            throw std::runtime_error("Failed to initialize resampler");
        }

        auto samplesPerFrame = codecCtx->frame_size * kChannelCount;
        auto framesWritten = 0;
        std::vector<float> workBuf(samplesPerFrame, 0);

        log.debug() << "Recorder created work buffer size " << samplesPerFrame;

        while (mRunning) {
            auto availableSamples = mRingBuffer.size();
            if (availableSamples < samplesPerFrame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            mRingBuffer.read(workBuf.data(), samplesPerFrame);

            float* src = const_cast<float*>(workBuf.data());
            uint8_t* dst = frame->data[0];

            if (swr_convert(swr_context, &dst, frame->nb_samples, (const uint8_t**) &src, codecCtx->frame_size) < 0) {
                throw std::runtime_error("Error converting samples");
            }

            frame->pts = av_rescale_q(framesWritten, {1, codecCtx->sample_rate}, audioStream->time_base);
            framesWritten += codecCtx->frame_size;

            if (avcodec_send_frame(codecCtx, frame) < 0) {
                throw std::runtime_error("Failed to send frame to encoder");
            }

            while (avcodec_receive_packet(codecCtx, packet) == 0) {
                packet->stream_index = audioStream->index;
                if (av_interleaved_write_frame(formatCtx, packet) < 0) {
                    throw std::runtime_error("Error writing packet");
                }
                av_packet_unref(packet);
            }
        }

        av_write_trailer(formatCtx);
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr_context);
        avcodec_free_context(&codecCtx);
        if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&formatCtx->pb);
        }
        avformat_free_context(formatCtx);
        log.info() << "Recorder finished";
    }

    void process(const float* tSamples, size_t nframes) {
        mRingBuffer.write(tSamples, nframes * kChannelCount);
    }

};
}