#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cwctype>
#include <fstream>

#include "archive.h"
#include "platform_files.h"
#include "tool_common.h"

// Lists and extracts files from the game's archives through CanopyCore, the
// same reader Canopy uses. Opens the game folder for reading only and never
// prints file contents, only paths, sizes and times.
//
//   canopy dump list <game folder> [prefix]
//   canopy dump extract [--unwrap] <game folder> <data path> <out file>
namespace canopy_tool {
namespace {

namespace fs = std::filesystem;

std::string Lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::wstring LowerWide(std::wstring text) {
    for (wchar_t& c : text) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return text;
}

std::string Time(uint64_t seconds) {
    const std::time_t value = static_cast<std::time_t>(seconds);
    std::tm parts = {};
#ifdef _WIN32
    if (gmtime_s(&parts, &value) != 0) {
        return "?";
    }
#else
    if (!gmtime_r(&value, &parts)) {
        return "?";
    }
#endif
    char text[32] = {};
    std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &parts);
    return text;
}

int Usage() {
    std::fprintf(
        stderr,
        "usage:\n"
        "  canopy dump list <game folder> [prefix]\n"
        "  canopy dump extract [--unwrap] <game folder> <data path> <out file>\n"
    );
    return 2;
}

bool OpenGame(const std::wstring& folder, archive::GameData& data) {
    std::string error;
    if (!data.Open(folder, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return false;
    }
    return true;
}

std::string ArchiveName(const archive::GameData& data, size_t index) {
    return Utf8(fs::path(data.ArchiveAt(index).Path()).filename().wstring());
}

int List(const std::wstring& folder, const std::string& prefix) {
    archive::GameData data;
    if (!OpenGame(folder, data)) {
        return 1;
    }
    const std::string folded = Lower(prefix);
    struct Row {
        const archive::Entry* entry;
        size_t archive;
    };
    std::vector<Row> rows;
    for (size_t index = 0; index < data.ArchiveCount(); ++index) {
        for (const archive::Entry& entry : data.ArchiveAt(index).Entries()) {
            if (entry.name.compare(0, folded.size(), folded) == 0) {
                rows.push_back({&entry, index});
            }
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
        return left.entry->name != right.entry->name ? left.entry->name < right.entry->name
                                                     : left.archive < right.archive;
    });
    for (const Row& row : rows) {
        // An entry an earlier archive also has is never read by the game.
        const bool shadowed = data.Find(row.entry->name).archive != row.archive;
        std::printf(
            "%12llu  %s  %-16s  %s%s\n", static_cast<unsigned long long>(row.entry->size),
            Time(row.entry->timestamp).c_str(), ArchiveName(data, row.archive).c_str(), row.entry->name.c_str(),
            shadowed ? "  (shadowed)" : ""
        );
    }
    std::printf("%zu entries in %zu archives\n", rows.size(), data.ArchiveCount());
    return 0;
}

int Extract(const std::wstring& folder, const std::string& gamePath, const std::wstring& outFile, bool unwrap) {
    if (InsideFolder(outFile, folder)) {
        std::fprintf(stderr, "error: refusing to write inside the game folder\n");
        return 1;
    }
    archive::GameData data;
    if (!OpenGame(folder, data)) {
        return 1;
    }
    const archive::Location location = data.Find(gamePath);
    if (!location.Found()) {
        std::fprintf(stderr, "error: %s is in no mounted archive\n", gamePath.c_str());
        return 1;
    }
    std::vector<uint8_t> bytes;
    std::string error;
    bool wrapped = false;
    const archive::Archive& source = data.ArchiveAt(location.archive);
    const bool read = unwrap ? source.Read(location.entry, bytes, error, &wrapped)
                             : source.ReadRaw(location.entry, bytes, error);
    if (!read) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }
    std::ofstream out(fs::path(outFile), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        std::fprintf(stderr, "error: cannot write %s\n", Utf8(outFile).c_str());
        return 1;
    }
    std::printf(
        "%s from %s: %zu bytes%s to %s\n", source.Entries()[location.entry].name.c_str(),
        ArchiveName(data, location.archive).c_str(), bytes.size(), wrapped ? " (YKCMP unpacked)" : "",
        Utf8(outFile).c_str()
    );
    return 0;
}

}  // namespace

std::string Utf8(const std::wstring& text) {
    return platform::WideToUtf8(text);
}

bool InsideFolder(const fs::path& path, const fs::path& folder) {
    std::error_code error;
    const std::wstring target = LowerWide(fs::weakly_canonical(fs::absolute(path, error), error).wstring());
    std::wstring root = LowerWide(fs::weakly_canonical(fs::absolute(folder, error), error).wstring());
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) {
        root.pop_back();
    }
    return target == root || (target.size() > root.size() && target.compare(0, root.size(), root) == 0 &&
                              (target[root.size()] == L'\\' || target[root.size()] == L'/'));
}

int RunDump(const std::vector<std::wstring>& input) {
    std::vector<std::wstring> args = input;
    if (args.empty()) {
        return Usage();
    }
    const std::wstring command = args[0];
    args.erase(args.begin());
    if (command == L"list" && (args.size() == 1 || args.size() == 2)) {
        return List(args[0], args.size() == 2 ? Utf8(args[1]) : std::string());
    }
    if (command == L"extract") {
        const bool unwrap = !args.empty() && args[0] == L"--unwrap";
        if (unwrap) {
            args.erase(args.begin());
        }
        if (args.size() == 3) {
            return Extract(args[0], Utf8(args[1]), args[2], unwrap);
        }
    }
    return Usage();
}

}  // namespace canopy_tool
