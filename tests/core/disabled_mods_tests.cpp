#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "disabled_mods.h"
#include "mod_rules.h"
#include "rules_report.h"
#include "test_support.h"

// Checks of disabled_mods.h: the list's text rules, the cached read, and the
// two places the desktop tools use it, ScanMods and Validate, against a mods
// folder written to a temporary folder. The loader's own uses (the DLL list
// and the file overrides) cannot run here.
namespace {

namespace fs = std::filesystem;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED disabled_mods_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

// A mods folder of its own under %TEMP%, removed when the test ends.
class TempMods {
public:
    explicit TempMods(const std::string& label) {
        root_ = fs::temp_directory_path() /
                ("canopy_disabled_tests_" + label + "_" + std::to_string(test_support::ProcessId()));
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_, error);
    }
    ~TempMods() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    void Write(const std::string& relative, const std::string& text) const {
        const fs::path path = root_ / fs::u8path(relative);
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    std::string Dir() const {
        return root_.string();
    }

private:
    fs::path root_;
};

using List = std::vector<std::string>;

void TestParse() {
    std::printf("Turned-off list text\n");
    CHECK(disabled_mods::Parse("").empty());
    CHECK(disabled_mods::Parse("\n\n  \r\n").empty());
    CHECK((disabled_mods::Parse("Alpha\nBeta\n") == List{"Alpha", "Beta"}));
    CHECK((disabled_mods::Parse("Alpha\r\nBeta\r\n") == List{"Alpha", "Beta"}));
    // No line ending after the last name.
    CHECK((disabled_mods::Parse("Alpha\nBeta") == List{"Alpha", "Beta"}));
    // A byte order mark is not part of the first name.
    CHECK((disabled_mods::Parse("\xEF\xBB\xBF" "Alpha\r\nBeta\r\n") == List{"Alpha", "Beta"}));
    CHECK((disabled_mods::Parse("\xEF\xBB\xBF") == List{}));
    // Comments and blank lines are skipped, spacing around a name is not kept,
    // and spacing inside one is.
    CHECK((disabled_mods::Parse("# turned off\n\n  Alpha  \n\t# indented comment\r\n \tBig Titles \t\r\n#\n") ==
           List{"Alpha", "Big Titles"}));
    // A # inside a line is part of the name.
    CHECK((disabled_mods::Parse("Mod#2\n") == List{"Mod#2"}));
    // The case is kept as written.
    CHECK((disabled_mods::Parse("VillageQoL.dll\n") == List{"VillageQoL.dll"}));
    // UTF-8 names are kept byte for byte.
    CHECK((disabled_mods::Parse("Caf\xC3\xA9\n") == List{"Caf\xC3\xA9"}));
}

void TestIsDisabled() {
    std::printf("Turned-off names\n");
    const List list = {"BigTitles", "VillageQoL.dll", "Caf\xC3\xA9"};
    CHECK(disabled_mods::IsDisabled(list, "BigTitles"));
    CHECK(disabled_mods::IsDisabled(list, "bigtitles"));
    CHECK(disabled_mods::IsDisabled(list, "BIGTITLES"));
    CHECK(disabled_mods::IsDisabled(list, "villageqol.DLL"));
    // A DLL is named with its extension, a folder without.
    CHECK(!disabled_mods::IsDisabled(list, "VillageQoL"));
    CHECK(!disabled_mods::IsDisabled(list, "BigTitles.dll"));
    CHECK(!disabled_mods::IsDisabled(list, "BigTitles2"));
    CHECK(!disabled_mods::IsDisabled(list, ""));
    CHECK(!disabled_mods::IsDisabled(List{}, "BigTitles"));
    // A UTF-8 name matches without case beyond ASCII.
    CHECK(disabled_mods::IsDisabled(list, "CAF\xC3\x89"));
}

void TestRead() {
    std::printf("Turned-off list file\n");
    {
        TempMods mods("read");
        // No file at all, and no CanopyData folder: nothing is off.
        CHECK(disabled_mods::Read(mods.Dir()).empty());
        mods.Write("CanopyData/disabled.txt", "\xEF\xBB\xBF# off\r\nAlpha\r\n  beta  \r\n\r\nGamma.dll");
        CHECK((disabled_mods::Read(mods.Dir()) == List{"Alpha", "beta", "Gamma.dll"}));
        // A trailing separator on the folder is fine.
        CHECK((disabled_mods::Read(mods.Dir() + "\\") == List{"Alpha", "beta", "Gamma.dll"}));
    }
    {
        // A folder where the file should be is not a list.
        TempMods mods("readdir");
        fs::create_directories(fs::path(mods.Dir()) / "CanopyData" / "disabled.txt");
        CHECK(disabled_mods::Read(mods.Dir()).empty());
        CHECK(disabled_mods::Cached(mods.Dir()).empty());
    }
    CHECK(disabled_mods::Read("Z:\\no\\such\\folder").empty());
}

void TestCached() {
    std::printf("Turned-off list cache\n");
    TempMods first("cachea");
    TempMods second("cacheb");
    CHECK(disabled_mods::Cached(first.Dir()).empty());
    first.Write("CanopyData/disabled.txt", "Alpha\n");
    // The file appearing is seen, and each folder has its own list.
    CHECK((disabled_mods::Cached(first.Dir()) == List{"Alpha"}));
    CHECK(disabled_mods::Cached(second.Dir()).empty());
    second.Write("CanopyData/disabled.txt", "Beta\nGamma\n");
    CHECK((disabled_mods::Cached(first.Dir()) == List{"Alpha"}));
    CHECK((disabled_mods::Cached(second.Dir()) == List{"Beta", "Gamma"}));
    // An edit is seen by the next read, as a desktop tool needs.
    first.Write("CanopyData/disabled.txt", "Alpha\nDelta\n");
    CHECK((disabled_mods::Cached(first.Dir()) == List{"Alpha", "Delta"}));
    // The file going away turns everything back on.
    std::error_code error;
    fs::remove(fs::path(first.Dir()) / "CanopyData" / "disabled.txt", error);
    CHECK(disabled_mods::Cached(first.Dir()).empty());
}

bool Has(const std::vector<mod_rules::ModFolder>& folders, const std::string& name) {
    for (const mod_rules::ModFolder& folder : folders) {
        if (folder.name == name) {
            return true;
        }
    }
    return false;
}

void TestScanMods() {
    std::printf("Turned-off mods in ScanMods\n");
    TempMods mods("scan");
    mods.Write("Alpha/canopy.ini", "[mod]\nkey = Alpha\n");
    mods.Write("Beta/canopy.ini", "[mod]\nkey = Beta\n");
    mods.Write("Gamma/readme.txt", "plain\n");
    mods.Write("Tool.dll", "not a real DLL");
    mods.Write("Other.dll", "not a real DLL");

    // With no list, every folder is a mod.
    std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    CHECK(folders.size() == 3);

    mods.Write("CanopyData/disabled.txt", "# turned off\r\nBETA\r\nTool.dll\r\nGhost\r\nGhost.dll\r\n");
    folders = mod_rules::ScanMods(mods.Dir());
    // CanopyData is a folder too, as it was before the list existed.
    CHECK(folders.size() == 3);
    CHECK(Has(folders, "Alpha"));
    CHECK(!Has(folders, "Beta"));
    CHECK(Has(folders, "Gamma"));
    CHECK(Has(folders, "CanopyData"));
    // Only Alpha is left with a canopy.ini for the content readers.
    CHECK(std::count_if(folders.begin(), folders.end(), [](const mod_rules::ModFolder& folder) {
              return folder.hasIni;
          }) == 1);

    // Existing names what is listed and present, as it is named on disk: the
    // folder and the DLL, not the two names with nothing behind them.
    const List existing = disabled_mods::Existing(mods.Dir());
    CHECK((existing == List{"Beta", "Tool.dll"}));

    // Listing a folder's name does not turn off a DLL of the same name.
    mods.Write("CanopyData/disabled.txt", "Tool\n");
    CHECK(disabled_mods::Existing(mods.Dir()).empty());
    CHECK(mod_rules::ScanMods(mods.Dir()).size() == 4);

    // Back to nothing off.
    std::error_code error;
    fs::remove(fs::path(mods.Dir()) / "CanopyData" / "disabled.txt", error);
    folders = mod_rules::ScanMods(mods.Dir());
    CHECK(Has(folders, "Beta"));
    CHECK(disabled_mods::Existing(mods.Dir()).empty());
}

void TestValidate() {
    std::printf("Turned-off mods in the desktop check\n");
    TempMods mods("validate");
    mods.Write("Alpha/canopy.ini", "[mod]\nkey = Alpha\n");
    // This folder would be an error if it were checked: the key is not valid.
    mods.Write("Beta/canopy.ini", "[mod]\nkey = bad key\n");
    mods.Write("Tool.dll", "not a real DLL");
    mods.Write("CanopyData/disabled.txt", "beta\nTool.dll\nGhost\n");

    const mod_rules::Validation validation = mod_rules::Validate(L"", fs::path(mods.Dir()).wstring());
    CHECK(validation.ok);
    bool betaChecked = false;
    for (const mod_rules::ModFolder& folder : validation.context.folders) {
        betaChecked = betaChecked || folder.name == "Beta";
    }
    CHECK(!betaChecked);

    size_t notes = 0;
    bool betaNote = false;
    bool toolNote = false;
    bool ghostNote = false;
    for (const mod_rules::Diagnostic& diagnostic : validation.context.report.Items()) {
        if (diagnostic.message.find("is turned off in mods\\CanopyData\\disabled.txt") == std::string::npos) {
            // Nothing else is said about the folder that is off.
            CHECK(diagnostic.folder != "Beta");
            continue;
        }
        ++notes;
        CHECK(diagnostic.severity == mod_rules::Severity::Note);
        if (diagnostic.message.rfind("Mod Beta is turned off", 0) == 0) {
            betaNote = true;
            CHECK(diagnostic.folder == "Beta");
            CHECK(diagnostic.message ==
                  "Mod Beta is turned off in mods\\CanopyData\\disabled.txt, so it is not checked.");
        }
        if (diagnostic.message.rfind("Mod Tool.dll is turned off", 0) == 0) {
            toolNote = true;
            CHECK(diagnostic.folder.empty());
        }
        ghostNote = ghostNote || diagnostic.message.find("Ghost") != std::string::npos;
    }
    CHECK(notes == 2);
    CHECK(betaNote);
    CHECK(toolNote);
    CHECK(!ghostNote);

    // The JSON counts them as notes.
    const std::string json = mod_rules::ValidationJson(validation);
    CHECK(json.find("\"folders\":[\"Alpha\",\"CanopyData\"]") != std::string::npos);
    CHECK(json.find("\"errors\":0") != std::string::npos);
}

}  // namespace

void TestDisabledMods(int& checks, int& failures) {
    TestParse();
    TestIsDisabled();
    TestRead();
    TestCached();
    TestScanMods();
    TestValidate();
    checks += g_checks;
    failures += g_failures;
}
