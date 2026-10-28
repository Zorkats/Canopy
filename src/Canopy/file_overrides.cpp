#define NOMINMAX
#include "file_overrides.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "core.h"
#include "disabled_mods.h"

// =============================================================
// FILE OVERRIDES DESIGN
// =============================================================
// The game reads every content file through its file system, by a relative
// path such as data/database/string.dat and a root. Each read (exists, size,
// open, and the database registry's own checks) first asks the archive lookup
// for an entry in the mounted archives. Only when there is none does it ask
// the path builder for the loose file's path, then stat and open that path.
// So two detours replace a file everywhere it is read: the lookup finds no
// entry for a path a mod ships, and the path builder returns the mod's file
// for it. The game's own loose file code then opens the mod's file.
//
// The path builder returns its result in a per-thread buffer. For the data
// root (2 in retail) and the save root (4) that is a WORD 1 marker followed by
// a wide path, and every caller checks the marker before it picks _wstat64,
// _waccess or CreateFileW over the narrow functions. The detour returns the
// same form, from a per-thread buffer of the same size, so the game and mod
// folders may have any name in any language. It only answers for the root the
// archives are mounted on, read from the file system at call time, and never
// for the save root, whose writer builds every save path with it, or for root
// 6, the raw path an open retries with.
//
// The lookup lowercases the path with _strlwr, which only changes ASCII
// letters, and compares it byte for byte with the archive names, which use
// forward slashes. The index keys are built the same way: the path below the
// mod folder, forward slashes, ASCII lowercase. A name with other characters
// could never match a game path, so it is skipped with a log line.
//
// The mod folders are indexed once, inside the game init detour (main.cpp),
// before the game creates its file system. The index never changes after
// that, so the detours take no lock. They run on every thread that reads a
// file, the file system's worker threads included. A path the index lacks
// costs a prefix check, or one binary search when it starts with data/.
//
// The game's write and delete calls go through the same path builder, so a
// write to an overridden path would reach the mod's file; the game never
// writes or deletes under data/. The file system's memory cache keys files by
// a path cut to 63 characters, but retail runs with the cache off. A file no
// archive has is outside this feature: nothing asks for it by name unless an
// overridden table does, and the script folder listing never sees mod folders.

namespace file_overrides {
namespace {

using namespace core;

using FindArchiveEntryFn = int32_t(__fastcall*)(void* fs, const char* path, int32_t root);
using BuildPathFn = void*(__fastcall*)(void* unused, int32_t root, const char* path);

constexpr const wchar_t* kModsFolder = L"mods";
constexpr const wchar_t* kDataFolder = L"data";
// Per-save mod data (save_data.cpp), never a data mod.
constexpr const wchar_t* kReservedFolder = L"CanopyData";
// The lookup's result for "not in any archive".
constexpr int32_t kNoEntry = -1;
// The lookup copies the path into a 512-byte buffer; a longer one is left to
// the game.
constexpr size_t kMaxKeyLength = 511;
// Folders deeper than this below data are not indexed, which also ends a
// junction that loops back on itself.
constexpr int kMaxDepth = 16;
// The first read of each override is logged, up to this many.
constexpr int kMaxReadLogs = 64;

// -------------------------------------------------------------
// Risk classes
// -------------------------------------------------------------
// What an override of each kind of file can change, from static analysis of
// the game's archives and of the code that reads each file. Every override of
// a class that warns is logged by mod and path, so a player who reports a
// broken save or a game that will not start can be asked for the log.
enum class Risk : uint8_t {
    Core,
    Script,
    Progression,
    Balance,
    Map,
    Presentation,
    Other,
};
constexpr size_t kRiskCount = 7;

struct RiskInfo {
    const char* name;
    // Null for a class that does not warn.
    const char* warning;
};

constexpr RiskInfo kRisks[kRiskCount] = {
    {"core", "can stop the game from loading"},
    {"script", "can change events, flags and progression"},
    {"progression", "can change events, quests, flags and unlocks"},
    {"balance", "can change prices, yields, recipes, costs and hitboxes"},
    {"map", "can move collision, event triggers and NPC paths"},
    {"presentation", nullptr},
    {"other", nullptr},
};

struct RiskRule {
    const char* pattern;
    Risk risk;
};

// The first rule that matches a path decides its class; a path no rule
// matches is Other. Patterns are lowercase, like the keys. '*' matches any
// run of characters within one folder or file name, and a pattern that ends
// in '/' matches everything below that folder.
constexpr RiskRule kRiskRules[] = {
    // Core: the archive list the mounts read, the framework tables that wire
    // the engine, the script types, and the archives themselves.
    {"data/database/archive.dat", Risk::Core},
    {"data/database/framework_*.dat", Risk::Core},
    {"data/database/scripttype.dat", Risk::Core},
    {"data/*_1_00.dat", Risk::Core},

    // Lua runs events, quests, flags and the main game task.
    {"data/script/", Risk::Script},

    // Map layout and materials, and the navmeshes NPC routes walk.
    {"data/map/", Risk::Map},
    {"data/navi/", Risk::Map},

    // Progression: events, flags, quests, rewards, unlocks, schedules and
    // achievements. demolist names Lua entry points. talk holds dialogue
    // structure next to its strings.
    {"data/database/event.dat", Risk::Progression},
    {"data/database/eventfeature.dat", Risk::Progression},
    {"data/database/gameflag.dat", Risk::Progression},
    {"data/database/quest.dat", Risk::Progression},
    {"data/database/secretquest.dat", Risk::Progression},
    {"data/database/subquestitem.dat", Risk::Progression},
    {"data/database/demolist.dat", Risk::Progression},
    {"data/database/demofeature.dat", Risk::Progression},
    {"data/database/debuggameprogress.dat", Risk::Progression},
    {"data/database/latenightevent.dat", Risk::Progression},
    {"data/database/letter.dat", Risk::Progression},
    {"data/database/lettersend.dat", Risk::Progression},
    {"data/database/reminiscence.dat", Risk::Progression},
    {"data/database/lostbook.dat", Risk::Progression},
    {"data/database/facilityrelease.dat", Risk::Progression},
    {"data/database/huntreward.dat", Risk::Progression},
    {"data/database/shootgamereward.dat", Risk::Progression},
    {"data/database/treasurebox.dat", Risk::Progression},
    {"data/database/placement*.dat", Risk::Progression},
    {"data/database/mapdiff.dat", Risk::Progression},
    {"data/database/characterschedule.dat", Risk::Progression},
    {"data/database/charactertalk.dat", Risk::Progression},
    {"data/database/talkcommand.dat", Risk::Progression},
    {"data/database/cmd.dat", Risk::Progression},
    {"data/database/gamevalue.dat", Risk::Progression},
    {"data/database/gameidvalue.dat", Risk::Progression},
    {"data/database/gametablevalue.dat", Risk::Progression},
    {"data/database/gamesystemvalue.dat", Risk::Progression},
    {"data/database/dailyresetvalue.dat", Risk::Progression},
    {"data/database/trophy.dat", Risk::Progression},
    {"data/database/talk.dat", Risk::Progression},

    // Balance: prices, yields, recipes and costs; the fairy collision and
    // bounds data behind hitboxes and contact triggers; the fishing areas.
    {"data/database/item.dat", Risk::Balance},
    {"data/database/itemfeature.dat", Risk::Balance},
    {"data/database/storesales.dat", Risk::Balance},
    {"data/database/randomsale.dat", Risk::Balance},
    {"data/database/cooking.dat", Risk::Balance},
    {"data/database/craft.dat", Risk::Balance},
    {"data/database/crops.dat", Risk::Balance},
    {"data/database/fish.dat", Risk::Balance},
    {"data/database/livestock.dat", Risk::Balance},
    {"data/database/skilltree.dat", Risk::Balance},
    {"data/database/skillspherecost.dat", Risk::Balance},
    {"data/database/gachapon.dat", Risk::Balance},
    {"data/database/blacksmith.dat", Risk::Balance},
    {"data/database/offeringitem.dat", Risk::Balance},
    {"data/fairy/anmlayer.dat", Risk::Balance},
    {"data/fairy/anmcollision.dat", Risk::Balance},
    {"data/fairy/anmaabb.dat", Risk::Balance},
    {"data/misc/map_9999_fisharea_*.tga", Risk::Balance},

    // Presentation tables: asset paths and render parameters (a bad row can
    // still break loading), UI text whose tags only display values, and the
    // player's cosmetics, whose rows may hold unlock conditions.
    {"data/database/texture.dat", Risk::Presentation},
    {"data/database/model.dat", Risk::Presentation},
    {"data/database/anime*.dat", Risk::Presentation},
    {"data/database/hsvreplacetexture.dat", Risk::Presentation},
    {"data/database/icon.dat", Risk::Presentation},
    {"data/database/font.dat", Risk::Presentation},
    {"data/database/se.dat", Risk::Presentation},
    {"data/database/bgm.dat", Risk::Presentation},
    {"data/database/environmentse.dat", Risk::Presentation},
    {"data/database/footselist.dat", Risk::Presentation},
    {"data/database/particle*.dat", Risk::Presentation},
    {"data/database/shader.dat", Risk::Presentation},
    {"data/database/framebuffer.dat", Risk::Presentation},
    {"data/database/renderstep.dat", Risk::Presentation},
    {"data/database/waterparam.dat", Risk::Presentation},
    {"data/database/window.dat", Risk::Presentation},
    {"data/database/string.dat", Risk::Presentation},
    {"data/database/msg.dat", Risk::Presentation},
    {"data/database/tips.dat", Risk::Presentation},
    {"data/database/updatetext.dat", Risk::Presentation},
    {"data/database/logtext.dat", Risk::Presentation},
    {"data/database/multilinetext.dat", Risk::Presentation},
    {"data/database/credit.dat", Risk::Presentation},
    {"data/database/talknameplate.dat", Risk::Presentation},
    {"data/database/playercrothing.dat", Risk::Presentation},
    {"data/database/player*hairstyle.dat", Risk::Presentation},
    {"data/database/player*accessory.dat", Risk::Presentation},

    // Assets: textures, models and animation, shaders, fonts, music and sound.
    {"data/texture/", Risk::Presentation},
    {"data/fairy/", Risk::Presentation},
    {"data/shader/", Risk::Presentation},
    {"data/misc/", Risk::Presentation},
    {"data/bgm/", Risk::Presentation},
    {"data/se/", Risk::Presentation},
};

// -------------------------------------------------------------
// Index
// -------------------------------------------------------------
struct Override {
    // Relative path as the game's lookup sees it: data/database/string.dat.
    std::string key;
    // The path builder's answer: the marker, then the mod file's full path.
    std::wstring path;
    size_t mod = 0;
    Risk risk = Risk::Other;
    // A file Canopy made, not one a mod ships.
    bool generated = false;
};

FindArchiveEntryFn g_OriginalFindArchiveEntry = nullptr;
BuildPathFn g_OriginalBuildPath = nullptr;
// Sorted by key, one entry per key. Built before the hooks are enabled,
// never changed after.
std::vector<Override> g_Index;
// Folder names in load order, UTF-8, for the log.
std::vector<std::string> g_ModNames;
// One flag per index entry, set on its first read.
std::unique_ptr<std::atomic<bool>[]> g_Read;
std::atomic<int> g_ReadLogs{0};
// Install indexed the mod folders on this build, so generated files may
// be added; and whether the hooks exist.
bool g_Ready = false;
bool g_Hooked = false;

// The path builder's buffer is per thread, like the game's own, which is
// 0x2000 bytes, so a caller sees the same capacity either way.
thread_local wchar_t t_Path[retail::kBuildPathCapacity];

// True when key matches pattern, as kRiskRules describes.
bool Matches(const char* pattern, const char* key) {
    while (true) {
        if (*pattern == '*') {
            ++pattern;
            for (const char* rest = key;; ++rest) {
                if (Matches(pattern, rest)) {
                    return true;
                }
                if (*rest == '\0' || *rest == '/') {
                    return false;
                }
            }
        }
        if (*pattern == '\0') {
            return *key == '\0';
        }
        if (*pattern == '/' && pattern[1] == '\0') {
            return *key == '/';
        }
        if (*pattern != *key) {
            return false;
        }
        ++pattern;
        ++key;
    }
}

Risk Classify(const std::string& key) {
    for (const RiskRule& rule : kRiskRules) {
        if (Matches(rule.pattern, key.c_str())) {
            return rule.risk;
        }
    }
    return Risk::Other;
}

std::string Utf8(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr
    );
    std::string result(static_cast<size_t>(size > 0 ? size : 0), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    }
    return result;
}

// The name lowercased like the game's lookup, false when it holds a
// character outside ASCII.
bool AsciiLower(const std::wstring& name, std::string& lowered) {
    lowered.clear();
    for (const wchar_t c : name) {
        if (c == 0 || c > 0x7F) {
            return false;
        }
        lowered += static_cast<char>(c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c);
    }
    return true;
}

bool IsFolder(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

// The folder Canopy was loaded from, wide, or empty.
std::wstring LoaderFolder() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&LoaderFolder),
            &module
        )) {
        return std::wstring();
    }
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return std::wstring();
    }
    std::wstring folder(path.data(), length);
    const size_t slash = folder.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : folder.substr(0, slash);
}

// Every folder in mods that holds a data folder, in load order.
std::vector<std::wstring> ModFolders(const std::wstring& mods) {
    std::vector<std::wstring> folders;
    // Folders turned off in mods\CanopyData\disabled.txt ship nothing. The
    // loader logs each one when it loads the mod DLLs, so this stays quiet.
    const std::vector<std::string> turnedOff = disabled_mods::Cached(ModFilePath("mods"));
    WIN32_FIND_DATAW found = {};
    HANDLE search =
        FindFirstFileExW((mods + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0);
    if (search == INVALID_HANDLE_VALUE) {
        return folders;
    }
    do {
        const std::wstring name = found.cFileName;
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name == L"." || name == L".." ||
            _wcsicmp(name.c_str(), kReservedFolder) == 0) {
            continue;
        }
        if (disabled_mods::IsDisabled(turnedOff, Utf8(name))) {
            continue;
        }
        if (IsFolder(mods + L"\\" + name + L"\\" + kDataFolder)) {
            folders.push_back(name);
        }
    } while (FindNextFileW(search, &found));
    FindClose(search);
    // The same order as mod DLLs: by name, without case.
    std::sort(folders.begin(), folders.end(), [](const std::wstring& left, const std::wstring& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    return folders;
}

// Adds every file below folder, whose key is key, to files.
void Walk(
    const std::wstring& folder, const std::string& key, int depth, size_t mod, std::vector<Override>& files
) {
    WIN32_FIND_DATAW found = {};
    HANDLE search =
        FindFirstFileExW((folder + L"\\*").c_str(), FindExInfoBasic, &found, FindExSearchNameMatch, nullptr, 0);
    if (search == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string& modName = g_ModNames[mod];
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const bool isFolder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        // Thumbs.db, desktop.ini and the like.
        if (!isFolder && (found.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
            continue;
        }
        const std::wstring path = folder + L"\\" + name;
        std::string lowered;
        if (!AsciiLower(name, lowered)) {
            Log("Override skipped in " + modName + ": " + key + "/" + Utf8(name) +
                " has a name with letters outside ASCII, which no game path uses.");
            continue;
        }
        const std::string child = key + "/" + lowered;
        if (isFolder) {
            if (depth < kMaxDepth) {
                Walk(path, child, depth + 1, mod, files);
            } else {
                Log("Override skipped in " + modName + ": " + child + " is nested too deep.");
            }
            continue;
        }
        // The game opens loose files without the long path prefix.
        if (path.size() >= MAX_PATH || child.size() > kMaxKeyLength) {
            Log("Override skipped in " + modName + ": the full path of " + child +
                " is longer than Windows allows here. Move the game to a shorter folder.");
            continue;
        }
        Override entry;
        entry.key = child;
        entry.path = std::wstring(1, static_cast<wchar_t>(retail::kWidePathMarker)) + path;
        entry.mod = mod;
        entry.risk = Classify(child);
        files.push_back(std::move(entry));
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

// "3 files (script 1, presentation 2)".
std::string Summary(const size_t (&counts)[kRiskCount]) {
    size_t total = 0;
    std::string parts;
    for (size_t risk = 0; risk < kRiskCount; ++risk) {
        if (counts[risk] == 0) {
            continue;
        }
        total += counts[risk];
        parts += (parts.empty() ? "" : ", ") + std::string(kRisks[risk].name) + " " + std::to_string(counts[risk]);
    }
    if (total == 0) {
        return "no files";
    }
    return std::to_string(total) + (total == 1 ? " file (" : " files (") + parts + ")";
}

// Indexes every mod folder in mods. Logs what each mod overrides, a warning
// per risky file and every conflict.
void BuildIndex(const std::wstring& mods) {
    std::vector<Override> files;
    for (const std::wstring& folder : ModFolders(mods)) {
        g_ModNames.push_back(Utf8(folder));
        Walk(mods + L"\\" + folder + L"\\" + kDataFolder, "data", 0, g_ModNames.size() - 1, files);
    }
    // Stable, so the files of one key stay in load order.
    std::stable_sort(files.begin(), files.end(), [](const Override& left, const Override& right) {
        return std::strcmp(left.key.c_str(), right.key.c_str()) < 0;
    });

    // The last mod in load order wins a key.
    std::vector<bool> replaced(files.size(), false);
    for (size_t index = 1; index < files.size(); ++index) {
        if (files[index].key == files[index - 1].key) {
            replaced[index - 1] = true;
            Log("Override conflict: " + g_ModNames[files[index - 1].mod] + " and " + g_ModNames[files[index].mod] +
                " both ship " + files[index].key + ". " + g_ModNames[files[index].mod] +
                " wins, it comes later in load order.");
        }
    }

    for (size_t mod = 0; mod < g_ModNames.size(); ++mod) {
        size_t counts[kRiskCount] = {};
        size_t lost = 0;
        for (size_t index = 0; index < files.size(); ++index) {
            if (files[index].mod == mod) {
                ++counts[static_cast<size_t>(files[index].risk)];
                lost += replaced[index] ? 1 : 0;
            }
        }
        std::string line = "Overrides from " + g_ModNames[mod] + ": " + Summary(counts);
        if (lost > 0) {
            line += ", " + std::to_string(lost) + " replaced by a later mod";
        }
        Log(line + ".");
        for (size_t index = 0; index < files.size(); ++index) {
            const Override& file = files[index];
            const RiskInfo& risk = kRisks[static_cast<size_t>(file.risk)];
            if (file.mod == mod && !replaced[index] && risk.warning) {
                Log("Warning: " + g_ModNames[mod] + " overrides " + file.key + ", " + risk.name + " data that " +
                    risk.warning + ".");
            }
        }
    }

    for (size_t index = 0; index < files.size(); ++index) {
        if (!replaced[index]) {
            g_Index.push_back(std::move(files[index]));
        }
    }
    g_Read = std::make_unique<std::atomic<bool>[]>(g_Index.size());
}

// The override for a game path, or null. Lowercases like the game's lookup,
// without allocating: this runs for every file the game reads.
const Override* Find(const char* path) {
    static constexpr char kPrefix[] = "data/";
    if (!path || g_Index.empty()) {
        return nullptr;
    }
    char key[kMaxKeyLength + 1];
    size_t length = 0;
    for (; path[length] != '\0'; ++length) {
        if (length == kMaxKeyLength) {
            return nullptr;
        }
        const char c = path[length];
        key[length] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        if (length < sizeof(kPrefix) - 1 && key[length] != kPrefix[length]) {
            return nullptr;
        }
    }
    key[length] = '\0';
    const auto found = std::lower_bound(g_Index.begin(), g_Index.end(), key, [](const Override& entry, const char* value) {
        return std::strcmp(entry.key.c_str(), value) < 0;
    });
    return found != g_Index.end() && found->key == key ? &*found : nullptr;
}

// True for the root the archives are mounted on, with the default root
// resolved the way the lookup resolves it. Never the save or raw root.
bool IsArchiveRoot(int32_t root) {
    const uint8_t* fs = ReadGlobal<const uint8_t*>(retail::kFileSystem);
    if (!fs) {
        return false;
    }
    if (root == retail::kFsRootDefault) {
        root = ReadAt<int32_t>(fs, retail::kFsDefaultRoot);
    }
    return root == ReadAt<int32_t>(fs, retail::kFsArchiveRoot) && root != retail::kFsRootSave &&
           root != retail::kFsRootRaw;
}

void LogFirstRead(const Override& entry) {
    const size_t index = static_cast<size_t>(&entry - g_Index.data());
    if (g_Read[index].exchange(true)) {
        return;
    }
    const int count = g_ReadLogs.fetch_add(1);
    if (count < kMaxReadLogs) {
        Log("Override read: " + entry.key + " from " + g_ModNames[entry.mod] + ".");
    } else if (count == kMaxReadLogs) {
        Log("Further override reads are not logged.");
    }
}

// An overridden file is never in the archives, so every read of it falls
// through to the path builder.
int32_t __fastcall Detour_FindArchiveEntry(void* fs, const char* path, int32_t root) {
    if (Find(path) && IsArchiveRoot(root)) {
        return kNoEntry;
    }
    return g_OriginalFindArchiveEntry(fs, path, root);
}

void* __fastcall Detour_BuildPath(void* unused, int32_t root, const char* path) {
    const Override* entry = Find(path);
    if (!entry || !IsArchiveRoot(root)) {
        return g_OriginalBuildPath(unused, root, path);
    }
    // The index rejects paths that do not fit, so this never truncates.
    wcsncpy_s(t_Path, entry->path.c_str(), _TRUNCATE);
    LogFirstRead(*entry);
    return t_Path;
}

bool CreateHooks() {
    DetourSet detours("File overrides");
    if (!detours.Add(
            retail::kFindArchiveEntry,
            reinterpret_cast<void*>(&Detour_FindArchiveEntry),
            reinterpret_cast<void**>(&g_OriginalFindArchiveEntry),
            "findArchiveEntry"
        ) ||
        !detours.Add(
            retail::kBuildPath,
            reinterpret_cast<void*>(&Detour_BuildPath),
            reinterpret_cast<void**>(&g_OriginalBuildPath),
            "buildPath"
        )) {
        detours.RemoveAll();
        g_Ready = false;
        return false;
    }
    g_Hooked = true;
    return true;
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kFileOverrideRequirements)) {
        return false;
    }
    const std::wstring folder = LoaderFolder();
    if (folder.empty()) {
        Log("The game folder's path could not be read, so file overrides are off.");
        return false;
    }
    BuildIndex(folder + L"\\" + kModsFolder);
    g_Ready = true;
    if (g_Index.empty()) {
        Log("No mod ships data files, so file overrides are off.");
        return false;
    }
    if (!CreateHooks()) {
        g_Index.clear();
        return false;
    }
    Log("File overrides: " + std::to_string(g_Index.size()) + (g_Index.size() == 1 ? " file from " : " files from ") +
        std::to_string(g_ModNames.size()) + (g_ModNames.size() == 1 ? " mod folder." : " mod folders."));
    return true;
}

std::vector<std::string> OverrideFolders() {
    std::vector<bool> used(g_ModNames.size(), false);
    for (const Override& entry : g_Index) {
        used[entry.mod] = used[entry.mod] || !entry.generated;
    }
    std::vector<std::string> folders;
    for (size_t mod = 0; mod < g_ModNames.size(); ++mod) {
        if (used[mod]) {
            folders.push_back(g_ModNames[mod]);
        }
    }
    return folders;
}

bool AddGeneratedFile(const std::string& gamePath, const std::wstring& diskPath, const std::string& modName) {
    if (!g_Ready || diskPath.empty() || diskPath.size() >= MAX_PATH || gamePath.size() > kMaxKeyLength ||
        gamePath.compare(0, 5, "data/") != 0) {
        return false;
    }
    // The lookup's form: printable ASCII, lowercase, forward slashes.
    for (const char c : gamePath) {
        if (c <= ' ' || c > '~' || (c >= 'A' && c <= 'Z') || c == '\\') {
            return false;
        }
    }
    const auto at = std::lower_bound(g_Index.begin(), g_Index.end(), gamePath, [](const Override& entry, const std::string& key) {
        return std::strcmp(entry.key.c_str(), key.c_str()) < 0;
    });
    if (at != g_Index.end() && at->key == gamePath) {
        Log("Override conflict: " + g_ModNames[at->mod] + " ships " + gamePath + ", which Canopy makes for " +
            modName + ". The shipped file is kept.");
        return false;
    }
    if (!g_Hooked && !CreateHooks()) {
        return false;
    }
    const auto name = std::find(g_ModNames.begin(), g_ModNames.end(), modName);
    Override entry;
    entry.key = gamePath;
    entry.path = std::wstring(1, static_cast<wchar_t>(retail::kWidePathMarker)) + diskPath;
    entry.mod = static_cast<size_t>(name - g_ModNames.begin());
    entry.risk = Classify(gamePath);
    entry.generated = true;
    if (name == g_ModNames.end()) {
        g_ModNames.push_back(modName);
    }
    g_Index.insert(at, std::move(entry));
    // Nothing has been read yet: the hooks are enabled once every module
    // has installed.
    g_Read = std::make_unique<std::atomic<bool>[]>(g_Index.size());
    return true;
}

}  // namespace file_overrides
