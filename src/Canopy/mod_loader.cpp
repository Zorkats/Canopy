#define NOMINMAX
#include "mod_loader.h"

#include <windows.h>

#include <malloc.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core.h"
#include "game_events.h"
#include "game_image.h"
#include "game_ui.h"
#include "hook_chain.h"
#include "input.h"
#include "mod_config.h"
#include "canopy_api.h"

// =============================================================
// MOD LOADER DESIGN
// =============================================================
// A mod is a DLL in the game's mods folder that exports CanopyModInit, and may
// export a CanopyManifest that names it and the mods it depends on. The loader
// first loads every DLL in file name order and reads each manifest. Then it
// plans the start order (Resolve): a mod starts after the mods it depends on,
// otherwise in file name order, and a mod is left out when a required
// dependency is missing, too old or left out, or when it is in a dependency
// cycle. The planned mods start one at a time on the startup thread, and each
// is called with one table of functions (sdk/canopy_api.h). Everything a mod
// registers from CanopyModInit (mods, settings, frame callbacks, hooks,
// interfaces) is recorded against the DLL being started, so a mod that fails
// or faults in CanopyModInit is backed out completely and unloaded, leaving the
// others untouched.
//
// Once every mod has started, the settings store is sealed and every hook is
// enabled in one go, the same way the loader's own hooks always were. Each
// frame tick runs the event callbacks, then a closed Yes/No popup's answer
// callback, then the frame callbacks; exit callbacks run once when the game
// closes. Every kind runs in the order mods registered them. A fault in any
// callback is caught: the loader logs it and stops calling that DLL's
// frame, event, button and answer callbacks, so the game keeps running.
// Exit callbacks still run, so every mod gets to save.
//
// Every string a mod passes is copied, so a mod may build its text on the
// stack. A language a mod leaves out shows its English text instead.
//
// Hooks go through the loader's hook chains (hook_chain.cpp), so any number
// of mods may detour the same function, each in creation order.
//
// Interfaces are written only during startup, so GetInterface reads them with
// no lock. Only the log of failed lookups takes a lock, since it may be
// written later from any thread.

namespace mod_loader {
namespace {

using namespace core;

constexpr const char* kModsFolder = "mods";

struct LoadedMod {
    HMODULE module;
    std::string file;
    CanopyModShutdownFn shutdown;
    // Set when one of its callbacks faulted. Only its exit callbacks still
    // run.
    bool crashed;
    // The manifest's id and version. Empty and 0 for a mod without one.
    std::string id;
    uint32_t version;
    // Set once CanopyModInit has returned true.
    bool started;
};

struct ModHook {
    // The DLL that created it, an index into g_Loaded.
    size_t owner;
    CanopyMod mod;
    void* target;
};

// A frame or exit callback. Both take the user pointer alone.
struct Callback {
    size_t owner;
    void (*callback)(void* user);
    void* user;
};

std::vector<LoadedMod> g_Loaded;
std::vector<ModHook> g_Hooks;
std::vector<Callback> g_Frames;
std::vector<Callback> g_Exits;

struct EventCallback {
    size_t owner;
    CanopyEventFn callback;
    void* user;
};

struct Button {
    // The mod_config setting the row shows.
    int setting;
    size_t owner;
    CanopyButtonFn callback;
    void* user;
};

// A table a mod provided with ProvideInterface.
struct Provided {
    // The DLL that provided it, an index into g_Loaded.
    size_t owner;
    std::string name;
    uint32_t version;
    const void* table;
};

// The Yes/No popup a mod opened, waiting for its answer.
struct PendingAnswer {
    bool active;
    size_t owner;
    CanopyMod mod;
    CanopyAnswerFn callback;
    void* user;
};

std::vector<EventCallback> g_Events;
std::vector<Button> g_Buttons;
// The tables mods provided, in the order they were provided.
std::vector<Provided> g_Provided;
PendingAnswer g_Answer = {};
// The thread the frame tick runs on, 0 before the first tick.
DWORD g_GameThread = 0;
// The DLL each registered mod belongs to, by mod handle.
std::vector<size_t> g_ModOwners;
// The DLL whose CanopyModInit is running.
size_t g_Current = 0;
bool g_Ticking = false;
bool g_ExitHooked = false;
bool g_Exited = false;
bool g_Started = false;
// The failed GetInterface messages already logged, guarded by g_MissMutex.
std::mutex g_MissMutex;
std::set<std::string> g_LoggedMisses;

// Copies of every string mods passed in. Never freed, since the options menu
// reads them for as long as the game runs.
std::deque<std::string> g_Strings;
std::vector<std::unique_ptr<mod_config::Text[]>> g_Texts;

CanopyApi g_Api = {};

const char* Intern(const char* text) {
    g_Strings.emplace_back(text);
    return g_Strings.back().c_str();
}

// Copies count CanopyTexts into one stored Text array, English standing in for
// any language left out. Null when a text has no string at all.
const mod_config::Text* InternTexts(const CanopyText* source, int32_t count) {
    auto texts = std::make_unique<mod_config::Text[]>(count);
    for (int32_t index = 0; index < count; ++index) {
        const CanopyText& text = source[index];
        const char* const fields[retail::kLanguageCount] = {
            text.japanese, text.english, text.french, text.spanish, text.chinese_traditional, text.korean,
            text.chinese_simplified,
        };
        const char* fallback = fields[retail::kLanguageEnglish];
        for (const char* field : fields) {
            if (!fallback) {
                fallback = field;
            }
        }
        if (!fallback) {
            return nullptr;
        }
        for (int language = 0; language < retail::kLanguageCount; ++language) {
            texts[index][language] = Intern(fields[language] ? fields[language] : fallback);
        }
    }
    g_Texts.push_back(std::move(texts));
    return g_Texts.back().get();
}

// The same string in every language.
const mod_config::Text* InternPlain(const char* text) {
    const CanopyText plain = {text, text, text, text, text, text, text};
    return InternTexts(&plain, 1);
}

bool ValidMod(CanopyMod mod) {
    return mod >= 0 && static_cast<size_t>(mod) < g_ModOwners.size();
}

std::string ModName(CanopyMod mod) {
    return ValidMod(mod) ? mod_config::ModAt(static_cast<size_t>(mod)).key : "unknown mod";
}

// The DLL a registration belongs to: the one starting, or the mod's own
// once startup is over.
size_t OwnerOf(CanopyMod mod) {
    return g_Started ? g_ModOwners[static_cast<size_t>(mod)] : g_Current;
}

void Reject(CanopyMod mod, const std::string& what, const char* reason) {
    Log("[" + ModName(mod) + "] " + what + " rejected, " + reason + ".");
}

// ---- API functions -----------------------------------------------------

uintptr_t Api_GameBase() {
    return g_ModuleBase;
}

int32_t Api_Language() {
    return CurrentLanguage();
}

bool Api_PlayerInField() {
    return PlayerInField();
}

bool Api_GameFocused() {
    return GameWindowFocused();
}

// A mod that logs before it registers is named by its DLL's file name.
void Api_Log(CanopyMod mod, const char* message) {
    const std::string name = ValidMod(mod) || g_Started || g_Current >= g_Loaded.size() ? ModName(mod)
                                                                                         : g_Loaded[g_Current].file;
    Log("[" + name + "] " + (message ? message : ""));
}

CanopyMod Api_RegisterMod(const char* key, const CanopyText* name) {
    if (!key) {
        Log("A mod passed no key to RegisterMod.");
        return CANOPY_NO_MOD;
    }
    const char* storedKey = Intern(key);
    const mod_config::Text* storedName = name ? InternTexts(name, 1) : InternPlain(key);
    if (!storedName) {
        storedName = InternPlain(key);
    }
    const int mod = mod_config::RegisterMod(storedKey, *storedName);
    if (mod == mod_config::kNoMod) {
        return CANOPY_NO_MOD;
    }
    g_ModOwners.resize(static_cast<size_t>(mod) + 1, g_Current);
    g_ModOwners[static_cast<size_t>(mod)] = g_Current;
    return mod;
}

// Copies a setting's key, label and help, or logs why it cannot.
bool InternSetting(
    CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, const char*& storedKey,
    const mod_config::Text*& storedLabel, const mod_config::Text*& storedHelp
) {
    const std::string what = std::string("Setting ") + (key ? key : "(null)");
    if (!key || !label) {
        Reject(mod, what, "it needs a key and a label");
        return false;
    }
    storedKey = Intern(key);
    storedLabel = InternTexts(label, 1);
    storedHelp = help ? InternTexts(help, 1) : InternPlain("");
    if (!storedLabel) {
        Reject(mod, what, "its label has no text");
        return false;
    }
    if (!storedHelp) {
        storedHelp = InternPlain("");
    }
    return true;
}

CanopySetting Api_AddToggle(CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, bool defaultOn) {
    const char* storedKey = nullptr;
    const mod_config::Text* storedLabel = nullptr;
    const mod_config::Text* storedHelp = nullptr;
    if (!InternSetting(mod, key, label, help, storedKey, storedLabel, storedHelp)) {
        return CANOPY_NO_SETTING;
    }
    return mod_config::AddToggle(mod, storedKey, *storedLabel, *storedHelp, defaultOn);
}

CanopySetting Api_AddChoice(
    CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, const CanopyText* choices, int32_t count,
    int32_t defaultIndex
) {
    const char* storedKey = nullptr;
    const mod_config::Text* storedLabel = nullptr;
    const mod_config::Text* storedHelp = nullptr;
    if (!InternSetting(mod, key, label, help, storedKey, storedLabel, storedHelp)) {
        return CANOPY_NO_SETTING;
    }
    if (!choices || count < 2 || count > mod_config::kMaxChoices) {
        Reject(mod, std::string("Setting ") + key, "it needs 2 to 8 choices");
        return CANOPY_NO_SETTING;
    }
    const mod_config::Text* storedChoices = InternTexts(choices, count);
    if (!storedChoices) {
        Reject(mod, std::string("Setting ") + key, "a choice has no text");
        return CANOPY_NO_SETTING;
    }
    return mod_config::AddChoice(mod, storedKey, *storedLabel, *storedHelp, storedChoices, count, defaultIndex);
}

CanopySetting Api_AddBinding(
    CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, CanopyDevice device, int32_t defaultPad
) {
    const char* storedKey = nullptr;
    const mod_config::Text* storedLabel = nullptr;
    const mod_config::Text* storedHelp = nullptr;
    if (!InternSetting(mod, key, label, help, storedKey, storedLabel, storedHelp)) {
        return CANOPY_NO_SETTING;
    }
    if (device != CANOPY_DEVICE_KEYBOARD_MOUSE && device != CANOPY_DEVICE_CONTROLLER) {
        Reject(mod, std::string("Setting ") + key, "its device is not valid");
        return CANOPY_NO_SETTING;
    }
    const input::Device inputDevice =
        device == CANOPY_DEVICE_CONTROLLER ? input::Device::Controller : input::Device::KeyboardMouse;
    return mod_config::AddBinding(mod, storedKey, *storedLabel, *storedHelp, inputDevice, defaultPad);
}

int32_t Api_GetSetting(CanopySetting setting, int32_t fallback) {
    return mod_config::Value(setting, fallback);
}

const char* Api_SettingsFile(CanopyMod mod) {
    return mod_config::SettingsFile(mod);
}

bool Api_OnFrame(CanopyMod mod, CanopyFrameFn callback, void* user) {
    if (!ValidMod(mod) || !callback) {
        Reject(mod, "Frame callback", "it needs a registered mod and a callback");
        return false;
    }
    if (!g_Ticking) {
        Reject(mod, "Frame callback", "the frame hook is not available on this game build");
        return false;
    }
    g_Frames.push_back({OwnerOf(mod), callback, user});
    return true;
}

bool Api_OnExit(CanopyMod mod, CanopyExitFn callback, void* user) {
    if (!ValidMod(mod) || !callback) {
        Reject(mod, "Exit callback", "it needs a registered mod and a callback");
        return false;
    }
    if (!g_ExitHooked) {
        Reject(mod, "Exit callback", "the exit hook is not available on this game build");
        return false;
    }
    g_Exits.push_back({OwnerOf(mod), callback, user});
    return true;
}

CanopySetting Api_AddButton(CanopyMod mod, const char* key, const CanopyText* label, CanopyButtonFn callback, void* user) {
    const char* storedKey = nullptr;
    const mod_config::Text* storedLabel = nullptr;
    const mod_config::Text* storedHelp = nullptr;
    if (!InternSetting(mod, key, label, nullptr, storedKey, storedLabel, storedHelp)) {
        return CANOPY_NO_SETTING;
    }
    if (!callback) {
        Reject(mod, std::string("Button ") + key, "it needs a callback");
        return CANOPY_NO_SETTING;
    }
    const int setting = mod_config::AddButton(mod, storedKey, *storedLabel);
    if (setting == mod_config::kNoSetting) {
        return CANOPY_NO_SETTING;
    }
    g_Buttons.push_back({setting, OwnerOf(mod), callback, user});
    return setting;
}

bool OnGameThread() {
    return g_GameThread != 0 && GetCurrentThreadId() == g_GameThread;
}

// Never logs the text itself: mods may show anything in it.
bool Api_ShowToast(CanopyMod mod, const char* text) {
    if (!ValidMod(mod) || !text) {
        Reject(mod, "Toast", "it needs a registered mod and a text");
        return false;
    }
    if (!OnGameThread()) {
        Reject(mod, "Toast", "it can only be shown from the game thread");
        return false;
    }
    if (const char* reason = game_ui::ShowToast(text)) {
        Reject(mod, "Toast", reason);
        return false;
    }
    return true;
}

bool Api_ShowYesNo(CanopyMod mod, const char* text, CanopyAnswerFn callback, void* user) {
    if (!ValidMod(mod) || !text || !callback) {
        Reject(mod, "Yes/No popup", "it needs a registered mod, a text and a callback");
        return false;
    }
    if (!OnGameThread()) {
        Reject(mod, "Yes/No popup", "it can only be opened from the game thread");
        return false;
    }
    if (const char* reason = g_Answer.active ? "another Yes/No popup is open" : game_ui::OpenYesNo(text)) {
        Reject(mod, "Yes/No popup", reason);
        return false;
    }
    g_Answer = {true, OwnerOf(mod), mod, callback, user};
    Log("[" + ModName(mod) + "] Yes/No popup opened.");
    return true;
}

bool Api_OnEvent(CanopyMod mod, CanopyEventFn callback, void* user) {
    if (!ValidMod(mod) || !callback) {
        Reject(mod, "Event callback", "it needs a registered mod and a callback");
        return false;
    }
    if (!g_Ticking) {
        Reject(mod, "Event callback", "the frame hook is not available on this game build");
        return false;
    }
    g_Events.push_back({OwnerOf(mod), callback, user});
    return true;
}

uintptr_t Api_FindPattern(const char* pattern) {
    const char* reason = nullptr;
    const uintptr_t address = game_image::FindPattern(pattern, reason);
    if (reason) {
        Log(std::string("FindPattern rejected the pattern \"") + (pattern ? pattern : "(null)") + "\", " + reason + ".");
    }
    return address;
}

uint32_t Api_GameBuild() {
    return game_image::BuildId();
}

bool Api_CreateHook(CanopyMod mod, void* target, void* detour, void** original) {
    if (!ValidMod(mod) || !target || !detour || !original) {
        Reject(mod, "Hook", "it needs a registered mod, a target, a detour and a place for the original");
        return false;
    }
    if (const char* reason = hook_chain::Add(target, mod, detour, original)) {
        Reject(mod, "Hook at " + Hex(reinterpret_cast<uintptr_t>(target)), reason);
        return false;
    }
    g_Hooks.push_back({OwnerOf(mod), mod, target});
    return true;
}

bool Api_RemoveHook(CanopyMod mod, void* target) {
    const auto hook = std::find_if(g_Hooks.begin(), g_Hooks.end(), [mod, target](const ModHook& entry) {
        return entry.mod == mod && entry.target == target;
    });
    if (hook == g_Hooks.end()) {
        return false;
    }
    hook_chain::Remove(target, mod);
    g_Hooks.erase(hook);
    return true;
}

bool Api_Pressed(int32_t pad) {
    return input::Pressed(pad);
}

bool Api_Held(int32_t pad) {
    return input::Held(pad);
}

int32_t Api_WheelSteps() {
    return input::WheelSteps();
}

const char* Api_PadName(int32_t pad) {
    return input::PadName(pad);
}

// The table provided under name, or null. Names compare without case.
const Provided* FindProvided(const std::string& name) {
    for (const Provided& entry : g_Provided) {
        if (_stricmp(entry.name.c_str(), name.c_str()) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

bool Api_ProvideInterface(CanopyMod mod, const char* name, uint32_t version, const void* table) {
    if (!ValidMod(mod) || !name || !*name || !table) {
        Reject(mod, "Interface", "it needs a registered mod, a name and a table");
        return false;
    }
    if (g_Started) {
        Reject(mod, std::string("Interface ") + name, "it can only be provided during CanopyModInit");
        return false;
    }
    if (FindProvided(name)) {
        Reject(mod, std::string("Interface ") + name, "its name is already provided");
        return false;
    }
    g_Provided.push_back({OwnerOf(mod), name, version, table});
    return true;
}

// Logs a failed lookup once for each distinct message. Any thread.
void LogMissOnce(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_MissMutex);
    if (g_LoggedMisses.insert(message).second) {
        Log(message);
    }
}

const void* Api_GetInterface(CanopyMod mod, const char* name, uint32_t minVersion) {
    if (!ValidMod(mod) || !name || !*name) {
        Reject(mod, "Interface lookup", "it needs a registered mod and a name");
        return nullptr;
    }
    const Provided* found = FindProvided(name);
    if (!found) {
        LogMissOnce("[" + ModName(mod) + "] Interface " + name + " is not provided.");
        return nullptr;
    }
    if (found->version < minVersion) {
        LogMissOnce(
            "[" + ModName(mod) + "] Interface " + name + " is version " + std::to_string(found->version) +
            ", and it needs " + std::to_string(minVersion) + " or later."
        );
        return nullptr;
    }
    return found->table;
}

// The version of the started mod with this id, or 0. Ids compare without case.
uint32_t Api_ModVersion(const char* id) {
    if (!id) {
        return 0;
    }
    for (const LoadedMod& mod : g_Loaded) {
        if (mod.started && _stricmp(mod.id.c_str(), id) == 0) {
            return mod.version;
        }
    }
    return 0;
}

void BuildApi() {
    g_Api.size = sizeof(CanopyApi);
    g_Api.version = CANOPY_API_VERSION;
    g_Api.GameBase = &Api_GameBase;
    g_Api.Language = &Api_Language;
    g_Api.PlayerInField = &Api_PlayerInField;
    g_Api.GameFocused = &Api_GameFocused;
    g_Api.Log = &Api_Log;
    g_Api.RegisterMod = &Api_RegisterMod;
    g_Api.AddToggle = &Api_AddToggle;
    g_Api.AddChoice = &Api_AddChoice;
    g_Api.AddBinding = &Api_AddBinding;
    g_Api.GetSetting = &Api_GetSetting;
    g_Api.SettingsFile = &Api_SettingsFile;
    g_Api.OnFrame = &Api_OnFrame;
    g_Api.CreateHook = &Api_CreateHook;
    g_Api.RemoveHook = &Api_RemoveHook;
    g_Api.Pressed = &Api_Pressed;
    g_Api.Held = &Api_Held;
    g_Api.WheelSteps = &Api_WheelSteps;
    g_Api.PadName = &Api_PadName;
    g_Api.OnExit = &Api_OnExit;
    g_Api.AddButton = &Api_AddButton;
    g_Api.ShowToast = &Api_ShowToast;
    g_Api.ShowYesNo = &Api_ShowYesNo;
    g_Api.OnEvent = &Api_OnEvent;
    g_Api.FindPattern = &Api_FindPattern;
    g_Api.GameBuild = &Api_GameBuild;
    g_Api.ProvideInterface = &Api_ProvideInterface;
    g_Api.GetInterface = &Api_GetInterface;
    g_Api.ModVersion = &Api_ModVersion;
}

// ---- Loading -----------------------------------------------------------

// Holds no C++ objects, so a fault in the mod is caught here.
bool CallInit(CanopyModInitFn init, const CanopyApi* api, bool& started, DWORD& code) {
    __try {
        started = init(api);
        return true;
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        started = false;
        return false;
    }
}

bool CallShutdown(CanopyModShutdownFn shutdown, DWORD& code) {
    __try {
        shutdown();
        return true;
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Calls one mod callback with the arguments call points to.
using Invoke = void (*)(const void* call);

// Holds no C++ objects, so a fault in the mod is caught here.
bool CallGuarded(Invoke invoke, const void* call, DWORD& code) {
    __try {
        invoke(call);
        return true;
    } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// One call of each callback kind, with its arguments.
struct EventCall {
    CanopyEventFn callback;
    CanopyEvent event;
    void* user;
};

struct AnswerCall {
    CanopyAnswerFn callback;
    bool yes;
    void* user;
};

void InvokeCallback(const void* call) {
    const auto& callback = *static_cast<const Callback*>(call);
    callback.callback(callback.user);
}

void InvokeEvent(const void* call) {
    const auto& event = *static_cast<const EventCall*>(call);
    event.callback(event.event, event.user);
}

void InvokeAnswer(const void* call) {
    const auto& answer = *static_cast<const AnswerCall*>(call);
    answer.callback(answer.yes, answer.user);
}

void InvokeButton(const void* call) {
    const auto& button = *static_cast<const Button*>(call);
    button.callback(button.user);
}

// Runs one callback of owner's, logging a fault as what in the DLL's name.
// False when it faulted.
bool RunGuarded(size_t owner, Invoke invoke, const void* call, const char* what) {
    DWORD code = 0;
    if (CallGuarded(invoke, call, code)) {
        return true;
    }
    // A stack overflow leaves the stack without its guard page until reset.
    if (code == EXCEPTION_STACK_OVERFLOW) {
        _resetstkoflw();
    }
    Log("Mod " + g_Loaded[owner].file + " crashed in " + what + " with exception " + Hex(code) + ".");
    return false;
}

// Runs a frame, event, button or answer callback, unless its DLL crashed
// before. A fault stops all of them for that DLL.
void RunGameCallback(size_t owner, Invoke invoke, const void* call, const char* what) {
    LoadedMod& mod = g_Loaded[owner];
    if (!mod.crashed && !RunGuarded(owner, invoke, call, what)) {
        mod.crashed = true;
        Log("Mod " + mod.file + " gets no more callbacks. The game keeps running.");
    }
}

template <typename Entry>
void RemoveByOwner(std::vector<Entry>& entries, size_t owner) {
    entries.erase(
        std::remove_if(entries.begin(), entries.end(), [owner](const Entry& entry) { return entry.owner == owner; }),
        entries.end()
    );
}

// Removes every hook and callback a DLL registered.
void RemoveOwned(size_t owner) {
    for (const ModHook& hook : g_Hooks) {
        if (hook.owner == owner) {
            hook_chain::Remove(hook.target, hook.mod);
        }
    }
    g_Hooks.erase(
        std::remove_if(g_Hooks.begin(), g_Hooks.end(), [owner](const ModHook& hook) { return hook.owner == owner; }),
        g_Hooks.end()
    );
    RemoveByOwner(g_Frames, owner);
    RemoveByOwner(g_Exits, owner);
    RemoveByOwner(g_Events, owner);
    RemoveByOwner(g_Buttons, owner);
    RemoveByOwner(g_Provided, owner);
}

// ---- Start order -------------------------------------------------------

// One dependency from a manifest.
struct Dependency {
    std::string id;
    uint32_t minVersion;
    bool optional;
};

// What Resolve needs to know about one DLL. A mod without a manifest has no
// id and no dependencies, and no other mod can depend on it.
struct ModInfo {
    std::string file;
    bool hasManifest = false;
    std::string id;
    uint32_t version = 0;
    uint32_t minApiVersion = 0;
    std::vector<Dependency> dependencies;
};

// The mods to start, in order, as indices into the mods Resolve was given,
// and one log line for each mod left out and each optional dependency that
// is missing, too old, or starts after the mod that names it.
struct Plan {
    std::vector<size_t> order;
    std::vector<std::string> messages;
};

bool SameId(const std::string& left, const std::string& right) {
    return _stricmp(left.c_str(), right.c_str()) == 0;
}

// 1 to 32 letters, digits, '_' or '-', as for a mod key.
bool ValidId(const std::string& id) {
    if (id.empty() || id.size() > mod_config::kMaxKeyLength) {
        return false;
    }
    for (const char c : id) {
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

// A packed version as major.minor.patch.
std::string VersionText(uint32_t version) {
    return std::to_string(version >> 16) + "." + std::to_string((version >> 8) & 0xFF) + "." +
           std::to_string(version & 0xFF);
}

// Plans the start order. It reads only its arguments, so it can be tested
// without the game. A mod is left out when its own manifest cannot be used,
// when a required dependency is missing, too old or left out, or when it is in
// a cycle of required dependencies. Optional dependencies never leave a mod
// out. A mod without a manifest is never left out. Ties are broken by file
// name order.
Plan Resolve(const std::vector<ModInfo>& mods, uint32_t apiVersion) {
    const size_t count = mods.size();
    std::vector<bool> alive(count, true);
    Plan plan;
    const auto leave = [&](size_t index, const std::string& reason) {
        plan.messages.push_back("Mod " + mods[index].file + " skipped, " + reason + ".");
        alive[index] = false;
    };
    // The alive mod with this id, or count when there is none.
    const auto findAlive = [&](const std::string& id) {
        for (size_t index = 0; index < count; ++index) {
            if (alive[index] && mods[index].hasManifest && SameId(mods[index].id, id)) {
                return index;
            }
        }
        return count;
    };
    // Whether any mod, alive or not, has this id.
    const auto installed = [&](const std::string& id) {
        return std::any_of(mods.begin(), mods.end(), [&id](const ModInfo& mod) {
            return mod.hasManifest && SameId(mod.id, id);
        });
    };

    // A manifest that cannot be used takes its mod out before anything else.
    for (size_t index = 0; index < count; ++index) {
        const ModInfo& mod = mods[index];
        if (!mod.hasManifest) {
            continue;
        }
        if (!ValidId(mod.id)) {
            leave(index, "its manifest id is not valid");
        } else if (mod.minApiVersion > apiVersion) {
            leave(
                index, "it needs API version " + std::to_string(mod.minApiVersion) +
                           ", and this loader has version " + std::to_string(apiVersion)
            );
        } else {
            for (const Dependency& dependency : mod.dependencies) {
                if (!ValidId(dependency.id)) {
                    leave(index, "a dependency id is not valid");
                    break;
                }
                if (SameId(dependency.id, mod.id)) {
                    leave(index, "it depends on itself");
                    break;
                }
            }
        }
    }

    // The first mod to claim an id keeps it. A later mod with the same id is
    // left out.
    for (size_t index = 0; index < count; ++index) {
        if (!alive[index] || !mods[index].hasManifest) {
            continue;
        }
        for (size_t earlier = 0; earlier < index; ++earlier) {
            if (alive[earlier] && mods[earlier].hasManifest && SameId(mods[earlier].id, mods[index].id)) {
                leave(index, "its id " + mods[index].id + " is already used by " + mods[earlier].file);
                break;
            }
        }
    }

    // Leaves out every mod with a required dependency that is missing, too old
    // or left out, until nothing changes.
    const auto settleRequired = [&]() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t index = 0; index < count; ++index) {
                if (!alive[index]) {
                    continue;
                }
                for (const Dependency& dependency : mods[index].dependencies) {
                    if (dependency.optional) {
                        continue;
                    }
                    const size_t target = findAlive(dependency.id);
                    if (target == count) {
                        leave(
                            index, "it needs " + dependency.id +
                                       (installed(dependency.id) ? ", which was skipped" : ", which is not installed")
                        );
                    } else if (dependency.minVersion != 0 && mods[target].version < dependency.minVersion) {
                        leave(
                            index, "it needs " + dependency.id + " version " + VersionText(dependency.minVersion) +
                                       " or later, found " + VersionText(mods[target].version)
                        );
                    } else {
                        continue;
                    }
                    changed = true;
                    break;
                }
            }
        }
    };

    // Orders the alive mods. A required dependency always comes before the mod
    // that names it. An optional dependency comes before it when the order
    // allows, and is dropped when it would close a cycle.
    for (;;) {
        settleRequired();
        // targets[index][k]: the alive mod that dependency k of index names, or
        // count when none does.
        std::vector<std::vector<size_t>> targets(count);
        // hard[index] and soft[index]: the required and optional dependencies of
        // index that are not placed yet.
        std::vector<size_t> hard(count, 0);
        std::vector<size_t> soft(count, 0);
        for (size_t index = 0; index < count; ++index) {
            if (!alive[index]) {
                continue;
            }
            for (const Dependency& dependency : mods[index].dependencies) {
                const size_t target = findAlive(dependency.id);
                targets[index].push_back(target);
                if (target != count) {
                    ++(dependency.optional ? soft : hard)[index];
                }
            }
        }

        // Each step places the lowest index whose dependencies are all placed.
        // When there is none, it places the lowest index whose required
        // dependencies are all placed, and the optional ones it still waits on
        // are dropped.
        std::vector<size_t> order;
        std::vector<bool> placed(count, false);
        for (;;) {
            size_t pick = count;
            for (size_t index = 0; index < count && pick == count; ++index) {
                if (alive[index] && !placed[index] && hard[index] == 0 && soft[index] == 0) {
                    pick = index;
                }
            }
            for (size_t index = 0; index < count && pick == count; ++index) {
                if (alive[index] && !placed[index] && hard[index] == 0) {
                    pick = index;
                }
            }
            if (pick == count) {
                break;
            }
            placed[pick] = true;
            order.push_back(pick);
            // Every dependency that names pick is no longer waited for.
            for (size_t index = 0; index < count; ++index) {
                if (!alive[index]) {
                    continue;
                }
                for (size_t k = 0; k < targets[index].size(); ++k) {
                    if (targets[index][k] == pick) {
                        --(mods[index].dependencies[k].optional ? soft : hard)[index];
                    }
                }
            }
        }

        std::vector<size_t> stuck;
        for (size_t index = 0; index < count; ++index) {
            if (alive[index] && !placed[index]) {
                stuck.push_back(index);
            }
        }
        if (stuck.empty()) {
            plan.order = order;
            break;
        }

        // The required dependencies of index that are not placed yet.
        const auto requiredWaits = [&](size_t index) {
            std::vector<size_t> waits;
            for (size_t k = 0; k < targets[index].size(); ++k) {
                const size_t target = targets[index][k];
                if (!mods[index].dependencies[k].optional && target != count && !placed[target]) {
                    waits.push_back(target);
                }
            }
            return waits;
        };
        // A stuck mod is in a cycle when it reaches itself again through the
        // required dependencies that are not placed.
        std::vector<size_t> members;
        for (const size_t start : stuck) {
            std::vector<bool> seen(count, false);
            std::vector<size_t> frontier = requiredWaits(start);
            bool reaches = false;
            while (!frontier.empty() && !reaches) {
                const size_t current = frontier.back();
                frontier.pop_back();
                if (current == start) {
                    reaches = true;
                } else if (!seen[current]) {
                    seen[current] = true;
                    const std::vector<size_t> next = requiredWaits(current);
                    frontier.insert(frontier.end(), next.begin(), next.end());
                }
            }
            if (reaches) {
                members.push_back(start);
            }
        }
        // Every stuck mod waits on a stuck mod through a required dependency, so
        // a cycle always exists. Leaving every stuck mod out is the fallback that
        // keeps this loop finite if that ever fails.
        if (members.empty()) {
            members = stuck;
        }
        for (const size_t member : members) {
            leave(member, "it is part of a dependency cycle");
        }
    }

    // Where each mod starts, so an optional dependency that starts after the
    // mod that names it can be noted.
    std::vector<size_t> position(count, count);
    for (size_t slot = 0; slot < plan.order.size(); ++slot) {
        position[plan.order[slot]] = slot;
    }
    // Optional dependencies that are missing or old still let the mod load. One
    // that starts after its mod means the two depend on each other.
    for (const size_t index : plan.order) {
        for (const Dependency& dependency : mods[index].dependencies) {
            if (!dependency.optional) {
                continue;
            }
            const size_t target = findAlive(dependency.id);
            if (target == count) {
                plan.messages.push_back(
                    "Mod " + mods[index].file + " loads without optional dependency " + dependency.id +
                    (installed(dependency.id) ? ", which was skipped." : ", which is not installed.")
                );
                continue;
            }
            if (dependency.minVersion != 0 && mods[target].version < dependency.minVersion) {
                plan.messages.push_back(
                    "Mod " + mods[index].file + " loads, but optional dependency " + dependency.id + " is version " +
                    VersionText(mods[target].version) + ", and it asks for " + VersionText(dependency.minVersion) +
                    " or later."
                );
            }
            if (position[target] > position[index]) {
                plan.messages.push_back(
                    "Mod " + mods[index].file + " starts before optional dependency " + dependency.id +
                    ", because they depend on each other."
                );
            }
        }
    }
    return plan;
}

// ---- Discovery and start -----------------------------------------------

// A mod DLL that exports CanopyModInit: loaded, and waiting for the start plan.
struct Candidate {
    HMODULE module = nullptr;
    CanopyModInitFn init = nullptr;
    CanopyModShutdownFn shutdown = nullptr;
    ModInfo info;
    // From the manifest. The start log names the mod with them.
    std::string name;
    std::string author;
};

// Copies a manifest into candidate. Returns why it cannot be used, or an empty
// string when it can.
std::string ReadManifest(const CanopyManifest& manifest, Candidate& candidate) {
    if (manifest.size < sizeof(CanopyManifest)) {
        return "is too small for this loader";
    }
    if (!manifest.id) {
        return "has no id";
    }
    if (manifest.dependencyCount > mod_config::kMaxMods) {
        return "has too many dependencies";
    }
    if (manifest.dependencyCount > 0 && !manifest.dependencies) {
        return "has no dependency list";
    }
    candidate.info.hasManifest = true;
    candidate.info.id = manifest.id;
    candidate.info.version = manifest.version;
    candidate.info.minApiVersion = manifest.minApiVersion;
    candidate.name = manifest.name ? manifest.name : manifest.id;
    candidate.author = manifest.author ? manifest.author : "unknown author";
    for (uint32_t index = 0; index < manifest.dependencyCount; ++index) {
        const CanopyDependency& dependency = manifest.dependencies[index];
        if (!dependency.id) {
            return "has a dependency without an id";
        }
        candidate.info.dependencies.push_back({dependency.id, dependency.minVersion, dependency.optional});
    }
    return std::string();
}

// Loads one DLL and reads its manifest when it has one. Returns false, after
// freeing the DLL and logging why, when the DLL cannot start.
bool Discover(const std::string& path, const std::string& file, Candidate& candidate) {
    HMODULE module = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) {
        Log("Mod " + file + " could not be loaded, error " + std::to_string(GetLastError()) + ".");
        return false;
    }
    const auto init = reinterpret_cast<CanopyModInitFn>(GetProcAddress(module, "CanopyModInit"));
    if (!init) {
        Log("Mod " + file + " skipped, it does not export CanopyModInit.");
        FreeLibrary(module);
        return false;
    }
    candidate.module = module;
    candidate.init = init;
    candidate.shutdown = reinterpret_cast<CanopyModShutdownFn>(GetProcAddress(module, "CanopyModShutdown"));
    candidate.info.file = file;
    const FARPROC symbol = GetProcAddress(module, "CanopyManifest");
    if (symbol) {
        const auto manifest = reinterpret_cast<const CanopyManifest*>(reinterpret_cast<uintptr_t>(symbol));
        const std::string problem = ReadManifest(*manifest, candidate);
        if (!problem.empty()) {
            Log("Mod " + file + " skipped, its manifest " + problem + ".");
            FreeLibrary(module);
            return false;
        }
    }
    return true;
}

// Whether a started mod has this id.
bool StartedById(const std::string& id) {
    return std::any_of(g_Loaded.begin(), g_Loaded.end(), [&id](const LoadedMod& mod) {
        return mod.started && SameId(mod.id, id);
    });
}

// Starts one planned mod. Each required dependency must have started first.
// CanopyModInit then runs, and what it registers is kept, or backed out when it
// fails.
void StartOne(Candidate& candidate) {
    for (const Dependency& dependency : candidate.info.dependencies) {
        if (!dependency.optional && !StartedById(dependency.id)) {
            Log("Mod " + candidate.info.file + " skipped, it needs " + dependency.id + ", which did not start.");
            FreeLibrary(candidate.module);
            return;
        }
    }

    const size_t modsBefore = mod_config::ModCount();
    const size_t settingsBefore = mod_config::Count();
    g_Current = g_Loaded.size();
    g_Loaded.push_back({candidate.module, candidate.info.file, candidate.shutdown, false, candidate.info.id,
                        candidate.info.version, false});

    bool started = false;
    DWORD code = 0;
    if (!CallInit(candidate.init, &g_Api, started, code)) {
        Log("Mod " + candidate.info.file + " crashed in CanopyModInit with exception " + Hex(code) + ".");
    }
    if (!started) {
        RemoveOwned(g_Current);
        mod_config::Truncate(modsBefore, settingsBefore);
        g_ModOwners.resize(std::min(g_ModOwners.size(), modsBefore));
        g_Loaded.pop_back();
        FreeLibrary(candidate.module);
        Log("Mod " + candidate.info.file + " did not start and was unloaded.");
        return;
    }
    g_Loaded[g_Current].started = true;
    const size_t hooks = std::count_if(g_Hooks.begin(), g_Hooks.end(), [](const ModHook& hook) {
        return hook.owner == g_Current;
    });
    const std::string counts = std::to_string(mod_config::Count() - settingsBefore) + " settings, " +
                               std::to_string(hooks) + " hooks.";
    if (candidate.info.hasManifest) {
        Log(
            "Mod " + candidate.name + " (" + candidate.info.id + " v" + VersionText(candidate.info.version) + " by " +
            candidate.author + ", " + candidate.info.file + ") started: " + counts
        );
    } else {
        Log("Mod " + candidate.info.file + " started: " + counts);
    }
}

}  // namespace

void LoadAll(bool ticking, bool exitHooked) {
    g_Ticking = ticking;
    g_ExitHooked = exitHooked;
    BuildApi();
    const std::string folder = ModFilePath(kModsFolder);
    if (CreateDirectoryA(folder.c_str(), nullptr)) {
        Log("Created the mods folder.");
    }

    std::vector<std::string> files;
    WIN32_FIND_DATAA found = {};
    HANDLE search = FindFirstFileA((folder + "\\*.dll").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                files.emplace_back(found.cFileName);
            }
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    std::sort(files.begin(), files.end(), [](const std::string& left, const std::string& right) {
        return _stricmp(left.c_str(), right.c_str()) < 0;
    });

    Log("Loading " + std::to_string(files.size()) + (files.size() == 1 ? " mod." : " mods."));

    // Discovery: every DLL is loaded and its manifest read before any mod starts.
    std::vector<Candidate> candidates;
    for (const std::string& file : files) {
        Candidate candidate;
        if (Discover(folder + "\\" + file, file, candidate)) {
            candidates.push_back(candidate);
        }
    }
    std::vector<ModInfo> infos;
    for (const Candidate& candidate : candidates) {
        infos.push_back(candidate.info);
    }
    const Plan plan = Resolve(infos, CANOPY_API_VERSION);
    for (const std::string& message : plan.messages) {
        Log(message);
    }
    std::vector<bool> planned(candidates.size(), false);
    for (const size_t index : plan.order) {
        planned[index] = true;
    }
    for (size_t index = 0; index < candidates.size(); ++index) {
        if (!planned[index]) {
            FreeLibrary(candidates[index].module);
        }
    }

    // Start: in plan order, so each mod starts after the mods it depends on.
    for (const size_t index : plan.order) {
        StartOne(candidates[index]);
    }
}

size_t HookCount() {
    return g_Hooks.size();
}

void Started() {
    g_Started = true;
    hook_chain::Started();
}

void ShutdownAll() {
    for (size_t index = g_Loaded.size(); index-- > 0;) {
        const LoadedMod& mod = g_Loaded[index];
        DWORD code = 0;
        if (mod.shutdown && !CallShutdown(mod.shutdown, code)) {
            Log("Mod " + mod.file + " crashed in CanopyModShutdown with exception " + Hex(code) + ".");
        }
        RemoveOwned(index);
    }
    g_Frames.clear();
    g_Exits.clear();
    g_Events.clear();
    g_Buttons.clear();
}

void Tick() {
    g_GameThread = GetCurrentThreadId();
    CanopyEvent events[game_events::kMaxPerFrame];
    const int eventCount = game_events::Poll(events);
    for (int event = 0; event < eventCount; ++event) {
        // By index: a callback may register another.
        for (size_t index = 0; index < g_Events.size(); ++index) {
            const EventCallback entry = g_Events[index];
            const EventCall call = {entry.callback, events[event], entry.user};
            RunGameCallback(entry.owner, &InvokeEvent, &call, "an event callback");
        }
    }
    bool yes = false;
    if (game_ui::PopupClosed(yes) && g_Answer.active) {
        const PendingAnswer answer = g_Answer;
        g_Answer = {};
        Log("[" + ModName(answer.mod) + "] Yes/No popup answered " + (yes ? "Yes." : "No."));
        const AnswerCall call = {answer.callback, yes, answer.user};
        RunGameCallback(answer.owner, &InvokeAnswer, &call, "a Yes/No answer callback");
    }
    for (size_t index = 0; index < g_Frames.size(); ++index) {
        const Callback frame = g_Frames[index];
        RunGameCallback(frame.owner, &InvokeCallback, &frame, "a frame callback");
    }
}

void PressButton(int setting) {
    const auto button = std::find_if(g_Buttons.begin(), g_Buttons.end(), [setting](const Button& entry) {
        return entry.setting == setting;
    });
    if (button == g_Buttons.end()) {
        return;
    }
    const mod_config::Setting& config = mod_config::At(static_cast<size_t>(setting));
    Log(std::string("Button ") + mod_config::ModAt(static_cast<size_t>(config.mod)).key + "." + config.key + " pressed.");
    const Button entry = *button;
    RunGameCallback(entry.owner, &InvokeButton, &entry, "a button callback");
}

void Exit() {
    if (g_Exited) {
        return;
    }
    g_Exited = true;
    Log("The game is closing.");
    for (size_t index = 0; index < g_Exits.size(); ++index) {
        const Callback exit = g_Exits[index];
        RunGuarded(exit.owner, &InvokeCallback, &exit, "an exit callback");
    }
}

}  // namespace mod_loader
