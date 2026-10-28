#define NOMINMAX
#include "mod_flags.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "game_events.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_flags.h"
#include "save_data.h"
#include "save_overlay.h"

// =============================================================
// MOD FLAGS DESIGN
// =============================================================
// A mod declares flags in its canopy.ini, one [flag.Name] section each,
// optionally with default = on. A folder with flags and no Lua or DLL is
// still registered as a mod. With no flags anywhere, nothing is hooked.
//
// Rules. What a [flag.Key] section accepts is CanopyCore's rules_flags, shared
// with the desktop tools; this file installs what they read.
//
// Ids. Each flag is a real bit of the game's flag set, in the band 95000 to
// 99998, so a native table row can test it the way it tests a story flag.
// No game table, script or native constant uses an id in the band (see
// docs/FLAGS.md). mods\CanopyData\flags.ini gives each Mod.Name its id the
// first time Canopy sees it and never gives that id out again, even after
// its mod is gone. When the band is used up a new flag gets none. An id
// outside the band is refused everywhere, so Canopy never writes a vanilla
// flag.
//
// Lua. canopy.flags.get, set and id reach only the calling mod's own flags.
// set goes through the game's flag write 0x147900, so the game's flag
// listeners hear of a change the way they hear of a story flag.
//
// Save. Flags are one adapter of the overlay save core (save_overlay.cpp).
// The strip clears every band bit directly, telling no listener, and keeps
// the bits that were on; the restore sets them again. The file
// slot<N>.flags.ini lists each declared flag as on or off and keeps the
// lines of flags no installed mod declares. After a load the whole band is
// cleared and the file's flags that are on are set; a declared flag the
// file has no line for, such as one from a mod installed after that save,
// takes its default. A new game clears the band and sets the declared
// defaults. Neither tells the listeners.
// Continue mode reads back a snapshot of the live, unstripped bits, so they
// are already right; only bits no installed flag owns are dropped. While
// the save is stripped, get answers from the kept bits and set is refused.
//
// Threads. Everything runs on the game thread.

namespace mod_flags {
namespace {

using namespace core;

using SetFlagFn = uint64_t(__fastcall*)(uint8_t* saveData, int64_t id, uint8_t on);

constexpr int32_t kFirst = retail::kModFlagFirst;
constexpr int32_t kLast = retail::kModFlagLast;
constexpr const char* kRegistryName = "flags.ini";
constexpr size_t kMaxSidecarLines = 8192;
// Before items, which strip last.
constexpr int kOverlayOrder = 40;

static_assert(kFirst > 0 && kLast < retail::kFlagCount, "the mod flag band must lie inside the flag set");

// What the rules read, plus the id that installing gives.
struct FlagDef : mod_rules::FlagSpec {
    int32_t id = 0;
};

std::vector<FlagDef> g_Flags;
// Indexed by id - kFirst: true when an installed flag owns the bit.
std::vector<bool> g_Declared;
bool g_Active = false;

// Band bits that were on at the strip.
std::vector<int32_t> g_Stash;
// File lines of flags no installed mod declares, written back as they were.
std::vector<std::string> g_Carried;
// The loaded file's flags that are on, set by afterLoad, and every
// declared flag the file has a line for.
std::vector<int32_t> g_LoadedOn;
std::vector<int32_t> g_LoadedNamed;
int32_t g_LoadedSlot = 0;
bool g_ApplyPending = false;

// ---- Small helpers -------------------------------------------------------

bool InBand(int64_t id) {
    return id >= kFirst && id <= kLast;
}

bool Declared(int32_t id) {
    return InBand(id) && g_Declared.size() > static_cast<size_t>(id - kFirst) && g_Declared[id - kFirst];
}

bool Contains(const std::vector<int32_t>& ids, int32_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::string Plural(size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

std::string DataPath(const std::string& name) {
    return ModFilePath((std::string("mods\\CanopyData\\") + name).c_str());
}

bool ParseNumber(const std::string& text, int64_t& value) {
    if (text.empty() || text.size() > 12) {
        return false;
    }
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    return true;
}

const FlagDef* FindByKey(const std::string& fullKey) {
    for (const FlagDef& flag : g_Flags) {
        if (_stricmp(flag.fullKey.c_str(), fullKey.c_str()) == 0) {
            return &flag;
        }
    }
    return nullptr;
}

const FlagDef* FindFlag(const std::string& modKey, const char* key) {
    for (const FlagDef& flag : g_Flags) {
        if (_stricmp(flag.modKey.c_str(), modKey.c_str()) == 0 && _stricmp(flag.key.c_str(), key) == 0) {
            return &flag;
        }
    }
    return nullptr;
}

// ---- Bits ----------------------------------------------------------------
// The flag set at kSaveDataFlags is CBitFlag<99999> with no bounds check, so
// every access goes through InBand first.

uint64_t* FlagWord(uint8_t* saveData, int32_t id) {
    return reinterpret_cast<uint64_t*>(saveData + retail::kSaveDataFlags + sizeof(uint64_t) * static_cast<size_t>(id >> 6));
}

bool BitOn(uint8_t* saveData, int32_t id) {
    return InBand(id) && ((*FlagWord(saveData, id) >> (id & 63)) & 1) != 0;
}

// Writes a band bit directly, telling no listener.
void WriteBit(uint8_t* saveData, int32_t id, bool on) {
    if (!InBand(id)) {
        return;
    }
    uint64_t* word = FlagWord(saveData, id);
    const uint64_t mask = 1ull << (id & 63);
    *word = on ? (*word | mask) : (*word & ~mask);
}

// Clears every band bit directly. Returns the ids that were on.
std::vector<int32_t> ClearBand(uint8_t* saveData) {
    std::vector<int32_t> on;
    for (int32_t id = kFirst; id <= kLast; ++id) {
        if (BitOn(saveData, id)) {
            on.push_back(id);
            WriteBit(saveData, id, false);
        }
    }
    return on;
}

// ---- Id registry ---------------------------------------------------------
// mods\CanopyData\flags.ini: [ids], next = N, then Mod.Name = id lines. The
// ids are the flag ids themselves.

struct Registry {
    int64_t next = kFirst;
    std::vector<std::pair<std::string, int32_t>> ids;
};

bool ReadRegistry(Registry& registry) {
    const std::string path = DataPath(kRegistryName);
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    std::string text;
    if (!save_data::ReadFileText(path, text)) {
        return false;
    }
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = canopy_ini::Trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || equals == std::string::npos) {
            continue;
        }
        const std::string key = canopy_ini::Trim(line.substr(0, equals));
        int64_t number = 0;
        if (!ParseNumber(canopy_ini::Trim(line.substr(equals + 1)), number)) {
            continue;
        }
        if (_stricmp(key.c_str(), "next") == 0) {
            registry.next = std::max(registry.next, std::min<int64_t>(number, kLast + 1));
            continue;
        }
        if (!InBand(number)) {
            Log(
                "Mod flags: " + std::string(kRegistryName) + " gives " + key + " the id " + std::to_string(number) +
                ", outside 95000 to 99998, so that line is ignored."
            );
            continue;
        }
        const bool taken = std::any_of(registry.ids.begin(), registry.ids.end(), [&](const auto& entry) {
            return entry.second == number || _stricmp(entry.first.c_str(), key.c_str()) == 0;
        });
        if (taken) {
            Log("Mod flags: " + std::string(kRegistryName) + " repeats " + key + " or its id, so that line is ignored.");
        } else {
            registry.ids.emplace_back(key, static_cast<int32_t>(number));
        }
        registry.next = std::max(registry.next, number + 1);
    }
    return true;
}

bool WriteRegistry(const Registry& registry) {
    std::string text =
        "; Canopy mod flag ids. Each mod flag keeps its id for good, and an id is never given out twice.\r\n"
        "; Do not edit or delete this file: saved mod flags and mod data refer to these ids.\r\n\r\n[ids]\r\nnext = " +
        std::to_string(registry.next) + "\r\n";
    for (const auto& [key, number] : registry.ids) {
        text += key + " = " + std::to_string(number) + "\r\n";
    }
    CreateDirectoryA(ModFilePath("mods\\CanopyData").c_str(), nullptr);
    if (const uint32_t error = save_data::WriteFileAtomically(DataPath(kRegistryName), text)) {
        Log("Mod flags: " + std::string(kRegistryName) + " could not be written, error " + std::to_string(error) + ".");
        return false;
    }
    return true;
}

// Gives every flag its id, assigning new ones when needed. False when the
// registry cannot be read or written, so no id is ever reused.
bool AssignIds() {
    Registry registry;
    if (!ReadRegistry(registry)) {
        Log("Mod flags: mods\\CanopyData\\" + std::string(kRegistryName) + " could not be read, so no ids are given out.");
        return false;
    }
    bool changed = false;
    for (FlagDef& flag : g_Flags) {
        const auto found = std::find_if(registry.ids.begin(), registry.ids.end(), [&](const auto& entry) {
            return _stricmp(entry.first.c_str(), flag.fullKey.c_str()) == 0;
        });
        int64_t id = 0;
        if (found != registry.ids.end()) {
            id = found->second;
        } else {
            if (registry.next > kLast) {
                Log("Mod flags: the flag band 95000 to 99998 is used up, so " + flag.fullKey + " gets no id.");
                continue;
            }
            id = registry.next++;
            registry.ids.emplace_back(flag.fullKey, static_cast<int32_t>(id));
            changed = true;
        }
        flag.id = InBand(id) ? static_cast<int32_t>(id) : 0;
    }
    if (changed && !WriteRegistry(registry)) {
        return false;
    }
    g_Flags.erase(
        std::remove_if(g_Flags.begin(), g_Flags.end(), [](const FlagDef& flag) { return flag.id == 0; }), g_Flags.end()
    );
    return true;
}

// ---- Lua -----------------------------------------------------------------

std::string Get(const FlagDef& flag) {
    uint8_t* live = save_overlay::LiveSaveData();
    if (!game_events::InGame() || !live) {
        return "ronly while a save is loaded";
    }
    // While the save is written the bit sits in the stash.
    const bool on = BitOn(live, flag.id) || (save_overlay::Stripped() && Contains(g_Stash, flag.id));
    return on ? "i1" : "i0";
}

std::string Set(const FlagDef& flag, bool on) {
    uint8_t* live = save_overlay::LiveSaveData();
    if (!game_events::InGame() || !live) {
        return "ronly while a save is loaded";
    }
    if (save_overlay::Stripped()) {
        return "rthe game is saving";
    }
    if (!InBand(flag.id)) {
        return "rthis flag has no id";
    }
    const bool was = BitOn(live, flag.id);
    GameFunction<SetFlagFn>(retail::kSetFlagValue)(live, flag.id, on ? 1 : 0);
    if (was != on) {
        Log("[" + flag.modKey + "] Flag " + flag.key + " (" + std::to_string(flag.id) + ") turned " + (on ? "on." : "off."));
    }
    return "i1";
}

// ---- Overlay adapter -------------------------------------------------------

void Reset(uint8_t* saveData) {
    ClearBand(saveData);
    size_t set = 0;
    for (const FlagDef& flag : g_Flags) {
        if (flag.defaultOn) {
            WriteBit(saveData, flag.id, true);
            ++set;
        }
    }
    if (set > 0) {
        Log("Mod flags: a new game state starts with " + Plural(set, "default flag", "default flags") + " on.");
    }
}

void Strip(uint8_t* saveData) {
    const std::vector<int32_t> on = ClearBand(saveData);
    for (const int32_t id : on) {
        if (!Contains(g_Stash, id)) {
            g_Stash.push_back(id);
        }
    }
    if (!on.empty()) {
        Log("Mod flags: took " + Plural(on.size(), "flag", "flags") + " out of the save while it is written.");
    }
}

std::string Snapshot(size_t& count) {
    count = 0;
    std::string body;
    // A save data that was not stripped still holds its bits.
    uint8_t* live = save_overlay::LiveSaveData();
    for (const FlagDef& flag : g_Flags) {
        const bool on = Contains(g_Stash, flag.id) || (live && BitOn(live, flag.id));
        body += flag.fullKey + " = " + (on ? "on" : "off") + "\r\n";
        ++count;
    }
    for (const std::string& line : g_Carried) {
        body += line + "\r\n";
        ++count;
    }
    return count > 0 ? "[flags]\r\n" + body : std::string();
}

void Restore(uint8_t* saveData) {
    if (saveData && !g_Stash.empty()) {
        for (const int32_t id : g_Stash) {
            WriteBit(saveData, id, true);
        }
        Log("Mod flags: " + Plural(g_Stash.size(), "flag is", "flags are") + " back after the save.");
    }
    g_Stash.clear();
}

void Loaded(int32_t slot, save_overlay::FileState state, const std::string& text) {
    g_LoadedOn.clear();
    g_LoadedNamed.clear();
    g_Carried.clear();
    g_LoadedSlot = slot;
    g_ApplyPending = true;
    if (state != save_overlay::FileState::Read) {
        return;
    }
    size_t lines = 0;
    size_t start = 0;
    while (start < text.size() && lines < kMaxSidecarLines) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = canopy_ini::Trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || equals == std::string::npos) {
            continue;
        }
        ++lines;
        const std::string key = canopy_ini::Trim(line.substr(0, equals));
        const std::string value = canopy_ini::Trim(line.substr(equals + 1));
        if (key.empty()) {
            continue;
        }
        const FlagDef* flag = FindByKey(key);
        bool on = false;
        if (!flag) {
            g_Carried.push_back(key + " = " + value);
            continue;
        }
        g_LoadedNamed.push_back(flag->id);
        if (mod_rules::ParseSwitch(value, on) && on) {
            g_LoadedOn.push_back(flag->id);
        }
    }
}

void AfterLoad(uint8_t* saveData) {
    if (!g_ApplyPending) {
        return;
    }
    g_ApplyPending = false;
    ClearBand(saveData);
    for (const int32_t id : g_LoadedOn) {
        WriteBit(saveData, id, true);
    }
    size_t defaults = 0;
    for (const FlagDef& flag : g_Flags) {
        if (flag.defaultOn && !Contains(g_LoadedNamed, flag.id)) {
            WriteBit(saveData, flag.id, true);
            ++defaults;
        }
    }
    if (defaults > 0) {
        Log(
            "Mod flags: slot " + std::to_string(g_LoadedSlot) + " has no line for " +
            Plural(defaults, "default-on flag, so it starts on.", "default-on flags, so they start on.")
        );
    }
    if (!g_LoadedOn.empty() || !g_Carried.empty()) {
        Log(
            "Mod flags: set " + Plural(g_LoadedOn.size(), "flag", "flags") + " from slot " +
            std::to_string(g_LoadedSlot) +
            (g_Carried.empty() ? "."
                               : ", and kept " + Plural(g_Carried.size(), "line", "lines") +
                                     " of flags no installed mod declares.")
        );
    }
    g_LoadedOn.clear();
    g_LoadedNamed.clear();
}

void AfterInPlace(uint8_t* saveData) {
    size_t dropped = 0;
    for (int32_t id = kFirst; id <= kLast; ++id) {
        if (BitOn(saveData, id) && !Declared(id)) {
            WriteBit(saveData, id, false);
            ++dropped;
        }
    }
    if (dropped > 0) {
        Log("Mod flags: continue mode brought back " + Plural(dropped, "flag", "flags") + " no installed mod declares, cleared.");
    }
}

void Clear() {
    g_Stash.clear();
    g_Carried.clear();
    g_LoadedOn.clear();
    g_LoadedNamed.clear();
    g_ApplyPending = false;
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData, bool beforeGameInit) {
    std::vector<mod_rules::ModFolder> folders =
        mod_rules::FlagFolders(mod_rules::ScanMods(ModFilePath("mods")));
    if (folders.empty()) {
        Log("No mod flags, so mod flags are off.");
        return false;
    }
    if (!ticking || !saveData) {
        Log("Mod flags need the frame tick and per-save data, so they are off.");
        return false;
    }
    if (!beforeGameInit) {
        Log("Mod flags need Canopy to start before game init, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModFlagsRequirements)) {
        Log("Mod flags are off.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    const auto accept = [](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Flags in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log(
            "Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
            Plural(count, "flag", "flags") + (joined ? "." : ", registered for its flags.")
        );
        return true;
    };
    for (mod_rules::FlagSpec& spec :
         mod_rules::ReadFlags(folders, ModFilePath("mods"), mod_tables::LoaderRefs(), report, accept)) {
        FlagDef flag;
        static_cast<mod_rules::FlagSpec&>(flag) = std::move(spec);
        g_Flags.push_back(std::move(flag));
    }
    if (g_Flags.empty() || !AssignIds() || g_Flags.empty()) {
        g_Flags.clear();
        Log("No usable mod flag, so mod flags are off.");
        return false;
    }
    g_Declared.assign(static_cast<size_t>(kLast - kFirst + 1), false);
    for (const FlagDef& flag : g_Flags) {
        g_Declared[flag.id - kFirst] = true;
    }

    const save_overlay::Adapter adapter = {
        "flags", "Mod flags", "flag", "flags", "Each line: Mod.Flag = on or off.", kOverlayOrder,
        &Reset, &Strip, &Snapshot, &Restore, &Loaded, &AfterLoad, &AfterInPlace, &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        g_Flags.clear();
        g_Declared.clear();
        Log("Mod flags are off, the overlay save could not start.");
        return false;
    }
    g_Active = true;
    for (const FlagDef& flag : g_Flags) {
        Log("Mod flag " + flag.fullKey + " is id " + std::to_string(flag.id) + (flag.defaultOn ? ", on in a new game." : "."));
    }
    std::vector<std::string> owners;
    for (const FlagDef& flag : g_Flags) {
        if (std::none_of(owners.begin(), owners.end(), [&](const std::string& owner) { return owner == flag.modKey; })) {
            owners.push_back(flag.modKey);
        }
    }
    Log(
        "Mod flags installed: " + Plural(g_Flags.size(), "flag", "flags") + " from " +
        Plural(owners.size(), "mod", "mods") + "."
    );
    return true;
}

std::string LuaRequest(const std::string& modKey, const char* action, const char* flag, const char* value) {
    if (!action || !flag) {
        return "ecanopy.flags needs a flag name";
    }
    const FlagDef* found = FindFlag(modKey, flag);
    if (!found) {
        return std::string("ethis mod defines no flag named ") + flag;
    }
    if (std::strcmp(action, "id") == 0) {
        return "i" + std::to_string(found->id);
    }
    if (std::strcmp(action, "get") == 0) {
        return g_Active ? Get(*found) : "rmod flags are off, see Canopy.log";
    }
    if (std::strcmp(action, "set") == 0) {
        if (!value || (std::strcmp(value, "1") != 0 && std::strcmp(value, "0") != 0)) {
            return "ecanopy.flags.set needs true or false";
        }
        return g_Active ? Set(*found, value[0] == '1') : "rmod flags are off, see Canopy.log";
    }
    return std::string("ecanopy.flags does not know ") + action;
}

int32_t IdOf(const std::string& fullKey) {
    const FlagDef* flag = FindByKey(fullKey);
    return flag ? flag->id : 0;
}

}  // namespace mod_flags
