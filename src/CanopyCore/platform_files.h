#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The file calls and text conversions CanopyCore needs, declared once, so no
// other core file calls Windows or POSIX itself. platform_files_win.cpp wraps
// exactly the Win32 calls the core made before, so Windows behaves as it
// always has; platform_files_posix.cpp does the same with POSIX calls.
//
// Paths come in two forms, as before. A wide path (std::wstring) is UTF-16
// on Windows and UTF-32 elsewhere. A narrow path (std::string) is the form
// the loader's rules open files with: the ANSI code page on Windows, UTF-8
// elsewhere.
//
// On POSIX a '\' in a path is a separator, like '/', because game paths and
// mods' canopy.ini files are written on Windows. A path that does not exist
// as written is looked up again with each part matched without case against
// its folder's entries, the way Windows, and the game under Wine, find
// Data\Foo.DAT whatever its case. PathLookup does that lookup; every call
// below that opens or tests an existing path goes through it.
namespace platform {

// ---- Text ----------------------------------------------------------------

// UTF-16 or UTF-32 as UTF-8. A code unit that is not a whole character
// becomes U+FFFD.
std::string WideToUtf8(const std::wstring& text);
// UTF-8 as wide text. False, with wide cleared, when text is not valid UTF-8.
bool Utf8ToWide(std::string_view text, std::wstring& wide);
// UTF-8 as wide text, a byte that is not part of a valid sequence as U+FFFD.
std::wstring Utf8ToWideLossy(std::string_view text);
// True when text is valid UTF-8. Empty text is.
bool IsUtf8(std::string_view text);
// Narrow (ANSI) text as wide text. Windows: the ANSI code page. POSIX:
// UTF-8, a bad byte as U+FFFD.
std::wstring AnsiToWide(std::string_view text);
// Wide text as narrow (ANSI) text. False when a character has no narrow
// form: Windows refuses best-fit replacements.
bool WideToAnsi(const std::wstring& text, std::string& ansi);

// Ordinal comparisons of ASCII letters without case, as _stricmp and
// _strnicmp do: negative, zero or positive.
int CompareNoCase(const char* left, const char* right);
int CompareNoCase(const char* left, const char* right, size_t count);
// True when the two names are the same without case. Windows:
// CompareStringOrdinal. POSIX: simple case folding of Latin, Greek and
// Cyrillic letters, enough for the names mods and players write.
bool EqualNoCase(const std::wstring& left, const std::wstring& right);

// ---- Paths ---------------------------------------------------------------

// The separator a joined path uses: '\' on Windows, '/' elsewhere.
#ifdef _WIN32
constexpr char kSeparator = '\\';
constexpr wchar_t kWideSeparator = L'\\';
#else
constexpr char kSeparator = '/';
constexpr wchar_t kWideSeparator = L'/';
#endif

// The longest path the Windows calls the loader makes take, counted in wide
// characters. A desktop check refuses what the loader would refuse.
constexpr size_t kWindowsMaxPath = 260;

// True for '\' and '/', the separators every platform here reads.
bool IsSeparator(wchar_t c);
// folder, the platform's separator, then name: always one separator added,
// as the core's paths have always been built.
std::string Join(const std::string& folder, const std::string& name);
std::wstring Join(const std::wstring& folder, const std::wstring& name);
// Every '/' and '\' in path as the platform's separator.
std::string NativeSeparators(std::string path);
std::wstring NativeSeparators(std::wstring path);

// Finds paths the way Windows does: by the exact name first, then, on
// POSIX, with each part matched without case against its folder's entries.
// The object remembers each folder it listed, so a caller that finds many
// paths in one call lists each folder once. Make one per call, not one for
// good: what it remembers goes stale when files change. On Windows, where
// the file system ignores case already, Find returns the path as given.
class PathLookup {
public:
    // The path as it exists on disk, with the platform's separators. From
    // the first part that matches nothing on, the parts stay as given, so
    // opening a missing file still fails, and a file that is to be made
    // lands in the folders that do exist.
    std::string Find(const std::string& path);
    std::wstring Find(const std::wstring& path);

private:
    std::map<std::string, std::vector<std::string>> folders_;
};

// ---- Files ---------------------------------------------------------------

// What Stat reads. written only compares with another written time of the
// same platform.
struct FileInfo {
    bool exists = false;
    bool folder = false;
    uint64_t size = 0;
    // Windows: the FILETIME. POSIX: nanoseconds since 1970.
    uint64_t written = 0;
};

FileInfo Stat(const std::wstring& path);
FileInfo Stat(const std::string& path);
// True when path is there, as a file or a folder.
bool Exists(const std::string& path);
bool IsFile(const std::wstring& path);
bool IsFile(const std::string& path);
bool IsFolder(const std::wstring& path);
bool IsFolder(const std::string& path);

// A time of day in the local time zone.
struct LocalTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};
// The file's last write time in local time. False when it cannot be read.
bool LocalWriteTime(const std::wstring& path, LocalTime& time);

// The whole file at path. False when it is missing, a folder, over
// maxBytes or could not be read.
bool ReadWholeFile(const std::string& path, uint64_t maxBytes, std::string& text);
bool ReadWholeFile(const std::wstring& path, uint64_t maxBytes, std::vector<uint8_t>& bytes);
// Up to count bytes from the start of the file, fewer when it is shorter.
// False when it cannot be opened.
bool ReadFileStart(const std::wstring& path, size_t count, std::vector<uint8_t>& bytes);

// Writes size bytes to path, replacing any file there. A file that could
// not be written whole is removed. error says why on failure.
bool WriteWholeFile(const std::wstring& path, const void* data, size_t size, std::string& error);
// Moves from over to, replacing a file at to, in one step where the file
// system allows it. (Not ReplaceFile: windows.h makes that name a macro.)
bool RenameOver(const std::wstring& from, const std::wstring& to);
// Removes the file at path. False when it is not there or cannot go.
bool RemoveFile(const std::wstring& path);
// Makes path and every folder above it that is missing. True when path is
// a folder afterwards.
bool CreateFolders(const std::wstring& path);

// One entry of a listed folder.
struct FolderEntry {
    std::string name;
    bool folder = false;
};
// Every entry of folder but "." and "..", in the order the file system
// gives them. False when folder cannot be listed.
bool ListFolder(const std::string& folder, std::vector<FolderEntry>& entries);

// A file opened for reading only, sharing read access, as the game opens
// its archives. ReadAt is positional, so once Open returns any number of
// threads may read at once. Messages are "cannot open <path>: <reason>"
// and the like, with the system's reason.
class ReadOnlyFile {
public:
    ReadOnlyFile();
    ~ReadOnlyFile();
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;

    // Opens path, closing whatever was open. name is the path as messages
    // show it.
    bool Open(const std::wstring& path, const std::string& name, std::string& error);
    void Close();
    bool IsOpen() const;
    uint64_t Size() const;
    // Reads exactly size bytes at offset.
    bool ReadAt(uint64_t offset, uint8_t* output, size_t size, std::string& error) const;

private:
    struct Handle;
    std::unique_ptr<Handle> handle_;
    uint64_t size_ = 0;
};

}  // namespace platform
