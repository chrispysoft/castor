/*
 *  Copyright (C) 2024-2025 Christoph Pastl
 *
 *  This file is part of Castor.
 *
 *  Castor is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Castor is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 *  If you use this program over a network, you must also offer access
 *  to the source code under the terms of the GNU Affero General Public License.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <regex>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <ctime>
#include <cstring>
#include <cmath>
#include <queue>

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
    std::smatch match;
    std::regex isoRegex(R"(^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(Z|([+-]\d{2}):?(\d{2}))?$)");

    if (!std::regex_match(datetime, match, isoRegex)) throw std::runtime_error("Invalid ISO 8601 format: " + datetime);

    std::string dt = match[1];
    std::string tz = match[2];
    std::tm t{};
    std::istringstream ss(dt);
    ss >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");

    if (ss.fail()) throw std::runtime_error("Failed to parse datetime: " + datetime);

    auto ts = timegm(&t);

    if (tz.size() && tz != "Z") {
        auto hours = std::stoi(match[3]);
        auto minutes = std::stoi(match[4]);
        auto offset = (hours * 3600) + (minutes * 60);
        ts -= offset;
    }

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

std::string stripLF(const std::string& line) {
    std::regex removeRgx("[\\n]");
    return std::regex_replace(line, removeRgx, "");
}

std::string getEnvar(const std::string& key) {
    char* val = getenv(key.c_str());
    return val == NULL ? std::string("") : std::string(val);
}

std::string readRawFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path);
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

enum class FileType { MP3, AAC, M4A, OGG, OPUS, FLAC, UNKNOWN };

std::string getFileExtension(const std::string& path) {
    auto dot = path.rfind('.'), slash = path.rfind('/');
    return (dot != std::string::npos && dot > slash) ? path.substr(dot) : "";
}

FileType getFileType(const std::string& path) {
    auto ext = getFileExtension(path);
    if (ext == ".mp3") return FileType::MP3;
    if (ext == ".aac") return FileType::AAC;
    if (ext == ".m4a") return FileType::M4A;
    if (ext == ".ogg") return FileType::OGG;
    if (ext == ".opus") return FileType::OPUS;
    if (ext == ".flac") return FileType::FLAC;
    return FileType::UNKNOWN;
}

template <typename T>
bool contains(const std::deque<T>& tDeque, const T& tItem) {
    return (std::find(tDeque.begin(), tDeque.end(), tItem) != tDeque.end());
}


void sleepCancellable(std::time_t seconds, std::atomic<bool>& running) {
    static constexpr time_t sleepMs = 100;
    auto niters = seconds * 1000 / sleepMs;
    for (auto i = 0; i < niters; ++i) {
        if (!running) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}


size_t nextMultiple(size_t value, size_t multiplier) {
    const auto prevMul = multiplier - 1;
    return (value + prevMul) & ~prevMul;
}


static float linearDB(float lin) {
    if (lin <= 0) return -INFINITY;
    return 20.0f * log10f(lin);
}

static float dbLinear(float db) {
    return powf(10.0f, db / 20.0f);
}


template <typename T>
class RingBuffer {
    size_t mCapacity;
    std::atomic<size_t> mSize = 0;
    std::atomic<size_t> mHead = 0;
    std::atomic<size_t> mTail = 0;
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

    void write(const T* tData, size_t tLen) {
        // std::lock_guard<std::mutex> lock(mMutex);

        for (auto i = 0; i < tLen; ++i) {
            mBuffer[mTail] = tData[i];
            mTail = (mTail + 1) % mCapacity;
            if (mSize < mCapacity) {
                ++mSize;
            } else {
                mHead = (mHead + 1) % mCapacity; // overwrite (prevented if tWait)
            }
        }

        mCV.notify_all();
    }

    size_t read(T* tData, size_t tLen) {
        if (mSize < tLen) {
            return 0;
        }

        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCV.wait(lock, [&]{ return mSize + tLen <= mCapacity; });
        }

        size_t read = 0;
        while (read < tLen && mSize > 0) {
            tData[read++] = mBuffer[mHead];
            mHead = (mHead + 1) % mCapacity;
            --mSize;
        }
        
        return read;
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mMutex);
        mSize = 0;
        mHead = 0;
        mTail = 0;
        // memset(mBuffer.data(), 0, mBuffer.size() * sizeof(T));
        mBuffer = {};
        mCV.notify_all();
    }
};


class ManualTimer {
    const time_t mTimeout;
    time_t mLastQuery = 0;

public:
    ManualTimer(time_t tTimeout) :
        mTimeout(tTimeout)
    {}

    bool query() {
        auto now = std::time(0);
        if (now - mLastQuery > mTimeout) {
            mLastQuery = now;
            return true;
        }
        return false;
    }
};


class AsyncTimer {
protected:
    const std::chrono::seconds mInterval;
    std::atomic<bool> mRunning = false;
    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCV;

public:

    std::function<void()> callback;

    AsyncTimer(time_t tIntervalSec) :
        mInterval(tIntervalSec)
    {}

    ~AsyncTimer() {
        if (mRunning.load()) stop();
    }

    void start() {
        if (mRunning.exchange(true)) return;
        mThread = std::thread(&AsyncTimer::run, this);
    }

    void stop() {
        if (!mRunning.exchange(false, std::memory_order_release)) return;
        mCV.notify_all();
        if (mThread.joinable()) mThread.join();
    }

private:
    virtual void run() {
        while (mRunning.load()) {
            {
                std::unique_lock<std::mutex> lock(mMutex);
                if (mCV.wait_for(lock, mInterval, [this]{ return !mRunning.load(std::memory_order_acquire); })) return;
            }
            if (callback) callback();
        }
    }
};


class AsyncAlignedTimer : public AsyncTimer {
public:
    AsyncAlignedTimer(time_t tIntervalSec) :
        AsyncTimer(tIntervalSec)
    {}

private:
    std::chrono::system_clock::time_point nextAlignedTime() const {
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = std::localtime(&now_time_t);
        auto passedSec = tm->tm_min * 60 + tm->tm_sec;
        int nextAligned = ((passedSec / mInterval.count()) + 1) * mInterval.count();
        tm->tm_min = 0;
        tm->tm_sec = 0;
        auto nextBase = std::chrono::system_clock::from_time_t(std::mktime(tm));
        return nextBase + std::chrono::seconds(nextAligned);
    }

    void run() override {
        if (callback) callback();
        while (mRunning.load()) {
            auto nextTimePt = nextAlignedTime();
            {
                std::unique_lock<std::mutex> lock(mMutex);
                if (mCV.wait_until(lock, nextTimePt, [this]{ return !mRunning.load(std::memory_order_acquire); })) return;
            }
            if (callback) callback();
        }
    }
};


template <typename T>
class AsyncWorker {
    std::atomic<bool> mRunning = false;
    std::thread mThread;
    std::mutex mMutex;
    std::condition_variable mCV;
    std::queue<T> mItems;

public:

    std::function<void(T t)> callback;

    AsyncWorker() = default;

    ~AsyncWorker() {
        if (mRunning.load()) stop();
    }

    void start() {
        if (mRunning.exchange(true)) return;
        mThread = std::thread(&AsyncWorker::run, this);
    }

    void stop() {
        if (!mRunning.exchange(false, std::memory_order_release)) return;
        mCV.notify_all();
        if (mThread.joinable()) mThread.join();
    }


    void async(T tItem) {
        std::lock_guard<std::mutex> lock(mMutex);
        mItems.push(std::move(tItem));
        mCV.notify_one();
    }

private:
    void run() {
        while (mRunning.load()) {
            T item;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mCV.wait(lock, [this]{ return mItems.size() > 0 || !mRunning.load(std::memory_order_acquire); });
                if (!mRunning) return;
                item = std::move(mItems.front());
                mItems.pop();
            }
            if (callback) callback(item);
        }
    }
};

}
}