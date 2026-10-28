#include "platform_files.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

// The Windows side of platform_files.h: the Win32 calls CanopyCore made
// before it had a platform layer, moved here unchanged. Windows matches
// names without case itself, so PathLookup has nothing to do.
namespace platform {
namespace {

// One ReadFile call of ReadOnlyFile::ReadAt reads at most this much.
constexpr DWORD kReadChunk = 1u << 26;
// One ReadFile call of ReadWholeFile reads at most this much.
constexpr DWORD kWholeChunk = 1u << 30;

std::string WindowsError(DWORD code) {
    char* text = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), reinterpret_cast<char*>(&text), 0, nullptr
    );
    std::string message = "Windows error " + std::to_string(code);
    if (length > 0 && text) {
        std::string detail(text, length);
        while (!detail.empty() && (detail.back() == '\n' || detail.back() == '\r' || detail.back() == '.')) {
            detail.pop_back();
        }
        message += " (" + detail + ")";
    }
    if (text) {
        LocalFree(text);
    }
    return message;
}

std::wstring ToWide(UINT page, DWORD flags, std::string_view text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), &wide[0], length);
    return wide;
}

FileInfo FromAttributes(BOOL found, const WIN32_FILE_ATTRIBUTE_DATA& data) {
    FileInfo info;
    if (!found) {
        return info;
    }
    info.exists = true;
    info.folder = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    info.written =
        (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return info;
}

}  // namespace

// ---- Text ----------------------------------------------------------------

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], length, nullptr, nullptr);
    return result;
}

bool Utf8ToWide(std::string_view text, std::wstring& wide) {
    wide.clear();
    if (text.empty()) {
        return true;
    }
    wide = ToWide(CP_UTF8, MB_ERR_INVALID_CHARS, text);
    return !wide.empty();
}

std::wstring Utf8ToWideLossy(std::string_view text) {
    return ToWide(CP_UTF8, 0, text);
}

bool IsUtf8(std::string_view text) {
    return text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                               static_cast<int>(text.size()), nullptr, 0) > 0;
}

std::wstring AnsiToWide(std::string_view text) {
    return ToWide(CP_ACP, 0, text);
}

bool WideToAnsi(const std::wstring& text, std::string& ansi) {
    ansi.clear();
    if (text.empty()) {
        return true;
    }
    BOOL lossy = FALSE;
    const int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, &lossy);
    if (length <= 0 || lossy) {
        return false;
    }
    ansi.assign(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, text.data(), static_cast<int>(text.size()), &ansi[0], length,
                        nullptr, nullptr);
    return true;
}

int CompareNoCase(const char* left, const char* right) {
    return _stricmp(left, right);
}

int CompareNoCase(const char* left, const char* right, size_t count) {
    return _strnicmp(left, right, count);
}

bool EqualNoCase(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(
               left.c_str(), static_cast<int>(left.size()), right.c_str(), static_cast<int>(right.size()), TRUE
           ) == CSTR_EQUAL;
}

// ---- Paths ---------------------------------------------------------------

std::string PathLookup::Find(const std::string& path) {
    return path;
}

std::wstring PathLookup::Find(const std::wstring& path) {
    return path;
}

// ---- Files ---------------------------------------------------------------

FileInfo Stat(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    const BOOL found = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data);
    return FromAttributes(found, data);
}

FileInfo Stat(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    const BOOL found = GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data);
    return FromAttributes(found, data);
}

bool Exists(const std::string& path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsFile(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool IsFolder(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool IsFolder(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool LocalWriteTime(const std::wstring& path, LocalTime& time) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    FILETIME local = {};
    SYSTEMTIME parts = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) ||
        !FileTimeToLocalFileTime(&data.ftLastWriteTime, &local) || !FileTimeToSystemTime(&local, &parts)) {
        return false;
    }
    time.year = parts.wYear;
    time.month = parts.wMonth;
    time.day = parts.wDay;
    time.hour = parts.wHour;
    time.minute = parts.wMinute;
    time.second = parts.wSecond;
    return true;
}

bool ReadWholeFile(const std::string& path, uint64_t maxBytes, std::string& text) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > maxBytes) {
        return false;
    }
    file.seekg(0);
    text.assign(static_cast<size_t>(size), '\0');
    return size == 0 || static_cast<bool>(file.read(&text[0], size));
}

bool ReadWholeFile(const std::wstring& path, uint64_t maxBytes, std::vector<uint8_t>& bytes) {
    bytes.clear();
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && static_cast<uint64_t>(size.QuadPart) <= maxBytes;
    if (ok) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        while (ok && done < bytes.size()) {
            DWORD read = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, kWholeChunk));
            ok = ReadFile(file, bytes.data() + done, chunk, &read, nullptr) && read > 0;
            done += read;
        }
    }
    CloseHandle(file);
    if (!ok) {
        bytes.clear();
    }
    return ok;
}

bool ReadFileStart(const std::wstring& path, size_t count, std::vector<uint8_t>& bytes) {
    bytes.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    bytes.resize(count);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count));
    bytes.resize(static_cast<size_t>(file.gcount()));
    return true;
}

bool WriteWholeFile(const std::wstring& path, const void* data, size_t size, std::string& error) {
    HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "the file could not be created: " + WindowsError(GetLastError());
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t done = 0;
    bool ok = true;
    while (ok && done < size) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - done, kWholeChunk));
        ok = WriteFile(file, bytes + done, chunk, &written, nullptr) && written > 0;
        if (!ok) {
            error = "the file could not be written: " + WindowsError(GetLastError());
        }
        done += written;
    }
    if (!CloseHandle(file) && ok) {
        error = "the file could not be written: " + WindowsError(GetLastError());
        ok = false;
    }
    if (!ok) {
        DeleteFileW(path.c_str());
    }
    return ok;
}

bool RenameOver(const std::wstring& from, const std::wstring& to) {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

bool RemoveFile(const std::wstring& path) {
    return DeleteFileW(path.c_str()) != 0;
}

bool CreateFolders(const std::wstring& path) {
    std::error_code failed;
    std::filesystem::create_directories(std::filesystem::path(path), failed);
    return std::filesystem::is_directory(std::filesystem::path(path), failed);
}

bool ListFolder(const std::string& folder, std::vector<FolderEntry>& entries) {
    entries.clear();
    WIN32_FIND_DATAA found = {};
    HANDLE search = FindFirstFileA((folder + "\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        const std::string name = found.cFileName;
        if (name == "." || name == "..") {
            continue;
        }
        entries.push_back({name, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileA(search, &found));
    FindClose(search);
    return true;
}

// ---- ReadOnlyFile ----------------------------------------------------------

struct ReadOnlyFile::Handle {
    HANDLE file = INVALID_HANDLE_VALUE;

    ~Handle() {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
};

ReadOnlyFile::ReadOnlyFile() = default;
ReadOnlyFile::~ReadOnlyFile() = default;

bool ReadOnlyFile::Open(const std::wstring& path, const std::string& name, std::string& error) {
    Close();
    auto handle = std::make_unique<Handle>();
    handle->file = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (handle->file == INVALID_HANDLE_VALUE) {
        error = "cannot open " + name + ": " + WindowsError(GetLastError());
        return false;
    }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(handle->file, &size)) {
        error = "cannot size " + name + ": " + WindowsError(GetLastError());
        return false;
    }
    size_ = static_cast<uint64_t>(size.QuadPart);
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

// The offset rides in the OVERLAPPED block, so no shared file position is
// used and threads may read at once.
bool ReadOnlyFile::ReadAt(uint64_t offset, uint8_t* output, size_t size, std::string& error) const {
    if (!handle_) {
        error = "the file is not open";
        return false;
    }
    while (size > 0) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size, kReadChunk));
        OVERLAPPED overlapped = {};
        overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD read = 0;
        if (!::ReadFile(handle_->file, output, chunk, &read, &overlapped)) {
            error = "read of " + std::to_string(chunk) + " bytes at " + std::to_string(offset) +
                    " failed: " + WindowsError(GetLastError());
            return false;
        }
        if (read != chunk) {
            error = "read of " + std::to_string(chunk) + " bytes at " + std::to_string(offset) + " returned " +
                    std::to_string(read);
            return false;
        }
        output += read;
        offset += read;
        size -= read;
    }
    return true;
}

}  // namespace platform
