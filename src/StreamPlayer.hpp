#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include "util.hpp"

namespace lap {
class StreamPlayer {

    static constexpr size_t kChannelCount = 2;
    static constexpr size_t kRingBufferSize = 1024 * 1024;
    static constexpr size_t kPipeBufferSize = 4096;
    static constexpr size_t kMinRenderBufferSize = 16384;


    const double mSampleRate;
    const size_t mChannelCount;
    util::RingBuffer<float> mRingBuffer;

public:
    StreamPlayer(double tSampleRate, size_t tChannelCount = kChannelCount, size_t tRingBufferSize = kRingBufferSize) :
        mSampleRate(tSampleRate),
        mChannelCount(tChannelCount),
        mRingBuffer(tRingBufferSize)
    {

    }

    ~StreamPlayer() {
        
    }

    void open(const std::string& tURL) {
        std::cout << "StreamPlayer open " << tURL << std::endl;
        
        std::string command = "ffmpeg -i \"" + tURL + "\" -ac " + std::to_string(mChannelCount) + " -ar " + std::to_string(int(mSampleRate)) + " -channel_layout stereo -f f32le - 2>/dev/null";
        // std::cout << command << std::endl;
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            std::cerr << "Failed to open pipe to ffmpeg." << std::endl;
            return;
        }

        std::vector<float> pipeBuffer(kPipeBufferSize);
        size_t bytesRead;
        while ((bytesRead = fread(pipeBuffer.data(), 1, kPipeBufferSize * sizeof(float), pipe)) > 0) {
            mRingBuffer.write(pipeBuffer.data(), bytesRead / sizeof(float));
            // std::cout << "wrote " << bytesRead << " bytes" << std::endl;
        }

        pclose(pipe);

        std::cout << "StreamPlayer finished" << std::endl;
    }
    
    bool read(float* tBuffer, size_t tFrameCount) {
        auto sampleCount = tFrameCount * mChannelCount;
        auto byteSize = sampleCount * sizeof(float);

        if (mRingBuffer.size() < kMinRenderBufferSize) {
            // std::cout << "mRingBuffer.size " << mRingBuffer.size << std::endl;
            memset(tBuffer, 0, byteSize);
            return false;
        }
        
        size_t bytesRead = mRingBuffer.read(tBuffer, sampleCount);
        if (bytesRead) {
            // std::cout << "read " << bytesRead << " from ringbuffer" << std::endl;
            // mReadPos += sampleCount;
            return true;
        } else {
            std::cout << "0 bytes read" << std::endl; 
            memset(tBuffer, 0, byteSize);
            return false;
        }
    }
};
}
