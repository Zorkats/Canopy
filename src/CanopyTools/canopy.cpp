#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "game_addresses.h"
#include "platform_files.h"
#include "rules_report.h"
#include "tool_common.h"

// The Canopy command line tool. It checks mods with the loader's own rules,
// packs a mod folder for sharing, lists the game's archives, and finds the
// game addresses in a village.exe. It only reads the game folder.
//
//   canopy validate [--game <folder>] [--mods <folder>] [--json]
//   canopy validate <game folder>
//   canopy schema
//   canopy pack <mod folder> <out.zip>
//   canopy dump list|extract ...
//   canopy addresses <village.exe> [--check | --build-section <label>]
//   canopy addresses <village.exe> --add-build <label> --builds <file> ...
namespace {

int Usage() {
    std::fprintf(
        stderr,
        "usage:\n"
        "  canopy validate [--game <folder>] [--mods <folder>] [--json]\n"
        "  canopy validate <game folder>\n"
        "  canopy schema\n"
        "  canopy pack <mod folder> <out.zip>\n"
        "  canopy dump list <game folder> [prefix]\n"
        "  canopy dump extract [--unwrap] <game folder> <data path> <out file>\n"
        "  canopy addresses <village.exe> [--patterns <file>] [--builds <file>]\n"
        "                   [--check | --build-section <label>]\n"
        "  canopy addresses <new village.exe> --add-build <label> --builds <file>\n"
        "                   [--previous <village.exe>] [--none <name>]... [--write]\n"
        "                   [--sample <folder>]\n"
        "\n"
        "validate checks every folder of the mods folder (the game's mods folder\n"
        "by default) the way Canopy does at startup, and with a game folder\n"
        "also against the game's own tables. It exits 1 when Canopy would skip\n"
        "something, 0 otherwise.\n"
        "\n"
        "addresses lists where Canopy finds each game address in the exe: from\n"
        "game_builds.txt for a checked build, by searching otherwise. --check\n"
        "searches a checked build too and exits 1 when the two differ.\n"
        "--build-section prints a searched game_builds.txt section to review and\n"
        "add. --patterns and --builds use edited files in place of the built-in.\n"
        "\n"
        "--add-build searches a build the builds file does not list and says what\n"
        "to review: names it could not find, names only some of their lines found,\n"
        "and with --previous (the exe of a listed build) functions whose length\n"
        "changed. --none names a site you checked the build does not have, which\n"
        "is then written as none. --write adds the section to the builds file,\n"
        "and --sample copies the exe into folder as \"village 0x<timestamp>.exe\"\n"
        "for the tests.\n"
        "It exits 0 when the build can be added, 1 when it cannot, 3 when the\n"
        "builds file lists it already.\n"
    );
    return 2;
}

void Print(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
}

int Validate(const std::vector<std::wstring>& args) {
    std::wstring game;
    std::wstring mods;
    bool json = false;
    for (size_t index = 0; index < args.size(); ++index) {
        const std::wstring& arg = args[index];
        if (arg == L"--json") {
            json = true;
        } else if (arg == L"--game" && index + 1 < args.size()) {
            game = args[++index];
        } else if (arg == L"--mods" && index + 1 < args.size()) {
            mods = args[++index];
        } else if (game.empty() && !arg.empty() && arg[0] != L'-') {
            game = arg;
        } else {
            return Usage();
        }
    }
    if (game.empty() && mods.empty()) {
        return Usage();
    }
    const mod_rules::Validation validation = mod_rules::Validate(game, mods);
    if (json) {
        Print(mod_rules::ValidationJson(validation) + "\n");
        return !validation.ok ? 2 : validation.context.report.Count(mod_rules::Severity::Error) > 0 ? 1 : 0;
    }
    if (!validation.ok) {
        std::fprintf(stderr, "error: %s\n", validation.error.c_str());
        return 2;
    }
    const mod_rules::Report& report = validation.context.report;
    for (const mod_rules::Diagnostic& diagnostic : report.Items()) {
        std::string where = diagnostic.folder.empty() ? "mods" : platform::Join(diagnostic.folder, "canopy.ini");
        if (!diagnostic.folder.empty() && diagnostic.line > 0) {
            where += ":" + std::to_string(diagnostic.line);
        }
        Print(mod_rules::ToUtf8(
            where + ": " + mod_rules::SeverityName(diagnostic.severity) + ": " + diagnostic.message + "\n"
        ));
    }
    const size_t errors = report.Count(mod_rules::Severity::Error);
    const size_t warnings = report.Count(mod_rules::Severity::Warning);
    const size_t notes = report.Count(mod_rules::Severity::Note);
    std::string summary = mod_rules::Plural(errors, "error", "errors") + ", " +
                          mod_rules::Plural(warnings, "warning", "warnings");
    if (notes > 0) {
        summary += ", " + mod_rules::Plural(notes, "note", "notes");
    }
    summary += " in " + mod_rules::Plural(validation.context.folders.size(), "mod folder", "mod folders") +
               (validation.vanilla ? ", checked against the game's tables.\n"
                                   : ". Give --game to also check against the game's tables.\n");
    Print(summary);
    return errors > 0 ? 1 : 0;
}

const char* StatusName(game_addresses::Status status) {
    switch (status) {
    case game_addresses::Status::Found:
        return "found";
    case game_addresses::Status::Absent:
        return "none";
    case game_addresses::Status::Missing:
        return "missing";
    case game_addresses::Status::Conflict:
        return "conflict";
    }
    return "?";
}

std::string Counts(const game_addresses::Resolution& resolution) {
    using game_addresses::Status;
    return std::to_string(resolution.Count(Status::Found)) + " found, " +
           std::to_string(resolution.Count(Status::Absent)) + " none, " +
           std::to_string(resolution.Count(Status::Missing)) + " missing, " +
           std::to_string(resolution.Count(Status::Conflict)) + " conflicting";
}

// Reads a village.exe and lays it out at its RVAs, or prints why not.
bool LoadExe(const std::wstring& path, std::vector<uint8_t>& laid, game_addresses::Header& header) {
    std::vector<uint8_t> file;
    std::string error;
    if (!platform::ReadWholeFile(path, 1u << 30, file)) {
        std::fprintf(stderr, "error: could not read %s\n", platform::WideToUtf8(path).c_str());
        return false;
    }
    if (!game_addresses::LayOut(file, laid, error)) {
        std::fprintf(stderr, "error: %s: %s\n", platform::WideToUtf8(path).c_str(), error.c_str());
        return false;
    }
    game_addresses::ReadHeader({laid.data(), laid.size(), 0}, header);
    return true;
}

// Copies exe into folder as "village 0x<timestamp>.exe", unless a
// "village *.exe" there is the same build already.
bool CopySample(const std::wstring& exe, const game_addresses::Header& header, const std::wstring& folder) {
    std::vector<platform::FolderEntry> entries;
    platform::CreateFolders(folder);
    if (platform::ListFolder(platform::WideToUtf8(folder), entries)) {
        for (const platform::FolderEntry& entry : entries) {
            const std::wstring name = platform::Utf8ToWideLossy(entry.name);
            if (entry.folder || name.rfind(L"village ", 0) != 0 || name.size() < 4 ||
                name.compare(name.size() - 4, 4, L".exe") != 0) {
                continue;
            }
            std::vector<uint8_t> start;
            game_addresses::Header other;
            if (platform::ReadFileStart(platform::Join(folder, name), 4096, start) &&
                game_addresses::ReadHeader({start.data(), start.size(), 0}, other) &&
                other.timestamp == header.timestamp && other.imageSize == header.imageSize) {
                Print("The tests' samples already have this build as " + entry.name + ".\n");
                return true;
            }
        }
    }
    char name[48];
    std::snprintf(name, sizeof(name), "village 0x%08X.exe", header.timestamp);
    const std::wstring target = platform::Join(folder, platform::Utf8ToWideLossy(name));
    std::vector<uint8_t> file;
    std::string error;
    if (!platform::ReadWholeFile(exe, 1u << 30, file) ||
        !platform::WriteWholeFile(target, file.data(), file.size(), error)) {
        std::fprintf(stderr, "error: could not copy the exe to %s %s\n", platform::WideToUtf8(target).c_str(),
                     error.c_str());
        return false;
    }
    Print("Copied the exe to " + platform::WideToUtf8(target) + " for the tests.\n");
    return true;
}

std::string Change(const game_addresses::BuildReview::Resized& change) {
    char text[64];
    std::snprintf(text, sizeof(text), " 0x%llX -> 0x%llX", static_cast<unsigned long long>(change.before),
                  static_cast<unsigned long long>(change.after));
    return text;
}

// --add-build: what adding image's build rests on, and with write, adding it.
int AddBuild(
    const game_addresses::Image& image, const std::wstring& exe, const std::string& patterns, const std::string& builds,
    const std::wstring& buildsFile, const std::string& label, const std::wstring& previousExe, bool write,
    const std::wstring& sample, const std::vector<std::string>& none
) {
    std::vector<uint8_t> previousLaid;
    game_addresses::Header previousHeader;
    if (!previousExe.empty() && !LoadExe(previousExe, previousLaid, previousHeader)) {
        return 2;
    }
    const game_addresses::Image previous{previousLaid.data(), previousLaid.size(), previousHeader.imageBase};
    const game_addresses::BuildReview review =
        game_addresses::ReviewBuild(image, patterns, builds, label, previousExe.empty() ? nullptr : &previous, none);
    if (review.listed) {
        Print(review.error + ".\n");
        return 3;
    }
    if (!review.error.empty()) {
        std::fprintf(stderr, "error: %s\n", review.error.c_str());
        return 2;
    }
    const game_addresses::Resolution& searched = review.searched;
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "0x%08X size=0x%X", searched.header.timestamp, searched.header.imageSize);
    Print(std::string("build ") + stamp + ": not listed\n");
    Print("search: " + Counts(searched) + "\n");
    if (!review.against.empty()) {
        Print("compared with " + review.against + ": " + std::to_string(review.moved) + " moved, " +
              std::to_string(review.unchanged) + " unchanged\n");
    }
    if (!review.absent.empty()) {
        Print("\nWritten as none, given with --none or as " + review.against + " has no such site either:\n");
        for (const std::string& name : review.absent) {
            Print("  " + name + "\n");
        }
    }
    if (!review.partial.empty()) {
        Print("\nFound by only some of their lines, so the code around them changed. Check these by hand:\n");
        for (const std::string& name : review.partial) {
            const game_addresses::Entry* entry = searched.Find(name);
            Print("  " + name + " (" + std::to_string(entry->hits) + " of " + std::to_string(entry->lines) +
                  " lines)\n");
        }
    }
    if (!review.resized.empty()) {
        Print("\nIn a function whose length changed. Check these by hand:\n");
        for (const game_addresses::BuildReview::Resized& resized : review.resized) {
            Print("  " + resized.name + Change(resized) + "\n");
        }
    }
    if (!review.fields.empty()) {
        Print("\nFields whose value changed, so the game's layout did. Features that use them stay off on this\n"
              "build until game_retail.h has the new value:\n");
        for (const game_addresses::BuildReview::Resized& field : review.fields) {
            Print("  " + field.name + Change(field) + "\n");
        }
    }
    if (!review.blocking.empty()) {
        Print("\nThe build cannot be added: nothing found these, or their lines disagree. Fix their lines in\n"
              "game_addresses.txt, or, once you have checked the build has no such site, give --none <name>:\n");
        for (const std::string& name : review.blocking) {
            Print("  " + name + " " + StatusName(searched.Find(name)->status) + "\n");
        }
        return 1;
    }
    if (!write) {
        Print("\nThe build can be added. Run again with --write to add it to the builds file.\n");
        return 0;
    }
    std::string text = builds;
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    text += "\n" + review.section;
    std::string error;
    if (!platform::WriteWholeFile(buildsFile, text.data(), text.size(), error)) {
        std::fprintf(stderr, "error: could not write %s %s\n", platform::WideToUtf8(buildsFile).c_str(),
                     error.c_str());
        return 2;
    }
    Print("\nAdded " + label + " to " + platform::WideToUtf8(buildsFile) + ".\n");
    return sample.empty() || CopySample(exe, searched.header, sample) ? 0 : 2;
}

int Addresses(const std::vector<std::wstring>& args) {
    std::wstring exe;
    std::wstring patternsFile;
    std::wstring buildsFile;
    std::wstring previousExe;
    std::wstring sample;
    std::vector<std::string> none;
    std::string label;
    bool check = false;
    bool section = false;
    bool addBuild = false;
    bool write = false;
    for (size_t index = 0; index < args.size(); ++index) {
        const std::wstring& arg = args[index];
        if (arg == L"--check") {
            check = true;
        } else if (arg == L"--write") {
            write = true;
        } else if ((arg == L"--build-section" || arg == L"--add-build") && index + 1 < args.size()) {
            (arg == L"--add-build" ? addBuild : section) = true;
            label = platform::WideToUtf8(args[++index]);
        } else if (arg == L"--previous" && index + 1 < args.size()) {
            previousExe = args[++index];
        } else if (arg == L"--none" && index + 1 < args.size()) {
            none.push_back(platform::WideToUtf8(args[++index]));
        } else if (arg == L"--sample" && index + 1 < args.size()) {
            sample = args[++index];
        } else if (arg == L"--patterns" && index + 1 < args.size()) {
            patternsFile = args[++index];
        } else if (arg == L"--builds" && index + 1 < args.size()) {
            buildsFile = args[++index];
        } else if (exe.empty() && !arg.empty() && arg[0] != L'-') {
            exe = arg;
        } else {
            return Usage();
        }
    }
    const int modes = check + section + addBuild;
    const bool addOnly = write || !previousExe.empty() || !sample.empty() || !none.empty();
    if (exe.empty() || modes > 1 || (addOnly && !addBuild) || (addBuild && (buildsFile.empty() || label.empty()))) {
        return Usage();
    }
    auto readText = [](const std::wstring& path, const std::string& builtIn, std::string& text) {
        if (path.empty()) {
            text = builtIn;
            return true;
        }
        std::vector<uint8_t> bytes;
        if (!platform::ReadWholeFile(path, 16u << 20, bytes)) {
            std::fprintf(stderr, "error: could not read %s\n", platform::WideToUtf8(path).c_str());
            return false;
        }
        text.assign(bytes.begin(), bytes.end());
        return true;
    };
    std::string patterns;
    std::string builds;
    if (!readText(patternsFile, game_addresses::PatternsText(), patterns) ||
        !readText(buildsFile, game_addresses::BuildsText(), builds)) {
        return 2;
    }
    std::vector<uint8_t> laid;
    game_addresses::Header header;
    if (!LoadExe(exe, laid, header)) {
        return 2;
    }
    const game_addresses::Image image{laid.data(), laid.size(), header.imageBase};
    if (addBuild) {
        return AddBuild(image, exe, patterns, builds, buildsFile, label, previousExe, write, sample, none);
    }

    const auto started = std::chrono::steady_clock::now();
    const game_addresses::Resolution searched = game_addresses::Search(image, patterns);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    if (section) {
        Print(game_addresses::BuildSection(searched, label));
        return searched.Count(game_addresses::Status::Missing) + searched.Count(game_addresses::Status::Conflict) > 0;
    }
    const game_addresses::Resolution resolved = game_addresses::Resolve(image, patterns, builds);
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "0x%08X size=0x%X", header.timestamp, header.imageSize);
    Print(std::string("build ") + stamp + ": " +
          (resolved.Verified() ? "checked, " + resolved.build : std::string("not checked, searched")) + "\n");
    if (check) {
        if (!resolved.Verified()) {
            std::fprintf(stderr, "error: --check needs a build listed in game_builds.txt\n");
            return 2;
        }
        const std::string differences = game_addresses::Compare(resolved, searched);
        Print("search: " + Counts(searched) + " in " + std::to_string(elapsed) + " ms\n");
        if (differences.empty()) {
            Print("The search agrees with every checked address.\n");
            return 0;
        }
        Print("name checked searched\n" + differences);
        return 1;
    }
    for (const game_addresses::Entry& entry : resolved.entries) {
        char rva[32] = "";
        if (entry.status == game_addresses::Status::Found) {
            std::snprintf(rva, sizeof(rva), " 0x%llX", static_cast<unsigned long long>(entry.rva));
        }
        Print(entry.name + " " + StatusName(entry.status) + rva + "\n");
    }
    Print(Counts(resolved) + "\n");
    return resolved.Count(game_addresses::Status::Missing) + resolved.Count(game_addresses::Status::Conflict) > 0;
}

// Runs the command args name, the arguments after the program's own.
int Run(std::vector<std::wstring> args) {
    if (args.empty()) {
        return Usage();
    }
    const std::wstring command = args[0];
    args.erase(args.begin());
    if (command == L"validate") {
        return Validate(args);
    }
    if (command == L"schema" && args.empty()) {
        Print(mod_rules::SectionsJson() + "\n");
        return 0;
    }
    if (command == L"pack") {
        return canopy_tool::RunPack(args);
    }
    if (command == L"dump") {
        return canopy_tool::RunDump(args);
    }
    if (command == L"addresses") {
        return Addresses(args);
    }
    return Usage();
}

}  // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    return Run(std::vector<std::wstring>(argv + 1, argv + argc));
}
#else
// The arguments arrive as UTF-8; a byte that is not becomes U+FFFD.
int main(int argc, char** argv) {
    std::vector<std::wstring> args;
    for (int index = 1; index < argc; ++index) {
        args.push_back(platform::Utf8ToWideLossy(argv[index]));
    }
    return Run(std::move(args));
}
#endif
