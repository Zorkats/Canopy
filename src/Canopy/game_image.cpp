#define NOMINMAX
#include "game_image.h"

#include <windows.h>

#include <cstring>
#include <vector>

#include "core.h"

namespace game_image {
namespace {

using core::g_ModuleBase;

// One pattern byte; wildcard matches any.
struct PatternByte {
    uint8_t value;
    bool wildcard;
};

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// Splits pattern at spaces into bytes. Null on success, else why not.
const char* Parse(const char* pattern, std::vector<PatternByte>& bytes) {
    for (const char* c = pattern; *c != '\0';) {
        if (*c == ' ' || *c == '\t') {
            ++c;
            continue;
        }
        const char* token = c;
        while (*c != '\0' && *c != ' ' && *c != '\t') {
            ++c;
        }
        const size_t length = static_cast<size_t>(c - token);
        if ((length == 1 && token[0] == '?') || (length == 2 && token[0] == '?' && token[1] == '?')) {
            bytes.push_back({0, true});
        } else if (length == 2 && HexDigit(token[0]) >= 0 && HexDigit(token[1]) >= 0) {
            bytes.push_back({static_cast<uint8_t>(HexDigit(token[0]) * 16 + HexDigit(token[1])), false});
        } else {
            return "every byte must be two hex digits, or ? or ?? for any byte";
        }
    }
    if (bytes.empty()) {
        return "it is empty";
    }
    for (const PatternByte& byte : bytes) {
        if (!byte.wildcard) {
            return nullptr;
        }
    }
    return "it needs at least one byte that is not a wildcard";
}

const IMAGE_NT_HEADERS64* Headers() {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_ModuleBase);
    return reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_ModuleBase + dos->e_lfanew);
}

// The first match in [begin, end), or null. Scans for the first fixed byte,
// then compares the rest.
const uint8_t* Search(const uint8_t* begin, const uint8_t* end, const std::vector<PatternByte>& bytes) {
    size_t anchor = 0;
    while (bytes[anchor].wildcard) {
        ++anchor;
    }
    if (static_cast<size_t>(end - begin) < bytes.size()) {
        return nullptr;
    }
    const uint8_t* last = end - bytes.size();
    for (const uint8_t* start = begin; start <= last;) {
        const void* hit = std::memchr(start + anchor, bytes[anchor].value, static_cast<size_t>(last - start) + 1);
        if (!hit) {
            return nullptr;
        }
        const uint8_t* candidate = static_cast<const uint8_t*>(hit) - anchor;
        bool matched = true;
        for (size_t index = 0; index < bytes.size(); ++index) {
            if (!bytes[index].wildcard && candidate[index] != bytes[index].value) {
                matched = false;
                break;
            }
        }
        if (matched) {
            return candidate;
        }
        start = candidate + 1;
    }
    return nullptr;
}

}  // namespace

uint32_t BuildId() {
    return Headers()->FileHeader.TimeDateStamp;
}

uintptr_t FindPattern(const char* pattern, const char*& reason) {
    reason = nullptr;
    if (!pattern) {
        reason = "it is null";
        return 0;
    }
    std::vector<PatternByte> bytes;
    if ((reason = Parse(pattern, bytes)) != nullptr) {
        return 0;
    }
    const IMAGE_NT_HEADERS64* headers = Headers();
    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(headers);
    for (WORD index = 0; index < headers->FileHeader.NumberOfSections; ++index, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
            continue;
        }
        const auto* begin = reinterpret_cast<const uint8_t*>(g_ModuleBase + section->VirtualAddress);
        if (const uint8_t* match = Search(begin, begin + section->Misc.VirtualSize, bytes)) {
            return reinterpret_cast<uintptr_t>(match);
        }
    }
    return 0;
}

}  // namespace game_image
