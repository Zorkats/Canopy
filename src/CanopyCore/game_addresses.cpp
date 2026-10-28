#include "game_addresses.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

namespace game_addresses {
namespace data {
// Written by cmake/embed_text.cmake from src/common.
extern const unsigned char kPatterns[];
extern const size_t kPatternsSize;
extern const unsigned char kBuilds[];
extern const size_t kBuildsSize;
}  // namespace data

namespace {

constexpr uint32_t kExecutable = 0x20000000;
constexpr uint32_t kExceptionDirectory = 3;
// UNW_FLAG_CHAININFO: this unwind range is a piece of another function.
constexpr uint8_t kChainedUnwind = 4;
// Hits a string line may have before its lea filter runs, and hits a code
// or ref line needs to be rejected as ambiguous.
constexpr size_t kStringHits = 64;
constexpr size_t kUniqueHits = 2;
constexpr size_t kMaxTypeName = 4096;

template <typename T>
T Read(const uint8_t* bytes) {
    T value;
    std::memcpy(&value, bytes, sizeof(T));
    return value;
}

// =============================================================
// IMAGE LAYOUT
// =============================================================
struct Section {
    uint32_t begin = 0;
    uint32_t size = 0;
    // The part the file fills; the rest is zeros on disk and whatever the
    // game wrote there in memory, so no search looks at it.
    uint32_t initialized = 0;
    bool code = false;
};

struct Function {
    uint32_t begin = 0;
    uint32_t end = 0;
};

struct Layout {
    Image image;
    Header header;
    std::vector<Section> code;
    std::vector<Section> data;
    std::vector<Function> functions;
    std::unordered_map<uint32_t, uint32_t> parents;

    bool InCode(int64_t rva) const { return In(code, rva); }
    bool InData(int64_t rva) const { return In(data, rva); }
    bool Readable(int64_t rva, size_t length) const {
        return rva >= 0 && static_cast<uint64_t>(rva) + length <= image.size;
    }

    static bool In(const std::vector<Section>& sections, int64_t rva) {
        for (const Section& section : sections) {
            if (rva >= section.begin && rva < static_cast<int64_t>(section.begin) + section.size) {
                return true;
            }
        }
        return false;
    }

    // Length of the unwind range that holds rva, or 0.
    uint32_t RangeLength(int64_t rva) const {
        auto after = std::upper_bound(functions.begin(), functions.end(), rva, [](int64_t value, const Function& f) {
            return value < f.begin;
        });
        if (after == functions.begin() || rva >= (after - 1)->end) {
            return 0;
        }
        return (after - 1)->end - (after - 1)->begin;
    }

    // Entry of the function whose unwind ranges hold rva, or -1.
    int64_t FunctionStart(int64_t rva) const {
        auto after = std::upper_bound(functions.begin(), functions.end(), rva, [](int64_t value, const Function& f) {
            return value < f.begin;
        });
        if (after == functions.begin()) {
            return -1;
        }
        const Function& function = *(after - 1);
        if (rva >= function.end) {
            return -1;
        }
        uint32_t begin = function.begin;
        for (int depth = 0; depth < 16; ++depth) {
            const auto parent = parents.find(begin);
            if (parent == parents.end()) {
                return begin;
            }
            begin = parent->second;
        }
        return -1;
    }
};

struct PeOffsets {
    uint32_t sections = 0;
    uint16_t sectionCount = 0;
    uint32_t optional = 0;
};

// Offsets of the parts of a PE32+ header, in bytes of size.
bool FindHeader(const uint8_t* bytes, size_t size, PeOffsets& offsets) {
    if (size < 0x40 || bytes[0] != 'M' || bytes[1] != 'Z') {
        return false;
    }
    const uint32_t pe = Read<uint32_t>(bytes + 0x3C);
    if (static_cast<uint64_t>(pe) + 24 + 112 > size || std::memcmp(bytes + pe, "PE\0\0", 4) != 0) {
        return false;
    }
    offsets.optional = pe + 24;
    if (Read<uint16_t>(bytes + offsets.optional) != 0x20B) {
        return false;
    }
    offsets.sectionCount = Read<uint16_t>(bytes + pe + 6);
    offsets.sections = offsets.optional + Read<uint16_t>(bytes + pe + 20);
    return static_cast<uint64_t>(offsets.sections) + offsets.sectionCount * 40ull <= size;
}

bool BuildLayout(const Image& image, Layout& layout) {
    PeOffsets offsets;
    if (!image.bytes || !FindHeader(image.bytes, image.size, offsets) || !ReadHeader(image, layout.header)) {
        return false;
    }
    layout.image = image;
    const uint8_t* bytes = image.bytes;
    for (uint16_t index = 0; index < offsets.sectionCount; ++index) {
        const uint8_t* entry = bytes + offsets.sections + index * 40u;
        Section section;
        section.begin = Read<uint32_t>(entry + 12);
        if (section.begin >= image.size) {
            continue;
        }
        const uint32_t room = static_cast<uint32_t>(image.size - section.begin);
        section.size = std::min(Read<uint32_t>(entry + 8), room);
        section.initialized = std::min(section.size, Read<uint32_t>(entry + 16));
        section.code = (Read<uint32_t>(entry + 36) & kExecutable) != 0;
        (section.code ? layout.code : layout.data).push_back(section);
    }

    const uint32_t directories = offsets.optional + 112;
    if (Read<uint32_t>(bytes + offsets.optional + 108) > kExceptionDirectory) {
        const uint8_t* directory = bytes + directories + kExceptionDirectory * 8;
        const uint32_t address = Read<uint32_t>(directory);
        const uint32_t size = Read<uint32_t>(directory + 4);
        if (layout.Readable(address, size)) {
            for (uint32_t offset = address; offset + 12 <= address + size; offset += 12) {
                const uint32_t begin = Read<uint32_t>(bytes + offset);
                const uint32_t end = Read<uint32_t>(bytes + offset + 4);
                const uint32_t unwind = Read<uint32_t>(bytes + offset + 8);
                if (!begin) {
                    continue;
                }
                layout.functions.push_back({begin, end});
                if (layout.Readable(unwind, 4) && (bytes[unwind] >> 3 & kChainedUnwind)) {
                    const uint32_t codes = bytes[unwind + 2];
                    const uint32_t parent = unwind + 4 + 2 * (codes + (codes & 1));
                    if (layout.Readable(parent, 4)) {
                        layout.parents[begin] = Read<uint32_t>(bytes + parent);
                    }
                }
            }
        }
    }
    std::sort(layout.functions.begin(), layout.functions.end(), [](const Function& left, const Function& right) {
        return left.begin < right.begin;
    });
    return true;
}

// =============================================================
// PATTERN LINES
// =============================================================
enum class Kind { Code, Ref, Rtti, String, Field };

struct Line {
    std::string name;
    Kind kind = Kind::Code;
    std::vector<uint8_t> values;
    std::vector<uint8_t> solid;
    // The type name of an rtti line, the text of a string line.
    std::string text;
    int64_t at = 0;
    bool atStart = false;
    int32_t disp = 0;
    int32_t next = 0;
    bool toCode = true;
    int32_t lea = 0;
    uint32_t offset = 0;
    int32_t slot = -1;
    // A field line's value: size bytes, read bytes in.
    int32_t read = 0;
    int32_t size = 0;
};

std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> parts;
    size_t at = 0;
    while (at < text.size()) {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
            ++at;
        }
        const size_t begin = at;
        while (at < text.size() && text[at] != ' ' && text[at] != '\t') {
            ++at;
        }
        if (at > begin) {
            parts.push_back(text.substr(begin, at - begin));
        }
    }
    return parts;
}

bool ParseInteger(const std::string& text, int64_t& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtoll(text.c_str(), &end, 0);
    return end && *end == '\0';
}

// Each line of text without its line break, comments and blank lines left
// out.
std::vector<std::string> ContentLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t begin = 0;
    while (begin < text.size()) {
        size_t end = text.find('\n', begin);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line[first] != '#') {
            lines.push_back(line);
        }
        begin = end + 1;
    }
    return lines;
}

bool ParseLine(const std::string& text, Line& line) {
    const std::vector<std::string> parts = Split(text);
    if (parts.size() < 3) {
        return false;
    }
    line.name = parts[0];
    const std::string& kind = parts[1];
    size_t rest = 2;
    if (kind == "rtti" || kind == "string") {
        line.kind = kind == "rtti" ? Kind::Rtti : Kind::String;
        line.text = parts[2];
        rest = 3;
    } else if (kind == "code") {
        line.kind = Kind::Code;
    } else if (kind == "ref") {
        line.kind = Kind::Ref;
    } else if (kind == "field") {
        line.kind = Kind::Field;
    } else {
        return false;
    }
    for (size_t index = rest; index < parts.size(); ++index) {
        const std::string& part = parts[index];
        const size_t equals = part.find('=');
        if (equals != std::string::npos) {
            const std::string key = part.substr(0, equals);
            const std::string value = part.substr(equals + 1);
            int64_t number = 0;
            if (key == "to") {
                if (value != "code" && value != "data") {
                    return false;
                }
                line.toCode = value == "code";
            } else if (key == "at" && value == "start") {
                line.atStart = true;
            } else if (!ParseInteger(value, number)) {
                return false;
            } else if (key == "at") {
                line.at = number;
            } else if (key == "disp") {
                line.disp = static_cast<int32_t>(number);
            } else if (key == "next") {
                line.next = static_cast<int32_t>(number);
            } else if (key == "lea") {
                line.lea = static_cast<int32_t>(number);
            } else if (key == "offset") {
                line.offset = static_cast<uint32_t>(number);
            } else if (key == "slot") {
                line.slot = static_cast<int32_t>(number);
            } else if (key == "read") {
                line.read = static_cast<int32_t>(number);
            } else if (key == "size") {
                line.size = static_cast<int32_t>(number);
            } else {
                return false;
            }
        } else if (line.kind == Kind::Rtti) {
            return false;
        } else if (part == "?" || part == "??") {
            line.values.push_back(0);
            line.solid.push_back(0);
        } else {
            int64_t number = 0;
            if (part.size() != 2 || !ParseInteger("0x" + part, number)) {
                return false;
            }
            line.values.push_back(static_cast<uint8_t>(number));
            line.solid.push_back(1);
        }
    }
    if (line.kind != Kind::Rtti && line.values.empty()) {
        return false;
    }
    if (line.kind == Kind::Ref && (line.disp < 0 || static_cast<size_t>(line.disp) + 4 > line.values.size())) {
        return false;
    }
    if (line.kind == Kind::Field &&
        ((line.size != 1 && line.size != 2 && line.size != 4) || line.read < 0 ||
         static_cast<size_t>(line.read + line.size) > line.values.size())) {
        return false;
    }
    return line.kind != Kind::String ||
           (line.lea >= 0 && static_cast<size_t>(line.lea) + 4 <= line.values.size());
}

std::vector<Line> ParsePatterns(const std::string& text) {
    std::vector<Line> lines;
    for (const std::string& raw : ContentLines(text)) {
        Line line;
        if (ParseLine(raw, line)) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

std::set<std::string> FieldNames(const std::vector<Line>& lines) {
    std::set<std::string> names;
    for (const Line& line : lines) {
        if (line.kind == Kind::Field) {
            names.insert(line.name);
        }
    }
    return names;
}

std::vector<std::string> NamesOf(const std::vector<Line>& lines) {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (const Line& line : lines) {
        if (seen.insert(line.name).second) {
            names.push_back(line.name);
        }
    }
    return names;
}

// =============================================================
// SEARCH
// =============================================================
struct VtableKey {
    uint32_t descriptor;
    uint32_t offset;
    bool operator<(const VtableKey& other) const {
        return descriptor != other.descriptor ? descriptor < other.descriptor : offset < other.offset;
    }
};

// Up to two places each wanted thing was found at; one means unique.
template <typename Key>
using Places = std::map<Key, std::vector<uint32_t>>;

void Note(std::vector<uint32_t>& places, uint32_t rva) {
    if (places.size() < kUniqueHits) {
        places.push_back(rva);
    }
}

template <typename Key>
int64_t Unique(const Places<Key>& places, const Key& key) {
    const auto found = places.find(key);
    return found != places.end() && found->second.size() == 1 ? found->second[0] : -1;
}

// The one NUL-delimited occurrence of each wanted string in the data.
Places<std::string> FindStrings(const Layout& layout, const std::set<std::string>& wanted) {
    Places<std::string> places;
    size_t longest = 0;
    for (const std::string& text : wanted) {
        longest = std::max(longest, text.size());
    }
    if (wanted.empty()) {
        return places;
    }
    const uint8_t* bytes = layout.image.bytes;
    for (const Section& section : layout.data) {
        const uint32_t end = section.begin + section.initialized;
        for (uint32_t at = section.begin + 1; at < end; ++at) {
            if (bytes[at - 1] != 0 || bytes[at] == 0) {
                continue;
            }
            const uint32_t limit = static_cast<uint32_t>(std::min<uint64_t>(end, at + longest + 1));
            const void* nul = std::memchr(bytes + at, 0, limit - at);
            if (!nul) {
                continue;
            }
            const std::string text(reinterpret_cast<const char*>(bytes + at), static_cast<const uint8_t*>(nul) - (bytes + at));
            if (wanted.count(text)) {
                Note(places[text], at);
            }
        }
    }
    return places;
}

// The vtable of each wanted (type, subobject offset), through MSVC RTTI:
// the type descriptor holds the decorated name 16 bytes in, a complete
// object locator (signature 1) names the descriptor and the offset, and the
// vtable's slot -1 points at the locator.
std::map<std::pair<std::string, uint32_t>, int64_t> FindVtables(
    const Layout& layout, const std::set<std::pair<std::string, uint32_t>>& wanted
) {
    std::map<std::pair<std::string, uint32_t>, int64_t> vtables;
    if (wanted.empty()) {
        return vtables;
    }
    std::set<std::string> names;
    for (const auto& type : wanted) {
        names.insert(type.first);
    }
    const uint8_t* bytes = layout.image.bytes;

    Places<std::string> descriptors;
    for (const Section& section : layout.data) {
        const uint32_t end = section.begin + section.initialized;
        for (uint32_t at = (section.begin + 7) & ~7u; at + 3 < end; at += 8) {
            if (bytes[at] != '.' || bytes[at + 1] != '?' || bytes[at + 2] != 'A' || bytes[at - 1] != 0) {
                continue;
            }
            const size_t room = std::min<size_t>(end - at, kMaxTypeName);
            const void* nul = std::memchr(bytes + at, 0, room);
            if (!nul || at < 16) {
                continue;
            }
            const std::string name(reinterpret_cast<const char*>(bytes + at), static_cast<const uint8_t*>(nul) - (bytes + at));
            if (names.count(name)) {
                Note(descriptors[name], at - 16);
            }
        }
    }
    std::set<uint32_t> descriptorSet;
    for (const std::string& name : names) {
        const int64_t descriptor = Unique(descriptors, name);
        if (descriptor >= 0) {
            descriptorSet.insert(static_cast<uint32_t>(descriptor));
        }
    }

    Places<VtableKey> locators;
    for (const Section& section : layout.data) {
        const uint32_t end = section.begin + section.initialized;
        for (uint32_t at = (section.begin + 3) & ~3u; at + 16 <= end; at += 4) {
            if (Read<uint32_t>(bytes + at) != 1) {
                continue;
            }
            const uint32_t descriptor = Read<uint32_t>(bytes + at + 12);
            if (descriptorSet.count(descriptor)) {
                Note(locators[{descriptor, Read<uint32_t>(bytes + at + 4)}], at);
            }
        }
    }
    std::set<uint64_t> locatorPointers;
    for (const auto& type : wanted) {
        const int64_t descriptor = Unique(descriptors, type.first);
        const int64_t locator = descriptor < 0 ? -1 : Unique(locators, VtableKey{static_cast<uint32_t>(descriptor), type.second});
        if (locator >= 0) {
            locatorPointers.insert(layout.image.pointerBase + static_cast<uint64_t>(locator));
        }
    }

    Places<uint64_t> slots;
    for (const Section& section : layout.data) {
        const uint32_t end = section.begin + section.initialized;
        for (uint32_t at = (section.begin + 7) & ~7u; at + 8 <= end; at += 8) {
            const uint64_t pointer = Read<uint64_t>(bytes + at);
            if (locatorPointers.count(pointer)) {
                Note(slots[pointer], at + 8);
            }
        }
    }
    for (const auto& type : wanted) {
        const int64_t descriptor = Unique(descriptors, type.first);
        const int64_t locator = descriptor < 0 ? -1 : Unique(locators, VtableKey{static_cast<uint32_t>(descriptor), type.second});
        vtables[type] = locator < 0 ? -1 : Unique(slots, layout.image.pointerBase + static_cast<uint64_t>(locator));
    }
    return vtables;
}

bool Matches(const uint8_t* at, const Line& line) {
    for (size_t index = 0; index < line.values.size(); ++index) {
        if (line.solid[index] && at[index] != line.values[index]) {
            return false;
        }
    }
    return true;
}

// Every match of each byte line in the code, in address order, up to the
// hits the line needs. One pass: each line is filed under its rarest pair of
// adjacent solid bytes, and only lines filed under the pair at hand are
// compared.
std::vector<std::vector<uint32_t>> FindCode(const Layout& layout, const std::vector<Line>& lines) {
    std::vector<std::vector<uint32_t>> hits(lines.size());
    const uint8_t* bytes = layout.image.bytes;
    std::vector<uint32_t> frequency(65536, 0);
    for (const Section& section : layout.code) {
        for (uint32_t at = section.begin; at + 1 < section.begin + section.size; ++at) {
            ++frequency[bytes[at] | bytes[at + 1] << 8];
        }
    }

    struct Filed {
        size_t line;
        uint32_t offset;
    };
    std::vector<std::vector<Filed>> buckets(65536);
    std::vector<size_t> unfiled;
    for (size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        if (line.kind == Kind::Rtti) {
            continue;
        }
        int64_t best = -1;
        for (size_t k = 0; k + 1 < line.values.size(); ++k) {
            if (line.solid[k] && line.solid[k + 1]) {
                const uint32_t key = line.values[k] | line.values[k + 1] << 8;
                if (best < 0 || frequency[key] < frequency[line.values[best] | line.values[best + 1] << 8]) {
                    best = static_cast<int64_t>(k);
                }
            }
        }
        if (best < 0) {
            unfiled.push_back(index);
        } else {
            buckets[line.values[best] | line.values[best + 1] << 8].push_back({index, static_cast<uint32_t>(best)});
        }
    }

    auto wants = [&](size_t index) {
        return hits[index].size() < (lines[index].kind == Kind::String ? kStringHits : kUniqueHits);
    };
    for (const Section& section : layout.code) {
        const uint32_t begin = section.begin;
        const uint64_t end = static_cast<uint64_t>(section.begin) + section.size;
        for (uint32_t at = begin; at + 1 < end; ++at) {
            const std::vector<Filed>& bucket = buckets[bytes[at] | bytes[at + 1] << 8];
            for (const Filed& filed : bucket) {
                if (at < begin + filed.offset) {
                    continue;
                }
                const uint32_t start = at - filed.offset;
                const Line& line = lines[filed.line];
                if (start + line.values.size() <= end && wants(filed.line) && Matches(bytes + start, line)) {
                    hits[filed.line].push_back(start);
                }
            }
        }
        for (size_t index : unfiled) {
            const Line& line = lines[index];
            for (uint64_t start = begin; start + line.values.size() <= end && wants(index); ++start) {
                if (Matches(bytes + start, line)) {
                    hits[index].push_back(static_cast<uint32_t>(start));
                }
            }
        }
    }
    return hits;
}

int64_t Moved(const Layout& layout, int64_t hit, const Line& line) {
    return line.atStart ? layout.FunctionStart(hit) : hit + line.at;
}

// The address each line finds, or -1.
std::vector<int64_t> Evaluate(const Layout& layout, const std::vector<Line>& lines) {
    std::set<std::string> strings;
    std::set<std::pair<std::string, uint32_t>> types;
    for (const Line& line : lines) {
        if (line.kind == Kind::String) {
            strings.insert(line.text);
        } else if (line.kind == Kind::Rtti) {
            types.insert({line.text, line.offset});
        }
    }
    const Places<std::string> stringPlaces = FindStrings(layout, strings);
    const auto vtables = FindVtables(layout, types);
    const std::vector<std::vector<uint32_t>> hits = FindCode(layout, lines);
    const uint8_t* bytes = layout.image.bytes;

    std::vector<int64_t> found(lines.size(), -1);
    for (size_t index = 0; index < lines.size(); ++index) {
        const Line& line = lines[index];
        const std::vector<uint32_t>& places = hits[index];
        switch (line.kind) {
        case Kind::Code:
            if (places.size() == 1) {
                found[index] = Moved(layout, places[0], line);
            }
            break;
        case Kind::Ref:
            if (places.size() == 1) {
                const int64_t target = static_cast<int64_t>(places[0]) + line.next +
                                       Read<int32_t>(bytes + places[0] + line.disp);
                if (line.toCode ? layout.InCode(target) : layout.InData(target)) {
                    found[index] = target;
                }
            }
            break;
        case Kind::Field:
            if (places.size() == 1) {
                // Offsets and sizes are never negative, so a negative read
                // finds nothing.
                const uint8_t* value = bytes + places[0] + line.read;
                const int64_t read = line.size == 1   ? Read<int8_t>(value)
                                     : line.size == 2 ? Read<int16_t>(value)
                                                      : Read<int32_t>(value);
                found[index] = std::max<int64_t>(read, -1);
            }
            break;
        case Kind::String: {
            const int64_t text = Unique(stringPlaces, line.text);
            if (text < 0) {
                break;
            }
            int64_t only = -1;
            size_t loading = 0;
            for (uint32_t place : places) {
                if (static_cast<int64_t>(place) + line.lea + 4 + Read<int32_t>(bytes + place + line.lea) == text) {
                    only = place;
                    ++loading;
                }
            }
            if (loading == 1) {
                found[index] = Moved(layout, only, line);
            }
            break;
        }
        case Kind::Rtti: {
            const int64_t vtable = vtables.at({line.text, line.offset});
            if (vtable < 0 || line.slot < 0) {
                found[index] = vtable;
                break;
            }
            const int64_t entry = vtable + static_cast<int64_t>(line.slot) * 8;
            if (!layout.Readable(entry, 8)) {
                break;
            }
            const int64_t function = static_cast<int64_t>(Read<uint64_t>(bytes + entry) - layout.image.pointerBase);
            if (layout.InCode(function)) {
                found[index] = function;
            }
            break;
        }
        }
        // A field's value is no address.
        if (line.kind != Kind::Field && found[index] >= 0 && !layout.Readable(found[index], 1)) {
            found[index] = -1;
        }
    }
    return found;
}

// =============================================================
// BUILDS
// =============================================================
struct Build {
    uint32_t timestamp = 0;
    uint32_t imageSize = 0;
    std::string label;
    std::vector<std::pair<std::string, int64_t>> addresses;
};

std::vector<Build> ParseBuilds(const std::string& text) {
    std::vector<Build> builds;
    for (const std::string& raw : ContentLines(text)) {
        const std::vector<std::string> parts = Split(raw);
        int64_t number = 0;
        if (parts.size() >= 3 && parts[0] == "build") {
            int64_t size = 0;
            if (!ParseInteger(parts[1], number) || parts[2].compare(0, 5, "size=") != 0 ||
                !ParseInteger(parts[2].substr(5), size)) {
                builds.push_back({});
                continue;
            }
            Build build;
            build.timestamp = static_cast<uint32_t>(number);
            build.imageSize = static_cast<uint32_t>(size);
            const size_t label = raw.find(parts[2]) + parts[2].size();
            build.label = raw.substr(label);
            build.label.erase(0, build.label.find_first_not_of(" \t"));
            builds.push_back(std::move(build));
        } else if (parts.size() == 2 && !builds.empty()) {
            if (parts[1] == "none") {
                builds.back().addresses.push_back({parts[0], -1});
            } else if (ParseInteger(parts[1], number)) {
                builds.back().addresses.push_back({parts[0], number});
            }
        }
    }
    return builds;
}

std::string HexText(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

std::string EntryText(const Entry* entry) {
    if (!entry) {
        return "unlisted";
    }
    switch (entry->status) {
    case Status::Found:
        return HexText(entry->rva);
    case Status::Absent:
        return "none";
    case Status::Missing:
        return "missing";
    case Status::Conflict:
        return "conflict";
    }
    return "?";
}

std::string Text(const unsigned char* bytes, size_t size) {
    return std::string(reinterpret_cast<const char*>(bytes), size);
}

}  // namespace

const Entry* Resolution::Find(const std::string& name) const {
    for (const Entry& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

size_t Resolution::Count(Status status) const {
    return static_cast<size_t>(std::count_if(entries.begin(), entries.end(), [status](const Entry& entry) {
        return entry.status == status;
    }));
}

bool ReadHeader(const Image& image, Header& header) {
    PeOffsets offsets;
    if (!image.bytes || !FindHeader(image.bytes, image.size, offsets)) {
        return false;
    }
    const uint32_t pe = offsets.optional - 24;
    header.timestamp = Read<uint32_t>(image.bytes + pe + 8);
    header.imageBase = Read<uint64_t>(image.bytes + offsets.optional + 24);
    header.imageSize = Read<uint32_t>(image.bytes + offsets.optional + 56);
    return true;
}

bool LayOut(const std::vector<uint8_t>& file, std::vector<uint8_t>& image, std::string& error) {
    PeOffsets offsets;
    if (!FindHeader(file.data(), file.size(), offsets)) {
        error = "not a 64-bit Windows executable";
        return false;
    }
    const uint32_t imageSize = Read<uint32_t>(file.data() + offsets.optional + 56);
    const uint32_t headersSize = Read<uint32_t>(file.data() + offsets.optional + 60);
    image.assign(imageSize, 0);
    std::memcpy(image.data(), file.data(), std::min<size_t>({headersSize, file.size(), image.size()}));
    for (uint16_t index = 0; index < offsets.sectionCount; ++index) {
        const uint8_t* entry = file.data() + offsets.sections + index * 40u;
        const uint32_t virtualSize = Read<uint32_t>(entry + 8);
        const uint32_t address = Read<uint32_t>(entry + 12);
        const uint32_t rawSize = Read<uint32_t>(entry + 16);
        const uint32_t raw = Read<uint32_t>(entry + 20);
        const uint64_t length = std::min(virtualSize, rawSize);
        if (static_cast<uint64_t>(address) + length > image.size() || static_cast<uint64_t>(raw) + length > file.size()) {
            error = "a section lies outside the file";
            return false;
        }
        std::memcpy(image.data() + address, file.data() + raw, static_cast<size_t>(length));
    }
    return true;
}

Resolution Search(const Image& image, const std::string& patterns) {
    Resolution resolution;
    Layout layout;
    const std::vector<Line> lines = ParsePatterns(patterns);
    const std::set<std::string> fields = FieldNames(lines);
    for (const std::string& name : NamesOf(lines)) {
        resolution.entries.push_back({name, Status::Missing, 0, fields.count(name) > 0});
    }
    if (!BuildLayout(image, layout)) {
        return resolution;
    }
    resolution.header = layout.header;
    const std::vector<int64_t> found = Evaluate(layout, lines);
    std::unordered_map<std::string, std::set<int64_t>> places;
    std::unordered_map<std::string, std::pair<uint16_t, uint16_t>> counts;
    for (size_t index = 0; index < lines.size(); ++index) {
        std::pair<uint16_t, uint16_t>& count = counts[lines[index].name];
        ++count.first;
        if (found[index] >= 0) {
            places[lines[index].name].insert(found[index]);
            ++count.second;
        }
    }
    for (Entry& entry : resolution.entries) {
        entry.lines = counts[entry.name].first;
        entry.hits = counts[entry.name].second;
        const auto place = places.find(entry.name);
        if (place == places.end()) {
            continue;
        }
        if (place->second.size() == 1) {
            entry.status = Status::Found;
            entry.rva = static_cast<uintptr_t>(*place->second.begin());
        } else {
            entry.status = Status::Conflict;
        }
    }
    return resolution;
}

bool LookUp(const Header& header, const std::string& patterns, const std::string& builds, Resolution& resolution) {
    for (const Build& build : ParseBuilds(builds)) {
        if (build.timestamp != header.timestamp || build.imageSize != header.imageSize || build.label.empty()) {
            continue;
        }
        resolution = Resolution();
        resolution.header = header;
        resolution.build = build.label;
        const std::vector<Line> lines = ParsePatterns(patterns);
        const std::set<std::string> fields = FieldNames(lines);
        std::set<std::string> listed;
        for (const auto& address : build.addresses) {
            listed.insert(address.first);
            const Status status = address.second < 0 ? Status::Absent : Status::Found;
            resolution.entries.push_back(
                {address.first, status, status == Status::Found ? static_cast<uintptr_t>(address.second) : 0,
                 fields.count(address.first) > 0}
            );
        }
        for (const std::string& name : NamesOf(lines)) {
            if (!listed.count(name)) {
                resolution.entries.push_back({name, Status::Missing, 0, fields.count(name) > 0});
            }
        }
        return true;
    }
    return false;
}

Resolution Resolve(const Image& image, const std::string& patterns, const std::string& builds) {
    Header header;
    Resolution resolution;
    if (ReadHeader(image, header) && LookUp(header, patterns, builds, resolution)) {
        return resolution;
    }
    return Search(image, patterns);
}

Resolution Resolve(const Image& image) {
    return Resolve(image, PatternsText(), BuildsText());
}

Resolution Search(const Image& image) {
    return Search(image, PatternsText());
}

bool LookUp(const Header& header, Resolution& resolution) {
    return LookUp(header, PatternsText(), BuildsText(), resolution);
}

bool IsChecked(const Header& header) {
    for (const Build& build : ParseBuilds(BuildsText())) {
        if (build.timestamp == header.timestamp && build.imageSize == header.imageSize && !build.label.empty()) {
            return true;
        }
    }
    return false;
}

std::string BuildSection(const Resolution& resolution, const std::string& label) {
    std::string out = "build 0x";
    char stamp[16];
    std::snprintf(stamp, sizeof(stamp), "%08X", resolution.header.timestamp);
    out += stamp;
    out += " size=" + HexText(resolution.header.imageSize) + " " + label + "\n";
    for (const Entry& entry : resolution.entries) {
        if (entry.status == Status::Found || entry.status == Status::Absent) {
            out += entry.name + " " + EntryText(&entry) + "\n";
        } else {
            out += "# " + entry.name + " " + EntryText(&entry) + ": find it by hand, or write none\n";
        }
    }
    return out;
}

std::string Compare(const Resolution& left, const Resolution& right) {
    // A search cannot tell a site the build lacks from one it failed to
    // find, so a listed none agrees with a miss.
    auto nothing = [](const Entry& entry) {
        return entry.status == Status::Absent || entry.status == Status::Missing;
    };
    std::string out;
    for (const Entry& entry : left.entries) {
        const Entry* other = right.Find(entry.name);
        const bool agree = other && ((nothing(entry) && nothing(*other)) ||
                                     (other->status == entry.status && other->rva == entry.rva));
        if (!agree) {
            out += entry.name + " " + EntryText(&entry) + " " + EntryText(other) + "\n";
        }
    }
    for (const Entry& entry : right.entries) {
        if (!left.Find(entry.name)) {
            out += entry.name + " unlisted " + EntryText(&entry) + "\n";
        }
    }
    return out;
}

std::string FieldProblem(const Resolution& resolution, const std::string& name, uint64_t compiled) {
    const Entry* entry = resolution.Find(name);
    if (!entry || entry->status != Status::Found) {
        return "Game field " + name + " was not found in this game build.";
    }
    if (entry->rva == compiled) {
        return std::string();
    }
    char text[160];
    std::snprintf(text, sizeof(text), "Game field %s is 0x%llX in this game build, not 0x%llX.", name.c_str(),
                  static_cast<unsigned long long>(entry->rva), static_cast<unsigned long long>(compiled));
    return text;
}

BuildReview ReviewBuild(
    const Image& image, const std::string& patterns, const std::string& builds, const std::string& label,
    const Image* previous, const std::vector<std::string>& none
) {
    BuildReview review;
    Header header;
    Resolution listed;
    if (!ReadHeader(image, header)) {
        review.error = "not a 64-bit Windows executable";
        return review;
    }
    if (LookUp(header, patterns, builds, listed)) {
        review.listed = true;
        review.error = "game_builds.txt already lists this build as " + listed.build;
        return review;
    }

    // The build compared with: previous's, or the newest listed.
    Resolution compared;
    Header comparedHeader;
    if (previous) {
        if (!ReadHeader(*previous, comparedHeader) || !LookUp(comparedHeader, patterns, builds, compared)) {
            review.error = "the previous exe is not a build game_builds.txt lists";
            return review;
        }
    } else {
        bool any = false;
        for (const Build& build : ParseBuilds(builds)) {
            if (!build.label.empty() && (!any || build.timestamp > comparedHeader.timestamp)) {
                comparedHeader.timestamp = build.timestamp;
                comparedHeader.imageSize = build.imageSize;
                any = true;
            }
        }
        if (any) {
            LookUp(comparedHeader, patterns, builds, compared);
        }
    }
    review.against = compared.build;

    review.searched = Search(image, patterns);
    const Resolution previousSearch = previous ? Search(*previous, patterns) : Resolution{};
    Layout layout;
    Layout previousLayout;
    const bool lengths = previous && BuildLayout(image, layout) && BuildLayout(*previous, previousLayout);
    for (Entry& entry : review.searched.entries) {
        const Entry* before = compared.Find(entry.name);
        const bool givenNone = std::find(none.begin(), none.end(), entry.name) != none.end();
        if (entry.status == Status::Missing && (givenNone || (before && before->status == Status::Absent))) {
            entry.status = Status::Absent;
            review.absent.push_back(entry.name);
            continue;
        }
        if (entry.status == Status::Missing || entry.status == Status::Conflict) {
            review.blocking.push_back(entry.name);
            continue;
        }
        if (entry.status != Status::Found) {
            continue;
        }
        const Entry* searchedBefore = previous ? previousSearch.Find(entry.name) : nullptr;
        if (entry.hits < (searchedBefore ? searchedBefore->hits : entry.lines)) {
            review.partial.push_back(entry.name);
        }
        if (!before || before->status != Status::Found) {
            continue;
        }
        if (entry.field) {
            if (before->rva != entry.rva) {
                review.fields.push_back({entry.name, before->rva, entry.rva});
            }
            continue;
        }
        ++(before->rva == entry.rva ? review.unchanged : review.moved);
        if (lengths && layout.InCode(static_cast<int64_t>(entry.rva)) &&
            previousLayout.InCode(static_cast<int64_t>(before->rva))) {
            const uint32_t length = layout.RangeLength(static_cast<int64_t>(entry.rva));
            const uint32_t previousLength = previousLayout.RangeLength(static_cast<int64_t>(before->rva));
            if (length != previousLength) {
                review.resized.push_back({entry.name, previousLength, length});
            }
        }
    }
    if (review.blocking.empty()) {
        review.section = BuildSection(review.searched, label);
    }
    return review;
}

const std::string& PatternsText() {
    static const std::string text = Text(data::kPatterns, data::kPatternsSize);
    return text;
}

const std::string& BuildsText() {
    static const std::string text = Text(data::kBuilds, data::kBuildsSize);
    return text;
}

}  // namespace game_addresses
