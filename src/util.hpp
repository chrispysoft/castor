#pragma once

#include <iostream>
#include <string>
#include <map>
#include <regex>
#include <mutex>
#include <condition_variable>

namespace lap {
namespace util {

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
    static constexpr char delimiter = ':';
    size_t lastColon = annotation.find_last_of(delimiter);
    if (lastColon != std::string::npos) {
        return annotation.substr(lastColon + 1);
    }
    return {};
}


template <typename T>
class RingBuffer {
    std::vector<T> buffer;
    size_t head = 0;
    size_t tail = 0;
    size_t mSize = 0;
    size_t capacity;
    std::mutex mtx;
    std::condition_variable dataAvailable;

public:
    explicit RingBuffer(size_t capacity) :
        buffer(capacity),
        capacity(capacity)
    {}

    size_t size() {
        return mSize;
    }

    void write(const T* data, size_t len) {
        std::unique_lock<std::mutex> lock(mtx);
        for (size_t i = 0; i < len; ++i) {
            buffer[tail] = data[i];
            tail = (tail + 1) % capacity;
            if (mSize < capacity) {
                ++mSize;
            } else {
                head = (head + 1) % capacity; // Overwrite the oldest data
            }
        }
        dataAvailable.notify_one();
    }

    size_t read(T* data, size_t len) {
        std::unique_lock<std::mutex> lock(mtx);
        while (mSize == 0) {
            dataAvailable.wait(lock); // Wait for data to be available
        }

        size_t nRead = 0;
        while (nRead < len && mSize > 0) {
            data[nRead++] = buffer[head];
            head = (head + 1) % capacity;
            --mSize;
        }

        return nRead;
    }
};

}
}