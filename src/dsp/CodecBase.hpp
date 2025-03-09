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

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>
}
#include "audio.hpp"
#include "../util/Log.hpp"

namespace castor {
namespace audio {

class CodecBase {
protected:
    static constexpr size_t kChannelCount = 2;

    const double mSampleRate;
    const std::string mURL;
    std::atomic<bool> mCancelled = false;
    std::vector<sam_t> mFrameBuffer;

    AVChannelLayout mChannelLayout;
    

public:
    CodecBase(double tSampleRate, size_t tFrameBufferSize, const std::string& tURL) :
        mSampleRate(tSampleRate),
        mURL(tURL),
        mFrameBuffer(tFrameBufferSize)
    {
        av_log_set_level(AV_LOG_ERROR);
        av_channel_layout_default(&mChannelLayout, kChannelCount);
    }

    ~CodecBase() {
        av_channel_layout_uninit(&mChannelLayout);
    }

    virtual void cancel() final {
        // if (mCancelled || !mActive) return;
        // log.debug() << "CodecBase cancel...";
        mCancelled = true;
        log.debug() << "CodecBase cancelled";
    }

    static std::string AVErrorString(int error) {
        char errbuf[256];
        av_strerror(error, errbuf, 256);
        std::string errstr(errbuf);
        return errstr;
    }
    
};

class Metadata {
    const std::vector<std::string> mKeys = {
        "title",
        "artist",
        "album",
        "track",
        "date",
        "genre",
        "comment",
        "composer",
        "performer",
        "publisher"
    };

    std::unordered_map<std::string, std::string> mMeta;

public:
    Metadata(AVDictionary* tDict) {
        AVDictionaryEntry* tag = nullptr;
        for (auto key : mKeys) {
            if ((tag = av_dict_get(tDict, key.c_str(), tag, AV_DICT_IGNORE_SUFFIX))) {
                mMeta[tag->key] = tag->value;
            }
        }
    }

    std::string get(const std::string& tKey) {
        try {
            return mMeta.at(tKey);
        }
        catch (...) {
            return "";
        }
    }


};

}
}