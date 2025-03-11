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

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <ctime>
#include <mutex>
#include "util.hpp"

namespace castor {

enum class LogLevel : int { Notset, Debug, Info, Warn, Error };

class LogStream {
    std::ostream& mOut;
    std::ostream& mFile;
    std::ostringstream mBuf;
    const std::string mLabel;
    const std::string mPrefix;
    const std::string mSuffix;
    std::mutex& mMutex;
    const bool mCondition;

public:
    LogStream(std::ostream& tOut, std::ostream& tFile, const std::string& tLabel, const std::string& tPrefix, const std::string& tSuffix, std::mutex& tMutex, bool tCondition) :
        mOut(tOut),
        mFile(tFile),
        mLabel(tLabel),
        mPrefix(tPrefix),
        mSuffix(tSuffix),
        mMutex(tMutex),
        mCondition(tCondition)
    {}

    template <typename T>
    LogStream& operator<<(const T& message) {
        mBuf << message;
        return *this;
    }

    ~LogStream() {
        if (!mCondition) return;
        const auto& str = mBuf.str();
        if (!str.empty()) {
            auto timefmt = util::currTimeFmtMs();
            std::lock_guard<std::mutex> lock(mMutex);
            mOut << mPrefix << timefmt << " " << mLabel << str << mSuffix << std::endl;
            mFile << timefmt << " " << mLabel << str << std::endl;
        }
    }
};

class Log {
    
    std::ofstream mFile;
    std::mutex mMutex;
    LogLevel mLevel = LogLevel::Debug;

public:

    static constexpr const char* Red = "\033[0;31m";
    static constexpr const char* Green = "\033[0;32m";
    static constexpr const char* Yellow = "\033[0;33m";
    static constexpr const char* Blue = "\033[0;34m";
    static constexpr const char* Magenta = "\033[0;35m";
    static constexpr const char* Cyan = "\033[0;36m";
    static constexpr const char* Reset = "\033[0m";

    void setFilePath(const std::string& tFilePath) {
        mFile.open(tFilePath, std::ios::app);
        if (mFile.is_open()) {
            info() << "Log logging to " << tFilePath;
        } else {
            error() << "Log failed to open file " << tFilePath;
        }
    }

    ~Log() {
        if (mFile.is_open()) mFile.close();
    }

    void setLevel(int tLevelRaw) {
        mLevel = LogLevel(tLevelRaw);
        info() << "Log set level " << tLevelRaw;
    }

    LogStream debug(const char* color = Cyan)  { return LogStream(std::cerr, mFile, "[DEBUG] ", color, Reset, mMutex, mLevel <= LogLevel::Debug); }
    LogStream info(const char* color = Green)  { return LogStream(std::cerr, mFile, "[INFO ] ", color, Reset, mMutex, mLevel <= LogLevel::Info); }
    LogStream warn(const char* color = Yellow) { return LogStream(std::cerr, mFile, "[WARN ] ", color, Reset, mMutex, mLevel <= LogLevel::Warn); }
    LogStream error(const char* color = Red)   { return LogStream(std::cerr, mFile, "[ERROR] ", color, Reset, mMutex, mLevel <= LogLevel::Error); }
};

Log log;

}
