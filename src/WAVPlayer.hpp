#include <iostream>
#include <cstring>
#include <string>
#include <sndfile.h>

namespace lap {
class WAVPlayer {

    SNDFILE* mFile;
    SF_INFO mInfo;

public:
    WAVPlayer(const std::string& tPath) {
        mFile = sf_open(tPath.data(), SFM_READ, &mInfo);
        if (sf_error(mFile) != SF_ERR_NO_ERROR) {
            std::string reason(sf_strerror(mFile));
            std::cout << "Failed to read file: " << reason << std::endl;
            throw 1;
        }
    }

    ~WAVPlayer() {
        sf_close(mFile);
    }

    bool read(float* tBuffer, size_t tFrameCount) {
        memset(tBuffer, 0, sizeof(float) * tFrameCount * mInfo.channels);
        auto nread = sf_read_float(mFile, tBuffer, tFrameCount * mInfo.channels);
        return nread == tFrameCount;
    }
};
}
