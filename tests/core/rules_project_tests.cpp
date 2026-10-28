#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "mod_rules.h"
#include "platform_files.h"
#include "rules_mod.h"
#include "rules_project.h"
#include "rules_whistles.h"
#include "test_support.h"

// Checks of rules_project.h, the check Canopy Studio's Create area runs on
// a mod project, and of ModFolder::dir, the folder of its own that lets the
// rules read a project kept outside the mods folder. Mods and projects are
// written to a temporary folder; the game is read, never written, and only
// when it is installed.
namespace {

namespace fs = std::filesystem;
using mod_rules::Diagnostic;
using mod_rules::Severity;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED rules_project_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

// A folder of its own under %TEMP%, removed when the test ends. Holds a mods
// folder and project folders beside it.
class TempRoot {
public:
    TempRoot() {
        root_ = fs::temp_directory_path() / ("canopy_project_tests_" + std::to_string(test_support::ProcessId()));
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_ / "mods", error);
    }
    ~TempRoot() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    void Write(const std::string& relative, const std::string& text) const {
        const fs::path path = root_ / fs::u8path(relative);
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    std::wstring Path(const std::string& relative) const {
        return (root_ / fs::u8path(relative)).wstring();
    }
    std::wstring Mods() const {
        return Path("mods");
    }

private:
    fs::path root_;
};

bool Has(const std::string& json, const std::string& part) {
    return json.find(part) != std::string::npos;
}

size_t Count(const std::string& json, const std::string& part) {
    size_t count = 0;
    for (size_t at = json.find(part); at != std::string::npos; at = json.find(part, at + part.size())) {
        ++count;
    }
    return count;
}

// A whistle section of key Key, learnt by flag, with optional extra lines.
std::string Whistle(const std::string& key, const std::string& notes, const std::string& learn,
                    const std::string& extra = "") {
    return "[whistle." + key + "]\nname = " + key + "\nnotes = " + notes + "\nlearn = " + learn + "\n" + extra;
}

// ---- ModFolder::dir ----------------------------------------------------------

// A folder whose dir is modsDir\name reads exactly as one whose dir is empty,
// which is what the loader passes: same specs, same messages, same lines.
void TestFolderDir() {
    std::printf("Folder paths\n");
    TempRoot root;
    root.Write(
        "mods/Alpha/canopy.ini",
        "[mod]\nkey = Alpha\n" + Whistle("Rise", "SSLL", "7", "script = whistles/rise.lua\nscore = 11\n") +
            Whistle("Gone", "LLSS", "7", "script = whistles/gone.lua\n")
    );
    root.Write("mods/Alpha/whistles/rise.lua", "function main() end\n");
    root.Write("mods/Alpha/lua/main.lua", "-- a Lua mod\n");
    const std::string modsDir = fs::path(root.Mods()).string();

    const std::vector<mod_rules::ModFolder> scanned = mod_rules::ScanMods(modsDir);
    CHECK(scanned.size() == 1 && scanned[0].dir.empty());
    std::vector<mod_rules::ModFolder> placed = scanned;
    for (mod_rules::ModFolder& folder : placed) {
        folder.dir = platform::Join(modsDir, folder.name);
    }
    CHECK(!scanned.empty() && mod_rules::FolderDir(modsDir, scanned[0]) == platform::Join(modsDir, "Alpha"));
    CHECK(!placed.empty() && mod_rules::FolderDir("Z:\\elsewhere", placed[0]) == platform::Join(modsDir, "Alpha"));

    mod_rules::KeyRefs refs;
    mod_rules::Report scannedReport;
    mod_rules::Report placedReport;
    const std::vector<mod_rules::WhistleSpec> fromScan =
        mod_rules::ReadWhistles(scanned, modsDir, refs, scannedReport, mod_rules::AcceptAll);
    // Another mods folder: dir alone must find the files.
    const std::vector<mod_rules::WhistleSpec> fromDir =
        mod_rules::ReadWhistles(placed, "Z:\\elsewhere", refs, placedReport, mod_rules::AcceptAll);
    CHECK(fromScan.size() == 2 && fromDir.size() == 2);
    CHECK(!fromScan.empty() && !fromDir.empty() && fromScan[0].source == fromDir[0].source);
    CHECK(!fromScan.empty() && fromScan[0].source == "function main() end\n");
    CHECK(scannedReport.Items().size() == placedReport.Items().size());
    bool same = scannedReport.Items().size() == placedReport.Items().size();
    for (size_t index = 0; same && index < scannedReport.Items().size(); ++index) {
        const Diagnostic& left = scannedReport.Items()[index];
        const Diagnostic& right = placedReport.Items()[index];
        same = left.severity == right.severity && left.folder == right.folder && left.line == right.line &&
               left.message == right.message;
    }
    CHECK(same);
    // The missing script and the bad score, as the loader logs them.
    CHECK(scannedReport.Count(Severity::Warning) == 2);

    CHECK(mod_rules::WithLuaScripts(scanned, modsDir).size() == 1);
    CHECK(mod_rules::WithLuaScripts(placed, "Z:\\elsewhere").size() == 1);
    CHECK(mod_rules::WithLuaScripts(scanned, "Z:\\elsewhere").empty());
}

// ---- The project check -------------------------------------------------------

void TestProjectFiles() {
    std::printf("Project outside the mods folder\n");
    TempRoot root;
    root.Write("mods/Beta/canopy.ini", "[mod]\nkey = Beta\n[flag.Seen]\n[whistle.X]\nname = X\nlearn = Nope\n");
    const std::string ini = "[mod]\nkey = Alpha\n" +
                            Whistle("Rise", "SSLL", "Beta.Seen", "script = whistles/rise.lua\n") +
                            "[villager.Walker]\nbase = 1000\nname = Walker\nscript = people/walker.lua\n";
    root.Write("projects/Alpha/whistles/rise.lua", "function main() end\n");
    root.Write("projects/Alpha/people/walker.lua", "function none() end\n");

    const std::string json = mod_rules::ValidateProjectJson(L"", root.Mods(), root.Path("projects/Alpha"), ini);
    CHECK(Has(json, "\"abi\":1,\"ok\":true,\"error\":null"));
    CHECK(Has(json, "\"vanilla\":false"));
    CHECK(Has(json, "\"folders\":[\"Alpha\",\"Beta\"]"));
    // Both scripts were read from the project, and Beta.Seen is a flag.
    CHECK(Has(json, "\"errors\":0,\"warnings\":0,\"notes\":0,\"diagnostics\":[]"));
    // Beta's own problems are not the project's.
    CHECK(!Has(json, "Beta\",\"file\""));

    // Without its files, each script is reported at its line, in the project.
    const std::string missing = mod_rules::ValidateProjectJson(L"", root.Mods(), root.Path("projects/Gamma"), ini);
    CHECK(Has(missing, "is not a folder."));
    CHECK(Has(missing, "\"ok\":false"));
    root.Write("projects/Gamma/readme.txt", "no scripts here\n");
    const std::string gamma = mod_rules::ValidateProjectJson(L"", root.Mods(), root.Path("projects/Gamma"), ini);
    CHECK(Has(gamma, "\"warnings\":2"));
    CHECK(Has(gamma, "\"folder\":\"Gamma\",\"file\":\"canopy.ini\",\"line\":7,"));
    CHECK(Has(gamma, "whistles/rise.lua could not be read."));
    CHECK(Has(gamma, "people/walker.lua could not be read."));

    // A flag no installed mod has.
    const std::string unknown = mod_rules::ValidateProjectJson(
        L"", root.Mods(), root.Path("projects/Alpha"), "[mod]\nkey = Alpha\n" + Whistle("Rise", "SSLL", "Beta.Nope")
    );
    CHECK(Has(unknown, "\"errors\":1"));
    CHECK(Has(unknown, "Beta.Nope is neither."));
    CHECK(Has(unknown, "\"line\":6,"));
}

void TestUnsavedText() {
    std::printf("Unsaved project text\n");
    TempRoot root;
    root.Write("projects/Alpha/canopy.ini", "[mod]\nkey = bad key\n" + Whistle("Rise", "SSLL", "7"));
    const std::wstring project = root.Path("projects/Alpha");

    // The editor's text is checked, not the file.
    const std::string fine =
        mod_rules::ValidateProjectJson(L"", root.Mods(), project, "[mod]\nkey = Alpha\n" + Whistle("Rise", "SSLL", "7"));
    CHECK(Has(fine, "\"ok\":true"));
    CHECK(Has(fine, "\"errors\":0,\"warnings\":0"));
    // And the other way round, at the lines of the text.
    const std::string broken = mod_rules::ValidateProjectJson(
        L"", root.Mods(), project, "; draft\n\n[mod]\nkey = bad key\n" + Whistle("Rise", "SSLL", "7", "score = 11\n")
    );
    CHECK(Has(broken, "\"errors\":1,\"warnings\":0"));
    CHECK(Has(broken, "\"line\":4,\"message\":\"Whistles in folder Alpha skipped, its key bad key must be"));
    const std::string warned = mod_rules::ValidateProjectJson(
        L"", root.Mods(), project,
        "; draft\r\n\r\n[mod]\r\nkey = Alpha\r\n[whistle.Rise]\r\nname = Rise\r\nnotes = SSLL\r\nlearn = 7\r\n"
        "score = 11\r\n"
    );
    CHECK(Has(warned, "\"errors\":0,\"warnings\":1"));
    CHECK(Has(warned, "\"line\":9,\"message\":\"[Alpha] Whistle Rise keeps its base whistle's score"));
    // Empty text is a project with nothing in it yet.
    const std::string empty = mod_rules::ValidateProjectJson(L"", root.Mods(), project, "");
    CHECK(Has(empty, "\"ok\":true") && Has(empty, "\"diagnostics\":[]"));
}

void TestShadowedFolder() {
    std::printf("Project shadowing an installed folder\n");
    TempRoot root;
    // The installed copy of the project, older and broken, and another mod
    // that takes the project's key first.
    root.Write("mods/Alpha/canopy.ini", "[mod]\nkey = bad key\n" + Whistle("Old", "LLLL", "Nope"));
    root.Write("mods/Aardvark/canopy.ini", "[mod]\nkey = Taken\n" + Whistle("First", "SLSL", "7"));
    root.Write("mods/Zed/canopy.ini", "[mod]\nkey = Zed\n" + Whistle("Same", "SSLL", "7"));
    root.Write("projects/alpha/canopy.ini", "");
    const std::string json = mod_rules::ValidateProjectJson(
        L"", root.Mods(), root.Path("projects/alpha"), "[mod]\nkey = Alpha\n" + Whistle("Rise", "SSLL", "7")
    );
    CHECK(Has(json, "\"folders\":[\"Aardvark\",\"alpha\",\"Zed\"]"));
    CHECK(Count(json, "lpha\"") == 1);
    // The installed Alpha's problems are gone; Zed's clash with the project
    // is Zed's, since the project loads first.
    CHECK(Has(json, "\"errors\":0,\"warnings\":0,\"notes\":0"));

    // A project that comes after another folder with its key is skipped.
    const std::string taken = mod_rules::ValidateProjectJson(
        L"", root.Mods(), root.Path("projects/alpha"), "[mod]\nkey = Taken\n" + Whistle("Rise", "LSLS", "7")
    );
    CHECK(Has(taken, "\"errors\":1"));
    CHECK(Has(taken, "another folder already has whistles under the key Taken."));

    // The project inside the mods folder is read from the text, not its file.
    const std::string inside = mod_rules::ValidateProjectJson(
        L"", root.Mods(), root.Path("mods/Alpha"), "[mod]\nkey = Alpha\n" + Whistle("Rise", "SSLL", "7")
    );
    CHECK(Has(inside, "\"folders\":[\"Aardvark\",\"Alpha\",\"Zed\"]"));
    CHECK(Has(inside, "\"errors\":0,\"warnings\":0,\"notes\":0"));
}

void TestTurnedOff() {
    std::printf("Project check and turned-off mods\n");
    TempRoot root;
    root.Write("mods/Off/canopy.ini", "[mod]\nkey = Off\n[flag.Seen]\n");
    root.Write("mods/CanopyData/disabled.txt", "Off\nAlpha\n");
    root.Write("projects/Alpha/canopy.ini", "");
    const std::string json = mod_rules::ValidateProjectJson(
        L"", root.Mods(), root.Path("projects/Alpha"), "[mod]\nkey = Alpha\n" + Whistle("Rise", "SSLL", "Off.Seen")
    );
    // No note about Off; its flag is not there; the project is checked even
    // though the list names it.
    CHECK(Has(json, "\"notes\":0"));
    CHECK(!Has(json, "turned off"));
    CHECK(Has(json, "\"errors\":1"));
    CHECK(Has(json, "Off.Seen is neither."));
}

void TestFailures() {
    std::printf("Project check failures\n");
    TempRoot root;
    root.Write("projects/Alpha/canopy.ini", "[mod]\n");
    const std::wstring project = root.Path("projects/Alpha");

    const std::string noProject = mod_rules::ValidateProjectJson(L"", L"", L"", "");
    CHECK(Has(noProject, "\"ok\":false,\"error\":\"Give the project's folder.\""));
    // No game and no mods folder: the project alone.
    const std::string alone = mod_rules::ValidateProjectJson(L"", L"", project + L"\\", "[mod]\nkey = Alpha\n");
    CHECK(Has(alone, "\"ok\":true") && Has(alone, "\"folders\":[\"Alpha\"]") && Has(alone, "\"vanilla\":false"));
    // A mods folder given by name must exist.
    const std::string noMods = mod_rules::ValidateProjectJson(L"", root.Path("nowhere"), project, "");
    CHECK(Has(noMods, "\"ok\":false,\"error\":\"The mods folder "));
    CHECK(Has(noMods, " is not a folder.\""));
    const std::string badGame = mod_rules::ValidateProjectJson(root.Path("nogame"), L"", project, "");
    CHECK(Has(badGame, "\"ok\":false,\"error\":\"The game folder could not be opened: "));

    // A path the ANSI file APIs cannot spell: a CJK and an emoji folder name.
    std::wstring name;
    platform::Utf8ToWide("\xE4\xB8\xAD\xF0\x9F\x8C\xB2", name);
    const std::wstring wide = platform::Join(root.Path("projects"), name);
    std::error_code error;
    fs::create_directories(fs::path(wide), error);
    std::string narrow;
    if (!platform::WideToAnsi(wide, narrow)) {
        const std::string json = mod_rules::ValidateProjectJson(L"", L"", wide, "[mod]\n");
        CHECK(Has(json, "\"ok\":false,\"error\":\"The project folder's path has characters the game cannot open"));
    }
}

void TestWithGame() {
    std::printf("Project check with the game\n");
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return;
    }
    TempRoot root;
    root.Write("projects/Alpha/canopy.ini", "");
    // An empty mods folder, so the installed mods do not count.
    const std::string json = mod_rules::ValidateProjectJson(
        folder.wstring(), root.Mods(), root.Path("projects/Alpha"), "[mod]\nkey = Alpha\n[flag.Seen]\n"
    );
    CHECK(Has(json, "\"ok\":true"));
    CHECK(Has(json, "\"vanilla\":true"));
    CHECK(Has(json, "\"folders\":[\"Alpha\"]"));
    CHECK(Has(json, "\"errors\":0"));
}

}  // namespace

void TestRulesProject(int& checks, int& failures) {
    TestFolderDir();
    TestProjectFiles();
    TestUnsavedText();
    TestShadowedFolder();
    TestTurnedOff();
    TestFailures();
    TestWithGame();
    checks += g_checks;
    failures += g_failures;
}
