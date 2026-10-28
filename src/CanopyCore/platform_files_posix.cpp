#include "platform_files.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <system_error>

// The POSIX side of platform_files.h. Narrow paths and narrow text are
// UTF-8; wide text is UTF-32. Every call that opens or tests an existing
// path finds it through a PathLookup of its own first, so a path written on
// Windows, with '\' and any case, reaches the file the game under Wine
// would read.
namespace platform {
namespace {

static_assert(sizeof(wchar_t) == 4, "wide text is UTF-32 here");

constexpr uint32_t kReplacement = 0xFFFD;
// One pread call of ReadOnlyFile::ReadAt reads at most this much, as one
// ReadFile call does on Windows.
constexpr size_t kReadChunk = size_t(1) << 26;

// The code point at text[at], or false when no valid sequence starts there.
// length is how many bytes the sequence takes when valid.
bool Decode(std::string_view text, size_t at, uint32_t& point, size_t& length) {
    const auto lead = static_cast<unsigned char>(text[at]);
    size_t extra = 0;
    uint32_t least = 0;
    if (lead < 0x80) {
        point = lead;
        length = 1;
        return true;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        extra = 1;
        point = lead & 0x1Fu;
        least = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        extra = 2;
        point = lead & 0x0Fu;
        least = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        extra = 3;
        point = lead & 0x07u;
        least = 0x10000;
    } else {
        return false;
    }
    if (text.size() - at <= extra) {
        return false;
    }
    for (size_t index = 1; index <= extra; ++index) {
        const auto next = static_cast<unsigned char>(text[at + index]);
        if ((next & 0xC0u) != 0x80u) {
            return false;
        }
        point = (point << 6) | (next & 0x3Fu);
    }
    if (point < least || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) {
        return false;
    }
    length = extra + 1;
    return true;
}

bool IsScalar(uint32_t point) {
    return point <= 0x10FFFF && (point < 0xD800 || point > 0xDFFF);
}

void Encode(uint32_t point, std::string& out) {
    if (point < 0x80) {
        out += static_cast<char>(point);
    } else if (point < 0x800) {
        out += static_cast<char>(0xC0 | (point >> 6));
        out += static_cast<char>(0x80 | (point & 0x3F));
    } else if (point < 0x10000) {
        out += static_cast<char>(0xE0 | (point >> 12));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (point >> 18));
        out += static_cast<char>(0x80 | ((point >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
    }
}

// The simple uppercase of c for the scripts names are written in, close to
// the table CompareStringOrdinal folds with.
uint32_t Fold(uint32_t c) {
    if (c >= 'a' && c <= 'z') {
        return c - 0x20;
    }
    if (c < 0x80) {
        return c;
    }
    // Latin-1.
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
        return c - 0x20;
    }
    if (c == 0xFF) {
        return 0x178;
    }
    // Latin Extended-A: pairs of upper then lower.
    if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) {
        return c & ~1u;
    }
    if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {
        return (c & 1u) ? c : c - 1;
    }
    // Greek.
    if (c == 0x3C2) {
        return 0x3A3;
    }
    if (c >= 0x3B1 && c <= 0x3CB) {
        return c - 0x20;
    }
    // Cyrillic.
    if (c >= 0x430 && c <= 0x44F) {
        return c - 0x20;
    }
    if (c >= 0x450 && c <= 0x45F) {
        return c - 0x50;
    }
    // Fullwidth Latin.
    if (c >= 0xFF41 && c <= 0xFF5A) {
        return c - 0x20;
    }
    return c;
}

int LowerAscii(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

std::string Reason(int code) {
    return "error " + std::to_string(code) + " (" + std::system_category().message(code) + ")";
}

std::string Narrow(const std::wstring& path) {
    return WideToUtf8(path);
}

// path as found on disk, through a lookup of its own.
std::string Found(const std::string& path) {
    PathLookup lookup;
    return lookup.Find(path);
}

std::string Found(const std::wstring& path) {
    return Found(Narrow(path));
}

bool StatPath(const std::string& path, struct stat& data) {
    return !path.empty() && ::stat(path.c_str(), &data) == 0;
}

FileInfo FromStat(const std::string& path) {
    FileInfo info;
    struct stat data = {};
    if (!StatPath(path, data)) {
        return info;
    }
    info.exists = true;
    info.folder = S_ISDIR(data.st_mode);
    info.size = info.folder ? 0 : static_cast<uint64_t>(data.st_size);
    info.written = static_cast<uint64_t>(data.st_mtim.tv_sec) * 1000000000ull + static_cast<uint64_t>(data.st_mtim.tv_nsec);
    return info;
}

int OpenRead(const std::string& path) {
    int file = -1;
    do {
        file = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    } while (file < 0 && errno == EINTR);
    return file;
}

void CloseFile(int file) {
    if (file >= 0) {
        ::close(file);
    }
}

// Reads up to size bytes into output from the file's position, stopping at
// its end. False on a read error.
bool ReadUpTo(int file, uint8_t* output, size_t size, size_t& done) {
    done = 0;
    while (done < size) {
        const ssize_t read = ::read(file, output + done, std::min(size - done, kReadChunk));
        if (read < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (read == 0) {
            break;
        }
        done += static_cast<size_t>(read);
    }
    return true;
}

// The whole regular file at path (already found) into buffer, which
// Resize sizes. False when it is missing, not a file, over maxBytes or
// could not be read.
template <typename Buffer>
bool ReadAll(const std::string& path, uint64_t maxBytes, Buffer& buffer) {
    buffer.clear();
    const int file = OpenRead(path);
    if (file < 0) {
        return false;
    }
    struct stat data = {};
    bool ok = ::fstat(file, &data) == 0 && S_ISREG(data.st_mode) && data.st_size >= 0 &&
              static_cast<uint64_t>(data.st_size) <= maxBytes;
    if (ok) {
        buffer.resize(static_cast<size_t>(data.st_size));
        size_t done = 0;
        ok = buffer.empty() ||
             (ReadUpTo(file, reinterpret_cast<uint8_t*>(&buffer[0]), buffer.size(), done) && done == buffer.size());
    }
    CloseFile(file);
    if (!ok) {
        buffer.clear();
    }
    return ok;
}

// The names in folder, sorted, so a lookup that matches two of them without
// case picks the same one every time.
std::vector<std::string> Names(const std::string& folder) {
    std::vector<std::string> names;
    DIR* listing = ::opendir(folder.empty() ? "." : folder.c_str());
    if (!listing) {
        return names;
    }
    while (const dirent* entry = ::readdir(listing)) {
        const char* name = entry->d_name;
        if (std::strcmp(name, ".") != 0 && std::strcmp(name, "..") != 0) {
            names.emplace_back(name);
        }
    }
    ::closedir(listing);
    std::sort(names.begin(), names.end());
    return names;
}

bool SameNameNoCase(const std::string& left, const std::string& right) {
    if (left.size() == right.size() && CompareNoCase(left.c_str(), right.c_str()) == 0) {
        return true;
    }
    return EqualNoCase(Utf8ToWideLossy(left), Utf8ToWideLossy(right));
}

}  // namespace

// ---- Text ----------------------------------------------------------------

std::string WideToUtf8(const std::wstring& text) {
    std::string result;
    result.reserve(text.size());
    for (const wchar_t c : text) {
        const auto point = static_cast<uint32_t>(c);
        Encode(IsScalar(point) ? point : kReplacement, result);
    }
    return result;
}

bool Utf8ToWide(std::string_view text, std::wstring& wide) {
    wide.clear();
    std::wstring result;
    result.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        uint32_t point = 0;
        size_t length = 0;
        if (!Decode(text, at, point, length)) {
            return false;
        }
        result += static_cast<wchar_t>(point);
        at += length;
    }
    wide = std::move(result);
    return true;
}

std::wstring Utf8ToWideLossy(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        uint32_t point = 0;
        size_t length = 0;
        if (!Decode(text, at, point, length)) {
            point = kReplacement;
            length = 1;
        }
        result += static_cast<wchar_t>(point);
        at += length;
    }
    return result;
}

bool IsUtf8(std::string_view text) {
    size_t at = 0;
    while (at < text.size()) {
        uint32_t point = 0;
        size_t length = 0;
        if (!Decode(text, at, point, length)) {
            return false;
        }
        at += length;
    }
    return true;
}

std::wstring AnsiToWide(std::string_view text) {
    return Utf8ToWideLossy(text);
}

bool WideToAnsi(const std::wstring& text, std::string& ansi) {
    ansi.clear();
    for (const wchar_t c : text) {
        if (!IsScalar(static_cast<uint32_t>(c))) {
            ansi.clear();
            return false;
        }
    }
    ansi = WideToUtf8(text);
    return true;
}

int CompareNoCase(const char* left, const char* right) {
    const auto* a = reinterpret_cast<const unsigned char*>(left);
    const auto* b = reinterpret_cast<const unsigned char*>(right);
    while (true) {
        const int difference = LowerAscii(*a) - LowerAscii(*b);
        if (difference != 0 || *a == 0) {
            return difference;
        }
        ++a;
        ++b;
    }
}

int CompareNoCase(const char* left, const char* right, size_t count) {
    const auto* a = reinterpret_cast<const unsigned char*>(left);
    const auto* b = reinterpret_cast<const unsigned char*>(right);
    for (size_t index = 0; index < count; ++index) {
        const int difference = LowerAscii(a[index]) - LowerAscii(b[index]);
        if (difference != 0 || a[index] == 0) {
            return difference;
        }
    }
    return 0;
}

bool EqualNoCase(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t index = 0; index < left.size(); ++index) {
        if (Fold(static_cast<uint32_t>(left[index])) != Fold(static_cast<uint32_t>(right[index]))) {
            return false;
        }
    }
    return true;
}

// ---- Paths ---------------------------------------------------------------

std::string PathLookup::Find(const std::string& path) {
    const std::string native = NativeSeparators(path);
    struct stat data = {};
    if (native.empty() || ::lstat(native.c_str(), &data) == 0) {
        return native;
    }
    // Walk the parts, keeping each that exists as written and matching the
    // first that does not against its folder's entries.
    std::string result = native[0] == '/' ? "/" : "";
    size_t start = 0;
    while (start < native.size()) {
        size_t end = native.find('/', start);
        if (end == std::string::npos) {
            end = native.size();
        }
        const std::string part = native.substr(start, end - start);
        const bool last = end == native.size();
        start = end + 1;
        if (part.empty()) {
            continue;
        }
        const std::string& folder = result;
        std::string next = folder.empty() || folder.back() == '/' ? folder + part : folder + "/" + part;
        if (part != "." && part != ".." && ::lstat(next.c_str(), &data) != 0) {
            const std::string key = folder.empty() ? "." : folder;
            auto listed = folders_.find(key);
            if (listed == folders_.end()) {
                listed = folders_.emplace(key, Names(folder)).first;
            }
            const auto match = std::find_if(listed->second.begin(), listed->second.end(), [&](const std::string& name) {
                return SameNameNoCase(name, part);
            });
            if (match == listed->second.end()) {
                // Nothing here: this part and the rest stay as written.
                return next + (last ? std::string() : native.substr(end));
            }
            next = folder.empty() || folder.back() == '/' ? folder + *match : folder + "/" + *match;
        }
        result = next;
    }
    if (native.back() == '/' && (result.empty() || result.back() != '/')) {
        result += '/';
    }
    return result;
}

std::wstring PathLookup::Find(const std::wstring& path) {
    const std::string narrow = Narrow(path);
    const std::string found = Find(narrow);
    if (found == narrow) {
        return path;
    }
    return Utf8ToWideLossy(found);
}

// ---- Files ---------------------------------------------------------------

FileInfo Stat(const std::wstring& path) {
    return FromStat(Found(path));
}

FileInfo Stat(const std::string& path) {
    return FromStat(Found(path));
}

bool Exists(const std::string& path) {
    return Stat(path).exists;
}

bool IsFile(const std::wstring& path) {
    const FileInfo info = Stat(path);
    return info.exists && !info.folder;
}

bool IsFile(const std::string& path) {
    const FileInfo info = Stat(path);
    return info.exists && !info.folder;
}

bool IsFolder(const std::wstring& path) {
    const FileInfo info = Stat(path);
    return info.exists && info.folder;
}

bool IsFolder(const std::string& path) {
    const FileInfo info = Stat(path);
    return info.exists && info.folder;
}

bool LocalWriteTime(const std::wstring& path, LocalTime& time) {
    struct stat data = {};
    if (!StatPath(Found(path), data)) {
        return false;
    }
    const std::time_t written = data.st_mtime;
    std::tm parts = {};
    if (!::localtime_r(&written, &parts)) {
        return false;
    }
    time.year = parts.tm_year + 1900;
    time.month = parts.tm_mon + 1;
    time.day = parts.tm_mday;
    time.hour = parts.tm_hour;
    time.minute = parts.tm_min;
    time.second = parts.tm_sec;
    return true;
}

bool ReadWholeFile(const std::string& path, uint64_t maxBytes, std::string& text) {
    return ReadAll(Found(path), maxBytes, text);
}

bool ReadWholeFile(const std::wstring& path, uint64_t maxBytes, std::vector<uint8_t>& bytes) {
    return ReadAll(Found(path), maxBytes, bytes);
}

bool ReadFileStart(const std::wstring& path, size_t count, std::vector<uint8_t>& bytes) {
    bytes.clear();
    const int file = OpenRead(Found(path));
    if (file < 0) {
        return false;
    }
    bytes.resize(count);
    size_t done = 0;
    const bool ok = ReadUpTo(file, bytes.data(), count, done);
    CloseFile(file);
    bytes.resize(ok ? done : 0);
    return true;
}

bool WriteWholeFile(const std::wstring& path, const void* data, size_t size, std::string& error) {
    const std::string target = Found(path);
    int file = -1;
    do {
        file = ::open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    } while (file < 0 && errno == EINTR);
    if (file < 0) {
        error = "the file could not be created: " + Reason(errno);
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    bool ok = true;
    while (ok && done < size) {
        const ssize_t written = ::write(file, bytes + done, std::min(size - done, kReadChunk));
        if (written < 0 && errno == EINTR) {
            continue;
        }
        ok = written > 0;
        if (!ok) {
            error = "the file could not be written: " + Reason(written < 0 ? errno : ENOSPC);
        } else {
            done += static_cast<size_t>(written);
        }
    }
    if (::close(file) != 0 && ok) {
        error = "the file could not be written: " + Reason(errno);
        ok = false;
    }
    if (!ok) {
        ::unlink(target.c_str());
    }
    return ok;
}

bool RenameOver(const std::wstring& from, const std::wstring& to) {
    return ::rename(Found(from).c_str(), Found(to).c_str()) == 0;
}

bool RemoveFile(const std::wstring& path) {
    return ::unlink(Found(path).c_str()) == 0;
}

bool CreateFolders(const std::wstring& path) {
    const std::string target = Found(path);
    if (target.empty()) {
        return false;
    }
    // Each folder from the top down; one that is there already is fine.
    for (size_t slash = target.find('/', 1); ; slash = target.find('/', slash + 1)) {
        const std::string folder = target.substr(0, slash);
        if (!folder.empty() && ::mkdir(folder.c_str(), 0777) != 0 && errno != EEXIST) {
            break;
        }
        if (slash == std::string::npos) {
            break;
        }
    }
    struct stat data = {};
    return StatPath(target, data) && S_ISDIR(data.st_mode);
}

bool ListFolder(const std::string& folder, std::vector<FolderEntry>& entries) {
    entries.clear();
    const std::string found = Found(folder);
    DIR* listing = ::opendir(found.empty() ? "." : found.c_str());
    if (!listing) {
        return false;
    }
    while (const dirent* entry = ::readdir(listing)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        bool isFolder = entry->d_type == DT_DIR;
        if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
            struct stat data = {};
            isFolder = StatPath(Join(found, name), data) && S_ISDIR(data.st_mode);
        }
        entries.push_back({name, isFolder});
    }
    ::closedir(listing);
    return true;
}

// ---- ReadOnlyFile ----------------------------------------------------------

struct ReadOnlyFile::Handle {
    int file = -1;

    ~Handle() {
        CloseFile(file);
    }
};

ReadOnlyFile::ReadOnlyFile() = default;
ReadOnlyFile::~ReadOnlyFile() = default;

bool ReadOnlyFile::Open(const std::wstring& path, const std::string& name, std::string& error) {
    Close();
    auto handle = std::make_unique<Handle>();
    handle->file = OpenRead(Found(path));
    if (handle->file < 0) {
        error = "cannot open " + name + ": " + Reason(errno);
        return false;
    }
    struct stat data = {};
    if (::fstat(handle->file, &data) != 0) {
        error = "cannot size " + name + ": " + Reason(errno);
        return false;
    }
    if (S_ISDIR(data.st_mode)) {
        error = "cannot open " + name + ": " + Reason(EISDIR);
        return false;
    }
    size_ = static_cast<uint64_t>(data.st_size);
    handle_ = std::move(handle);
    return true;
}

void ReadOnlyFile::Close() {
    handle_.reset();
    size_ = 0;
}

bool ReadOnlyFile::IsOpen() const {
    return handle_ != nullptr;
}

uint64_t ReadOnlyFile::Size() const {
    return size_;
}

// pread takes the offset itself, so no shared file position is used and
// threads may read at once.
bool ReadOnlyFile::ReadAt(uint64_t offset, uint8_t* output, size_t size, std::string& error) const {
    if (!handle_) {
        error = "the file is not open";
        return false;
    }
    while (size > 0) {
        const size_t chunk = std::min(size, kReadChunk);
        size_t done = 0;
        while (done < chunk) {
            const ssize_t read =
                ::pread(handle_->file, output + done, chunk - done, static_cast<off_t>(offset + done));
            if (read < 0 && errno == EINTR) {
                continue;
            }
            if (read < 0) {
                error = "read of " + std::to_string(chunk) + " bytes at " + std::to_string(offset) +
                        " failed: " + Reason(errno);
                return false;
            }
            if (read == 0) {
                break;
            }
            done += static_cast<size_t>(read);
        }
        if (done != chunk) {
            error = "read of " + std::to_string(chunk) + " bytes at " + std::to_string(offset) + " returned " +
                    std::to_string(done);
            return false;
        }
        output += done;
        offset += done;
        size -= done;
    }
    return true;
}

}  // namespace platform
