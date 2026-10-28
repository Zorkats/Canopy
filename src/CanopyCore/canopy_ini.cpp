#include "canopy_ini.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

#include "platform_files.h"

// =============================================================
// CANOPY.INI READER
// =============================================================
// A mod folder's canopy.ini names the mod, declares its settings and its
// content. The loader and the desktop tools read it the same way, so the
// reader lives here. It never writes the file.

namespace canopy_ini {

std::string Trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string();
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string Lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::vector<IniSection> ReadIni(const std::string& path) {
    std::string text;
    if (!platform::ReadWholeFile(path, std::numeric_limits<uint64_t>::max(), text)) {
        return {};
    }
    return ParseIni(std::move(text));
}

std::vector<IniSection> ParseIni(std::string text) {
    std::vector<IniSection> sections;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        text.erase(0, 3);
    }
    size_t start = 0;
    int lineNumber = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = Trim(text.substr(start, end - start));
        start = end + 1;
        ++lineNumber;
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            IniSection section;
            section.name = Trim(line.substr(1, line.size() - 2));
            section.line = lineNumber;
            sections.push_back(std::move(section));
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos || sections.empty()) {
            continue;
        }
        sections.back().values.emplace_back(Lower(Trim(line.substr(0, equals))), Trim(line.substr(equals + 1)));
        sections.back().valueLines.push_back(lineNumber);
    }
    return sections;
}

std::string Value(const IniSection* section, const char* key, const std::string& fallback) {
    if (section) {
        for (const auto& entry : section->values) {
            if (entry.first == key) {
                return entry.second;
            }
        }
    }
    return fallback;
}

int ValueLine(const IniSection* section, const char* key) {
    if (!section) {
        return 0;
    }
    for (size_t index = 0; index < section->values.size(); ++index) {
        if (section->values[index].first == key && index < section->valueLines.size()) {
            return section->valueLines[index];
        }
    }
    return section->line;
}

const IniSection* FindSection(const std::vector<IniSection>& sections, const char* name) {
    for (const IniSection& section : sections) {
        if (platform::CompareNoCase(section.name.c_str(), name) == 0) {
            return &section;
        }
    }
    return nullptr;
}

bool Truthy(const std::string& text) {
    const std::string value = Lower(text);
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

}  // namespace canopy_ini
