#define NOMINMAX
#include "lua_host.h"

#include <windows.h>

#include <intrin.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core.h"
#include "game_events.h"
#include "lua_commands.h"
#include "lua_host_source.h"
#include "mod_config.h"
#include "mod_flags.h"
#include "mod_items.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "mod_villagers.h"
#include "mod_whistles.h"
#include "rules_mod.h"
#include "save_data.h"

// =============================================================
// LUA HOST DESIGN
// =============================================================
// A Lua mod is a folder in the mods folder with a lua\main.lua, and may have
// a canopy.ini that names it and declares settings. It needs no DLL. At
// startup each one is registered with the settings store like a DLL mod, or
// joins the DLL mod that already has its key.
//
// Rules. The [mod] and [setting.<Key>] sections are read by rules_mod in
// CanopyCore, which the desktop tools share. This file registers what the
// rules accept, one folder at a time.
//
// The game runs every script in one shared lua_State, the script root, with
// io, os and debug open. A Lua mod gets a script player of its own in that
// root, so the engine resumes it every frame like any game script, and so
// every game command it calls finds a context on the engine's script stack.
// Three layers keep a safe mod from changing the story or the save:
//
// - Sandbox. The bootstrap (lua_host_source.h) runs the mod in an
//   environment of its own. It holds only safe functions and the
//   allowlisted game commands (lua_commands.h), which refuse to run unless a
//   save is loaded and the player is on a map. It never sees or writes the
//   real globals.
// - Guard. Every command that writes the save, or that starts game scripts,
//   is wrapped in a thin C function. It walks the engine's script stack in
//   thread local storage: when any context on it is a safe Lua mod's, the
//   call is refused with a Lua error and logged once per mod and command.
//   An unsafe mod is allowed and logged once. A game script gets the
//   original by a tail call, so the game's own behavior is unchanged. The
//   sandbox already hides these commands, so the guard is the second line.
// - Save values. canopy.save goes through the same per-save store and API
//   functions DLL mods use, never the game's save.
//
// An unsafe mod (unsafe = true in canopy.ini) reads the real globals, and
// the guard lets it through. It is logged as a warning, and its Mod
// Settings entry shows a warning row.
//
// Lua errors are longjmps here (luaD_throw 0x66A550), not C++ exceptions.
// So every C function Lua calls, and every frame a Lua error can cross,
// holds no C++ object: the work that needs one is done in a separate
// function that catches every exception and leaves its result in a static
// buffer.
//
// Hooks, both created only when a Lua mod exists:
// - The game command registrar 0x4D6B00, post-hooked. The engine runs it
//   while it builds the script root, after the standard libraries and the
//   framework commands and before any game script. The post-hook wraps the
//   guarded commands and loads the host chunk with the root's real globals.
// - The script module loader 0x73BA90. A mod's player requires the module
//   canopy_<key>, which the host builds while Canopy creates that player. A
//   game script that requires a module that does not exist gets a Lua error
//   instead of the original's null dereference. Every other name goes to
//   the original.
//
// The players are created from the frame tick once the root exists, one per
// mod in folder name order, and updated right after with the frame's own
// timing. A player with no parent task is never updated by the engine, so
// this is its only update. If the root is ever replaced, the players are
// made again.

namespace lua_host {
namespace {

using namespace core;

struct lua_State;
using LuaCFunction = int (*)(lua_State* state);

using LuaSetTopFn = void(__fastcall*)(lua_State* state, int index);
using LuaTypeFn = int(__fastcall*)(lua_State* state, int index);
using LuaToLStringFn = const char*(__fastcall*)(lua_State* state, int index, size_t* length);
using LuaPushLStringFn = const char*(__fastcall*)(lua_State* state, const char* text, size_t length);
using LuaPushStringFn = const char*(__fastcall*)(lua_State* state, const char* text);
using LuaPushCClosureFn = void(__fastcall*)(lua_State* state, LuaCFunction function, int upvalues);
// The API's internal field get and set, on a value pointer instead of a stack
// index: get pushes table[key] and returns its type, set pops the value.
using LuaGetStrFn = int(__fastcall*)(lua_State* state, const void* table, const char* key);
using LuaSetStrFn = void(__fastcall*)(lua_State* state, const void* table, const char* key);
using LuaCreateTableFn = void(__fastcall*)(lua_State* state, int arrayCount, int recordCount);
using LuaSetGlobalFn = void(__fastcall*)(lua_State* state, const char* name);
using LuaPCallKFn = int(__fastcall*)(
    lua_State* state, int argumentCount, int resultCount, int handler, intptr_t context, void* continuation
);
using LuaErrorFn = int(__fastcall*)(lua_State* state);
using LuaLoadBufferXFn = int(__fastcall*)(
    lua_State* state, const char* buffer, size_t size, const char* name, const char* mode
);
using RegistrarFn = void(__fastcall*)(lua_State* state);
using GetCodeFn = uint8_t**(__fastcall*)(void* manager, uint8_t** code, const char* name);
using CreatePlayerFn = uint8_t**(__fastcall*)(
    void* manager, uint8_t** player, const char* name, const char* module, const char* entry, void* rootSlot
);
using PlayerUpdateFn = uint8_t(__fastcall*)(uint8_t* player, const void* updateInfo);

struct LuaApi {
    LuaSetTopFn setTop;
    LuaTypeFn type;
    LuaToLStringFn toLString;
    LuaPushLStringFn pushLString;
    LuaPushStringFn pushString;
    LuaPushCClosureFn pushCClosure;
    LuaGetStrFn getStr;
    LuaSetStrFn setStr;
    LuaCreateTableFn createTable;
    LuaSetGlobalFn setGlobal;
    LuaPCallKFn pcallK;
    LuaErrorFn error;
    LuaLoadBufferXFn loadBufferX;
};

LuaApi g_Lua = {};

constexpr int kTypeNumber = 3;
constexpr int kTypeString = 4;
constexpr int kTypeTable = 5;
constexpr int kTypeFunction = 6;
// Value tags: a light C function, and a table (collectable bit set).
constexpr uint8_t kTagLightFunction = 0x16;
constexpr uint8_t kTagTable = 0x45;
constexpr size_t kValueSize = 16;
constexpr size_t kValueTag = 8;

// Where the host table lives in the registry.
constexpr const char* kHostKey = "canopy.host";
constexpr const char* kModulePrefix = "canopy_";
constexpr size_t kModulePrefixLength = 7;
constexpr const char* kEntry = "main";
// A module name longer than this goes to the original loader unchecked.
constexpr size_t kMaxCheckedModuleName = 100;
// Deepest script stack the guard walks.
constexpr int kMaxScriptDepth = 64;
// Events kept for a mod whose player has not run yet.
constexpr size_t kMaxQueuedEvents = 16;
constexpr int kMaxErrors = 10;
// Longest Lua source file and longest log text from a mod.
constexpr size_t kMaxSourceSize = 1024 * 1024;
constexpr size_t kMaxLogText = 1000;
constexpr size_t kMaxModuleName = 64;

struct SettingInfo {
    std::string name;
    mod_config::Kind kind;
    CanopySetting handle;
    std::vector<std::string> choices;
};

struct LuaMod {
    std::string folder;
    std::string key;
    bool unsafe = false;
    CanopyMod mod = CANOPY_NO_MOD;
    std::vector<SettingInfo> settings;
    // canopy_<key>, and the player's name canopy:<key>.
    std::string module;
    std::string playerName;
    // The player Canopy holds a reference to, and the script context of its
    // thread, which the loader records while the player is created.
    uint8_t* player = nullptr;
    void* context = nullptr;
    // One creation attempt per script root.
    bool created = false;
    int errors = 0;
    bool stopped = false;
    std::vector<std::string> events;
};

std::vector<LuaMod> g_Mods;
const CanopyApi* g_Api = nullptr;
bool g_Active = false;
bool g_Exited = false;
// The state the host chunk was loaded into, and the root the players live in.
lua_State* g_HostState = nullptr;
uint8_t* g_Root = nullptr;
bool g_LoggedNoHost = false;
bool g_LoggedMissingModule = false;
// The mod whose player is being created, -1 when none.
int g_Serving = -1;
// The result a native or a check leaves for its caller.
std::string g_Reply;
std::string g_GuardMessage;
std::set<std::string> g_LoggedGuard;

RegistrarFn g_OriginalRegistrar = nullptr;
LuaCFunction g_OriginalLoader = nullptr;
LuaCFunction g_GuardOriginal[lua_commands::kGuardedCount] = {};

// ---- Lua state access ----------------------------------------------------

uint8_t* Top(lua_State* state) {
    return ReadAt<uint8_t*>(state, retail::kLuaStateTop);
}

uint8_t TopTag(lua_State* state) {
    return *(Top(state) - kValueSize + kValueTag);
}

int GetTop(lua_State* state) {
    const uint8_t* callInfo = ReadAt<uint8_t*>(state, retail::kLuaStateCallInfo);
    const uint8_t* function = ReadAt<uint8_t*>(callInfo, 0);
    return static_cast<int>((Top(state) - function) / static_cast<ptrdiff_t>(kValueSize)) - 1;
}

const void* Registry(lua_State* state) {
    return ReadAt<uint8_t*>(state, retail::kLuaStateGlobal) + retail::kLuaGlobalRegistry;
}

// The registry's array part holds the main thread, then the globals table.
const void* Globals(lua_State* state) {
    const uint8_t* global = ReadAt<uint8_t*>(state, retail::kLuaStateGlobal);
    const uint8_t* registry = ReadAt<uint8_t*>(global, retail::kLuaGlobalRegistry);
    return ReadAt<uint8_t*>(registry, retail::kLuaTableArray) + kValueSize;
}

// A string argument, null for anything else.
const char* Arg(lua_State* state, int index) {
    return g_Lua.type(state, index) == kTypeString ? g_Lua.toLString(state, index, nullptr) : nullptr;
}

int PushReply(lua_State* state) {
    g_Lua.pushLString(state, g_Reply.data(), g_Reply.size());
    return 1;
}

// ---- Script stack --------------------------------------------------------

// The engine keeps the script contexts of the threads being run on a stack
// in its thread local storage, slot 0 of the TLS array.
uint8_t* ScriptTls() {
    return reinterpret_cast<uint8_t**>(__readgsqword(0x58))[0];
}

void* TopContext() {
    uint8_t* tls = ScriptTls();
    const int depth = ReadAt<int32_t>(tls, retail::kTlsScriptDepth);
    if (depth <= 0 || depth > kMaxScriptDepth) {
        return nullptr;
    }
    return ReadAt<void*>(tls, retail::kTlsScriptStack + sizeof(void*) * static_cast<size_t>(depth - 1));
}

// Copies the name of the thread a context runs, empty when it cannot be
// read. Holds no C++ object, so the read is guarded.
void ReadThreadName(const void* context, char (&name)[96]) {
    name[0] = '\0';
    __try {
        const uint8_t* thread = ReadAt<uint8_t*>(context, retail::kScriptContextThread);
        const char* text = thread ? ReadAt<const char*>(thread, retail::kScriptBaseName) : nullptr;
        if (text) {
            size_t length = 0;
            while (length + 1 < sizeof(name) && text[length]) {
                name[length] = text[length];
                ++length;
            }
            name[length] = '\0';
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        name[0] = '\0';
    }
}

// The Lua mod whose thread, canopy_<key>-main, a context runs. -1 for a
// game script.
int NameOwner(const void* context) {
    char name[96];
    ReadThreadName(context, name);
    if (_strnicmp(name, kModulePrefix, kModulePrefixLength) != 0) {
        return -1;
    }
    constexpr char kSuffix[] = "-main";
    const size_t length = std::strlen(name);
    if (length <= kModulePrefixLength + sizeof(kSuffix) - 1 ||
        std::strcmp(name + length - (sizeof(kSuffix) - 1), kSuffix) != 0) {
        return -1;
    }
    name[length - (sizeof(kSuffix) - 1)] = '\0';
    for (size_t index = 0; index < g_Mods.size(); ++index) {
        if (_stricmp(g_Mods[index].key.c_str(), name + kModulePrefixLength) == 0) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

// The Lua mod a context belongs to: the one recorded for its player, or the
// one its thread's name names. -1 for a game script.
int ContextOwner(const void* context) {
    if (!context) {
        return -1;
    }
    for (size_t index = 0; index < g_Mods.size(); ++index) {
        if (g_Mods[index].context == context) {
            return static_cast<int>(index);
        }
    }
    return NameOwner(context);
}

// The first Lua mod with a context anywhere on the script stack, -1 when
// only game scripts are running.
int StackOwner() {
    uint8_t* tls = ScriptTls();
    const int depth = ReadAt<int32_t>(tls, retail::kTlsScriptDepth);
    if (depth <= 0 || depth > kMaxScriptDepth) {
        return -1;
    }
    for (int level = depth; level-- > 0;) {
        const void* context =
            ReadAt<void*>(tls, retail::kTlsScriptStack + sizeof(void*) * static_cast<size_t>(level));
        const int owner = ContextOwner(context);
        if (owner >= 0) {
            return owner;
        }
    }
    return -1;
}

// ---- Guard ---------------------------------------------------------------

constexpr const char* kGuardFallback = "this command is refused for safe Lua mods";

// Logs the guard's decision once per mod and command. The refusal message
// for a safe mod, null to let the call through.
__declspec(noinline) const char* GuardDecide(size_t owner, size_t command) {
    const LuaMod& mod = g_Mods[owner];
    try {
        const char* name = lua_commands::kGuarded[command];
        const char* what = lua_commands::kGuardedStarts[command] ? "starts game scripts" : "writes the save";
        const bool first = g_LoggedGuard.insert(mod.key + "\n" + name).second;
        if (mod.unsafe) {
            if (first) {
                Log(
                    "Lua guard: " + mod.key + " called " + name + ", which " + what +
                    ", and was allowed because the mod is unsafe."
                );
            }
            return nullptr;
        }
        if (first) {
            Log("Lua guard: " + mod.key + " called " + name + ", which " + what + ", and was refused.");
        }
        g_GuardMessage = std::string(name) + " " + what + ", so a safe Lua mod cannot call it";
        return g_GuardMessage.c_str();
    } catch (...) {
        return mod.unsafe ? nullptr : kGuardFallback;
    }
}

__declspec(noinline) const char* GuardCheck(size_t command) {
    const int owner = StackOwner();
    return owner < 0 ? nullptr : GuardDecide(static_cast<size_t>(owner), command);
}

// One per guarded command. Holds nothing, so the refusal's longjmp crosses
// a bare frame, and a game script's call is a plain tail call.
template <size_t N>
int Guard(lua_State* state) {
    if (const char* message = GuardCheck(N)) {
        g_Lua.pushString(state, message);
        return g_Lua.error(state);
    }
    return g_GuardOriginal[N](state);
}

template <size_t... I>
constexpr std::array<LuaCFunction, sizeof...(I)> MakeGuards(std::index_sequence<I...>) {
    return {{&Guard<I>...}};
}

constexpr std::array<LuaCFunction, lua_commands::kGuardedCount> kGuards =
    MakeGuards(std::make_index_sequence<lua_commands::kGuardedCount>());

// ---- Helpers for the natives ---------------------------------------------

LuaMod* FindMod(const char* key) {
    if (!key) {
        return nullptr;
    }
    for (LuaMod& mod : g_Mods) {
        if (mod.key == key) {
            return &mod;
        }
    }
    return nullptr;
}

// One line for the log: no line breaks, cut on a UTF-8 boundary.
std::string LogLine(const char* text, size_t limit) {
    std::string line = text ? text : "";
    for (char& c : line) {
        if (c == '\r' || c == '\n' || c == '\t') {
            c = ' ';
        }
    }
    if (line.size() > limit) {
        size_t length = limit;
        while (length > 0 && (static_cast<uint8_t>(line[length]) & 0xC0) == 0x80) {
            --length;
        }
        line.resize(length);
        line += "...";
    }
    return line;
}

// A whole int32 written the way it reads back, so "007" stays a string.
bool ParseInt(const std::string& text, int32_t& value) {
    if (text.empty() || text.size() > 11) {
        return false;
    }
    size_t index = text[0] == '-' ? 1 : 0;
    if (index == text.size()) {
        return false;
    }
    int64_t number = 0;
    for (; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        number = number * 10 + (text[index] - '0');
    }
    if (text[0] == '-') {
        number = -number;
    }
    if (number < INT32_MIN || number > INT32_MAX || std::to_string(number) != text) {
        return false;
    }
    value = static_cast<int32_t>(number);
    return true;
}

std::string ModsFolder() {
    return ModFilePath("mods");
}

void Native_DoLog(const char* key, const char* text) {
    try {
        const LuaMod* mod = FindMod(key);
        if (mod) {
            g_Api->Log(mod->mod, LogLine(text, kMaxLogText).c_str());
        }
    } catch (...) {
    }
}

void Native_DoInfo(const char* key) {
    try {
        const LuaMod* mod = FindMod(key);
        g_Reply = mod ? (mod->unsafe ? "u" : "s") : "";
    } catch (...) {
        g_Reply.clear();
    }
}

void Native_DoSetting(const char* key, const char* name) {
    try {
        g_Reply.clear();
        const LuaMod* mod = FindMod(key);
        if (!mod || !name) {
            return;
        }
        for (const SettingInfo& setting : mod->settings) {
            if (_stricmp(setting.name.c_str(), name) != 0) {
                continue;
            }
            const int32_t value = g_Api->GetSetting(setting.handle, 0);
            if (setting.kind == mod_config::Kind::Toggle) {
                g_Reply = value ? "b1" : "b0";
            } else if (setting.kind == mod_config::Kind::Choice) {
                if (value >= 0 && static_cast<size_t>(value) < setting.choices.size()) {
                    g_Reply = "s" + setting.choices[static_cast<size_t>(value)];
                }
            } else {
                g_Reply = "i" + std::to_string(value);
            }
            return;
        }
    } catch (...) {
        g_Reply.clear();
    }
}

void Native_DoSaveGet(const char* key, const char* name) {
    try {
        g_Reply.clear();
        const LuaMod* mod = FindMod(key);
        if (!mod || !name) {
            return;
        }
        std::vector<char> buffer(save_data::kMaxValueLength + 1, '\0');
        if (!g_Api->GetSaveString(mod->mod, name, buffer.data(), static_cast<uint32_t>(buffer.size()))) {
            return;
        }
        const std::string text = buffer.data();
        int32_t number = 0;
        g_Reply = ParseInt(text, number) ? "i" + text : "s" + text;
    } catch (...) {
        g_Reply.clear();
    }
}

void Native_DoSaveSet(const char* key, const char* name, const char* kind, const char* value) {
    try {
        g_Reply = "0";
        const LuaMod* mod = FindMod(key);
        if (!mod || !name || !kind || !value) {
            return;
        }
        bool stored = false;
        int32_t number = 0;
        if (std::strcmp(kind, "i") == 0 && ParseInt(value, number)) {
            stored = g_Api->SetSaveInt(mod->mod, name, number);
        } else if (std::strcmp(kind, "s") == 0) {
            stored = g_Api->SetSaveString(mod->mod, name, value);
        }
        g_Reply = stored ? "1" : "0";
    } catch (...) {
        g_Reply = "0";
    }
}

void Native_DoSaveHas(const char* key, const char* name, bool remove) {
    try {
        const LuaMod* mod = FindMod(key);
        bool result = false;
        if (mod && name) {
            result = remove ? g_Api->DeleteSaveValue(mod->mod, name) : g_Api->HasSaveValue(mod->mod, name);
        }
        g_Reply = result ? "1" : "0";
    } catch (...) {
        g_Reply = "0";
    }
}

void Native_DoState() {
    try {
        g_Reply = std::string(game_events::InGame() ? "1" : "0") + (PlayerInField() ? "1" : "0");
    } catch (...) {
        g_Reply = "00";
    }
}

// A module name is dot-separated parts of letters, digits and '_'.
bool ValidModuleName(const char* name) {
    const size_t length = name ? std::strlen(name) : 0;
    if (length == 0 || length > kMaxModuleName || name[0] == '.' || name[length - 1] == '.') {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const char c = name[index];
        const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!word && !(c == '.' && name[index + 1] != '.')) {
            return false;
        }
    }
    return true;
}

// "1" and the text of <name>.lua in the mod's lua folder, or "0" and why not.
void Native_DoSource(const char* key, const char* name) {
    try {
        const LuaMod* mod = FindMod(key);
        if (!mod) {
            g_Reply = "0this Lua mod is not registered";
            return;
        }
        if (!ValidModuleName(name)) {
            g_Reply = std::string("0module '") + (name ? name : "?") +
                      "' is not a valid name: use letters, digits and '_', with dots between folders";
            return;
        }
        std::string relative = name;
        std::replace(relative.begin(), relative.end(), '.', '\\');
        const std::string path = ModsFolder() + "\\" + mod->folder + "\\lua\\" + relative + ".lua";
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            g_Reply = std::string("0module '") + name + "' not found in the mod's lua folder";
            return;
        }
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (text.size() > kMaxSourceSize) {
            g_Reply = std::string("0module '") + name + "' is larger than 1 MB";
            return;
        }
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            text.erase(0, 3);
        }
        g_Reply = "1" + text;
    } catch (...) {
        g_Reply = "0module could not be read";
    }
}

void Native_DoEvents(const char* key) {
    try {
        g_Reply.clear();
        LuaMod* mod = FindMod(key);
        if (!mod) {
            return;
        }
        for (const std::string& event : mod->events) {
            if (!g_Reply.empty()) {
                g_Reply += ",";
            }
            g_Reply += event;
        }
        mod->events.clear();
    } catch (...) {
        g_Reply.clear();
    }
}

// Logs one error of a mod. "stop" when the mod's Lua stops with it.
void Native_DoFail(const char* key, const char* where, const char* message, const char* fatal) {
    try {
        g_Reply.clear();
        LuaMod* mod = FindMod(key);
        if (!mod || mod->stopped) {
            g_Reply = "stop";
            return;
        }
        Log("[" + mod->key + "] Lua error in " + (where ? where : "?") + ": " + LogLine(message, kMaxLogText));
        const bool isFatal = fatal && *fatal;
        if (isFatal) {
            mod->stopped = true;
            Log("[" + mod->key + "] Lua stopped, main.lua did not load. The game keeps running.");
        } else if (++mod->errors >= kMaxErrors) {
            mod->stopped = true;
            Log("[" + mod->key + "] Lua stopped after " + std::to_string(kMaxErrors) + " errors. The game keeps running.");
        }
        if (mod->stopped) {
            mod->events.clear();
            g_Reply = "stop";
        }
    } catch (...) {
        g_Reply.clear();
    }
}

// canopy.items: the reply is mod_items::LuaRequest's.
void Native_DoItems(const char* key, const char* action, const char* name, const char* count) {
    try {
        const LuaMod* mod = FindMod(key);
        g_Reply = mod ? mod_items::LuaRequest(mod->key, action, name, count) : "ethis Lua mod is not registered";
    } catch (...) {
        g_Reply = "rthe item call failed";
    }
}

// canopy.flags: the reply is mod_flags::LuaRequest's.
void Native_DoFlags(const char* key, const char* action, const char* name, const char* value) {
    try {
        const LuaMod* mod = FindMod(key);
        g_Reply = mod ? mod_flags::LuaRequest(mod->key, action, name, value) : "ethis Lua mod is not registered";
    } catch (...) {
        g_Reply = "rthe flag call failed";
    }
}

void Native_DoAllowed() {
    try {
        g_Reply.clear();
        for (const char* name : lua_commands::kAllowed) {
            g_Reply += name;
            g_Reply += '\n';
        }
    } catch (...) {
        g_Reply.clear();
    }
}

// ---- Natives -------------------------------------------------------------
// Called by the host chunk. Each one only reads its arguments, calls a
// helper and pushes the reply.

int Native_Log(lua_State* state) {
    Native_DoLog(Arg(state, 1), Arg(state, 2));
    return 0;
}

int Native_Info(lua_State* state) {
    Native_DoInfo(Arg(state, 1));
    return PushReply(state);
}

int Native_Setting(lua_State* state) {
    Native_DoSetting(Arg(state, 1), Arg(state, 2));
    return PushReply(state);
}

int Native_SaveGet(lua_State* state) {
    Native_DoSaveGet(Arg(state, 1), Arg(state, 2));
    return PushReply(state);
}

int Native_SaveSet(lua_State* state) {
    Native_DoSaveSet(Arg(state, 1), Arg(state, 2), Arg(state, 3), Arg(state, 4));
    return PushReply(state);
}

int Native_SaveHas(lua_State* state) {
    Native_DoSaveHas(Arg(state, 1), Arg(state, 2), false);
    return PushReply(state);
}

int Native_SaveDelete(lua_State* state) {
    Native_DoSaveHas(Arg(state, 1), Arg(state, 2), true);
    return PushReply(state);
}

int Native_State(lua_State* state) {
    Native_DoState();
    return PushReply(state);
}

int Native_Source(lua_State* state) {
    Native_DoSource(Arg(state, 1), Arg(state, 2));
    return PushReply(state);
}

int Native_Events(lua_State* state) {
    Native_DoEvents(Arg(state, 1));
    return PushReply(state);
}

int Native_Fail(lua_State* state) {
    Native_DoFail(Arg(state, 1), Arg(state, 2), Arg(state, 3), Arg(state, 4));
    return PushReply(state);
}

int Native_Allowed(lua_State* state) {
    Native_DoAllowed();
    return PushReply(state);
}

int Native_Items(lua_State* state) {
    Native_DoItems(Arg(state, 1), Arg(state, 2), Arg(state, 3), Arg(state, 4));
    return PushReply(state);
}

int Native_Flags(lua_State* state) {
    Native_DoFlags(Arg(state, 1), Arg(state, 2), Arg(state, 3), Arg(state, 4));
    return PushReply(state);
}

struct Native {
    const char* name;
    LuaCFunction function;
};

constexpr Native kNatives[] = {
    {"log", &Native_Log},
    {"info", &Native_Info},
    {"setting", &Native_Setting},
    {"save_get", &Native_SaveGet},
    {"save_set", &Native_SaveSet},
    {"save_has", &Native_SaveHas},
    {"save_delete", &Native_SaveDelete},
    {"state", &Native_State},
    {"source", &Native_Source},
    {"events", &Native_Events},
    {"fail", &Native_Fail},
    {"allowed", &Native_Allowed},
    {"items", &Native_Items},
    {"flags", &Native_Flags},
};

// ---- Script root setup ---------------------------------------------------

// Logs why the host chunk failed, with the Lua message on top of the stack.
__declspec(noinline) void LogHostFailure(const char* what, const char* message) {
    try {
        Log(std::string("The Lua host ") + what + ": " + LogLine(message, kMaxLogText) + ". Lua mods do not run.");
    } catch (...) {
    }
}

__declspec(noinline) void LogHostReady(size_t wrapped, size_t found) {
    try {
        Log(
            "Lua host ready in the script root: " + std::to_string(wrapped) + " of " +
            std::to_string(lua_commands::kGuardedCount) + " guarded commands wrapped, " + std::to_string(found) +
            " of " + std::to_string(lua_commands::kAllowedCount) + " sandbox commands found."
        );
    } catch (...) {
    }
}

const char* TopMessage(lua_State* state) {
    return g_Lua.type(state, -1) == kTypeString ? g_Lua.toLString(state, -1, nullptr) : "no message";
}

// Wraps every guarded command found in the globals. A command already
// wrapped, by an earlier call on the same state, is left alone.
size_t WrapGuards(lua_State* state) {
    size_t wrapped = 0;
    for (size_t index = 0; index < lua_commands::kGuardedCount; ++index) {
        const char* name = lua_commands::kGuarded[index];
        g_Lua.getStr(state, Globals(state), name);
        uint8_t* value = Top(state) - kValueSize;
        if (value[kValueTag] == kTagLightFunction) {
            const LuaCFunction current = ReadAt<LuaCFunction>(value, 0);
            g_Lua.setTop(state, -2);
            if (current != kGuards[index]) {
                g_GuardOriginal[index] = current;
                g_Lua.pushCClosure(state, kGuards[index], 0);
                g_Lua.setGlobal(state, name);
            }
            ++wrapped;
        } else {
            g_Lua.setTop(state, -2);
        }
    }
    return wrapped;
}

size_t CountAllowed(lua_State* state) {
    size_t found = 0;
    for (const char* name : lua_commands::kAllowed) {
        if (g_Lua.getStr(state, Globals(state), name) == kTypeFunction) {
            ++found;
        }
        g_Lua.setTop(state, -2);
    }
    return found;
}

// Loads the host chunk, calls it with the natives, and keeps the host table
// it returns in the registry. True on success.
bool LoadHost(lua_State* state) {
    const int base = GetTop(state);
    const char* source = lua_host_source::kSource;
    if (g_Lua.loadBufferX(state, source, sizeof(lua_host_source::kSource) - 1, "=canopy/host", "t") != 0) {
        LogHostFailure("did not compile", TopMessage(state));
        g_Lua.setTop(state, base);
        return false;
    }
    g_Lua.createTable(state, 0, static_cast<int>(std::size(kNatives)));
    for (const Native& native : kNatives) {
        g_Lua.pushCClosure(state, native.function, 0);
        g_Lua.setStr(state, Top(state) - 2 * kValueSize, native.name);
    }
    if (g_Lua.pcallK(state, 1, 1, 0, 0, nullptr) != 0) {
        LogHostFailure("failed to start", TopMessage(state));
        g_Lua.setTop(state, base);
        return false;
    }
    if (TopTag(state) != kTagTable) {
        LogHostFailure("failed to start", "it returned no host table");
        g_Lua.setTop(state, base);
        return false;
    }
    g_Lua.setStr(state, Registry(state), kHostKey);
    g_Lua.setTop(state, base);
    return true;
}

// Runs in the registrar's post-hook. Every call here is either protected or
// cannot raise a Lua error on fresh globals.
void PrepareState(lua_State* state) {
    const size_t wrapped = WrapGuards(state);
    const size_t found = CountAllowed(state);
    if (LoadHost(state)) {
        g_HostState = state;
        LogHostReady(wrapped, found);
    }
}

void __fastcall Detour_Registrar(lua_State* state) {
    g_OriginalRegistrar(state);
    PrepareState(state);
}

// ---- Module loader -------------------------------------------------------

bool CodeExists(const char* name) {
    uint8_t* manager = ReadGlobal<uint8_t*>(retail::kScriptManager);
    if (!manager) {
        return true;
    }
    uint8_t* code = nullptr;
    GameFunction<GetCodeFn>(retail::kScriptGetCode)(manager, &code, name);
    const bool exists = code != nullptr;
    Release(code, retail::kScriptCodeRefCount);
    return exists;
}

// Decides what the loader does with name. Null to use the original, or with
// serve set to the mod whose module to build; otherwise the Lua error to
// raise.
__declspec(noinline) const char* LoaderCheck(const char* name, int& serve) {
    try {
        if (_strnicmp(name, kModulePrefix, kModulePrefixLength) == 0) {
            if (g_Serving >= 0 && _stricmp(name, g_Mods[static_cast<size_t>(g_Serving)].module.c_str()) == 0) {
                // Recorded only when it is the new thread's own context.
                void* context = TopContext();
                if (context && NameOwner(context) == g_Serving) {
                    g_Mods[static_cast<size_t>(g_Serving)].context = context;
                }
                serve = g_Serving;
                return nullptr;
            }
            g_Reply = std::string("module '") + name + "' belongs to a Canopy Lua mod and cannot be required";
            return g_Reply.c_str();
        }
        // A custom villager's, whistle's or script item's module is served
        // further down the chain.
        if (std::strlen(name) > kMaxCheckedModuleName || mod_villagers::OwnsModule(name) ||
            mod_whistles::OwnsModule(name) || mod_items::OwnsModule(name) || CodeExists(name)) {
            return nullptr;
        }
        if (!g_LoggedMissingModule) {
            g_LoggedMissingModule = true;
            Log("A game script required a module that does not exist. It got a Lua error instead of a crash.");
        }
        g_Reply = std::string("module '") + name + "' not found";
        return g_Reply.c_str();
    } catch (...) {
        return nullptr;
    }
}

__declspec(noinline) void LogServeFailure(size_t index, const char* message) {
    try {
        LuaMod& mod = g_Mods[index];
        Log("[" + mod.key + "] Lua host could not build the script module: " + LogLine(message, kMaxLogText) + ".");
        mod.stopped = true;
    } catch (...) {
    }
}

// The main of a module the host could not build: returns at once, so the
// player's thread ends.
int Idle(lua_State*) {
    return 0;
}

// Pushes the module for mod index: host.module(key), or a module whose main
// does nothing when that fails, so the player's creation never raises.
int ServeModule(lua_State* state, size_t index) {
    const int base = GetTop(state);
    if (g_Lua.getStr(state, Registry(state), kHostKey) == kTypeTable &&
        g_Lua.getStr(state, Top(state) - kValueSize, "module") == kTypeFunction) {
        g_Lua.pushString(state, g_Mods[index].key.c_str());
        if (g_Lua.pcallK(state, 1, 1, 0, 0, nullptr) == 0 && TopTag(state) == kTagTable) {
            return 1;
        }
        LogServeFailure(index, TopMessage(state));
    } else {
        LogServeFailure(index, "the host is missing");
    }
    g_Lua.setTop(state, base);
    g_Lua.createTable(state, 0, 1);
    g_Lua.pushCClosure(state, &Idle, 0);
    g_Lua.setStr(state, Top(state) - 2 * kValueSize, kEntry);
    return 1;
}

int Detour_Loader(lua_State* state) {
    const int type = g_Lua.type(state, 1);
    if (type != kTypeString && type != kTypeNumber) {
        g_Lua.pushString(state, "require needs a module name");
        return g_Lua.error(state);
    }
    const char* name = g_Lua.toLString(state, 1, nullptr);
    int serve = -1;
    if (const char* message = LoaderCheck(name, serve)) {
        g_Lua.pushString(state, message);
        return g_Lua.error(state);
    }
    if (serve >= 0) {
        return ServeModule(state, static_cast<size_t>(serve));
    }
    return g_OriginalLoader(state);
}

// ---- Players -------------------------------------------------------------

// Sets the player's stopped byte, so no update runs it again. Its reference
// is kept: releasing it could free a thread whose root is already gone.
void StopPlayer(const LuaMod& mod) {
    if (mod.player) {
        WriteAt<uint8_t>(mod.player, retail::kScriptPlayerStopped, 1);
    }
}

void CreatePlayer(uint8_t* manager, size_t index) {
    LuaMod& mod = g_Mods[index];
    mod.created = true;
    uint8_t* player = nullptr;
    g_Serving = static_cast<int>(index);
    GameFunction<CreatePlayerFn>(retail::kScriptCreatePlayer)(
        manager, &player, mod.playerName.c_str(), mod.module.c_str(), kEntry, manager + retail::kScriptManagerRoot
    );
    g_Serving = -1;
    mod.player = player;
    if (!player) {
        Log("[" + mod.key + "] Lua script player could not be created.");
    } else if (!mod.context) {
        Log("[" + mod.key + "] Lua script player started, but its context was not seen. The guard goes by name.");
    } else {
        Log("[" + mod.key + "] Lua script player started.");
    }
}

// ---- Mod discovery and registration --------------------------------------

// The Lua mods are read by mod_rules, which the desktop tools share. This
// file adds what the rules read to the settings store and to g_Mods.

CanopyText Plain(const char* text) {
    return {text, text, text, text, text, text, text};
}

// Adds one setting the rules read to the store. A refusal is logged by the
// store, in the setting's place among the rules' reports.
bool AddSettingToStore(LuaMod& mod, const mod_rules::SettingSpec& setting) {
    const CanopyText labelText = Plain(setting.label.c_str());
    const CanopyText helpText = Plain(setting.help.c_str());
    SettingInfo info{setting.key, mod_config::Kind::Toggle, CANOPY_NO_SETTING, {}};
    switch (setting.type) {
        case mod_rules::SettingType::Toggle:
            info.handle = g_Api->AddToggle(mod.mod, setting.key.c_str(), &labelText, &helpText, setting.on);
            break;
        case mod_rules::SettingType::Slider:
            info.kind = mod_config::Kind::Slider;
            info.handle = g_Api->AddSlider(
                mod.mod, setting.key.c_str(), &labelText, &helpText, setting.min, setting.max, setting.step,
                setting.start
            );
            break;
        case mod_rules::SettingType::Choice: {
            info.kind = mod_config::Kind::Choice;
            info.choices = setting.choices;
            std::vector<CanopyText> texts;
            for (const std::string& choice : setting.choices) {
                texts.push_back(Plain(choice.c_str()));
            }
            info.handle = g_Api->AddChoice(
                mod.mod, setting.key.c_str(), &labelText, &helpText, texts.empty() ? nullptr : texts.data(),
                static_cast<int32_t>(texts.size()), setting.start
            );
            break;
        }
    }
    if (info.handle == CANOPY_NO_SETTING) {
        return false;
    }
    mod.settings.push_back(info);
    return true;
}

// "1 setting." or "2 settings.", as the registered line counts them.
std::string SettingsText(size_t count) {
    return std::to_string(count) + (count == 1 ? " setting." : " settings.");
}

// The loader's side of the Lua mods the rules read. Each folder is attached,
// its settings are added, and then it is registered in g_Mods. Its log lines
// come in the order the loader always wrote them.
class LuaRegistrar {
public:
    // Attaches the mod and adds its unsafe notice row. False when the loader
    // cannot register it; the loader has logged why.
    bool Attach(const mod_rules::ModSpec& spec) {
        pending_ = LuaMod();
        pending_.folder = spec.folder;
        pending_.key = spec.key;
        pending_.unsafe = spec.unsafe;
        pending_.mod = mod_loader::AttachLuaMod(
            spec.key.c_str(), spec.name.c_str(), ("mods\\" + spec.folder + "\\lua").c_str(), joined_
        );
        if (pending_.mod == CANOPY_NO_MOD) {
            return false;
        }
        if (pending_.unsafe) {
            mod_loader::AddNoticeRow(pending_.mod, "_unsafe_lua", "Warning: unsafe Lua, full game access");
        }
        return true;
    }

    // Adds one setting that passed the rules to the mod being attached.
    bool AddSetting(const mod_rules::SettingSpec& setting) {
        return AddSettingToStore(pending_, setting);
    }

    // Logs the mod as registered, or as joined to its DLL mod, and keeps it.
    void Registered(const mod_rules::ModSpec& spec) {
        pending_.module = kModulePrefix + spec.key;
        pending_.playerName = "canopy:" + spec.key;
        Log(
            "Lua mod " + spec.name + " (" + spec.key + " v" + spec.version + " by " + spec.author + ", folder " +
            spec.folder + ") " + (joined_ ? "joined the DLL mod with the same key: " : "registered: ") +
            SettingsText(pending_.settings.size())
        );
        if (pending_.unsafe) {
            Log(
                "Warning: " + spec.key +
                " runs Lua with full game access (unsafe). It can change story flags, quests and saves."
            );
        }
        g_Mods.push_back(std::move(pending_));
        pending_ = LuaMod();
    }

private:
    LuaMod pending_;
    bool joined_ = false;
};

void ResolveLuaApi() {
    g_Lua.setTop = GameFunction<LuaSetTopFn>(retail::kLuaSetTop);
    g_Lua.type = GameFunction<LuaTypeFn>(retail::kLuaType);
    g_Lua.toLString = GameFunction<LuaToLStringFn>(retail::kLuaToLString);
    g_Lua.pushLString = GameFunction<LuaPushLStringFn>(retail::kLuaPushLString);
    g_Lua.pushString = GameFunction<LuaPushStringFn>(retail::kLuaPushString);
    g_Lua.pushCClosure = GameFunction<LuaPushCClosureFn>(retail::kLuaPushCClosure);
    g_Lua.getStr = GameFunction<LuaGetStrFn>(retail::kLuaGetStr);
    g_Lua.setStr = GameFunction<LuaSetStrFn>(retail::kLuaSetStr);
    g_Lua.createTable = GameFunction<LuaCreateTableFn>(retail::kLuaCreateTable);
    g_Lua.setGlobal = GameFunction<LuaSetGlobalFn>(retail::kLuaSetGlobal);
    g_Lua.pcallK = GameFunction<LuaPCallKFn>(retail::kLuaPCallK);
    g_Lua.error = GameFunction<LuaErrorFn>(retail::kLuaError);
    g_Lua.loadBufferX = GameFunction<LuaLoadBufferXFn>(retail::kLuaLoadBufferX);
}

const char* EventName(CanopyEvent event) {
    switch (event) {
        case CANOPY_EVENT_FIELD_ENTER:
            return "control";
        case CANOPY_EVENT_FIELD_LEAVE:
            return "no_control";
        case CANOPY_EVENT_SAVE_LOADED:
            return "save_loaded";
        case CANOPY_EVENT_TITLE_SCREEN:
            return "title";
        default:
            return nullptr;
    }
}

}  // namespace

bool Install(bool ticking) {
    const std::string modsDir = ModsFolder();
    const std::vector<mod_rules::ModFolder> folders =
        mod_rules::WithLuaScripts(mod_rules::ScanMods(modsDir), modsDir);
    if (folders.empty()) {
        Log("No Lua mods, so the Lua host is off.");
        return false;
    }
    if (!ticking) {
        Log("The Lua host needs the frame tick, so it is off.");
        return false;
    }
    if (!RequirementsMet(retail::kLuaHostRequirements)) {
        Log("The Lua host is off.");
        return false;
    }
    ResolveLuaApi();
    g_Api = mod_loader::Api();

    DetourSet hooks("Lua host");
    if (!hooks.Add(
            retail::kRegisterGameCommands, reinterpret_cast<void*>(&Detour_Registrar),
            reinterpret_cast<void**>(&g_OriginalRegistrar), "registerGameCommands"
        ) ||
        !hooks.Add(
            retail::kScriptModuleLoader, reinterpret_cast<void*>(&Detour_Loader),
            reinterpret_cast<void**>(&g_OriginalLoader), "scriptModuleLoader"
        )) {
        hooks.RemoveAll();
        Log("The Lua host is off, its hooks could not be created.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    LuaRegistrar registrar;
    mod_rules::ModAccept accept;
    accept.attach = [&registrar](const mod_rules::ModSpec& mod) { return registrar.Attach(mod); };
    accept.setting = [&registrar](const mod_rules::ModSpec&, const mod_rules::SettingSpec& setting) {
        return registrar.AddSetting(setting);
    };
    accept.registered = [&registrar](const mod_rules::ModSpec& mod) { registrar.Registered(mod); };
    mod_rules::ReadMods(folders, report, accept);
    if (g_Mods.empty()) {
        hooks.RemoveAll();
        Log("No Lua mod could be registered, so the Lua host is off.");
        return false;
    }
    g_Active = true;
    Log(
        "Lua host installed for " + std::to_string(g_Mods.size()) + (g_Mods.size() == 1 ? " Lua mod." : " Lua mods.")
    );
    return true;
}

void OnEvent(CanopyEvent event) {
    const char* name = EventName(event);
    if (!g_Active || !name) {
        return;
    }
    for (LuaMod& mod : g_Mods) {
        if (mod.stopped) {
            continue;
        }
        if (mod.events.size() == kMaxQueuedEvents) {
            mod.events.erase(mod.events.begin());
        }
        mod.events.emplace_back(name);
    }
}

bool PostEvent(const std::string& modKey, const std::string& event) {
    LuaMod* mod = g_Active ? FindMod(modKey.c_str()) : nullptr;
    if (!mod || mod->stopped) {
        return false;
    }
    if (mod->events.size() == kMaxQueuedEvents) {
        mod->events.erase(mod->events.begin());
    }
    mod->events.push_back(event);
    return true;
}

void Tick() {
    if (!g_Active || g_Exited) {
        return;
    }
    uint8_t* manager = ReadGlobal<uint8_t*>(retail::kScriptManager);
    uint8_t* root = manager ? ReadAt<uint8_t*>(manager, retail::kScriptManagerRoot) : nullptr;
    if (!root) {
        return;
    }
    if (ReadAt<lua_State*>(root, retail::kScriptRootState) != g_HostState || !g_HostState) {
        if (!g_LoggedNoHost) {
            g_LoggedNoHost = true;
            Log("The script root has no Lua host, so Lua mods do not run.");
        }
        return;
    }
    if (root != g_Root) {
        if (g_Root) {
            Log("The script root changed, so the Lua mods restart.");
            for (LuaMod& mod : g_Mods) {
                StopPlayer(mod);
                mod.player = nullptr;
                mod.context = nullptr;
                mod.created = false;
                mod.errors = 0;
                mod.stopped = false;
            }
        }
        g_Root = root;
    }
    for (size_t index = 0; index < g_Mods.size(); ++index) {
        if (!g_Mods[index].created && !g_Mods[index].stopped) {
            CreatePlayer(manager, index);
        }
    }
    const uint8_t* timer = ReadGlobal<uint8_t*>(retail::kTimeManager);
    if (!timer) {
        return;
    }
    // The frame's timing, as the engine passes it to its own players.
    alignas(8) uint8_t info[retail::kUpdateInfoSize];
    std::memcpy(info, timer + retail::kTimeUpdateInfo, sizeof(info));
    const auto update = GameFunction<PlayerUpdateFn>(retail::kScriptPlayerUpdate);
    for (LuaMod& mod : g_Mods) {
        if (mod.player && !mod.stopped) {
            update(mod.player, info);
        }
    }
}

void Exit() {
    if (!g_Active || g_Exited) {
        return;
    }
    g_Exited = true;
    for (LuaMod& mod : g_Mods) {
        StopPlayer(mod);
    }
}

std::string RunningScript() {
    const void* context = TopContext();
    if (!context) {
        return std::string();
    }
    char name[96];
    ReadThreadName(context, name);
    return name;
}

}  // namespace lua_host
