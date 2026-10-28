#pragma once
#include <string>
#include <utility>
#include <vector>

// The reader for a mod folder's canopy.ini, shared by the loader and the
// desktop tools. Plain UTF-8 text: [section] lines, key = value lines, and
// ';' or '#' comment lines. Keys are stored lowercase, so they compare
// without case; section names and values keep theirs. Every section and
// value remembers its line, so a tool can point at it.
namespace canopy_ini {

struct IniSection {
    std::string name;
    std::vector<std::pair<std::string, std::string>> values;
    // The 1-based line of the [section] header.
    int line = 0;
    // The line of each entry in values, in the same order.
    std::vector<int> valueLines;
};

std::string Trim(const std::string& text);
std::string Lower(std::string text);

// Every section of the file at path, in file order. Empty when the file
// cannot be read.
std::vector<IniSection> ReadIni(const std::string& path);
// Every section of text, as ReadIni reads a file. A leading UTF-8 BOM is
// skipped.
std::vector<IniSection> ParseIni(std::string text);

// The value under key (lowercase) in section, or fallback when the section
// is null or has no such key.
std::string Value(const IniSection* section, const char* key, const std::string& fallback);
// The line of key's value in section, or the section's own line when it has
// no such key. 0 for a null section.
int ValueLine(const IniSection* section, const char* key);

// The first section named name, without case, or null.
const IniSection* FindSection(const std::vector<IniSection>& sections, const char* name);

// true, 1, yes and on, without case.
bool Truthy(const std::string& text);

}  // namespace canopy_ini
