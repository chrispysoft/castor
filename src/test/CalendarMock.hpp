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

#include <ctime>
#include <deque>
#include "../Config.hpp"
#include "../api/API.hpp"

namespace castor {
namespace test {
class CalendarMock{
    const Config& mConfig;
    std::deque<PlayItem> mItems;

public:

    CalendarMock(const Config& tConfig) :
        mConfig(tConfig)
    {
        auto now = std::time(0);

        mItems.push_back({ now +  5, now + 10, "./audio/test/A maj.mp3" });
        mItems.push_back({ now + 10, now + 15, "http://stream.fro.at/fro-128.ogg" });
        // mItems.push_back({ now + 10, now + 15, "./audio/test/D maj.mp3" });
        mItems.push_back({ now + 15, now + 20, "./audio/test/G maj.mp3" });
        mItems.push_back({ now + 20, now + 30, "http://stream.fro.at/oggst3.ogg" });
        // mItems.push_back({ now + 20, now + 25, "./audio/test/C maj.mp3" });
        // mItems.push_back({ now + 25, now + 30, "./audio/test/F maj.mp3" });
        mItems.push_back({ now + 30, now + 35, "./audio/test/A# maj.mp3" });
        mItems.push_back({ now + 35, now + 40, "./audio/test/D# maj.mp3" });
        mItems.push_back({ now + 40, now + 45, "./audio/test/G# maj.mp3" });
        mItems.push_back({ now + 45, now + 50, "http://stream.fro.at/non-existing.ogg" });
        // mItems.push_back({ now + 45, now + 50, "./audio/test/C# maj.mp3" });
        mItems.push_back({ now + 50, now + 55, "./audio/test/F# maj.mp3" });
        mItems.push_back({ now + 55, now + 60, "./audio/test/B maj.mp3" });
        mItems.push_back({ now + 60, now + 65, "./audio/test/E maj.mp3" });
    }

    std::deque<PlayItem> items() {
        return mItems;
    }
    
    std::deque<PlayItem> fetchItems() {
        return mItems;
    }

    void refresh() {
        
    }

    void start() {
        
    }

    void stop() {
        
    }

};
}
}