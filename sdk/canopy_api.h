// Canopy API, version 3.
//
// The one header a mod for Village in the Shade needs. A mod is a 64-bit DLL
// placed in the game's mods folder. The loader (dinput8.dll) loads every DLL
// there at startup and calls its CanopyModInit with this table of functions. A
// mod starts after the mods its CanopyManifest depends on; other mods start in
// file name order. See docs/MODDING.md for a walkthrough and docs/API.md for
// the full contract of every function.
//
// Copyright (c) Zorkats.

#ifndef CANOPY_API_H
#define CANOPY_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The API version this header describes. The loader passes its own version
// in CanopyApi.version; it is never lower than the version that added a
// function, and functions are only ever appended.
#define CANOPY_API_VERSION 3

// Marks a mod's exported entry points.
#ifdef __cplusplus
#define CANOPY_MOD_EXPORT extern "C" __declspec(dllexport)
#else
#define CANOPY_MOD_EXPORT __declspec(dllexport)
#endif

// True when the loader's table includes the named function, for a mod that
// wants to use a function newer than the version it requires.
#define CANOPY_HAS(api, function) \
    ((api)->size >= offsetof(CanopyApi, function) + sizeof((api)->function) && (api)->function != NULL)

// A registered mod, and a registered setting. Negative values mean the
// registration failed; the loader logs why.
typedef int32_t CanopyMod;
typedef int32_t CanopySetting;

#define CANOPY_NO_MOD ((CanopyMod)-1)
#define CANOPY_NO_SETTING ((CanopySetting)-1)

// A framework pad id: the game's own id for every keyboard key, mouse button
// and controller button. 0 means no pad. docs/API.md lists every id.
#define CANOPY_NO_PAD 0

// The device a key binding takes its pad from.
typedef enum CanopyDevice {
    CANOPY_DEVICE_KEYBOARD_MOUSE = 0,
    CANOPY_DEVICE_CONTROLLER = 1,
} CanopyDevice;

// The game's UI languages, in the order of CanopyText's fields.
typedef enum CanopyLanguage {
    CANOPY_LANGUAGE_JAPANESE = 0,
    CANOPY_LANGUAGE_ENGLISH = 1,
    CANOPY_LANGUAGE_FRENCH = 2,
    CANOPY_LANGUAGE_SPANISH = 3,
    CANOPY_LANGUAGE_CHINESE_TRADITIONAL = 4,
    CANOPY_LANGUAGE_KOREAN = 5,
    CANOPY_LANGUAGE_CHINESE_SIMPLIFIED = 6,
} CanopyLanguage;

// One UTF-8 string per game language. Leave a language NULL to show the
// English text in its place. The loader copies the strings when you register,
// so they may be temporary.
typedef struct CanopyText {
    const char* japanese;
    const char* english;
    const char* french;
    const char* spanish;
    const char* chinese_traditional;
    const char* korean;
    const char* chinese_simplified;
} CanopyText;

// Called once per frame on the game thread, after the game's own frame
// update. user is the pointer passed to OnFrame.
typedef void (*CanopyFrameFn)(void* user);

// Called once on the game thread when the game starts closing, while every
// game system still works. user is the pointer passed to OnExit.
typedef void (*CanopyExitFn)(void* user);

// Called on the game thread when the player presses a button row added with
// AddButton. user is the pointer passed to AddButton.
typedef void (*CanopyButtonFn)(void* user);

// Called once on the game thread after a ShowYesNo popup closes. yes is true
// only when the player picked Yes. user is the pointer passed to ShowYesNo.
typedef void (*CanopyAnswerFn)(bool yes, void* user);

// The game events OnEvent reports. New events may be added in later
// versions, so ignore any value you do not know.
typedef enum CanopyEvent {
    // The player gained control in the field: PlayerInField became true.
    CANOPY_EVENT_FIELD_ENTER = 0,
    // The player lost control: PlayerInField became false.
    CANOPY_EVENT_FIELD_LEAVE = 1,
    // A save finished loading, before the field it sets up gains the player.
    CANOPY_EVENT_SAVE_LOADED = 2,
    // The game arrived at the title screen: at startup, and after leaving a
    // game for the title.
    CANOPY_EVENT_TITLE_SCREEN = 3,
} CanopyEvent;

// Called on the game thread for each game event, before the frame
// callbacks of that frame. user is the pointer passed to OnEvent.
typedef void (*CanopyEventFn)(CanopyEvent event, void* user);

typedef struct CanopyApi {
    // sizeof(CanopyApi) as the loader built it, and the loader's API version.
    uint32_t size;
    uint32_t version;

    // ---- Game -------------------------------------------------------------

    // Load address of village.exe. Add a function's offset to it to get the
    // function's address.
    uintptr_t (*GameBase)(void);
    // The current UI language, a CanopyLanguage.
    int32_t (*Language)(void);
    // True while the player walks the field with control: no menu, dialog,
    // event or loading screen owns the input. Game thread only.
    bool (*PlayerInField)(void);
    // True while the game window has keyboard focus.
    bool (*GameFocused)(void);

    // ---- Log --------------------------------------------------------------

    // Appends a line to Canopy.log, prefixed with the mod's key.
    // Any thread.
    void (*Log)(CanopyMod mod, const char* message);

    // ---- Mods and settings ------------------------------------------------

    // Registers a mod: one dropdown on the Mod Settings page, holding the
    // mod's settings, which are saved in mods\<key>.ini. key is 1 to 32
    // letters, digits, '_' or '-', and unique. During CanopyModInit only.
    CanopyMod (*RegisterMod)(const char* key, const CanopyText* name);
    // Adds an on/off setting. key is unique within the mod, same characters
    // as a mod key. help is the description shown at the bottom of the page.
    // During CanopyModInit only, like every Add function.
    CanopySetting (*AddToggle)(
        CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, bool defaultOn
    );
    // Adds a setting that picks one of count choices, 2 to 8.
    CanopySetting (*AddChoice)(
        CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, const CanopyText* choices,
        int32_t count, int32_t defaultIndex
    );
    // Adds a key binding: one pad of device, CANOPY_NO_PAD for none. The player
    // binds it by pressing the key or button they want.
    CanopySetting (*AddBinding)(
        CanopyMod mod, const char* key, const CanopyText* label, const CanopyText* help, CanopyDevice device,
        int32_t defaultPad
    );
    // A setting's current value: 0 or 1 for a toggle, the chosen index for a
    // choice, the pad for a binding. fallback when setting is not valid.
    // Values change while the game runs, so read them when you use them.
    int32_t (*GetSetting)(CanopySetting setting, int32_t fallback);
    // Full path of the mod's settings file, mods\<key>.ini, for a mod that
    // keeps more data there in sections of its own. NULL for a bad mod.
    const char* (*SettingsFile)(CanopyMod mod);

    // ---- Frame ------------------------------------------------------------

    // Calls callback once per frame on the game thread. During CanopyModInit,
    // or later from the game thread. If a callback crashes, the loader logs
    // it and stops calling that DLL's frame, event, button and answer
    // callbacks; the game keeps running.
    bool (*OnFrame)(CanopyMod mod, CanopyFrameFn callback, void* user);

    // ---- Hooks ------------------------------------------------------------

    // Detours target to detour and stores in *original the address to call
    // to run the original: the next mod's detour, or the game's code. Any
    // number of mods may hook the same function; detours run in creation
    // order, the loader's own first. Hooks created during CanopyModInit are
    // enabled together once every mod has loaded; later ones are enabled at
    // once. Fails when this mod already hooks target.
    bool (*CreateHook)(CanopyMod mod, void* target, void* detour, void** original);
    // Removes a hook the mod created. Other mods' detours on the same
    // function keep working.
    bool (*RemoveHook)(CanopyMod mod, void* target);

    // ---- Input ------------------------------------------------------------

    // True on the frame pad goes down, whatever the game did with the press.
    // Once per press: holding it reports nothing more. Every pad is tracked
    // on its own, so pads pressed on the same frame all report. False while
    // the window lacks focus. Game thread only.
    bool (*Pressed)(int32_t pad);
    // True while pad is held down. False while the window lacks focus. Game
    // thread only.
    bool (*Held)(int32_t pad);
    // This frame's mouse wheel: 1 for a notch up, -1 for a notch down, else 0.
    int32_t (*WheelSteps)(void);
    // The pad's English name, "None" for CANOPY_NO_PAD, NULL for an unknown id.
    const char* (*PadName)(int32_t pad);

    // ---- Version 2 --------------------------------------------------------
    // Check for these with CANOPY_HAS before calling them.

    // Calls callback once when the game starts closing, from its menu or its
    // window, so the mod can save its state. Callbacks run in the order they
    // were registered. During CanopyModInit, or later from the game thread.
    // Added in version 2.
    bool (*OnExit)(CanopyMod mod, CanopyExitFn callback, void* user);
    // Adds a button row to the mod's settings. Pressing it calls callback
    // on the game thread, from the frame after the press. GetSetting reads 0
    // for it, and nothing is saved. During CanopyModInit only, like every Add
    // function. Added in version 2.
    CanopySetting (*AddButton)(CanopyMod mod, const char* key, const CanopyText* label, CanopyButtonFn callback, void* user);
    // Shows UTF-8 text in the game's own popup log, the window the game uses
    // for its own notices. Up to 512 bytes; longer text is cut. Only while a
    // game runs, from a save load or a new game's first field until the title
    // screen: the game draws no popup log at the title. Game thread only.
    // Added in version 2.
    bool (*ShowToast)(CanopyMod mod, const char* text);
    // Opens the game's Yes/No popup with UTF-8 text, which may use the
    // game's <br> and <c red>...</c> tags. Opens over the Mod Settings page
    // while it is open, else only while the player is in the field. After it
    // closes, callback gets the answer once. One popup at a time across all
    // mods. Game thread only. Added in version 2.
    bool (*ShowYesNo)(CanopyMod mod, const char* text, CanopyAnswerFn callback, void* user);
    // Calls callback for every game event, a CanopyEvent. During CanopyModInit, or
    // later from the game thread. Added in version 2.
    bool (*OnEvent)(CanopyMod mod, CanopyEventFn callback, void* user);
    // The address of the first match of pattern in village.exe's code, or 0.
    // pattern is hex bytes separated by spaces, with ? or ?? for any byte:
    // "48 8B 05 ?? ?? ?? ?? 48 85 C0". Any thread. Added in version 2.
    uintptr_t (*FindPattern)(const char* pattern);
    // An id of the running village.exe build: its PE header TimeDateStamp.
    // Compare it with the build you reverse engineered. Any thread. Added in
    // version 2.
    uint32_t (*GameBuild)(void);

    // ---- Version 3 --------------------------------------------------------
    // Check for these with CANOPY_HAS before calling them.

    // Provides a table under name, for other mods to get with GetInterface.
    // table is a struct of function pointers or data you own; it must stay
    // valid until the game exits. Names are shared by every mod, so start
    // yours with your mod id, like "ExampleMod.Greeter". Names compare without
    // case. version is the table's version: a consumer asks for a minimum with
    // GetInterface. Fails when the name is taken. During CanopyModInit only, like
    // every Add function. Added in version 3.
    bool (*ProvideInterface)(CanopyMod mod, const char* name, uint32_t version, const void* table);
    // Returns the table another mod provided under name, when its version is
    // at least minVersion, else NULL. A dependency that loaded has started
    // first, so its table is there. Other mods' tables are there once they
    // have started. Each distinct miss is logged once. Call it from CanopyModInit
    // while the loader is still starting mods, or from any thread after that.
    // Added in version 3.
    const void* (*GetInterface)(CanopyMod mod, const char* name, uint32_t minVersion);
    // The version, packed with CANOPY_VERSION, in the manifest of the started mod
    // with this id; 0 when no started mod has that id. Ids compare without
    // case. Call it from CanopyModInit while the loader is still starting mods,
    // or from any thread after that. Added in version 3.
    uint32_t (*ModVersion)(const char* id);
} CanopyApi;

// Exported by every mod. Register the mod, its settings, frame callbacks and
// hooks here, then return true. Returning false unloads the mod and removes
// everything it registered. Runs on a loader thread while the game starts,
// so it must not call game functions.
typedef bool (*CanopyModInitFn)(const CanopyApi* api);

// Optional. Called only when the loader has to back out after CanopyModInit
// succeeded, so the mod can undo anything it wrote to game memory itself.
// Hooks are removed by the loader.
typedef void (*CanopyModShutdownFn)(void);

// Packs a version for CanopyManifest.version and CanopyDependency.minVersion. The
// minor and patch parts take 0 to 255 each, the major part 0 to 65535:
// CANOPY_VERSION(1, 2, 3) is 1.2.3.
#define CANOPY_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 16) | (((uint32_t)(minor)) << 8) | ((uint32_t)(patch)))

// A mod that another mod needs. id is that mod's manifest id. minVersion is the
// least version that works, packed with CANOPY_VERSION, or 0 for any version. An
// optional dependency may be missing or too old: the mod then loads without
// it, and ModVersion tells it what it got.
typedef struct CanopyDependency {
    const char* id;
    uint32_t minVersion;
    bool optional;
} CanopyDependency;

// A mod's manifest, exported as the data symbol CanopyManifest. It gives the mod
// an id other mods can depend on, and the order it loads in. A mod without a
// manifest loads as it did before: no id, no dependencies, file name order.
// Declare it with CANOPY_MOD_MANIFEST, which sets size for you.
struct CanopyManifest {
    // sizeof(struct CanopyManifest) as the mod was built.
    uint32_t size;
    // The mod's id: 1 to 32 letters, digits, '_' or '-', compared without case.
    const char* id;
    // The name shown in the loader's log.
    const char* name;
    const char* author;
    // The mod's version, packed with CANOPY_VERSION.
    uint32_t version;
    // The lowest API version the mod runs on. A loader on an older API skips
    // the mod.
    uint32_t minApiVersion;
    // The mods this one needs; NULL when dependencyCount is 0.
    const CanopyDependency* dependencies;
    uint32_t dependencyCount;
};

// Declares the CanopyManifest symbol at file scope. Pass NULL and 0 when the mod
// needs no other mod. For example:
//   CANOPY_MOD_MANIFEST("MyMod", "My Mod", "Me", CANOPY_VERSION(1, 0, 0), 3, NULL, 0);
#define CANOPY_MOD_MANIFEST(modId, modName, modAuthor, modVersion, minApi, deps, depCount) \
    CANOPY_MOD_EXPORT const struct CanopyManifest CanopyManifest = { \
        sizeof(struct CanopyManifest), modId, modName, modAuthor, modVersion, minApi, deps, depCount }

#ifdef __cplusplus
}
#endif

#endif  // CANOPY_API_H
