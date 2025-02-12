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

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <regex>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <cstring>
#include <cmath>

namespace castor {
namespace util {

std::string currTimeFmtMs() {
    std::stringstream strstr;
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&tt);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    strstr << std::put_time(&tm, "%Y-%m-%d %H:%M:%S.") << std::setw(3) << std::setfill('0') << ms.count();
    return strstr.str();
}

std::string timefmt(const time_t& tTime, const char* tFormat) {
    std::stringstream strstr;
    auto tm = *std::localtime(&tTime);
    strstr << std::put_time(&tm, tFormat);
    return strstr.str();
}

std::string utcFmt(const time_t& tTime = std::time(nullptr)) {
    static constexpr const char* fmt = "%Y-%m-%dT%H:%M:%S";
    return timefmt(tTime, fmt);
}

std::time_t parseDatetime(const std::string& datetime) {
    std::tm t{};
    std::istringstream ss(datetime);
    ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");
    if (ss.fail()) {
        throw std::runtime_error("Failed to parse datetime: " + datetime);
    }
    std::time_t ts = mktime(&t);
    return ts;
}

std::pair<std::string, std::string> splitBy(const std::string& input, const char& delim) {
    size_t pos = input.find(delim);
    if (pos == std::string::npos) {
        return {input, ""};
    }
    return {input.substr(0, pos), input.substr(pos + 1)};
}

void stripM3ULine(std::string& line) {
    std::regex removeRgx("[\\r]");
    line = std::regex_replace(line, removeRgx, "");
}

std::string getEnvar(const std::string& key) {
    char* val = getenv(key.c_str());
    return val == NULL ? std::string("") : std::string(val);
}


template <typename T>
bool contains(const std::deque<T>& tDeque, const T& tItem) {
    return (std::find(tDeque.begin(), tDeque.end(), tItem) != tDeque.end());
}


template <typename T>
class RingBuffer {
    const size_t mCapacity;
    size_t mSize = 0;
    size_t mHead = 0;
    size_t mTail = 0;
    std::vector<T> mBuffer;
    std::mutex mMutex;
    std::condition_variable mCV;

public:
    RingBuffer(size_t tCapacity) :
        mCapacity(tCapacity),
        mBuffer(mCapacity)
    {}

    size_t size() {
        return mSize;
    }

    size_t capacity() {
        return mCapacity;
    }

    size_t remaining() {
        return mCapacity - mSize;
    }

    void write(const T* tData, size_t tLen) {
        std::unique_lock<std::mutex> lock(mMutex);
        mCV.wait(lock, [&]{ return mSize + tLen <= mCapacity; });
        for (auto i = 0; i < tLen; ++i) {
            mBuffer[mTail] = tData[i];
            mTail = (mTail + 1) % mCapacity;
            if (mSize < mCapacity) {
                ++mSize;
            } else {
                mHead = (mHead + 1) % mCapacity; // overwrite
            }
        }
    }

    size_t read(T* tData, size_t tLen) {
        if (mSize < tLen) {
            return 0;
        }

        std::unique_lock<std::mutex> lock(mMutex);
        size_t read = 0;
        while (read < tLen && mSize > 0) {
            tData[read++] = mBuffer[mHead];
            mHead = (mHead + 1) % mCapacity;
            --mSize;
        }
        mCV.notify_all();
        return read;
    }

    void resetHead() {
        mHead = 0;
    }

    void flush() {
        std::unique_lock<std::mutex> lock(mMutex);
        mSize = 0;
        mHead = 0;
        mTail = 0;
        // memset(mBuffer.data(), 0, mBuffer.size() * sizeof(T));
        mCV.notify_all();
    }
};

}
}