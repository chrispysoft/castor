#pragma once

#include <iostream>
#include <string>
#include <utility>
#include <map>
#include <regex>
#include <mutex>
#include <ctime>

namespace lap {
namespace util {

std::string boolstr(const bool& flag) {
    return flag ? "true" : "false";
}

bool strbool(const std::string& str) {
    return str == "true" || str == "True";
}

std::string timestr(std::time_t sec) {
    using namespace std;
    static constexpr time_t kSecMin = 60;
    static constexpr time_t kSecHour = kSecMin * 60;
    static constexpr time_t kSecDay = kSecHour * 24;
    auto d = sec / kSecDay;
    auto h = (sec / kSecHour) % kSecHour;
    auto m = (sec / kSecMin) % kSecMin;
    auto s = sec % kSecMin;
    auto fmt = to_string(d) + "d " + to_string(h) + "h " + to_string(m) + "m " + to_string(s) + "s"; // "0d 00h 01m 11s";
    return fmt;
}

std::pair<std::string, std::string> splitBy(const std::string& input, const char& delim) {
    size_t pos = input.find(delim);
    if (pos == std::string::npos) {
        return {input, ""};
    }
    return {input.substr(0, pos), input.substr(pos + 1)};
}

std::map<std::string, std::string> extractMetadata(const std::string& annotation) {
    using namespace std;
    map<string, string> keyValuePairs;
    
    regex kvRegex(R"((\w+)=["]?([^",]+)["]?)");
    smatch match;
    string::const_iterator searchStart(annotation.begin());
    while (regex_search(searchStart, annotation.end(), match, kvRegex)) {
        keyValuePairs[match[1]] = match[2]; // Group 1: key, Group 2: value
        searchStart = match.suffix().first; // Continue searching from the end of the current match
    }
    
    return keyValuePairs;
}

std::string extractUrl(const std::string& annotation) {
    std::regex pattern(R"((?:[^:]*:){2}(.*))");
    std::smatch match;
    if (std::regex_match(annotation, match, pattern)) {
        return match[1];
    }
    return "";
}

std::string getEnvar(const std::string& key) {
    char* val = getenv(key.c_str());
    return val == NULL ? std::string("") : std::string(val);
}


template <typename T>
class RingBuffer {
    const size_t mCapacity;
    size_t mSize = 0;
    size_t mHead = 0;
    size_t mTail = 0;
    std::vector<T> mBuffer;
    std::mutex mMutex;

public:
    explicit RingBuffer(size_t tCapacity) :
        mCapacity(tCapacity),
        mBuffer(mCapacity)
    {}

    size_t size() {
        return mSize;
    }

    void write(const T* tData, size_t tLen) {
        std::unique_lock<std::mutex> lock(mMutex);
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
        std::unique_lock<std::mutex> lock(mMutex);
        if (mSize < tLen) {
            return 0;
        }

        size_t read = 0;
        while (read < tLen && mSize > 0) {
            tData[read++] = mBuffer[mHead];
            mHead = (mHead + 1) % mCapacity;
            --mSize;
        }

        return read;
    }
};


class Timer {
    time_t mStartTime;

public:

    Timer() :
        mStartTime(std::time(0))
    {}

    time_t get() {
        return std::time(0) - mStartTime;
    }
};

}
}