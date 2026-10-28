# Canopy API reference

Version 4. Everything a mod can call, and the rules for calling it. The API is one C header, [sdk/canopy_api.h](../sdk/canopy_api.h). It works from C and C++.

New to modding the game? Start with the walkthrough in [MODDING.md](MODDING.md). This page is for looking things up. Lua mods, which need no DLL, have their own page: [LUA.md](LUA.md).

## Contents

- [Rules at a glance](#rules-at-a-glance)
- [Entry points](#entry-points): `CanopyModInit`, `CanopyModShutdown`
- [Manifest](#manifest): dependencies and load order
- [Threads and lifetime](#threads-and-lifetime)
- [Versions](#versions)
- [Types](#types)
- Functions:
  [Game](#game) ·
  [Log](#log) ·
  [Mods and settings](#mods-and-settings) ·
  [Frame](#frame) ·
  [Exit](#exit) ·
  [Events](#events) ·
  [Popups](#popups) ·
  [Hooks](#hooks) ·
  [Input](#input) ·
  [Interfaces](#interfaces) ·
  [Per-save data](#per-save-data)
- [Keys and limits](#keys-and-limits)
- [Settings files](#settings-files)
- [Per-save data files](#per-save-data-files)
- [Pad ids](#pad-ids)
- [Known limitations](#known-limitations)

## Rules at a glance

- [ ] Export `CanopyModInit` with `CANOPY_MOD_EXPORT`. Return `true` to stay loaded.
- [ ] Register your mod, settings, callbacks and hooks in `CanopyModInit`, and nothing else. Never call game functions there.
- [ ] Do game work in a frame or event callback, which runs on the game thread.
- [ ] Check functions newer than version 1 with `CANOPY_HAS` before calling them.
- [ ] Declare a [manifest](#manifest) when another mod must depend on yours, or yours on another. A mod without one loads as before.
- [ ] Check what [GetInterface](#getinterface) returns for `NULL` before you use it.
- [ ] Read settings when you use them. The player can change them at any time.
- [ ] Never rename a mod key, a setting key or a per-save key after release.
- [ ] Check a game function's bytes before you hook it, or find it with [FindPattern](#findpattern).
- [ ] Every failure is logged to `Canopy.log` with the reason. Check it first.

## Entry points

### CanopyModInit

```c
CANOPY_MOD_EXPORT bool CanopyModInit(const CanopyApi* api);
```

**Required.** The loader calls it once, after it has loaded every DLL in `mods`, in start order (see [Manifest](#manifest)).

| | |
|---|---|
| `api` | The function table. Valid until the game exits, so you can keep the pointer. |
| Returns | `true` to stay loaded. `false` to be unloaded: the loader removes your hooks, callbacks, mod and settings, then frees your DLL. |
| Thread | The game's main thread, as the game initializes, before it reads any file. The title screen does not exist yet. On a game build the loader does not recognize, a loader thread that runs while the game starts instead. |
| May call | Every API function. Registration functions only work here. |
| Must not | Call game functions, or wait on anything the game thread does. |

If `CanopyModInit` crashes, the loader catches the exception, logs `Mod X.dll crashed in CanopyModInit with exception 0x...`, and unloads the mod as if it returned `false`.

Mods start in file name order (case-insensitive), so `A.dll` starts before `B.dll`, unless a [manifest](#manifest) says `B.dll` must start first.

### CanopyModShutdown

```c
CANOPY_MOD_EXPORT void CanopyModShutdown(void);
```

**Optional.** It is **not** called when the game exits; register an [OnExit](#onexit) callback for that. It is called in one case only: every mod started, then the loader failed to enable the hooks and has to back out. It lets you undo anything you wrote to game memory yourself. The loader has already disabled your hooks and removes them after this call.

Mods are shut down in reverse load order. Afterwards, none of their callbacks runs again.

## Manifest

A manifest says what a mod is and what it needs. The loader reads it from every DLL before it starts any mod, and starts the mods in an order that respects their dependencies. A mod without a manifest still loads, in file name order, with no id and no dependencies.

Declare the manifest at file scope with `CANOPY_MOD_MANIFEST`. A mod with no dependencies passes `NULL` and `0`:

```c
CANOPY_MOD_MANIFEST("MyMod", "My Mod", "Me", CANOPY_VERSION(1, 0, 0), 3, NULL, 0);
```

A mod with dependencies lists them in an array of `CanopyDependency`:

```c
static const CanopyDependency kDependencies[] = {
    {"ExampleMod", CANOPY_VERSION(1, 0, 0), false},
    {"VillageQoL", 0, true},
};

CANOPY_MOD_MANIFEST("MyMod", "My Mod", "Me", CANOPY_VERSION(1, 0, 0), 3, kDependencies, 2);
```

The macro declares the data symbol `CanopyManifest` and sets its `size` field. Because the symbol is named `CanopyManifest`, the struct has no typedef: write `struct CanopyManifest` where you need the type.

| Macro argument | Field | Meaning |
|---|---|---|
| `modId` | `id` | The mod's id: 1 to 32 letters, digits, `_` or `-`. Other mods name the mod by it, compared without case. |
| `modName` | `name` | The name in the loader's log. `NULL` shows the id. |
| `modAuthor` | `author` | The author in the loader's log. `NULL` shows `unknown author`. |
| `modVersion` | `version` | The mod's version, from [CANOPY_VERSION](#types). |
| `minApi` | `minApiVersion` | The lowest API version the mod runs on. A loader with a lower version skips the mod. |
| `deps` | `dependencies` | The mods this one needs, as an array of `CanopyDependency`. `NULL` for none. |
| `depCount` | `dependencyCount` | The number of entries in `deps`, at most 64. |

Each dependency is a `CanopyDependency`:

| Field | Meaning |
|---|---|
| `id` | The id of the mod it needs. |
| `minVersion` | The lowest version that works, packed with `CANOPY_VERSION`. `0` accepts any version. |
| `optional` | `false` for a required dependency. It must be installed at `minVersion` or later, and it must start, or the mod is skipped. `true` for an optional one: the mod loads without it. |

Load order:

- A mod starts after each of its required dependencies. Where the order allows, it also starts after its optional ones.
- Otherwise mods start in file name order, case-insensitive.
- The first mod in file name order to claim an id keeps it. A later mod with the same id is skipped.
- A mod without a manifest keeps its place in file name order.

The loader skips a mod, and logs why, when:

| Log line | Cause | Fix |
|---|---|---|
| `Mod X.dll skipped, its manifest is too small for this loader.` | The manifest's `size` is smaller than the loader's struct. | Build the mod against this header. |
| `Mod X.dll skipped, its manifest has no id.` | `id` is `NULL`. | Pass the id. |
| `Mod X.dll skipped, its manifest id is not valid.` | The id is empty, longer than 32 characters, or has other characters. | Use 1 to 32 letters, digits, `_` or `-`. |
| `Mod X.dll skipped, its manifest has too many dependencies.` | `dependencyCount` is more than 64. | List fewer dependencies. |
| `Mod X.dll skipped, its manifest has no dependency list.` | `dependencyCount` is above 0, and `deps` is `NULL`. | Pass the array. |
| `Mod X.dll skipped, its manifest has a dependency without an id.` | A `CanopyDependency` has a `NULL` `id`. | Fill in every `id`. |
| `Mod X.dll skipped, a dependency id is not valid.` | A dependency's id breaks the id rules. | Use the id rules above. |
| `Mod X.dll skipped, it depends on itself.` | A dependency names the mod's own id. | Remove that dependency. |
| `Mod X.dll skipped, it needs API version 5, and this loader has version 4.` | `minApi` is above the loader's version. | Use a newer loader, or lower `minApi` if the mod really runs on this one. |
| `Mod X.dll skipped, its id Y is already used by Z.dll.` | An earlier mod in file name order has the same id. | Pick an id of your own. |
| `Mod X.dll skipped, it needs Y, which is not installed.` | A required dependency is not in `mods`. | Install it, or make the dependency optional. |
| `Mod X.dll skipped, it needs Y, which was skipped.` | A required dependency was skipped, and its own line says why. | Fix that mod first. |
| `Mod X.dll skipped, it needs Y version 1.2.0 or later, found 1.1.0.` | A required dependency is older than `minVersion`. | Update the dependency, or lower `minVersion`. |
| `Mod X.dll skipped, it needs Y, which did not start.` | The required dependency's `CanopyModInit` returned `false` or crashed. | Read that mod's lines in the log. |
| `Mod X.dll skipped, it is part of a dependency cycle.` | Mods need each other through required dependencies. | Make one of the dependencies optional. |

These lines say a mod loads anyway:

| Log line | Meaning |
|---|---|
| `Mod X.dll loads without optional dependency Y, which is not installed.` | An optional dependency is missing. |
| `Mod X.dll loads, but optional dependency Y is version 1.0.0, and it asks for 2.0.0 or later.` | An optional dependency is too old. |
| `Mod X.dll starts before optional dependency Y, because they depend on each other.` | Two mods name each other as optional. |

A started mod is logged with its manifest, for example `Mod Example Mod (ExampleMod v1.0.0 by Zorkats, ExampleMod.dll) started: 5 settings, 0 hooks.`

> **Code runs earlier.** The loader loads every DLL before it starts any mod. `DllMain` and static constructors run during that load, so they run before any `CanopyModInit`, and before the mods your manifest names have started. Keep them free of loader and game calls.

A loader older than version 3 does not read manifests. It starts the mod in file name order and does not check its dependencies, so check for what you need in `CanopyModInit`.

## Threads and lifetime

| Function | When | Thread |
|---|---|---|
| `RegisterMod`, `AddToggle`, `AddChoice`, `AddSlider`, `AddBinding`, `AddButton` | During `CanopyModInit` only | Loader |
| `ProvideInterface` | During `CanopyModInit` only | Loader |
| `OnFrame`, `OnExit`, `OnEvent` | During `CanopyModInit`, or later | Loader, or the game thread later |
| `CreateHook`, `RemoveHook` | During `CanopyModInit`, or later | Loader, or the game thread later |
| `PlayerInField`, `Pressed`, `Held`, `WheelSteps`, `ShowToast`, `ShowYesNo` | After startup | Game thread only |
| `GameBase`, `GameBuild`, `FindPattern`, `Language`, `GameFocused`, `Log`, `GetSetting`, `SettingsFile`, `PadName` | Any time | Any thread |
| `GetInterface`, `ModVersion` | While the loader is still starting mods, or after startup | Loader, or any thread after startup |
| `SetSaveInt`, `GetSaveInt`, `SetSaveString`, `GetSaveString`, `HasSaveValue`, `DeleteSaveValue` | Any time | Any thread |

"The game thread" means inside a frame, event, button, answer or exit callback, or inside a detour of a function the game runs on its main thread. `ShowToast` and `ShowYesNo` check it, and refuse with a log line anywhere else.

Every callback the loader calls (frame, event, button, answer and exit) runs on the game thread, outside the game's own task updates.

Lifetimes:

- **Strings you pass** (keys, `CanopyText`) are copied. They can be temporary.
- **Strings you get back** from `SettingsFile` and `PadName` stay valid until the game exits. `GetSaveString` copies into your buffer.
- **Your DLL** stays loaded until the game exits, unless `CanopyModInit` returns `false`. There is no unload or reload while the game runs.
- **Tables you provide** with [ProvideInterface](#provideinterface) stay valid until the game exits. The loader removes them when your `CanopyModInit` returns `false`, or when it backs out of a failed start.

## Versions

`api->version` is the loader's API version, and `api->size` is `sizeof(CanopyApi)` as the loader built it. New versions only append functions to the end of `CanopyApi`. Nothing is ever removed or reordered.

Pick one of two approaches:

| Approach | Code |
|---|---|
| Require everything this header has. Simplest. | `if (api->version < CANOPY_API_VERSION) return false;` |
| Run on older loaders, and use a newer function only when present. | `if (CANOPY_HAS(api, SomeNewFunction)) { api->SomeNewFunction(...); }` |

`CANOPY_HAS` checks that the loader's table is large enough to include the function and that the entry is not null.

| Version | Added |
|---|---|
| 1 | Everything up to `PadName`. |
| 2 | [OnExit](#onexit), [AddButton](#addbutton), [ShowToast](#showtoast), [ShowYesNo](#showyesno), [OnEvent](#onevent), [FindPattern](#findpattern) and [GameBuild](#gamebuild). Version 2 loaders also chain hooks, guard callbacks against crashes, report every key press on its own, and raise the limits; mods built for version 1 get these without changes. |
| 3 | [ProvideInterface](#provideinterface), [GetInterface](#getinterface) and [ModVersion](#modversion), and the [manifest](#manifest), with dependencies and a load order. A version 2 loader does not read manifests, and it starts every mod in file name order. |
| 4 | [AddSlider](#addslider), and [per-save data](#per-save-data): `SetSaveInt`, `GetSaveInt`, `SetSaveString`, `GetSaveString`, `HasSaveValue` and `DeleteSaveValue`. |

Functions are only ever appended, so a loader that has a function has every function before it in the table. A mod that uses the whole version 4 table can check for its last entry once: `CANOPY_HAS(api, DeleteSaveValue)`.

## Types

| Type | Meaning |
|---|---|
| `CanopyMod` | A registered mod. `CANOPY_NO_MOD` (-1) when registration failed. |
| `CanopySetting` | A registered setting. `CANOPY_NO_SETTING` (-1) when registration failed. |
| `int32_t` pad | A key, mouse button or controller button. `CANOPY_NO_PAD` (0) is none. See [Pad ids](#pad-ids). |
| `CanopyDevice` | `CANOPY_DEVICE_KEYBOARD_MOUSE` (0) or `CANOPY_DEVICE_CONTROLLER` (1). |
| `CanopyLanguage` | The game's UI languages, `CANOPY_LANGUAGE_JAPANESE` (0) to `CANOPY_LANGUAGE_CHINESE_SIMPLIFIED` (6). |
| `CanopyFrameFn` | `void (*)(void* user)`, a frame callback. |
| `CanopyExitFn` | `void (*)(void* user)`, an exit callback. |
| `CanopyButtonFn` | `void (*)(void* user)`, a button row's callback. Version 2. |
| `CanopyAnswerFn` | `void (*)(bool yes, void* user)`, a Yes/No popup's answer callback. Version 2. |
| `CanopyEvent` | A game event, see [OnEvent](#onevent). Version 2. |
| `CanopyEventFn` | `void (*)(CanopyEvent event, void* user)`, an event callback. Version 2. |
| `CANOPY_VERSION(major, minor, patch)` | Packs a version into one `uint32_t`, for manifests. Minor and patch take 0 to 255, major 0 to 65535. Version 3. |
| `CanopyDependency` | One dependency of a mod: `id`, `minVersion` and `optional`. See [Manifest](#manifest). Version 3. |
| `struct CanopyManifest` | A mod's manifest, the data symbol `CanopyManifest`. Declare it with `CANOPY_MOD_MANIFEST`. See [Manifest](#manifest). Version 3. |

### CanopyText

```c
typedef struct CanopyText {
    const char* japanese;
    const char* english;
    const char* french;
    const char* spanish;
    const char* chinese_traditional;
    const char* korean;
    const char* chinese_simplified;
} CanopyText;
```

One UTF-8 string per language, in the order of `CanopyLanguage`. A `NULL` language shows the English text. If English is `NULL` too, the first non-null string stands in. `{NULL, "Walk Speed"}` is an English-only text.

The game's fonts lack many symbols and do no fallback. Stick to letters, digits and common punctuation.

## Game

### GameBase

```c
uintptr_t GameBase(void);
```

The load address of `village.exe`. Add a function's offset (its RVA) to it to get the function's address. Any thread.

### Language

```c
int32_t Language(void);
```

The current UI language, a `CanopyLanguage`. Returns English for any value the game should never have. Read it when you need it: the player can switch languages, and during `CanopyModInit` the game may not have loaded its settings yet. Any thread.

### PlayerInField

```c
bool PlayerInField(void);
```

True while the player walks the field with control: no menu, talk, event, popup or loading screen owns the input. The check every field hotkey should make before acting. Game thread only.

### GameFocused

```c
bool GameFocused(void);
```

True while the game window is the foreground window. Any thread.

### GameBuild

```c
uint32_t GameBuild(void);
```

**Version 2.** An id of the running `village.exe` build: the `TimeDateStamp` in its PE header, which changes with every build the developer ships. Any thread.

Compare it with the build you reverse engineered, and log or refuse when it differs. The current Steam build, Ver. 1.30, is `0x6ABD400D`.

```c
if (api->GameBuild() != 0x6ABD400D) {
    api->Log(g_Mod, "Untested game build, hooks are off.");
}
```

### FindPattern

```c
uintptr_t FindPattern(const char* pattern);
```

**Version 2.** Searches `village.exe`'s code for a byte pattern and returns the address of the first match, or `0`. Any thread.

| | |
|---|---|
| `pattern` | Hex bytes separated by spaces. `?` or `??` matches any byte. Example: `"48 8B 05 ?? ?? ?? ?? 48 85 C0"`. |
| Returns | The address of the first match in the executable's code sections, or `0` for no match or an invalid pattern. |

An invalid pattern (a token that is not two hex digits or a wildcard, an empty pattern, or one made only of wildcards) returns `0` and logs:

```text
FindPattern rejected the pattern "48 8B 05 ??? 00", every byte must be two hex digits, or ? or ?? for any byte.
```

A pattern finds a function after a game update moves it, where a fixed offset breaks. Turn the bytes that change between builds into wildcards: call targets and RIP-relative addresses, the four bytes after `E8`, `E9`, `48 8D 05` and similar.

> **Make it unique.** Only the first match comes back. Many functions start with the same bytes (every task constructor in the game begins `48 89 4C 24 08 53 48 83 EC 40`), so extend the pattern until your disassembler finds one match only.

It scans the whole executable, which takes milliseconds. Search once, in `CanopyModInit`, and keep the address.

## Log

### Log

```c
void Log(CanopyMod mod, const char* message);
```

Appends one line to `Canopy.log` in the game folder, as `[time] [ModKey] message`. Each line is flushed to disk before `Log` returns, so the log survives a crash. Any thread.

Before `RegisterMod` succeeds, pass `CANOPY_NO_MOD`: during `CanopyModInit` the line is tagged with your DLL's file name.

The log grows across game starts. Keep per-frame logging out of release builds.

## Mods and settings

Every setting shows up as a row on the Mod Settings page, under its mod's dropdown, in the order you add them. Values are saved to the mod's [settings file](#settings-files) the moment the player changes them.

Every registration function works **during `CanopyModInit` only**, and logs the reason when it fails.

### RegisterMod

```c
CanopyMod RegisterMod(const char* key, const CanopyText* name);
```

Registers your mod: one dropdown on the Mod Settings page, shown as `+ Name` when collapsed and `- Name` when expanded.

| | |
|---|---|
| `key` | Your mod's permanent id, and the name of its settings file, `mods\<key>.ini`. See [Keys](#keys-and-limits). Unique across all mods, case-insensitive. |
| `name` | The dropdown's title. `NULL` shows the key. |
| Returns | The mod handle, or `CANOPY_NO_MOD` when the key is invalid or taken, the mod list is full, or registration has closed. |

Call it once. One DLL may register more than one mod, but nearly every mod needs exactly one.

### AddToggle

```c
CanopySetting AddToggle(CanopyMod mod, const char* key, const CanopyText* label,
                     const CanopyText* help, bool defaultOn);
```

Adds an on/off setting. Its value is `0` or `1`.

| | |
|---|---|
| `mod` | Your mod handle. |
| `key` | The setting's permanent id within your mod, case-insensitive. See [Keys](#keys-and-limits). |
| `label` | The row's text. Required. |
| `help` | The description shown at the bottom of the page while the row is selected. `NULL` for none. |
| `defaultOn` | The value before the player changes it. |
| Returns | The setting handle, or `CANOPY_NO_SETTING`. |

It fails when the mod is invalid, the key is invalid or already used in this mod, the label is `NULL` or has no text, the setting list is full, or registration has closed.

### AddChoice

```c
CanopySetting AddChoice(CanopyMod mod, const char* key, const CanopyText* label,
                     const CanopyText* help, const CanopyText* choices,
                     int32_t count, int32_t defaultIndex);
```

Adds a setting that picks one of `count` choices. Its value is the chosen index, `0` to `count - 1`.

| | |
|---|---|
| `choices` | An array of `count` texts, one per choice. |
| `count` | 2 to 8. |
| `defaultIndex` | `0` to `count - 1`. |

Everything else, and every way it fails, is as for `AddToggle`. It also fails when `count` or `defaultIndex` is out of range, or a choice has no text.

> **Append new choices at the end.** The saved value is the index. Inserting a choice in the middle shifts what every player's saved value means.

### AddBinding

```c
CanopySetting AddBinding(CanopyMod mod, const char* key, const CanopyText* label,
                      const CanopyText* help, CanopyDevice device, int32_t defaultPad);
```

Adds a key binding. The row shows the label, with the bound key's glyph centered in the value column, drawn in the style of the controller in use, like the game's own Key Config. The player selects it and presses the key or button they want; **Esc** cancels and **Delete** sets it to none. Its value is a [pad id](#pad-ids), or `CANOPY_NO_PAD`.

| | |
|---|---|
| `device` | `CANOPY_DEVICE_KEYBOARD_MOUSE` accepts any keyboard key and the mouse buttons except Left and Right. `CANOPY_DEVICE_CONTROLLER` accepts controller buttons. |
| `defaultPad` | A pad the device accepts, or `CANOPY_NO_PAD` for unbound. Esc, Delete, Mouse Left and Mouse Right cannot be bound. |

A binding holds one pad of one device. For a key **and** a controller button, add two bindings and check both. It fails as `AddToggle` does, and also when `device` is invalid or `defaultPad` cannot be bound. A saved value the device does not accept falls back to `defaultPad`.

### AddSlider

```c
CanopySetting AddSlider(CanopyMod mod, const char* key, const CanopyText* label,
                     const CanopyText* help, int32_t min, int32_t max,
                     int32_t step, int32_t defaultValue);
```

**Version 4.** Adds a setting that picks a whole number from `min` to `max` in steps of `step`. Its value is the number itself, such as `25`, not its position.

| | |
|---|---|
| `min`, `max` | The lowest and highest numbers. `min` must be below `max`. Negative numbers work. |
| `step` | 1 or more. `max - min` must be a whole number of steps. |
| `defaultValue` | One of the numbers: `min` plus a whole number of steps. |

A step of 1 shows the game's own number selector. A larger step shows the numbers as values the row cycles through, like a choice. Either way the range holds at most 256 numbers: `(max - min) / step + 1`.

Everything else, and every way it fails, is as for `AddToggle`. It also fails when the numbers do not fit, with one of these reasons:

| Log line | Cause |
|---|---|
| `Setting ExampleMod.X not registered, its step must be 1 or more.` | `step` is 0 or negative. |
| `Setting ExampleMod.X not registered, its min must be below its max.` | `min` is `max` or above it. |
| `Setting ExampleMod.X not registered, its max must be min plus a whole number of steps.` | `max - min` does not divide by `step`. |
| `Setting ExampleMod.X not registered, its range holds more than 256 values.` | Use a larger step or a smaller range. |
| `Setting ExampleMod.X not registered, its default is not one of its values.` | `defaultValue` is outside the range, or between two steps. |

A saved number outside the range is clamped to it, then moved to the nearest step, a tie going up.

> **Keep the range when you update.** The saved value is the number. A new range or step moves every player's saved number to the nearest one the new range allows.

### AddButton

```c
CanopySetting AddButton(CanopyMod mod, const char* key, const CanopyText* label,
                     CanopyButtonFn callback, void* user);
```

**Version 2.** Adds a button row. When the player presses it, the loader calls `callback(user)` on the game thread, on the frame after the press, outside the menu's own update. The callback may open a [ShowYesNo](#showyesno) popup over the page.

| | |
|---|---|
| `key` | Names the row in the log, `Button ExampleMod.ShowToast pressed.` Unique among the mod's settings, like a setting key. |
| `label` | The row's text. Required. |
| `callback` | Required. |
| `user` | Passed back to `callback` unchanged. |
| Returns | The setting handle, or `CANOPY_NO_SETTING`. |

A button has no help text: the game shows no description for button rows. It stores nothing. `GetSetting` reads `0` for it, and the settings file gets no line for it. It fails as `AddToggle` does, and also when `callback` is `NULL`.

A crash in the callback is caught like a frame callback's; see [OnFrame](#onframe).

### GetSetting

```c
int32_t GetSetting(CanopySetting setting, int32_t fallback);
```

The setting's current value: `0` or `1` for a toggle, the index for a choice, the number for a slider, the pad for a binding, `0` for a button. Returns `fallback` when `setting` is not a valid handle, for example `CANOPY_NO_SETTING` from a failed registration. Any thread.

Read it each time you use it, not once at startup: the player changes values while the game runs.

### SettingsFile

```c
const char* SettingsFile(CanopyMod mod);
```

The full path of the mod's settings file, `<game folder>\mods\<key>.ini`, or `NULL` for an invalid mod. Use it to keep extra data of your own in the same file, in **sections of your own**. The `[<key>]` section belongs to the loader. Any thread.

## Frame

### OnFrame

```c
bool OnFrame(CanopyMod mod, CanopyFrameFn callback, void* user);
```

Calls `callback(user)` once per frame on the game thread, after the game's own frame update. This is where a mod does its game work.

| | |
|---|---|
| `user` | Passed back to `callback` unchanged. |
| Returns | `false` when the mod is invalid, `callback` is `NULL`, or the loader could not hook the game's frame update on this game build. |

Callbacks run in the order they were added, so mods' callbacks run in load order. A callback can add another callback, which first runs in the same frame. A callback cannot be removed: check your own flag to stop work.

If a callback crashes, the loader catches the exception and logs:

```text
Mod X.dll crashed in a frame callback with exception 0xC0000005.
Mod X.dll gets no more callbacks. The game keeps running.
```

From then on, none of that DLL's frame, event, button or answer callbacks run; other mods are not affected. Its exit callbacks still run, so it can save. This is a best effort: memory your mod damaged before the crash stays damaged, and a crash inside a [hook](#hooks) detour is not caught.

## Exit

### OnExit

```c
bool OnExit(CanopyMod mod, CanopyExitFn callback, void* user);
```

**Version 2.** Calls `callback(user)` once on the game thread when the game starts closing, so the mod can save its state. Every game system still works at that point.

| | |
|---|---|
| `user` | Passed back to `callback` unchanged. |
| Returns | `false` when the mod is invalid, `callback` is `NULL`, or the loader could not hook the game's shutdown on this game build. |

It runs whichever way the game closes: **Exit Game** in the options menu, the window's close button, or Alt+F4. It does not run when the process is killed or crashes, so save anything precious as it changes too.

Exit callbacks run in the order they were added, even for a DLL whose other callbacks crashed. A crash in one is caught and logged, and the next one still runs. Check for it first, since a version 1 loader does not have it:

```c
if (CANOPY_HAS(api, OnExit)) {
    api->OnExit(g_Mod, &SaveState, NULL);
}
```

## Events

### OnEvent

```c
bool OnEvent(CanopyMod mod, CanopyEventFn callback, void* user);
```

**Version 2.** Calls `callback(event, user)` on the game thread for every game event. Events are reported from the frame tick, before that frame's frame callbacks.

| Event | When |
|---|---|
| `CANOPY_EVENT_FIELD_ENTER` (0) | The player gained control in the field: [PlayerInField](#playerinfield) became true. |
| `CANOPY_EVENT_FIELD_LEAVE` (1) | The player lost control: a menu, talk, event, popup or loading screen took over. |
| `CANOPY_EVENT_SAVE_LOADED` (2) | A save finished loading, by any path: the title's Load, a load in game, or a quick save. It comes before the field gains the player. |
| `CANOPY_EVENT_TITLE_SCREEN` (3) | The title screen appeared: at startup, and after leaving a game for the title. Not repeated when the title rebuilds itself after a language change. |

| | |
|---|---|
| `user` | Passed back to `callback` unchanged. |
| Returns | `false` when the mod is invalid, `callback` is `NULL`, or the loader could not hook the game's frame update on this game build. |

A new game loads no save, so it gets no `SAVE_LOADED`; its first `FIELD_ENTER` marks the start. When one frame has several events, they come in table order: title, load, then field.

On a game build where the title and load hooks do not match, the log says `Title and load events disabled.` and only the field events are reported.

New events may be added in later versions. Ignore any value you do not know, with a `default:` that does nothing. A crash in the callback is caught like a frame callback's; see [OnFrame](#onframe).

```c
void OnEvent(CanopyEvent event, void* user) {
    switch (event) {
        case CANOPY_EVENT_SAVE_LOADED:
            g_Api->ShowToast(g_Mod, "Example Mod: save loaded.");
            break;
        default:
            break;
    }
}
```

## Popups

The game's own windows, with its fonts, art and button labels.

### ShowToast

```c
bool ShowToast(CanopyMod mod, const char* text);
```

**Version 2.** Shows `text` in the game's popup log, the window the game uses for its own notices. Toasts queue and stack like the game's.

| | |
|---|---|
| `text` | UTF-8. Up to 512 bytes; longer text is cut at a character boundary. |
| Returns | `true` when the toast was queued. |

It works only while a game runs: from a save load, or a new game's first field, until the title screen. The game draws no popup log on the title screen, so a toast there is refused instead of waiting unseen.

It returns `false`, and logs why, when the mod is invalid, `text` is `NULL`, it is not called on the game thread, no game runs, or the loader could not hook the popup log on this game build. The loader never logs the text itself.

### ShowYesNo

```c
bool ShowYesNo(CanopyMod mod, const char* text, CanopyAnswerFn callback, void* user);
```

**Version 2.** Opens the game's Yes/No popup with `text` and the game's own Yes and No labels. After the popup has closed, the loader calls `callback(yes, user)` once, on the game thread, from a later frame.

| | |
|---|---|
| `text` | UTF-8. `<br>` starts a new line and `<c red>...</c>` colors text red, as in the game's own prompts. |
| `callback` | Required. `yes` is `true` only when the player picked Yes. |
| `user` | Passed back to `callback` unchanged. |
| Returns | `true` when the popup opened. |

Where it opens:

| The game is in | The popup |
|---|---|
| The Mod Settings page | Opens over the page. A [button](#addbutton) callback can ask before acting. |
| The field, with [PlayerInField](#playerinfield) true | Opens over the field. The player loses control while it shows, so `FIELD_LEAVE` and `FIELD_ENTER` events fire around it. |
| Anywhere else | Refused. |

One popup shows at a time, across every mod. It returns `false`, and logs why, when the mod is invalid, `text` or `callback` is `NULL`, it is not called on the game thread, another popup is open, the game is in neither place above, or the popup is not available on this game build.

```c
void OnAnswer(bool yes, void* user) {
    if (yes) {
        g_Api->ShowToast(g_Mod, "Hello from Example Mod.");
    }
}

void OnShowToastPressed(void* user) {
    g_Api->ShowYesNo(g_Mod, "Show a toast from Example Mod?", &OnAnswer, NULL);
}
```

## Hooks

The loader owns one [MinHook](https://github.com/TsudaKageyu/minhook) instance for every mod. Do not link your own copy of MinHook; two instances patch the same code without knowing about each other.

Any number of mods can hook the same function. The loader chains them in creation order, its own hooks first:

```text
game calls target -> first detour -> its original -> next detour -> ... -> the game's code
```

Each detour's `original` leads to the next detour, so every detour must call its `original` to let the mods after it run, unless it means to block them and the game's code. The links between detours change no register, so even functions with unusual register use chain safely.

### CreateHook

```c
bool CreateHook(CanopyMod mod, void* target, void* detour, void** original);
```

Adds `detour` to the end of `target`'s hook chain, and stores in `*original` the pointer that continues the chain: the next mod's detour, or the game's own function. `*original` is filled in before the detour can run.

| | |
|---|---|
| `target` | The game function's address, usually `GameBase() + offset`. |
| `detour` | Your replacement. Its calling convention and signature must match the target's exactly. |
| `original` | Where to store the pointer to the original. Call it from your detour to keep the game's behavior. |
| Returns | `false` when an argument is `NULL`, the mod is invalid, the target cannot be hooked, or **this mod already hooks the target**. Other mods hooking it is fine. |

Timing:

- Created **during `CanopyModInit`**: enabled together with every other mod's hooks after the last mod has started. Your detour never runs before `CanopyModInit` returns.
- Created **later**: enabled at once, before `CreateHook` returns.

Check the target's first bytes against the build you reverse engineered before hooking. A game update moves functions, and hooking a stale offset crashes the game. [MODDING.md](MODDING.md#step-9-hook-a-game-function) shows the pattern.

### RemoveHook

```c
bool RemoveHook(CanopyMod mod, void* target);
```

Removes a hook this mod created on `target`. Returns `false` when the mod has no hook there. Other mods' detours on the same function keep running, in the same order.

Your `original` pointer stays callable, so a call already inside your detour finishes normally. Still, remove a hook from a place where your detour cannot be starting, such as a frame callback for a function the game calls on its main thread.

## Input

Input functions use framework [pad ids](#pad-ids), which cover the keyboard, the mouse and the controller with one number. They read the devices directly, so they work even when a game menu consumed the press.

All four input functions return nothing useful if the loader logged `Input disabled, hotkeys will not respond.`

### Pressed

```c
bool Pressed(int32_t pad);
```

True on the frame `pad` goes down. `Pressed(CANOPY_NO_PAD)` is always false, so an unbound binding never fires. Always false while the game window lacks focus. Game thread only.

Each press is reported once: holding the key reports nothing more until it is released and pressed again. Every pad is tracked on its own, so keys that go down on the same frame are all reported. A key already down when the window gains focus is not reported.

### Held

```c
bool Held(int32_t pad);
```

True while `pad` is down. Always false while the game window lacks focus, and for `CANOPY_NO_PAD`. Use it for hold-to-act features, not to detect a single press. Game thread only.

### WheelSteps

```c
int32_t WheelSteps(void);
```

This frame's mouse wheel movement: `1` for a notch up, `-1` for a notch down, otherwise `0`, which includes while the game window lacks focus. Every mod sees the same value, so two mods that both use the wheel react to the same notch. Game thread only.

### PadName

```c
const char* PadName(int32_t pad);
```

The pad's English name, such as `F9`, `Mouse Middle` or `RT`. `"None"` for `CANOPY_NO_PAD`, and `NULL` for an id that is not a pad. Any thread.

## Interfaces

A mod can provide a table of functions or data to other mods. The provider names it and gives it a version; a consumer asks for it by name and a minimum version. The table is a plain struct that the provider declares, so the loader checks only the name and the version. The consumer declares the same struct itself.

### ProvideInterface

```c
bool ProvideInterface(CanopyMod mod, const char* name, uint32_t version, const void* table);
```

**Version 3.** Provides `table` under `name`, for other mods to get with [GetInterface](#getinterface).

| | |
|---|---|
| `name` | Shared by every mod, compared without case. Start it with your mod's id, such as `ExampleMod.Greeter`. |
| `version` | The table's version. Consumers ask for a minimum, so raise it when a consumer must know about a change. |
| `table` | Your struct or data. It must stay valid until the game exits. |
| Returns | `true` when the table is provided. |

It fails, and logs why, when the mod is invalid, `name` is empty, `table` is `NULL`, `name` is already provided, or the call comes after startup. Call it during `CanopyModInit` only.

### GetInterface

```c
const void* GetInterface(CanopyMod mod, const char* name, uint32_t minVersion);
```

**Version 3.** Returns the table another mod provided under `name`, when that table's version is at least `minVersion`. Otherwise it returns `NULL`. Cast the result to the struct the provider declares.

| | |
|---|---|
| `name` | The name the provider used. Compared without case. |
| `minVersion` | The lowest version you accept. |
| Returns | The table, or `NULL` when no mod provides `name`, or when the provided table is older than `minVersion`. |

Call it from `CanopyModInit` while the loader is still starting mods, or from any thread after that. A provider that you list as a required dependency has started first, so its table is there. For any other provider, check the result: it is `NULL` until the provider has started.

Each distinct miss is logged once, so a lookup that runs often does not flood the log:

```text
[MyMod] Interface ExampleMod.Greeter is not provided.
[MyMod] Interface ExampleMod.Greeter is version 1, and it needs 2 or later.
```

A consumer that uses the table from [example_mod_api.h](../examples/ExampleMod/example_mod_api.h) looks like this:

```c
const ExampleModGreeter* greeter =
    (const ExampleModGreeter*)api->GetInterface(g_Mod, "ExampleMod.Greeter", 1);
if (greeter) {
    greeter->Greet("the player");
}
```

### ModVersion

```c
uint32_t ModVersion(const char* id);
```

**Version 3.** Returns the version, packed with `CANOPY_VERSION`, of the started mod whose manifest id is `id`. Ids compare without case. It returns `0` when no started mod has that id, or when that mod's version is `0.0.0`.

Packed versions compare in version order, so a plain comparison checks a minimum:

```c
if (api->ModVersion("VillageQoL") >= CANOPY_VERSION(1, 0, 0)) {
    api->Log(g_Mod, "VillageQoL is installed and new enough.");
}
```

Call it from `CanopyModInit` while the loader is still starting mods, or from any thread after that. Use it for an optional dependency, to see which version of it loaded.

## Per-save data

**Version 4.** Values a mod keeps with the save being played: a counter, a choice the player made, a stage of a quest of its own. The game's save data never holds them. The loader keeps them in memory and writes them to a [file of its own](#per-save-data-files) for each save slot, one section per mod.

| When | What happens to the values |
|---|---|
| The game saves to slot N, and the save succeeds | Every mod's values are written to `mods\CanopyData\slot<N>.ini`. With no values at all, the slot's old file is deleted. |
| A save in slot N finishes loading | The values become the ones in that file, or none when there is no file. This happens before `CANOPY_EVENT_SAVE_LOADED`. |
| The game deletes slot N | Its file is deleted. The values in memory stay. |
| The title screen appears, at startup or after leaving a game | Every value is dropped, so a new game starts with none. |

A failed save, load or delete changes nothing. The game's options and key config go through the same save code with a smaller buffer, and never touch these files. VillageQoL's quick save uses the game's own save code too: a quick save writes its slot's file, and a quick load reads it before VillageQoL deletes that save, and the file with it.

A change takes effect in memory at once, and reaches the file only when the player saves, like the game's own progress. Every function here works on any thread, at any time; values only mean something while a game runs.

A per-save key follows the [setting key rules](#keys-and-limits), case-insensitive, and each mod has its own. Ints and strings share the keys: an int is stored as its digits.

### SetSaveInt

```c
bool SetSaveInt(CanopyMod mod, const char* key, int32_t value);
```

Stores `value` under `key`, replacing what was there. Returns `false` when it cannot, and logs the reason the first time; see the table below.

### GetSaveInt

```c
int32_t GetSaveInt(CanopyMod mod, const char* key, int32_t fallback);
```

The number stored under `key`. Returns `fallback` when there is none, or when the value is not a whole number that fits an `int32_t`.

### SetSaveString

```c
bool SetSaveString(CanopyMod mod, const char* key, const char* value);
```

Stores `value`, one line of UTF-8 text, under `key`. Up to 1024 bytes, with no line breaks or other control characters. An empty string is a value. Returns `false` when it cannot, and logs the reason the first time.

### GetSaveString

```c
bool GetSaveString(CanopyMod mod, const char* key, char* buffer, uint32_t size);
```

Copies the text stored under `key` into `buffer`, which holds `size` bytes. A longer value is cut to fit, at a UTF-8 character boundary, and the copy is always terminated: a buffer of 1025 bytes holds any value. Returns `false`, and writes `""`, when there is none. An int reads as its digits.

### HasSaveValue

```c
bool HasSaveValue(CanopyMod mod, const char* key);
```

`true` when `key` has a value. Use it to tell a save that never had the value from one where it is `0`.

### DeleteSaveValue

```c
bool DeleteSaveValue(CanopyMod mod, const char* key);
```

Removes the value under `key`. Returns `false` when there was none. The next save writes the file without it.

### Per-save log lines

A refused value is logged once for each key and reason:

| Log line | Cause |
|---|---|
| `[ExampleMod] Per-save value X rejected, its key must be 1 to 32 letters, digits, '_' or '-'.` | A bad key. |
| `[ExampleMod] Per-save value X rejected, it needs a value.` | `SetSaveString` got `NULL`. |
| `[ExampleMod] Per-save value X rejected, its value is longer than 1024 bytes.` | Too long. |
| `[ExampleMod] Per-save value X rejected, its value has a line break or another control character.` | Keep it to one line. |
| `[ExampleMod] Per-save value X rejected, the mod already keeps 1024 values.` | Delete some first. |
| `[ExampleMod] Per-save value X rejected, per-save data is not available on this game build.` | A game update changed the save code, or the build is not one Canopy has checked; the log said `Per-save data disabled.` at startup. |
| `[unknown mod] Per-save value X rejected, it needs a registered mod.` | `mod` is not a valid handle. |

The loader logs what it does with the files:

| Log line | Meaning |
|---|---|
| `Per-save data: wrote slot 3, 2 mods.` | A save wrote the file. |
| `Per-save data: slot 3 saved with no values, its old file was deleted.` | No mod kept a value, so the slot's old file went. |
| `Per-save data: slot 3 could not be written, error 5.` | A Windows error. The old file stays; the game's save is fine. |
| `Per-save data: loaded slot 3, 2 mods.` | A load read the file. |
| `Per-save data: slot 3 has none.` | The slot has no file, so the values start empty. |
| `Per-save data: slot 3 could not be read, starting with none.` | The file is locked or too large. |
| `Per-save data: deleted slot 3.` | The game deleted the slot. |

## Keys and limits

A **key** is a mod key, a setting key or a per-save key. Keys name files and INI entries, so they keep to safe characters:

| Rule | Value |
|---|---|
| Length | 1 to 32 characters |
| Characters | `A-Z`, `a-z`, `0-9`, `_` and `-` |
| Mod keys | Unique across all mods, case-insensitive |
| Setting keys | Unique within the mod, case-insensitive |

Limits shared by every installed mod:

| Limit | Value |
|---|---|
| Mods | 64 |
| Settings, all mods together, buttons included | 256 |
| Choices per choice setting | 2 to 8 |
| Numbers per slider | 2 to 256 |
| Per-save values per mod | 1024 |
| Per-save value length | 1024 bytes |

Keep binding labels short, since each language's label has to fit on one row beside the glyph.

## Settings files

Each mod's settings live in `mods\<key>.ini`, in a section named after the mod key:

```ini
[ExampleMod]
FieldMessages=1
MessageStyle=0
PingKey=131
PingButton=12
PingValue=10
```

| When | What happens |
|---|---|
| A setting is registered and the file has no value for it | The default is written to the file, so the file always lists every setting. |
| A setting is registered and the file has a value | The file's value is used. A value out of range (a missing choice, an unbindable pad) falls back to the default. A slider's number is clamped to its range instead, then moved to the nearest step. |
| The player changes a setting | The new value is written at once. |
| A mod stops registering a setting | Its old line stays in the file, unused. |
| A button is registered | Nothing: buttons store no value. |

Players may edit the file while the game is closed.

## Per-save data files

The loader keeps each save slot's [per-save data](#per-save-data) in `mods\CanopyData\slot<N>.ini`, next to the loader's other files and never in the game's save folder, with one section per mod key:

```ini
; Canopy per-save mod data for save slot 3.

[ExampleMod]
PingTotal=40
```

Each save to the slot rewrites the whole file. The loader writes a temporary file and then swaps it in, so a failed write leaves the last good file. Sections of mods that are no longer installed are kept, and written back by the next save of a game that loaded them.

The files follow the slot, not the save: copying or moving a save outside the game leaves its values behind. Deleting the folder drops every mod's per-save data and leaves the game's saves unchanged.

## Pad ids

A pad id is one number for any key or button, the same one the game's own key config uses. Pads marked `*` cannot be bound with `AddBinding`. Controller buttons have Xbox names.

### Controller pads

Ids 5 to 8 are the left stick's directions. They have no name and cannot be bound.

| Pad | Name | Pad | Name | Pad | Name | Pad | Name |
|---:|---|---:|---|---:|---|---:|---|
| 1 | D-Pad Up | 2 | D-Pad Down | 3 | D-Pad Left | 4 | D-Pad Right |
| 9 | A | 10 | B | 11 | X | 12 | Y |
| 13 | LB | 14 | RB | 15 | LT | 16 | RT |
| 17 | LS | 18 | RS | 19 | Back | 20 | Start |

### Keyboard pads

Pads 65 to 147 follow the DirectInput key order (pad = DirectInput code + 64). The names are those of a US layout.

| Pad | Name | Pad | Name | Pad | Name | Pad | Name |
|---:|---|---:|---|---:|---|---:|---|
| 65 | Esc * | 66 | 1 | 67 | 2 | 68 | 3 |
| 69 | 4 | 70 | 5 | 71 | 6 | 72 | 7 |
| 73 | 8 | 74 | 9 | 75 | 0 | 76 | - |
| 77 | = | 78 | Backspace | 79 | Tab | 80 | Q |
| 81 | W | 82 | E | 83 | R | 84 | T |
| 85 | Y | 86 | U | 87 | I | 88 | O |
| 89 | P | 90 | [ | 91 | ] | 92 | Enter |
| 93 | Left Ctrl | 94 | A | 95 | S | 96 | D |
| 97 | F | 98 | G | 99 | H | 100 | J |
| 101 | K | 102 | L | 103 | ; | 104 | ' |
| 105 | <code>`</code> | 106 | Left Shift | 107 | \\ | 108 | Z |
| 109 | X | 110 | C | 111 | V | 112 | B |
| 113 | N | 114 | M | 115 | , | 116 | . |
| 117 | / | 118 | Right Shift | 119 | Num * | 120 | Left Alt |
| 121 | Space | 122 | Caps Lock | 123 | F1 | 124 | F2 |
| 125 | F3 | 126 | F4 | 127 | F5 | 128 | F6 |
| 129 | F7 | 130 | F8 | 131 | F9 | 132 | F10 |
| 133 | Num Lock | 134 | Scroll Lock | 135 | Num 7 | 136 | Num 8 |
| 137 | Num 9 | 138 | Num - | 139 | Num 4 | 140 | Num 5 |
| 141 | Num 6 | 142 | Num + | 143 | Num 1 | 144 | Num 2 |
| 145 | Num 3 | 146 | Num 0 | 147 | Num . | 148 | F11 |
| 149 | F12 | 150 | F13 | 151 | F14 | 152 | F15 |
| 153 | Kana | 154 | Convert | 155 | NoConvert | 156 | Yen |
| 157 | Num = | 158 | ^ | 159 | @ | 160 | : |
| 161 | _ | 162 | Kanji | 163 | Stop | 164 | AX |
| 165 | Unlabeled | 166 | Num Enter | 167 | Right Ctrl | 168 | Num , |
| 169 | Num / | 170 | SysRq | 171 | Right Alt | 172 | Pause |
| 173 | Home | 174 | Up | 175 | Page Up | 176 | Left |
| 177 | Right | 178 | End | 179 | Down | 180 | Page Down |
| 181 | Insert | 182 | Delete * | 183 | Left Win | 184 | Right Win |
| 185 | Menu | 186 | Power | 187 | Sleep |  |  |

### Mouse pads

| Pad | Name | Pad | Name | Pad | Name | Pad | Name |
|---:|---|---:|---|---:|---|---:|---|
| 191 | Mouse Left * | 192 | Mouse Right * | 193 | Mouse Middle | 194 | Mouse Back |
| 195 | Mouse Forward |  |  |  |  |  |  |

The wheel is not a pad. Read it with [WheelSteps](#wheelsteps).

## Known limitations

| Limitation | What to do |
|---|---|
| Hook detours are not crash guarded; only the loader's callbacks are. | Keep detours small, and guard risky memory reads yourself, for example with `__try` / `__except` in a function that holds no C++ objects. |
| A mod whose detour skips its `original` also skips every detour after it. | Call `original` unless blocking the game's code is the point. List the functions you hook in your mod's description. |
| No unload or reload while the game runs. | Restart the game to test a new build. |
| Settings are toggles, choices, sliders, bindings and buttons only. There are no text fields. | Use a choice with a few preset values. |
| Per-save values set at the title screen carry into a new game, and values set in `CanopyModInit` are dropped when the title appears. | Set them while a game runs: after `CANOPY_EVENT_SAVE_LOADED`, or in the field. |
| Per-save data follows the save slot. A save copied outside the game, for example to another computer, arrives without its values. | Copy `mods\CanopyData` along with the saves. |
| Toasts do not show on the title screen. | Show them once a game runs: after `CANOPY_EVENT_SAVE_LOADED` or the first `CANOPY_EVENT_FIELD_ENTER`. |
| A Yes/No popup opens only over the field or the Mod Settings page, one at a time. | Ask from a button row, or while the player walks. Try again later when another popup is open. |
| The **Return to Title** row in the options menu belongs to the loader. | Nothing; it is always there in game. |
| An interface has no type information. The consumer casts it to the struct the provider declares, and a mismatch crashes the game. | Give each table its own name, and raise its version when its layout changes. |
| A table cannot be provided after startup, and cannot be removed while the game runs. | Provide every table in `CanopyModInit`, and check what `GetInterface` returns before you use it. |
