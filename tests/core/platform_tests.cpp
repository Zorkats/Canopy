#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "mod_rules.h"
#include "platform_files.h"
#include "test_support.h"

// Checks of platform_files.h: joins and separators, the text conversions,
// names compared without case, and the file calls against a temporary
// folder, with the lookup that opens a path written on Windows, in another
// case and with '\' separators, on a file system that minds case.
namespace {

namespace fs = std::filesystem;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED platform_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

void TestPaths() {
    std::printf("Platform paths\n");
    CHECK(platform::IsSeparator(L'\\') && platform::IsSeparator(L'/') && !platform::IsSeparator(L':'));
#ifdef _WIN32
    CHECK(platform::Join("a", "b") == "a\\b");
    CHECK(platform::Join(L"a", L"b") == L"a\\b");
    CHECK(platform::NativeSeparators("a/b\\c") == "a\\b\\c");
    CHECK(platform::NativeSeparators(L"a/b\\c") == L"a\\b\\c");
#else
    CHECK(platform::Join("a", "b") == "a/b");
    CHECK(platform::Join(L"a", L"b") == L"a/b");
    CHECK(platform::NativeSeparators("a/b\\c") == "a/b/c");
    CHECK(platform::NativeSeparators(L"a/b\\c") == L"a/b/c");
#endif
    // One separator is always added, as the core's paths have been built.
    CHECK(platform::Join("", "b") == std::string(1, platform::kSeparator) + "b");
    CHECK(platform::Join(L"a", L"") == std::wstring(L"a") + platform::kWideSeparator);
}

void TestText() {
    std::printf("Platform text\n");
    std::wstring wide;
    CHECK(platform::Utf8ToWide("caf\xC3\xA9", wide) && wide == L"caf\u00e9");
    CHECK(platform::WideToUtf8(L"caf\u00e9") == "caf\xC3\xA9");
    CHECK(platform::Utf8ToWide("", wide) && wide.empty());
    CHECK(!platform::Utf8ToWide("a\xFF", wide) && wide.empty());
    CHECK(platform::Utf8ToWideLossy("a\xFF" "b") == L"a\uFFFDb");
    CHECK(platform::IsUtf8("") && platform::IsUtf8("\xE4\xB8\xAD") && !platform::IsUtf8("\xC3") &&
          !platform::IsUtf8("\xFF"));

    // A character outside the BMP: two UTF-16 units on Windows, one UTF-32
    // unit elsewhere, the same UTF-8 both ways.
    std::wstring tree;
    CHECK(platform::Utf8ToWide("\xF0\x9F\x8C\xB2", tree) && platform::WideToUtf8(tree) == "\xF0\x9F\x8C\xB2");
    CHECK(tree.size() == (sizeof(wchar_t) == 2 ? 2u : 1u));

    std::string ansi;
    CHECK(platform::WideToAnsi(L"plain", ansi) && ansi == "plain");
    CHECK(platform::WideToAnsi(L"", ansi) && ansi.empty());
    CHECK(platform::AnsiToWide("plain") == L"plain");
#ifndef _WIN32
    // The narrow form is UTF-8 here, so it spells every character.
    CHECK(platform::WideToAnsi(tree, ansi) && ansi == "\xF0\x9F\x8C\xB2");
    CHECK(platform::AnsiToWide("\xE9") == L"\uFFFD");
#endif

    CHECK(platform::CompareNoCase("Canopy.INI", "canopy.ini") == 0);
    CHECK(platform::CompareNoCase("a", "B") < 0 && platform::CompareNoCase("b", "A") > 0);
    CHECK(platform::CompareNoCase("modx", "MODY", 3) == 0 && platform::CompareNoCase("modx", "MODY", 4) < 0);
    CHECK(platform::EqualNoCase(L"\u00c9t\u00e9", L"\u00e9T\u00c9"));
    CHECK(platform::EqualNoCase(L"\u0416\u03a3", L"\u0436\u03c3"));
    CHECK(!platform::EqualNoCase(L"ab", L"abc") && !platform::EqualNoCase(L"ab", L"ac"));
}

void TestFiles() {
    std::printf("Platform files\n");
    const fs::path root =
        fs::temp_directory_path() / ("canopy_platform_tests_" + std::to_string(test_support::ProcessId()));
    std::error_code ignored;
    fs::remove_all(root, ignored);
    const std::wstring base = root.wstring();
    std::string narrowBase;
    if (!CHECK(platform::WideToAnsi(base, narrowBase))) {
        return;
    }

    const std::wstring folder = platform::Join(platform::Join(base, L"Mods"), L"Alpha");
    CHECK(platform::CreateFolders(folder));
    CHECK(platform::CreateFolders(folder));
    CHECK(platform::IsFolder(folder) && !platform::IsFile(folder));
    const std::wstring file = platform::Join(folder, L"Canopy.INI");
    const std::string text = "[mod]\nkey = Alpha\n";
    std::string error;
    CHECK(platform::WriteWholeFile(file, text.data(), text.size(), error));
    const platform::FileInfo info = platform::Stat(file);
    CHECK(info.exists && !info.folder && info.size == text.size() && info.written != 0);
    CHECK(!platform::Stat(platform::Join(folder, L"missing.ini")).exists);
    platform::LocalTime time;
    CHECK(platform::LocalWriteTime(file, time) && time.year >= 2020 && time.month >= 1 && time.month <= 12);

    // The path as a mod written on Windows gives it: other case, '\' and '/'.
    const std::string mixed = narrowBase + "\\mods/ALPHA\\canopy.ini";
    const std::wstring mixedWide = platform::AnsiToWide(mixed);
    CHECK(platform::IsFile(mixed) && platform::Exists(mixed) && platform::IsFile(mixedWide));
    CHECK(platform::IsFolder(narrowBase + "\\MODS") && platform::IsFolder(platform::AnsiToWide(narrowBase + "/mods/")));
    std::string read;
    CHECK(platform::ReadWholeFile(mixed, 1024, read) && read == text);
    CHECK(!platform::ReadWholeFile(mixed, 4, read));
    std::vector<uint8_t> bytes;
    CHECK(platform::ReadWholeFile(mixedWide, 1024, bytes) && bytes.size() == text.size());
    CHECK(platform::ReadFileStart(mixedWide, 5, bytes) && std::string(bytes.begin(), bytes.end()) == "[mod]");
    CHECK(platform::ReadFileStart(mixedWide, 4096, bytes) && bytes.size() == text.size());

    platform::PathLookup lookup;
#ifdef _WIN32
    // Windows ignores case itself, so the lookup gives the path back.
    CHECK(lookup.Find(mixed) == mixed);
    CHECK(lookup.Find(mixedWide) == mixedWide);
    CHECK(lookup.Find(narrowBase + "\\mods\\nothing\\x.ini") == narrowBase + "\\mods\\nothing\\x.ini");
#else
    // The name as it is on disk; from the first part that matches nothing
    // on, the rest stays as written.
    CHECK(lookup.Find(mixed) == narrowBase + "/Mods/Alpha/Canopy.INI");
    CHECK(lookup.Find(mixedWide) == platform::AnsiToWide(narrowBase + "/Mods/Alpha/Canopy.INI"));
    CHECK(lookup.Find(narrowBase + "\\mods\\nothing\\x.ini") == narrowBase + "/Mods/nothing/x.ini");
#endif

    platform::ReadOnlyFile reader;
    CHECK(reader.Open(mixedWide, "canopy.ini", error) && reader.IsOpen() && reader.Size() == text.size());
    uint8_t part[3] = {};
    CHECK(reader.ReadAt(1, part, 3, error) && std::memcmp(part, "mod", 3) == 0);
    error.clear();
    CHECK(!reader.ReadAt(text.size() - 1, part, 3, error) && error.find("returned 1") != std::string::npos);
    reader.Close();
    CHECK(!reader.IsOpen() && reader.Size() == 0);
    error.clear();
    CHECK(!reader.Open(platform::Join(folder, L"missing.dat"), "missing.dat", error) &&
          error.find("cannot open missing.dat: ") == 0);

    // ScanMods finds the mod and its Canopy.INI through a mods path in
    // another case.
    const std::vector<mod_rules::ModFolder> mods = mod_rules::ScanMods(narrowBase + "\\mods");
    CHECK(mods.size() == 1 && mods[0].name == "Alpha" && mods[0].hasIni && !mods[0].sections.empty());

    std::vector<platform::FolderEntry> entries;
    CHECK(platform::ListFolder(narrowBase + "\\mods\\alpha", entries) && entries.size() == 1 &&
          entries[0].name == "Canopy.INI" && !entries[0].folder);
    CHECK(platform::ListFolder(narrowBase, entries) && entries.size() == 1 && entries[0].name == "Mods" &&
          entries[0].folder);
    CHECK(!platform::ListFolder(narrowBase + "\\nowhere", entries));

    const std::wstring empty = platform::Join(folder, L"empty.txt");
    CHECK(platform::WriteWholeFile(empty, "", 0, error) && platform::ReadWholeFile(empty, 16, bytes) && bytes.empty());
    const std::wstring other = platform::Join(folder, L"other.txt");
    CHECK(platform::WriteWholeFile(other, "x", 1, error));
    CHECK(platform::RenameOver(other, file) && !platform::IsFile(other) && platform::Stat(file).size == 1);
    CHECK(platform::RemoveFile(file) && !platform::IsFile(file));
    CHECK(!platform::RemoveFile(file));
    // A file whose folder is missing cannot be made.
    error.clear();
    CHECK(!platform::WriteWholeFile(platform::Join(platform::Join(folder, L"none"), L"x.txt"), "x", 1, error) &&
          !error.empty());
    fs::remove_all(root, ignored);
}

}  // namespace

void TestPlatform(int& checks, int& failures) {
    TestPaths();
    TestText();
    TestFiles();
    checks += g_checks;
    failures += g_failures;
}
