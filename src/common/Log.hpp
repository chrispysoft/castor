#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <mutex>
#include "util.hpp"

namespace cst {

class LogStream {
    std::ostream& mOut;
    std::ostream& mFile;
    std::ostringstream mBuf;
    std::string mLabel;
    std::string mPrefix;
    std::string mSuffix;
    std::mutex& mMutex;

public:
    LogStream(std::ostream& tOut, std::ostream& tFile, const std::string& tLabel, const std::string& tPrefix, const std::string& tSuffix, std::mutex& tMutex) :
        mOut(tOut),
        mFile(tFile),
        mLabel(tLabel),
        mPrefix(tPrefix),
        mSuffix(tSuffix),
        mMutex(tMutex)
    {}

    template <typename T>
    LogStream& operator<<(const T& message) {
        mBuf << message;
        return *this;
    }

    ~LogStream() {
        std::lock_guard<std::mutex> guard(mMutex);
        const auto& str = mBuf.str();
        if (!str.empty()) {
            auto timefmt = util::currTimeFmtMs();
            mOut << mPrefix << timefmt << " " << mLabel << str << mSuffix << std::endl;
            mFile << timefmt << " " << mLabel << str << std::endl;
        }
    }
};

class Log {
    static constexpr const char* Red = "\033[0;31m";
    static constexpr const char* Green = "\033[0;32m";
    static constexpr const char* Yellow = "\033[0;33m";
    static constexpr const char* Blue = "\033[0;34m";
    static constexpr const char* Magenta = "\033[0;35m";
    static constexpr const char* Cyan = "\033[0;36m";
    static constexpr const char* Reset = "\033[0m";

    std::ofstream mFile;
    std::mutex mMutex;

public:
    Log(const std::string tFilePath) :
        mFile(tFilePath, std::ios::app)
    {}

    Log& operator=(Log other) {
        std::swap(mFile, other.mFile);
        return *this;
    }

    LogStream debug(const char* color = Cyan)  { return LogStream(std::cerr, mFile, "[DEBUG] ", color, Reset, mMutex); }
    LogStream info(const char* color = Green)  { return LogStream(std::cerr, mFile, "[INFO ] ", color, Reset, mMutex); }
    LogStream warn(const char* color = Yellow) { return LogStream(std::cerr, mFile, "[WARN ] ", color, Reset, mMutex); }
    LogStream error(const char* color = Red)   { return LogStream(std::cerr, mFile, "[ERROR] ", color, Reset, mMutex); }
};

Log log("../castoria.log");

}