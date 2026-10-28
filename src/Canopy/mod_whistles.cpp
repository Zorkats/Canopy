#define NOMINMAX
#include "mod_whistles.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "hook_chain.h"
#include "mod_art.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_whistles.h"
#include "save_data.h"
#include "save_overlay.h"

// =============================================================
// CUSTOM WHISTLES DESIGN
// =============================================================
// A mod defines whistles in its canopy.ini, one [whistle.Key] section each,
// as a copy of a vanilla whistle with its own name, melody, learn flag and,
// optionally, its own text Lua script. A folder with whistles and no Lua or
// DLL is still registered as a mod. With no whistles anywhere, nothing
// below is hooked.
//
// Rules. What a [whistle.Key] section accepts is CanopyCore's
// rules_whistles, shared with the desktop tools; this file installs what
// they read.
//
// Ids. Each whistle gets 1800000000 + n, where n comes from
// mods\CanopyData\whistles.ini, given out once and never reused, like the
// item ids. The range sits above the recipe range and inside int32.
//
// Table. CDatabase<WhistleData>::load 0xAE090 takes (db, name, blob) like
// the item table's load, once, on the boot worker. The detour builds one
// blob with the vanilla rows, then the mod rows, and calls the original once
// with it. A mod row is the base whistle's row with a new id, enum name,
// name, learn flag and melody, the mod's script module and entry or none,
// and, when the mod picks one, another vanilla score icon or a mod icon
// in its place. The base's guide animation and its other texts stay.
//
// Play. The whistle menu 0x441DF0 lists every row whose learn flag bit is
// set, and the matcher 0x443CC0 runs the listed row whose note count and
// bits equal what was played, so each melody must be unique: a mod melody
// that equals a vanilla one or an earlier mod's is skipped. The learn flag
// is a mod flag or a vanilla flag; Canopy never sets it. On a match the game
// runs module.entry through its script engine 0x445200 when the row has a
// module. The script loader 0x73BA90 is detoured, chained after the Lua
// host's and the villagers' detours; a whistle module is
// whistle_<mod>_<key>, compiled from the mod folder in text mode and called
// with its module name, like require does. A module that does not compile
// or returns no table is replaced by one whose every entry does nothing.
// Whistle scripts are game scripts: they run with every game command,
// outside the Lua host's sandbox.
//
// Save. Nothing in the save names a whistle: the menu rebuilds its list
// from the flags each time it opens, and the flags are saved by their
// owners. So whistles have no overlay adapter.
//
// Title. The tick looks each id up through the game's own find and checks
// its learn flag and melody; a row that fails is taken out of the record
// list the menu walks, so it is never listed.
//
// Threads. The table detour runs on the boot worker and only touches the
// blob, the whistle definitions and the log. Everything else runs on the
// main thread.

namespace mod_whistles {
namespace {

using namespace core;
using mod_tables::Plural;

using DatabaseLoadFn = int64_t(__fastcall*)(uint8_t* db, const char* name, const uint8_t* blob);

struct lua_State;
using LuaCFunction = int (*)(lua_State* state);
using LuaSetTopFn = void(__fastcall*)(lua_State* state, int index);
using LuaTypeFn = int(__fastcall*)(lua_State* state, int index);
using LuaToLStringFn = const char*(__fastcall*)(lua_State* state, int index, size_t* length);
using LuaPushStringFn = const char*(__fastcall*)(lua_State* state, const char* text);
using LuaPCallKFn = int(__fastcall*)(
    lua_State* state, int argumentCount, int resultCount, int handler, intptr_t context, void* continuation
);
using LuaLoadBufferXFn = int(__fastcall*)(
    lua_State* state, const char* buffer, size_t size, const char* name, const char* mode
);

// ---- whistle.dat row format ----------------------------------------------
// 300 bytes: id, enum name, names, learn flag (i32), guide animation (i32),
// score icon, notes, note count (i32), script module and entry, 21 texts the
// game keeps the same placeholder in, and 16 zero bytes.
constexpr size_t kRowId = 0;
constexpr size_t kRowEnum = 8;
constexpr size_t kRowNames = 16;
constexpr size_t kRowLearn = 72;
constexpr size_t kRowScore = 80;
constexpr size_t kRowNotes = 88;
constexpr size_t kRowNoteCount = 96;
constexpr size_t kRowModule = 100;
constexpr size_t kRowEntry = 108;
constexpr size_t kRowSize = 300;

constexpr const char* kTableFile = "whistle.dat";
constexpr uint64_t kBandBase = 1800000000;
constexpr uint64_t kBandSize = 100000000;
constexpr const char* kModulePrefix = "whistle_";
// The loader's chain owner, after the Lua host (-1) and villagers (-2).
constexpr int64_t kLoaderChainOwner = -3;
constexpr int kTypeString = 4;
constexpr int kTypeTable = 5;
constexpr const char* kRegistryName = "whistles.ini";
constexpr const char* kRegistryHeader =
    "; Canopy custom whistle ids. Each mod whistle keeps its number for good, and a number is never given\r\n"
    "; out twice. Do not edit or delete this file.\r\n";
// Stands in for a module that failed: every entry is a function that does
// nothing.
constexpr const char* kStubSource = "return setmetatable({}, {__index = function() return function() end end})";
constexpr const char* kStubName = "=canopy whistle stub";

// What the rules read, and what installing it adds.
struct WhistleDef : mod_rules::WhistleSpec {
    uint64_t id = 0;
    std::string enumName;
    // Set by the table detour before its done flag.
    bool appended = false;
    // Set at the title once the game's own lookup finds it.
    bool enabled = false;
    bool loggedScript = false;
};

struct LuaApi {
    LuaSetTopFn setTop;
    LuaTypeFn type;
    LuaToLStringFn toLString;
    LuaPushStringFn pushString;
    LuaPCallKFn pcallK;
    LuaLoadBufferXFn loadBufferX;
};

std::vector<WhistleDef> g_Whistles;
bool g_Active = false;
bool g_LoaderHooked = false;
LuaApi g_Lua = {};

DatabaseLoadFn g_OriginalLoad = nullptr;
LuaCFunction g_OriginalLoader = nullptr;

// Table load, boot worker.
std::atomic<bool> g_TableSeen{false};
std::atomic<bool> g_TableDone{false};
std::atomic<bool> g_InLoad{false};
std::atomic<uint8_t*> g_Database{nullptr};
bool g_Checked = false;
// Records the title check took out of the record list.
std::set<const void*> g_Broken;

using mod_rules::MelodyText;

// ---- Table append (boot worker) ------------------------------------------

std::vector<uint8_t> BuildRow(const WhistleDef& whistle, const mod_tables::Row& base, mod_tables::TextPool& pool) {
    std::vector<uint8_t> row(base.data, base.data + base.size);
    WriteAt<uint64_t>(row.data(), kRowId, whistle.id);
    pool.Write(row, kRowEnum, whistle.enumName);
    for (size_t language = 0; language < retail::kLanguageCount; ++language) {
        pool.Write(row, kRowNames + mod_tables::kTextRef * language, whistle.names[language]);
    }
    WriteAt<int32_t>(row.data(), kRowLearn, whistle.learn);
    if (whistle.icon != 0 && mod_art::IconAdded(whistle.icon)) {
        WriteAt<uint64_t>(row.data(), kRowScore, whistle.icon);
    } else if (whistle.score != 0) {
        WriteAt<uint64_t>(row.data(), kRowScore, whistle.score);
    }
    WriteAt<uint64_t>(row.data(), kRowNotes, whistle.notes);
    WriteAt<int32_t>(row.data(), kRowNoteCount, whistle.noteCount);
    pool.Write(row, kRowModule, whistle.module);
    pool.Write(row, kRowEntry, whistle.module.empty() ? "" : whistle.entry);
    return row;
}

size_t BuildBlob(uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged) {
    mod_tables::Table table;
    if (!mod_tables::ParseTable(blob, kRowSize, kRowEnum, table)) {
        Log("Mod whistles: the whistle table rows do not add up, so no whistles were appended.");
        return 0;
    }
    std::set<mod_rules::Melody> melodies;
    for (const auto& [id, row] : table.rows) {
        melodies.insert(mod_rules::RowMelody(row.data));
    }
    mod_rules::Report report = mod_tables::LogReport();
    mod_tables::Appender appender(table);
    for (WhistleDef& whistle : g_Whistles) {
        const std::string where = "Mod whistle " + whistle.fullKey;
        if (table.rows.count(whistle.id) || mod_tables::DatabaseHas(db, whistle.id)) {
            Log(where + " skipped, its id " + std::to_string(whistle.id) + " is already in the whistle table.");
            continue;
        }
        const auto base = table.rows.find(whistle.baseId);
        const bool found = base != table.rows.end();
        const blob::Row baseRow = found ? blob::Row{base->second.data, base->second.size} : blob::Row{};
        if (!mod_rules::CheckAgainstTable(whistle, found ? &baseRow : nullptr, melodies, report)) {
            continue;
        }
        if (whistle.icon != 0 && !mod_art::IconAdded(whistle.icon)) {
            Log(where + " keeps its " + (whistle.score != 0 ? "score" : "base whistle's score icon") +
                ", its mod icon " + std::to_string(whistle.icon) + " is not in the icon table.");
        }
        whistle.enumName =
            appender.UniqueEnum("WHISTLE_ID_CANOPY_" + mod_tables::EnumPart(whistle.modKey) + "_" +
                                mod_tables::EnumPart(whistle.key));
        appender.Add(BuildRow(whistle, base->second, appender.Pool()));
        whistle.appended = true;
    }
    if (appender.Count() == 0) {
        return 0;
    }
    appender.Merge(blob, merged);
    return appender.Count();
}

__declspec(noinline) void TryBuild(uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged, size_t& appended) {
    try {
        appended = BuildBlob(db, blob, merged);
    } catch (...) {
        merged.clear();
        appended = 0;
        for (WhistleDef& whistle : g_Whistles) {
            whistle.appended = false;
        }
        Log("Mod whistles: building the whistle rows failed, so none were appended.");
    }
}

int64_t __fastcall Detour_TableLoad(uint8_t* db, const char* name, const uint8_t* blob) {
    if (!blob || g_InLoad.load() || !mod_tables::IsTable(name, kTableFile) || g_TableSeen.exchange(true)) {
        return g_OriginalLoad(db, name, blob);
    }
    std::vector<uint8_t> merged;
    size_t appended = 0;
    TryBuild(db, blob, merged, appended);
    g_InLoad = true;
    const int64_t result = g_OriginalLoad(db, name, merged.empty() ? blob : merged.data());
    g_InLoad = false;
    if (!merged.empty()) {
        Log("Mod whistles: " + Plural(appended, "whistle", "whistles") + " appended to the whistle table.");
    }
    g_Database.store(db, std::memory_order_release);
    g_TableDone.store(true, std::memory_order_release);
    return result;
}

// ---- Script modules --------------------------------------------------------

WhistleDef* FindModule(const char* name) {
    if (!name || _strnicmp(name, kModulePrefix, std::strlen(kModulePrefix)) != 0) {
        return nullptr;
    }
    for (WhistleDef& whistle : g_Whistles) {
        if (!whistle.module.empty() && _stricmp(whistle.module.c_str(), name) == 0) {
            return &whistle;
        }
    }
    return nullptr;
}

__declspec(noinline) void LogScriptFailure(WhistleDef& whistle, const char* message) {
    try {
        if (whistle.loggedScript) {
            return;
        }
        whistle.loggedScript = true;
        std::string text = message ? message : "it returned no module table";
        if (text.size() > 300) {
            text.resize(300);
        }
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');
        Log("[" + whistle.modKey + "] Whistle " + whistle.key + "'s script " + whistle.script +
            " failed, so the whistle does nothing: " + text + ".");
    } catch (...) {
    }
}

// Compiles and runs the whistle's module and leaves its table on the stack.
// On failure a module whose entries do nothing is left instead.
int ServeModule(lua_State* state, WhistleDef& whistle) {
    if (g_Lua.loadBufferX(state, whistle.source.data(), whistle.source.size(), whistle.chunkName.c_str(), "t") == 0) {
        g_Lua.pushString(state, whistle.module.c_str());
        if (g_Lua.pcallK(state, 1, 1, 0, 0, nullptr) == 0 && g_Lua.type(state, -1) == kTypeTable) {
            return 1;
        }
    }
    LogScriptFailure(whistle, g_Lua.type(state, -1) == kTypeString ? g_Lua.toLString(state, -1, nullptr) : nullptr);
    g_Lua.setTop(state, 0);
    if (g_Lua.loadBufferX(state, kStubSource, std::strlen(kStubSource), kStubName, "t") == 0 &&
        g_Lua.pcallK(state, 0, 1, 0, 0, nullptr) == 0) {
        return 1;
    }
    g_Lua.setTop(state, 0);
    return 0;
}

int Detour_Loader(lua_State* state) {
    if (g_Lua.type(state, 1) == kTypeString) {
        if (WhistleDef* whistle = FindModule(g_Lua.toLString(state, 1, nullptr))) {
            return ServeModule(state, *whistle);
        }
    }
    return g_OriginalLoader(state);
}

// ---- Title self-check ----------------------------------------------------

bool MatchBroken(const void* element) {
    return element && g_Broken.count(ReadAt<const void*>(element, 0)) > 0;
}

void SelfCheck() {
    g_Checked = true;
    if (!g_TableDone.load(std::memory_order_acquire)) {
        Log("Mod whistles: the whistle table was never seen loading, so custom whistles are off.");
        return;
    }
    uint8_t* db = g_Database.load(std::memory_order_acquire);
    for (WhistleDef& whistle : g_Whistles) {
        if (!whistle.appended) {
            continue;
        }
        const std::string what = "Mod whistle " + whistle.fullKey + " is id " + std::to_string(whistle.id);
        const uint8_t* record = mod_tables::FindRecord(db, whistle.id);
        if (!record || ReadAt<int32_t>(record, retail::kWhistleRecordLearn) != whistle.learn ||
            ReadAt<uint64_t>(record, retail::kWhistleRecordNotes) != whistle.notes ||
            ReadAt<int32_t>(record, retail::kWhistleRecordNoteCount) != whistle.noteCount) {
            if (record) {
                g_Broken.insert(record);
            }
            Log(what + ", but the whistle table does not have it as written. It stays off.");
            continue;
        }
        whistle.enabled = true;
        Log(what + ", melody " + MelodyText(whistle.notes, whistle.noteCount) + ", learned by flag " +
            std::to_string(whistle.learn) +
            (whistle.module.empty() ? ", with no script." : ", with script module " + whistle.module + "."));
    }
    if (!g_Broken.empty() && db) {
        std::vector<save_overlay::VectorEntry> removed;
        save_overlay::VectorRemove(db + retail::kDatabaseRecords, &MatchBroken, removed);
        Log("Mod whistles: took " + Plural(removed.size(), "whistle", "whistles") + " out of the whistle menu.");
    }
}

void ResolveLuaApi() {
    g_Lua.setTop = GameFunction<LuaSetTopFn>(retail::kLuaSetTop);
    g_Lua.type = GameFunction<LuaTypeFn>(retail::kLuaType);
    g_Lua.toLString = GameFunction<LuaToLStringFn>(retail::kLuaToLString);
    g_Lua.pushString = GameFunction<LuaPushStringFn>(retail::kLuaPushString);
    g_Lua.pcallK = GameFunction<LuaPCallKFn>(retail::kLuaPCallK);
    g_Lua.loadBufferX = GameFunction<LuaLoadBufferXFn>(retail::kLuaLoadBufferX);
}

// Drops every script, so the whistles play with no effect.
void DropScripts(const char* why) {
    for (WhistleDef& whistle : g_Whistles) {
        whistle.module.clear();
        whistle.source.clear();
    }
    Log(std::string("Mod whistles have no scripts, ") + why + ".");
}

bool HasScripts() {
    return std::any_of(g_Whistles.begin(), g_Whistles.end(), [](const WhistleDef& whistle) {
        return !whistle.module.empty();
    });
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool beforeGameInit) {
    std::vector<mod_tables::Folder> folders = mod_tables::FindFolders({"whistle."});
    if (folders.empty()) {
        Log("No mod whistles, so custom whistles are off.");
        return false;
    }
    if (!ticking) {
        Log("Custom whistles need the frame tick, so they are off.");
        return false;
    }
    if (!beforeGameInit) {
        Log("Custom whistles need Canopy to start before game init, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModWhistlesRequirements)) {
        Log("Custom whistles are off.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    const auto accept = [](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Whistles in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
            Plural(count, "whistle", "whistles") + (joined ? "." : ", registered for its whistles."));
        return true;
    };
    for (mod_rules::WhistleSpec& spec :
         mod_rules::ReadWhistles(folders, ModFilePath("mods"), mod_tables::LoaderRefs(), report, accept)) {
        WhistleDef whistle;
        static_cast<mod_rules::WhistleSpec&>(whistle) = std::move(spec);
        g_Whistles.push_back(std::move(whistle));
    }
    std::vector<std::string> keys;
    for (const WhistleDef& whistle : g_Whistles) {
        keys.push_back(whistle.fullKey);
    }
    std::vector<uint64_t> numbers;
    if (g_Whistles.empty() ||
        !mod_tables::AssignNumbers(kRegistryName, kRegistryHeader, "Custom whistles", kBandSize, keys, numbers)) {
        g_Whistles.clear();
        Log("No usable mod whistle, so custom whistles are off.");
        return false;
    }
    for (size_t index = 0; index < g_Whistles.size(); ++index) {
        g_Whistles[index].id = numbers[index] ? kBandBase + numbers[index] : 0;
    }
    g_Whistles.erase(
        std::remove_if(g_Whistles.begin(), g_Whistles.end(), [](const WhistleDef& whistle) { return whistle.id == 0; }),
        g_Whistles.end()
    );
    if (g_Whistles.empty()) {
        Log("No usable mod whistle, so custom whistles are off.");
        return false;
    }
    if (HasScripts() && !RequirementsMet(retail::kLuaHostRequirements)) {
        DropScripts("the script loader does not match");
    }

    DetourSet hooks("Custom whistles");
    if (!hooks.Add(
            retail::kWhistleTableLoad, reinterpret_cast<void*>(&Detour_TableLoad),
            reinterpret_cast<void**>(&g_OriginalLoad), "whistleTableLoad"
        )) {
        hooks.RemoveAll();
        g_Whistles.clear();
        Log("Custom whistles are off, their hook could not be created.");
        return false;
    }
    if (HasScripts()) {
        ResolveLuaApi();
        void* loader = reinterpret_cast<void*>(g_ModuleBase + retail::kScriptModuleLoader);
        if (const char* reason = hook_chain::Add(
                loader, kLoaderChainOwner, reinterpret_cast<void*>(&Detour_Loader),
                reinterpret_cast<void**>(&g_OriginalLoader)
            )) {
            DropScripts(("the script loader hook was not created, " + std::string(reason)).c_str());
        } else {
            g_LoaderHooked = true;
        }
    }
    g_Active = true;
    std::set<std::string> mods;
    size_t scripted = 0;
    for (const WhistleDef& whistle : g_Whistles) {
        mods.insert(whistle.modKey);
        scripted += whistle.module.empty() ? 0 : 1;
    }
    Log("Custom whistles installed: " + Plural(g_Whistles.size(), "whistle", "whistles") + " from " +
        Plural(mods.size(), "mod", "mods") + ", " + Plural(scripted, "with a script", "with scripts") +
        (scripted > 0 ? ". Whistle scripts run as game scripts, outside the Lua sandbox." : "."));
    return true;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

bool OwnsModule(const char* name) {
    return g_LoaderHooked && FindModule(name) != nullptr;
}

uint64_t IdOf(const std::string& fullKey) {
    if (!g_Active) {
        return 0;
    }
    for (const WhistleDef& whistle : g_Whistles) {
        if (_stricmp(whistle.fullKey.c_str(), fullKey.c_str()) == 0) {
            return whistle.id;
        }
    }
    return 0;
}

}  // namespace mod_whistles
