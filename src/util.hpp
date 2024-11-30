#pragma once

#include <iostream>
#include <string>
#include <map>
#include <regex>

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

}
}