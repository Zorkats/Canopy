#include "disabled_mods.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>

#include "platform_files.h"

namespace disabled_mods {
namespace {

// A list this long is not one a person wrote. Larger files are not read.
constexpr uint64_t kMaxFileBytes = 1024 * 1024;

std::string ListPath(const std::string& modsDir) {
    std::string folder = modsDir;
    while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/')) {
        folder.pop_back();
    }
    return platform::Join(platform::Join(folder, "CanopyData"), "disabled.txt");
}

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

std::string Trim(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && IsSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && IsSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// The text as wide characters: UTF-8 when it is valid, else the system code
// page, which is what a folder name from the A functions is in.
std::wstring ToWide(const std::string& text) {
    std::wstring wide;
    if (platform::Utf8ToWide(text, wide)) {
        return wide;
    }
    return platform::AnsiToWide(text);
}

bool SameName(const std::string& left, const std::string& right) {
    const std::wstring wideLeft = ToWide(left);
    const std::wstring wideRight = ToWide(right);
    if (wideLeft.empty() || wideRight.empty()) {
        return false;
    }
    return platform::EqualNoCase(wideLeft, wideRight);
}

// False when the file is missing, too large or could not be read.
bool ReadText(const std::string& path, std::string& text) {
    return platform::ReadWholeFile(path, kMaxFileBytes, text);
}

// What Cached compares to know the file changed.
struct Stamp {
    bool exists = false;
    uint64_t size = 0;
    uint64_t written = 0;

    bool operator==(const Stamp& other) const {
        return exists == other.exists && size == other.size && written == other.written;
    }
};

Stamp StampOf(const std::string& path) {
    Stamp stamp;
    const platform::FileInfo info = platform::Stat(path);
    if (!info.exists || info.folder) {
        return stamp;
    }
    stamp.exists = true;
    stamp.size = info.size;
    stamp.written = info.written;
    return stamp;
}

struct CacheEntry {
    Stamp stamp;
    std::vector<std::string> list;
};

std::mutex g_cacheLock;
std::map<std::string, CacheEntry> g_cache;

std::string LowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

bool EndsWithDll(const std::string& name) {
    return name.size() > 4 && platform::CompareNoCase(name.c_str() + name.size() - 4, ".dll") == 0;
}

}  // namespace

std::vector<std::string> Parse(const std::string& text) {
    std::vector<std::string> names;
    size_t start = 0;
    // A byte order mark belongs to the file, not to the first name.
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        start = 3;
    }
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = Trim(text.substr(start, end - start));
        if (!line.empty() && line[0] != '#') {
            names.push_back(line);
        }
        start = end + 1;
    }
    return names;
}

std::vector<std::string> Read(const std::string& modsDir) {
    std::string text;
    if (!ReadText(ListPath(modsDir), text)) {
        return {};
    }
    return Parse(text);
}

bool IsDisabled(const std::vector<std::string>& list, const std::string& name) {
    if (name.empty()) {
        return false;
    }
    return std::any_of(list.begin(), list.end(), [&](const std::string& listed) {
        return SameName(listed, name);
    });
}

std::vector<std::string> Cached(const std::string& modsDir) {
    const std::string path = ListPath(modsDir);
    const std::string key = LowerAscii(path);
    const Stamp stamp = StampOf(path);
    std::lock_guard<std::mutex> lock(g_cacheLock);
    const auto found = g_cache.find(key);
    if (found != g_cache.end() && found->second.stamp == stamp) {
        return found->second.list;
    }
    std::vector<std::string> list;
    if (stamp.exists) {
        std::string text;
        if (!ReadText(path, text)) {
            // Busy or half written: nothing is off for now, and the next call tries again.
            g_cache.erase(key);
            return {};
        }
        list = Parse(text);
    }
    g_cache[key] = {stamp, list};
    return list;
}

std::vector<std::string> Existing(const std::string& modsDir) {
    std::vector<std::string> names;
    const std::vector<std::string> list = Cached(modsDir);
    if (list.empty()) {
        return names;
    }
    std::string folder = modsDir;
    while (!folder.empty() && (folder.back() == '\\' || folder.back() == '/')) {
        folder.pop_back();
    }
    std::vector<platform::FolderEntry> entries;
    if (!platform::ListFolder(folder, entries)) {
        return names;
    }
    for (const platform::FolderEntry& entry : entries) {
        if ((entry.folder || EndsWithDll(entry.name)) && IsDisabled(list, entry.name)) {
            names.push_back(entry.name);
        }
    }
    std::sort(names.begin(), names.end(), [](const std::string& left, const std::string& right) {
        return platform::CompareNoCase(left.c_str(), right.c_str()) < 0;
    });
    return names;
}

}  // namespace disabled_mods
