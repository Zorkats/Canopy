# Make a mod for Village in the Shade

This guide builds a complete mod, step by step, that shows up on the game's **Mod Settings** page with its own settings, responds to a key you can rebind in game, reacts to the game's events, keeps a number with each save, and talks to the player with a button, a Yes/No popup and toasts. The finished code is [examples/ExampleMod/example_mod.cpp](../examples/ExampleMod/example_mod.cpp), under 250 lines.

You need to know some C or C++. You do not need to know how the game works inside until the last step, which hooks a game function.

## Quick path

1. Copy `sdk/canopy_api.h` into a new folder with a `.cpp` file and a `CMakeLists.txt` ([Step 1](#step-1-set-up-the-project)).
2. Export `CanopyModInit`, register your mod and its settings there ([Steps 2 and 4](#step-2-write-the-entry-point)).
3. Declare a manifest with your id, version and dependencies ([Step 3](#step-3-give-your-mod-a-manifest)).
4. Add a frame callback that reads your settings and acts on them ([Step 5](#step-5-do-something-every-frame)).
5. React to game events, and talk to the player ([Steps 6 and 7](#step-6-react-to-the-game)).
6. Build a 64-bit DLL, drop it in the game's `mods` folder, and check `Canopy.log` ([Step 8](#step-8-try-it-in-game)).

A mod that only replaces game files, such as text or textures, needs no code. See [Replace game files](#replace-game-files).

A mod can also be a Lua script, with no DLL: settings, per-save values, events and the game's own read-only commands, in a sandbox. See [LUA.md](LUA.md).

A mod can add its own items in its `canopy.ini`, built from scratch or copied from vanilla items, that can be eaten, run Lua or place furniture. See [ITEMS.md](ITEMS.md). Its furniture, copies of vanilla furniture, is in [OBJECTS.md](OBJECTS.md).

A mod can keep its own on/off flags in the game's flag set, also declared in its `canopy.ini`. See [FLAGS.md](FLAGS.md).

A mod can add its own villagers, copies of vanilla villagers with their own names and Lua scripts, in its `canopy.ini`. See [VILLAGERS.md](VILLAGERS.md).

A mod can add its own whistles, new melodies with their own Lua scripts, in its `canopy.ini`. See [WHISTLES.md](WHISTLES.md).

A mod can add its own craft and cooking recipes, with mod items or vanilla items, in its `canopy.ini`. See [RECIPES.md](RECIPES.md).

A mod can add its own textures, icons and model texture swaps, from plain PNG files, in its `canopy.ini`. See [ART.md](ART.md).

A mod can change fields of rows the game already has, such as an icon's rectangle, in its `canopy.ini`, without touching the game's files. See [PATCHES.md](PATCHES.md).

A mod can add rows to the game's other tables, such as a tip, a fishing spot or a skill tree node, each a copy of a vanilla row with some fields set, in its `canopy.ini`. See [ROWS.md](ROWS.md).

A mod can add a fish to fishing spots, a catch to traps, an item to forage spots, a wild crop to areas and an item to offering pools, vanilla or its own, in its `canopy.ini`. See [CATCHES.md](CATCHES.md) and [SHOPS.md](SHOPS.md).

A mod can sell an item in a shop, vanilla or its own, and add a blacksmith upgrade, in its `canopy.ini`. See [SHOPS.md](SHOPS.md).

A mod can add a crop, copied from a vanilla crop, with its own name, seasons, growth skins and harvest, planted by a mod seed item. See [CROPS.md](CROPS.md).

While you work on a mod, the **Canopy debug** entry of the Mod Settings page writes what the game's scripts print for debugging to `Canopy.log`. See [DEBUG.md](DEBUG.md).

## How a mod runs

Read this once. Every rule in the API follows from it.

```text
Game starts
  └─ Loader starts on the game's main thread, as the game initializes,
     before it reads any file
       └─ Finds the files mods ship in mods\<Folder>\data\ (see Replace game files)
       └─ Loads every mods\*.dll, in file name order, before any mod starts.
            Each DLL's DllMain and static constructors run here.
            Reads the DLL's manifest, if it has one.
       └─ Plans the start order from the manifests (see Step 3).
       └─ For each mod, in that order:
            CanopyModInit(api)      register your mod, settings, callbacks and hooks
  └─ Registration closes, every hook is enabled at once
Game runs
  └─ Every frame, on the game thread:
       the game's own frame update, then your event callbacks,
       then a popup's answer, then your frame callbacks
```

Four consequences:

| Fact | So you must |
|---|---|
| `CanopyModInit` runs on the game's main thread while the game is still starting, before it reads any file. | Only register things there. Never call game functions from it, and keep it quick: the game waits for it. |
| `DllMain` and static constructors run while the loader loads every DLL, before any `CanopyModInit`. | Keep them free of loader and game calls. See [Step 3](#step-3-give-your-mod-a-manifest). |
| Registration closes after the last mod starts. | Add every setting in `CanopyModInit`. You cannot add one later. |
| Callbacks run on the game thread, once per frame or once per event. | Do your game work there. Read settings there too, since the player can change them at any time. |

## What you need

- Visual Studio 2022 with the **Desktop development with C++** workload. Its **C++ CMake tools for Windows** component, on by default, provides CMake and Ninja.
- The game with the loader installed: `dinput8.dll` next to `village.exe` (see the [README](../README.md#install)).

## Step 1: Set up the project

Create a folder with three files:

```text
ExampleMod\
├── CMakeLists.txt
├── example_mod.cpp
└── sdk\
    └── canopy_api.h        copied from this repository's sdk folder
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(ExampleMod CXX)

# village.exe is 64-bit, so a mod must be too.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with a 64-bit (x64) compiler.")
endif()

# Link the C++ runtime into the DLL, so players need no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_library(ExampleMod SHARED example_mod.cpp)
target_include_directories(ExampleMod PRIVATE sdk)
# Every text you pass the loader is UTF-8.
target_compile_options(ExampleMod PRIVATE /utf-8)
```

Without a manifest, a mod's file name decides its place in the load order, and mods start in file name order. [Step 3](#step-3-give-your-mod-a-manifest) shows how a manifest can make a mod start after the mods it needs.

## Step 2: Write the entry point

The loader calls one exported function, `CanopyModInit`, and passes it the API table. Start with the smallest mod that works:

```cpp
#include "canopy_api.h"

namespace {
const CanopyApi* g_Api = nullptr;
CanopyMod g_Mod = CANOPY_NO_MOD;
}  // namespace

CANOPY_MOD_EXPORT bool CanopyModInit(const CanopyApi* api) {
    // Refuse a loader older than the API this mod was built for.
    if (!api || api->version < CANOPY_API_VERSION) {
        return false;
    }
    g_Api = api;

    const CanopyText name = {nullptr, "Example Mod"};
    g_Mod = api->RegisterMod("ExampleMod", &name);
    if (g_Mod == CANOPY_NO_MOD) {
        return false;
    }
    api->Log(g_Mod, "Started.");
    return true;
}
```

What each part does:

| Code | Why |
|---|---|
| `CANOPY_MOD_EXPORT` | Exports the function with a plain C name, so the loader finds it. |
| `api->version < CANOPY_API_VERSION` | The table only has the functions of the loader's version. See [Versions](API.md#versions). |
| `RegisterMod("ExampleMod", &name)` | Creates your dropdown on the Mod Settings page. The key `ExampleMod` also names your settings file, `mods\ExampleMod.ini`. |
| `return false` | The loader unloads your DLL and removes everything you registered. |
| `return true` | You are in. |

Keep the `api` pointer: the table stays valid for as long as the game runs.

### Text in every language

Every visible text is a `CanopyText`: one string per game language, in this order.

```cpp
const CanopyText name = {"サンプルMOD", "Example Mod", "Mod d'exemple", "Mod de ejemplo"};
//                     Japanese       English        French           Spanish
//                     then Chinese (Traditional), Korean, Chinese (Simplified)
```

Any language you leave out, or set to `nullptr`, shows the English text. `{nullptr, "Example Mod"}` is an English-only text. The loader copies the strings, so they can be temporary.

The game's fonts lack many symbols. Stick to letters, digits and common punctuation.

## Step 3: Give your mod a manifest

A manifest tells the loader your mod's id, name, author and version, and which mods it needs. Declare it at file scope, next to `CanopyModInit`:

```cpp
CANOPY_MOD_MANIFEST("ExampleMod", "Example Mod", "Zorkats", CANOPY_VERSION(1, 0, 0), 4, NULL, 0);
```

| Argument | Meaning |
|---|---|
| `modId` | Your id: 1 to 32 letters, digits, `_` or `-`. Other mods name you by it, compared without case. |
| `modName` | The name in the log. `NULL` shows the id. |
| `modAuthor` | The author in the log. `NULL` shows `unknown author`. |
| `modVersion` | Your version, from `CANOPY_VERSION(major, minor, patch)`. |
| `minApi` | The lowest loader API version your mod runs on. A mod that uses version 4 functions, such as `AddSlider`, says `4`. |
| `deps`, `depCount` | Your dependencies, as an array of `CanopyDependency` and its length. `NULL, 0` when there are none. |

A mod that needs other mods lists them in that array. Each entry has the mod's `id`, the lowest `minVersion` you accept (`0` accepts any version), and whether it is `optional`:

```cpp
static const CanopyDependency kDependencies[] = {
    {"ExampleMod", CANOPY_VERSION(1, 0, 0), false},  // Required: installed at this version, and it starts first.
    {"VillageQoL", 0, true},                       // Optional: this mod loads without it.
};

CANOPY_MOD_MANIFEST("MyMod", "My Mod", "Me", CANOPY_VERSION(1, 0, 0), 3, kDependencies, 2);
```

Load order:

- A mod starts after each of its required dependencies. Where the order allows, it also starts after its optional ones.
- Otherwise mods start in file name order, case-insensitive. A mod without a manifest keeps its place in that order.
- A mod is skipped, and the log says why, when a required dependency is missing, too old or skipped, or when the mod is part of a dependency cycle. [API.md](API.md#manifest) lists every log line.

> **Static code runs before any mod starts.** The loader loads every DLL before it starts any mod. `DllMain` and static constructors run during that load, before any `CanopyModInit` and before the mods you depend on start. Keep them free of loader and game calls.

A loader older than version 3 ignores the manifest. It starts your mod in file name order without checking its dependencies, so check in `CanopyModInit` for anything your mod needs.

### Use another mod's interface

A mod can provide a table of functions to other mods with `ProvideInterface`, and another mod gets it by name with `GetInterface`. ExampleMod provides `ExampleMod.Greeter`. The provider declares the table's struct, and the consumer declares the same struct itself. This mod asks ExampleMod to greet the player when a save loads, and still works without it:

```c
#include "canopy_api.h"

// The table ExampleMod provides, declared the same way ExampleMod declares it.
typedef struct ExampleModGreeter {
    void (*Greet)(const char* who);
} ExampleModGreeter;

// Optional: without ExampleMod, g_Greeter stays NULL.
static const CanopyDependency kDependencies[] = {
    {"ExampleMod", CANOPY_VERSION(1, 0, 0), true},
};

CANOPY_MOD_MANIFEST("MyMod", "My Mod", "Me", CANOPY_VERSION(1, 0, 0), 3, kDependencies, 1);

static CanopyMod g_Mod = CANOPY_NO_MOD;
static const ExampleModGreeter* g_Greeter = NULL;

static void OnEvent(CanopyEvent event, void* user) {
    (void)user;
    if (event == CANOPY_EVENT_SAVE_LOADED && g_Greeter) {
        g_Greeter->Greet("the player");
    }
}

CANOPY_MOD_EXPORT bool CanopyModInit(const CanopyApi* api) {
    if (!api || api->version < CANOPY_API_VERSION) {
        return false;
    }
    const CanopyText name = {NULL, "My Mod"};
    g_Mod = api->RegisterMod("MyMod", &name);
    if (g_Mod == CANOPY_NO_MOD) {
        return false;
    }
    // NULL when no mod provides the table, so check it before you call it.
    g_Greeter = (const ExampleModGreeter*)api->GetInterface(g_Mod, "ExampleMod.Greeter", 1);
    return api->OnEvent(g_Mod, &OnEvent, NULL);
}
```

The consumer snippet is C. It also compiles as C++, so you can paste it into your `.cpp` file.

To provide a table to other mods, declare its struct, give your mod a manifest with the id other mods depend on, and call `ProvideInterface` from `CanopyModInit`:

```c
#include "canopy_api.h"

typedef struct ExampleModGreeter {
    void (*Greet)(const char* who);
} ExampleModGreeter;

static const CanopyApi* g_Api = NULL;
static CanopyMod g_Mod = CANOPY_NO_MOD;

static void Greet(const char* who) {
    g_Api->Log(g_Mod, who ? who : "someone");
}

// The loader keeps this pointer until the game exits, so the table is static.
static const ExampleModGreeter kGreeter = {&Greet};

CANOPY_MOD_MANIFEST("ExampleMod", "Example Mod", "Zorkats", CANOPY_VERSION(1, 0, 0), 3, NULL, 0);

CANOPY_MOD_EXPORT bool CanopyModInit(const CanopyApi* api) {
    if (!api || api->version < CANOPY_API_VERSION || !CANOPY_HAS(api, ProvideInterface)) {
        return false;
    }
    g_Api = api;
    const CanopyText name = {NULL, "Example Mod"};
    g_Mod = api->RegisterMod("ExampleMod", &name);
    if (g_Mod == CANOPY_NO_MOD) {
        return false;
    }
    // Consumers ask for this name and a minimum version, 1 here.
    return api->ProvideInterface(g_Mod, "ExampleMod.Greeter", 1, &kGreeter);
}
```

`ProvideInterface` returns `false` and logs the reason when the name is already taken. [API.md](API.md#provideinterface) lists every case.

## Step 4: Add settings

Add settings right after `RegisterMod`, still in `CanopyModInit`. There are four kinds that store a value, and buttons, which [Step 7](#step-7-talk-to-the-player) adds:

| Kind | Shown as | Value you read |
|---|---|---|
| Toggle | An on/off row | `0` or `1` |
| Choice | A row that cycles through 2 to 8 choices | The chosen index, from `0` |
| Binding | The label, with the bound key's glyph in the value column. The player selects it and presses a key or button. | A pad id, or `CANOPY_NO_PAD` (`0`) for none |
| Slider | A row that steps through whole numbers, from a minimum to a maximum | The number itself |

```cpp
CanopySetting g_FieldMessages = CANOPY_NO_SETTING;
CanopySetting g_MessageStyle = CANOPY_NO_SETTING;
CanopySetting g_PingKey = CANOPY_NO_SETTING;
CanopySetting g_PingButton = CANOPY_NO_SETTING;

// In CanopyModInit, after RegisterMod:
const CanopyText fieldLabel = {nullptr, "Field Messages"};
const CanopyText fieldHelp = {nullptr, "Log a line when the player gains or loses control."};
g_FieldMessages = api->AddToggle(g_Mod, "FieldMessages", &fieldLabel, &fieldHelp, true);

const CanopyText styleLabel = {nullptr, "Message Style"};
const CanopyText styleHelp = {nullptr, "How much each log line says."};
const CanopyText styles[] = {{nullptr, "Short"}, {nullptr, "Detailed"}};
g_MessageStyle = api->AddChoice(g_Mod, "MessageStyle", &styleLabel, &styleHelp, styles, 2, 0);

const CanopyText pingHelp = {nullptr, "Press it to write a line to Canopy.log."};
const CanopyText keyLabel = {nullptr, "Ping Key"};
g_PingKey = api->AddBinding(g_Mod, "PingKey", &keyLabel, &pingHelp, CANOPY_DEVICE_KEYBOARD_MOUSE, 131);
const CanopyText buttonLabel = {nullptr, "Ping Button"};
g_PingButton = api->AddBinding(g_Mod, "PingButton", &buttonLabel, &pingHelp, CANOPY_DEVICE_CONTROLLER, 12);
```

`131` is F9 and `12` is the controller's Y button. Every id is in the [pad id tables](API.md#pad-ids).

A binding takes one device. To offer both a key and a controller button, add two bindings, as above, and check both.

A slider picks a number. ExampleMod's **Ping Value** runs from 0 to 100 in steps of 5, and starts at 10. It is a version 4 function:

```cpp
CanopySetting g_PingValue = CANOPY_NO_SETTING;

// In CanopyModInit, after the other settings:
const CanopyText valueLabel = {nullptr, "Ping Value"};
const CanopyText valueHelp = {nullptr, "How much each ping adds to this save's ping total."};
g_PingValue = api->AddSlider(g_Mod, "PingValue", &valueLabel, &valueHelp, 0, 100, 5, 10);
```

A step of 1 shows the game's own number selector; a larger step makes the row cycle through the numbers, like a choice. A slider holds at most 256 numbers. [AddSlider](API.md#addslider) lists the rules.

The **help** text appears at the bottom of the page while the row is selected.

### Where values are saved

The first start writes every setting's default to `mods\ExampleMod.ini`:

```ini
[ExampleMod]
FieldMessages=1
MessageStyle=0
PingKey=131
PingButton=12
PingValue=10
```

From then on, the file wins over your defaults, and every change on the Mod Settings page is written at once. A slider's saved number outside its range, or between its steps, moves to the nearest number it allows.

> **Setting keys are permanent.** The key, such as `PingKey`, is the name in the file. Rename it in a later version and every player's saved value is lost. Change the label instead; labels are only for display.

## Step 5: Do something every frame

Register a frame callback in `CanopyModInit`:

```cpp
if (!api->OnFrame(g_Mod, &OnFrame, nullptr)) {
    return false;
}
```

The callback runs once per frame on the game thread, after the game's own update:

```cpp
#include <cstdio>

void OnFrame(void*) {
    // Read the bindings every frame: the player can rebind them at any time.
    const int32_t key = g_Api->GetSetting(g_PingKey, CANOPY_NO_PAD);
    const int32_t button = g_Api->GetSetting(g_PingButton, CANOPY_NO_PAD);
    const bool keyPressed = g_Api->Pressed(key);
    if (keyPressed || g_Api->Pressed(button)) {
        char line[128];
        std::snprintf(line, sizeof(line), "Ping from %s.", g_Api->PadName(keyPressed ? key : button));
        g_Api->Log(g_Mod, line);
    }
}
```

The functions it uses:

| Call | Answers |
|---|---|
| `PlayerInField()` | Is the player walking around with control, with no menu, talk, event or loading screen in the way? Check this before any action meant for the field. The finished example adds it to the ping line. |
| `GetSetting(setting, fallback)` | The setting's current value. `fallback` comes back only if the setting failed to register. |
| `Pressed(pad)` | Did this key or button go down this frame? Once per press. `Pressed(CANOPY_NO_PAD)` is always false, so an unbound setting does nothing. |
| `Held(pad)` | Is it down right now? Use it for hold-to-act features. |
| `PadName(pad)` | The English name, such as `F9`, for log lines. |

The finished [example_mod.cpp](../examples/ExampleMod/example_mod.cpp) adds the detailed message style and translated mod names on top of this.

## Step 6: React to the game

A frame callback sees every frame. When you only care about moments, such as a save finishing loading, register an event callback instead:

```cpp
if (!api->OnFrame(g_Mod, &OnFrame, nullptr) || !api->OnEvent(g_Mod, &OnEvent, nullptr)) {
    return false;
}
```

```cpp
void OnEvent(CanopyEvent event, void*) {
    switch (event) {
        case CANOPY_EVENT_FIELD_ENTER:
            if (g_Api->GetSetting(g_FieldMessages, 1) != 0) {
                g_Api->Log(g_Mod, "In the field.");
            }
            break;
        case CANOPY_EVENT_FIELD_LEAVE:
            if (g_Api->GetSetting(g_FieldMessages, 1) != 0) {
                g_Api->Log(g_Mod, "Left the field.");
            }
            break;
        case CANOPY_EVENT_SAVE_LOADED:
            g_Api->ShowToast(g_Mod, "Example Mod: save loaded.");
            break;
        case CANOPY_EVENT_TITLE_SCREEN:
            g_Api->Log(g_Mod, "At the title screen.");
            break;
        default:
            // A newer loader may report events this mod does not know.
            break;
    }
}
```

| Event | Fires when |
|---|---|
| `CANOPY_EVENT_FIELD_ENTER` | The player gains control in the field. |
| `CANOPY_EVENT_FIELD_LEAVE` | A menu, talk, event, popup or loading screen takes control. |
| `CANOPY_EVENT_SAVE_LOADED` | A save finishes loading, by any path. A new game loads none. |
| `CANOPY_EVENT_TITLE_SCREEN` | The title screen appears, at startup or after leaving a game. |

Event callbacks run on the game thread, before that frame's frame callbacks. Keep the `default:` case: later loader versions may add events.

### Keep data with the save

Settings belong to the player. Some data belongs to one save instead: a counter, a choice the player made, a stage of a quest of your own. Keep it with the per-save functions, version 4, and the loader saves and loads it along with the game's save. ExampleMod adds each ping's **Ping Value** to a total kept with the save:

```cpp
// In OnFrame, when the player pings:
const int32_t total = g_Api->GetSaveInt(g_Mod, "PingTotal", 0) + g_Api->GetSetting(g_PingValue, 10);
g_Api->SetSaveInt(g_Mod, "PingTotal", total);

// In OnEvent, on CANOPY_EVENT_SAVE_LOADED. The loaded save's values are in place by then.
if (g_Api->HasSaveValue(g_Mod, "PingTotal")) {
    char line[64];
    std::snprintf(line, sizeof(line), "This save has a ping total of %d.", g_Api->GetSaveInt(g_Mod, "PingTotal", 0));
    g_Api->Log(g_Mod, line);
}
```

| When | Your values |
|---|---|
| The game saves to a slot | Written to `mods\CanopyData\slot<N>.ini`, one section per mod. |
| A save loads | Replaced by the values written with that save, or none. |
| The title screen appears | Dropped. A new game starts from the title, so it starts with none. |
| The game deletes a slot | Its file goes too. |

A change takes effect at once in memory, and reaches the file only when the player saves, like the game's own progress. `SetSaveString` and `GetSaveString` do the same for text. The game's own save data is never touched. [API.md](API.md#per-save-data) has the limits and every log line.

## Step 7: Talk to the player

The loader can show the game's own windows for you:

| Call | Shows |
|---|---|
| `AddButton` | A row on the Mod Settings page that runs your callback. Registered in `CanopyModInit`, like a setting. |
| `ShowYesNo` | The game's Yes/No popup. Your callback gets the answer after it closes. |
| `ShowToast` | A line in the game's popup log, the window the game uses for its own notices. |

The example chains all three: its **Show a Toast** row asks first, and a Yes shows a toast.

```cpp
// Gets the popup's answer once it has closed.
void OnAnswer(bool yes, void*) {
    if (yes) {
        g_Api->ShowToast(g_Mod, "Hello from Example Mod.");
    } else {
        g_Api->Log(g_Mod, "The player declined the toast.");
    }
}

// Runs when the player presses the "Show a Toast" row.
void OnShowToastPressed(void*) {
    g_Api->ShowYesNo(g_Mod, "Show a toast from Example Mod?", &OnAnswer, nullptr);
}

// In CanopyModInit, with the other settings:
const CanopyText toastLabel = {nullptr, "Show a Toast"};
api->AddButton(g_Mod, "ShowToast", &toastLabel, &OnShowToastPressed, nullptr);
```

Where each one works:

| Call | Works |
|---|---|
| `ShowYesNo` | Over the Mod Settings page, or in the field while `PlayerInField()` is true. One popup at a time, across all mods. |
| `ShowToast` | While a game runs, from a save load or a new game's first field until the title screen. The title screen draws no popup log. |

Both are game thread only, and both return `false` and log the reason when they cannot show. Pressing **Show a Toast** on the title screen's Mod Settings page opens the popup, but the toast is refused: no game runs yet. Try it after loading a save.

These are version 2 functions. The example refuses loaders older than version 4 with `api->version < CANOPY_API_VERSION`, since its slider and ping total need version 4. `CANOPY_HAS(api, HasSaveValue)` then checks that the loader's table reaches the last function the example uses.

## Step 8: Try it in game

1. Build a Release DLL. From an **x64 Native Tools Command Prompt for VS 2022**, in your project folder:

   ```bat
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

2. Copy `build\ExampleMod.dll` into the game's `mods` folder.
3. Start the game and open **Options > Mod Settings**. Select `+ Example Mod` to see your six rows.
4. Load a save. A toast says `Example Mod: save loaded.` Walk around and press **F9**.
5. Save the game, return to the title, and load that save again. The ping total comes back with it.
6. Open **Options > Mod Settings** in game, press **Show a Toast** and pick **Yes**.
7. Close the game and open `Canopy.log` in the game folder:

   ```text
   Loading 2 mods.
   [ExampleMod] Running the game build this mod was tested on.
   [ExampleMod] Found the title screen constructor at village.exe+0x44DEC0.
   [ExampleMod] Started.
   Mod Example Mod (ExampleMod v1.0.0 by Zorkats, ExampleMod.dll) started: 6 settings, 0 hooks.
   ...
   Reached the title screen.
   [ExampleMod] At the title screen.
   Per-save data: slot 1 has none.
   A save finished loading.
   [ExampleMod] This save has no ping total yet.
   [ExampleMod] In the field.
   [ExampleMod] Ping from F9.
   [ExampleMod] This save's ping total is now 10.
   Per-save data: wrote slot 1, 1 mod.
   ...
   Per-save data: loaded slot 1, 1 mod.
   A save finished loading.
   [ExampleMod] This save has a ping total of 10.
   ...
   Button ExampleMod.ShowToast pressed.
   [ExampleMod] Yes/No popup opened.
   [ExampleMod] Yes/No popup answered Yes.
   ```

8. Rebind **Ping Key** on the Mod Settings page, press the new key, and check the log again.

The game must be closed to replace a DLL in `mods`. Windows locks loaded DLLs.

## Step 9: Hook a game function

Settings and frame callbacks cover a lot. To change what the game itself does, you replace one of its functions with yours: a **hook**. This needs a disassembler such as IDA or Ghidra to find the function and learn its arguments.

The pattern, using the player's walk update (offset `0x256B10` in the current Steam build) as the target:

```cpp
#include <cstdint>
#include <cstring>

using WalkUpdateFn = char(__fastcall*)(uint8_t* state, void* frame);

constexpr uintptr_t kWalkUpdate = 0x256B10;
// The function's first bytes in the build this offset is from.
constexpr uint8_t kWalkUpdateStart[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
};

WalkUpdateFn g_OriginalWalkUpdate = nullptr;
bool g_LoggedWalk = false;

char __fastcall Detour_WalkUpdate(uint8_t* state, void* frame) {
    if (!g_LoggedWalk) {
        g_LoggedWalk = true;
        g_Api->Log(g_Mod, "The player walked.");
    }
    return g_OriginalWalkUpdate(state, frame);  // Run the game's own code.
}

// In CanopyModInit:
void* target = reinterpret_cast<void*>(api->GameBase() + kWalkUpdate);
if (std::memcmp(target, kWalkUpdateStart, sizeof(kWalkUpdateStart)) != 0) {
    api->Log(g_Mod, "This game version is not supported.");
    return false;
}
if (!api->CreateHook(g_Mod, target, reinterpret_cast<void*>(&Detour_WalkUpdate),
                     reinterpret_cast<void**>(&g_OriginalWalkUpdate))) {
    return false;
}
```

Three rules keep a hook safe:

1. **Check the bytes before you hook.** A game update moves functions. Hooking a stale offset crashes the game; comparing the first bytes turns that into a clean "not supported" in the log.
2. **Match the signature exactly.** The detour's return type, arguments and `__fastcall` must match the game's function, or the stack is corrupted.
3. **Call the original** through the pointer `CreateHook` filled in, unless you mean to replace the function entirely. Other mods may hook the same function after you, and `original` is how their detours run.

Hooks created in `CanopyModInit` are enabled together once every mod has started, so the detour never runs before `CanopyModInit` returns.

### Survive game updates

A fixed offset breaks when the developer ships a new build. Two version 2 functions help:

| Call | Use |
|---|---|
| `GameBuild()` | The running build's id. Log it, and compare it with the build you reverse engineered. The current Steam build, Ver. 1.30, is `0x6ABD400D`. |
| `FindPattern(pattern)` | Finds a function by its bytes, wherever it moved. Wildcards (`??`) cover the bytes that change between builds. |

The example finds the title screen's constructor this way:

```cpp
// Call targets and addresses change between builds, so their four bytes are
// wildcards. The pattern is long because many constructors start alike.
constexpr const char* kTitleCtorPattern =
    "48 89 4C 24 08 53 48 83 EC 40 48 8B D9 E8 ?? ?? ?? ?? 90 48 8D 05 ?? ?? ?? ?? 48 89 03 "
    "48 8D 05 ?? ?? ?? ?? 48 89 43 10 48 8D 05 ?? ?? ?? ?? 48 89 43 20 48 8D 05 ?? ?? ?? ?? "
    "48 89 83 00 02 00 00 33 C0 48 89 83 18 02 00 00";

const uintptr_t address = api->FindPattern(kTitleCtorPattern);
if (address == 0) {
    api->Log(g_Mod, "The title screen constructor was not found.");
}
```

Only the first match comes back, so make sure the pattern matches one place: search for it in your disassembler before you ship. A shorter version of this pattern matches five constructors. Search once in `CanopyModInit` and keep the address; a match is a starting point you still verify, like the bytes check above.

> **Sharing a function.** VillageQoL hooks this walk update too. Both detours run: the loader chains every hook on a function in creation order, so the mod whose file name comes first runs first, and the last `original` leads to the game's code. List the functions you hook in your mod's description, so players can tell which mods touch the same thing.

For a real, larger example, read [src/VillageQoL/walk_speed.cpp](../src/VillageQoL/walk_speed.cpp). It checks two functions' bytes and hooks both, and it only changes anything while `PlayerInField()` is true.

## Replace game files

A mod can replace any file the game reads from its `data` archives, with no code at all. Put the new file in a folder of its own under `mods`, at the same path the file has in the archive:

```text
Village in the Shade\
└── mods\
    └── BigTitles\                  the mod folder, its name shows in Canopy.log
        └── data\
            └── database\
                └── string.dat      replaces data/database/string.dat
```

- **No DLL is needed.** Any folder in `mods` with a `data` folder inside is a file mod. A mod with a DLL can ship files too: name the folder after your mod key, so the log lines match.
- **Use the archive's own path.** Paths match without case, like the game's lookup, and only names with ASCII letters, digits and punctuation can match. A file whose full path, from the drive letter, reaches 260 characters is skipped with a log line.
- **Replace whole files.** A data table must keep the original's columns and row layout; change its values, not its shape.
- **Restart the game** after you add, change or remove a file. The loader finds the files once, as the game starts.
- **Only existing files can be replaced.** A file with a new name is not supported: the game only asks for names it already knows, and it finds its scripts by listing the archive, never your folder.
- `mods\CanopyData` is the loader's own folder and is never read for files.

### When two mods ship the same file

Mod folders are read in name order, without case, and the last one wins. Manifests do not change this order. Every conflict is logged with both names:

```text
Override conflict: BigTitles and RedTitles both ship data/database/string.dat. RedTitles wins, it comes later in load order.
```

To change the winner, rename a folder, or remove the file from the mod you want to lose.

### What the log says about your files

Each mod folder gets one line counting its files by kind, such as `Overrides from BigTitles: 1 file (presentation 1).` Some kinds can do more than change how the game looks, so every such file also gets a warning line. Nothing is refused:

| Kind | Files | Warning line |
|---|---|---|
| core | `data/database/archive.dat`, `framework_*.dat`, `scripttype.dat`, and the archives `data/*_1_00.dat` themselves | Can stop the game from loading. |
| script | Everything in `data/script/` | Can change events, flags and progression. |
| progression | Tables of events, flags, quests, rewards, unlocks, schedules, talk and achievements, such as `event.dat`, `gameflag.dat`, `quest.dat` and `talk.dat` | Can change events, quests, flags and unlocks. |
| balance | Tables of prices, yields, recipes and costs, such as `item.dat`, `storesales.dat`, `crops.dat` and `cooking.dat`, the fairy collision files and the fishing area maps | Can change prices, yields, recipes, costs and hitboxes. |
| map | Everything in `data/map/` and `data/navi/` | Can move collision, event triggers and NPC paths. |
| presentation | Textures, models, animation, shaders, fonts, music and sounds, and display tables such as `string.dat`, `msg.dat` and `icon.dat` | None |
| other | Everything else | None |

A warning means the file deserves a test of everything it touches, and a backup of your saves before you play with it. The full list of files in each kind is in [src/Canopy/file_overrides.cpp](../src/Canopy/file_overrides.cpp).

## Turn a mod off without removing it

To keep a mod installed but stop it from loading, list it in `mods\CanopyData\disabled.txt`. Canopy Studio writes this file when you turn a mod off, and you can edit it by hand too:

```text
# One name per line. Lines starting with # and blank lines are ignored.
BigTitles
VillageQoL.dll
```

- **A folder name** turns off that folder under `mods`: its `canopy.ini` content, its Lua scripts and its `data` files are all skipped.
- **A DLL file name**, such as `VillageQoL.dll`, turns off that DLL, so it is not loaded and its hooks never run.
- Names match without case. The file is UTF-8 and its lines may end in CRLF or LF. A name with no folder or DLL behind it does nothing.
- A missing file turns nothing off, and the loader behaves exactly as before.
- **Restart the game** after you change the list. The loader reads it once, as the game starts.

Each turned-off entry gets one line in `Canopy.log`:

```text
Mod BigTitles is turned off in mods\CanopyData\disabled.txt, so it is not loaded.
```

`canopy validate` also skips turned-off folders and reports a note for each one.

## When it does not work

Every problem ends up in `Canopy.log`. Find your mod's lines:

| Log line | Cause | Fix |
|---|---|---|
| No `Canopy.log` at all, on Linux or a Steam Deck | Proton used its own `dinput8.dll`. | Set the Proton launch option from the README's [Linux and Steam Deck](../README.md#linux-and-steam-deck) section. |
| No line about your DLL at all | The DLL is not in `mods`, or it is not named `*.dll`. | Check the path. |
| `Mod X.dll could not be loaded, error 193.` | A 32-bit DLL. | Build with the x64 toolset. |
| `Mod X.dll could not be loaded, error 126.` | A DLL your mod depends on is missing. | Link the runtime statically (Step 1) and avoid other DLLs. |
| `Mod X.dll skipped, it does not export CanopyModInit.` | The name is mangled or not exported. | Use `CANOPY_MOD_EXPORT` exactly as in Step 2. |
| `Mod X.dll skipped, it needs Y, which is not installed.` or `Mod X.dll skipped, it needs Y, which was skipped.` | A required dependency is not in `mods`, or it was skipped or did not start. The line for that mod says why. | Install it, or make the dependency optional in your manifest. |
| `Mod X.dll skipped, it is part of a dependency cycle.` | Mods need each other through required dependencies. | Make one of those dependencies optional. |
| `Mod ExampleMod not registered, another mod already uses this key.` | Two mods share a key. | Pick a key unique to your mod. |
| `[ExampleMod] Setting X rejected, ...` or `Setting ExampleMod.X not registered, ...` | A bad key, a missing label, a bad choice count, an unbindable default or a slider range that does not fit. | The rest of the line says which. See [AddToggle](API.md#addtoggle) and the rules below it. |
| `Mod X.dll crashed in CanopyModInit with exception 0xC0000005.` | Your init read bad memory, or called a game function too early. | Only register in `CanopyModInit`. |
| `Mod X.dll did not start and was unloaded.` | `CanopyModInit` returned false. | Log the reason before returning false. |
| `Mod X.dll crashed in a frame callback with exception 0xC0000005.` | Your callback read or wrote bad memory. The line names the kind: frame, event, button or Yes/No answer. That DLL's frame, event, button and answer callbacks no longer run. | Log before the risky code to find the line, and check pointers the game hands you. |
| `[ExampleMod] Toast rejected, toasts only show while a game runs, not at the title screen.` | A toast on the title screen. | Show it after a save loads. |
| `[ExampleMod] Yes/No popup rejected, it opens only while the player is in the field or from the Mod Settings page.` | A popup asked for from a menu, an event or the title. | Ask from a button row, or check `PlayerInField()` first. |
| `[ExampleMod] Toast rejected, it can only be shown from the game thread.` | A call from `CanopyModInit` or a thread of your own. | Call it from a callback. |
| `[ExampleMod] Per-save value X rejected, ...` | A bad key, a value too long or with a line break, or a game build the loader cannot save on. | The rest of the line says which. See [Per-save data](API.md#per-save-data). |
| `FindPattern rejected the pattern "...", ...` | A token that is not two hex digits, `?` or `??`. | Separate every byte with a space. |
| `Starting on a loader thread, which races game init: ...` | The loader does not know this game build's startup. Mods still load, but file overrides are off. | Update the loader. |
| `Override conflict: A and B both ship data/.... B wins, it comes later in load order.` | Two mod folders ship the same file. | Keep the file in one mod, or rename a folder. See [Replace game files](#when-two-mods-ship-the-same-file). |
| `Warning: X overrides data/..., script data that ...` | Not an error. That kind of file can change more than looks. | Test everything it touches. |
| `Override skipped in X: ...` | A name outside ASCII, folders nested too deep, or a path too long. | The rest of the line says which. |
| No `Override read:` line for your file | The path does not match an archived file, or the game has not read it yet. | Check every folder and file name against the archive. |

If the game crashes while your mod runs, for example inside a hook detour, the log stops at the last line written before the crash. Log at the start of risky code paths while you develop.

## Before you release

- [ ] The DLL is a 64-bit Release build with the static runtime (`MultiThreaded`).
- [ ] Your mod key and setting keys are final. Renaming them later loses players' settings.
- [ ] Your per-save keys are final too. A renamed key reads as missing from every older save.
- [ ] Your manifest id is unique, and each dependency lists the lowest version you tested.
- [ ] Every game offset you use is checked against the function's bytes before use, or found with `FindPattern`.
- [ ] Your description lists the game version you tested and the functions you hook.
- [ ] Nothing in `CanopyModInit` calls a game function, a toast or a popup.
- [ ] A clean start shows `Mod Your Mod (YourMod v1.0.0 by You, YourMod.dll) started` in the log, and the game runs with your mod as the only one in `mods`.

## Next

- [API.md](API.md): the full contract of every function, the manifest, interfaces, limits and every pad id.
- [LUA.md](LUA.md): Lua mods, which need no DLL.
- [ITEMS.md](ITEMS.md): custom items.
- [OBJECTS.md](OBJECTS.md): mod objects (furniture).
- [FLAGS.md](FLAGS.md): mod flags.
- [VILLAGERS.md](VILLAGERS.md): custom villagers.
- [WHISTLES.md](WHISTLES.md): custom whistles.
- [RECIPES.md](RECIPES.md): custom craft and cooking recipes.
- [ART.md](ART.md): mod textures, icons and skins.
- [ROWS.md](ROWS.md): rows a mod adds to the game's other tables.
- [CATCHES.md](CATCHES.md): fish, trap catches, forage items and wild crops.
- [SHOPS.md](SHOPS.md): sales, blacksmith upgrades and offerings.
- [DEBUG.md](DEBUG.md): script debug output and debug configs.
- [CROPS.md](CROPS.md): mod crops and their seeds.
- [example_mod.cpp](../examples/ExampleMod/example_mod.cpp): the finished mod.
