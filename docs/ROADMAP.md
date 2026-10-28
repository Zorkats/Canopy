# Roadmap

Where Canopy and VillageQoL are going, in order. Each milestone lists what it delivers and how we know it is done.

| Milestone | Delivers | Status |
|---|---|---|
| [v1](#v1-the-framework) | The loader, API v1, VillageQoL as a mod, docs | Done |
| [M0](#m0-repository) | A publishable repository with automated builds and releases | Done, waiting for the first push |
| [M1](#m1-api-v2-robustness) | API v2, part 1: fixes for every v1 limitation | Done, waiting for gameplay checks |
| [M2](#m2-api-v2-ui-events-and-helpers) | API v2, part 2: buttons, popups, game events, signature search | Done, waiting for gameplay checks |
| [API v3](#api-v3-manifests-and-mod-interfaces) | Manifests with dependencies and load order, and interfaces between mods | Done, waiting for gameplay checks |
| [M3](#m3-research-spikes) | Research: sliders, loose file overrides, a Lua bridge. Sliders and per-save data ship as API v4; file overrides and Lua mods need no API change | In progress: sliders, per-save data, loose file overrides, Lua mods, custom items and mod flags done, waiting for gameplay checks |
| [M4](#m4-content-api-api-v4) | API v4: new items, text and mechanics | Depends on M3 |
| [M5](#m5-developer-tools) | Reload a mod without restarting the game | Planned |

## Compatibility rules

These hold for every milestone:

- **The API only grows.** New functions are appended to `CanopyApi`. Nothing is removed, reordered or changed in meaning. A v1 mod runs unchanged on every later loader.
- **The version number goes up once per released API.** API v2 covers M1 and M2, and ships when both are done. API v3 is the manifest and interfaces section after M2. API v4 starts with M3's sliders and per-save data; whether M4's content API joins it or becomes v5 depends on when v4 ships. Each new function's comment names the version that added it, and mods check for it with `CANOPY_HAS`.
- **Behavior fixes need no version check.** M1's fixes change what existing functions do in cases that used to fail; a mod written for v1 only sees fewer failures.
- **Setting keys and file formats stay readable.** No change may lose a player's saved settings.

## v1: The framework

Done. `dinput8.dll` loads every DLL in `mods`, gives it the API v1 table, and shows its settings on the Mod Settings page. VillageQoL is the first mod. See [API.md](API.md) and [MODDING.md](MODDING.md).

## M0: Repository

One repository holds the loader, the SDK header, VillageQoL and the example mod. They change together and share the game's address table (`src/common/game_retail.h`), so splitting them would mean syncing that table between repositories. Mod authors only need `sdk/canopy_api.h` and the docs, which ship as their own download.

- [x] `LICENSE` (MIT).
- [x] `vcpkg.json` manifest, so MinHook installs on its own.
- [x] `CMakePresets.json`, so a build is two commands.
- [x] CMake install rules that lay out the player packages: `dinput8.dll` at the top, mods in `mods\`.
- [x] A GitHub Actions workflow: build every push, and on a version tag publish three zips:
  - `Canopy-<version>.zip` for players: the loader and the README.
  - `VillageQoL-<version>.zip` for players: the VillageQoL mod, installed on its own.
  - `Canopy-SDK-<version>.zip` for modders: the header, the docs and the example mod's source.
- [x] `.gitattributes` for consistent line endings.
- [x] `THIRD_PARTY_NOTICES.txt` with MinHook's license, shipped in the Canopy package.
- [x] README build steps updated to the presets.

Research material (reverse engineering notes, save tools, game data dumps) stays out of the repository.

## M1: API v2, robustness

Removes the limitations listed in [API.md](API.md#known-limitations).

### Hook chaining

**Today:** a second hook on the same game function fails.
**After:** any number of mods hook the same function. Each detour's `original` pointer leads to the next mod's detour, and the last one leads to the game's code.

- [x] Built: `src/Canopy/hook_chain.cpp`. The loader's own hooks go through it too.

| Design point | Decision |
|---|---|
| Mechanism | The loader keeps the one real MinHook hook per function. Its detour, and each mod's `original`, is a 14-byte jump through a pointer the loader owns. Linking or unlinking a mod rewrites one pointer. |
| Order | Hooks run in creation order: the loader's own first, then mods in load order. The first one created sees the call first. |
| Registers | The jumps touch no register, so functions with unusual register contracts still chain safely. |
| Removal | `RemoveHook` unlinks the mod and leaves the others in place. |

Done when: two test mods hook the same function, both detours run in load order, and removing either one leaves the other working. **Met:** two test mods chained behind the loader on the frame update ran in order; removing the middle one or the last one first left the rest running, and a second hook by the same mod was rejected.

### Crash-guarded frame callbacks

**Today:** a crash in a frame callback crashes the game.
**After:** the loader catches it, logs the mod and the exception code, and stops calling that mod's frame callbacks. The game keeps running. This is a best effort: memory the mod damaged before crashing stays damaged.

Done when: a test mod that dereferences null in its callback logs the crash once and the game keeps running. **Met:** logged once, the mod's other frame callback stopped too, and the game ran on. Exit callbacks are guarded the same way.

### Exit callback

**Today:** mods are not told when the game closes.
**After:** `OnExit(mod, callback, user)` runs on the game thread when the game starts closing, so mods can save state.

The game's window procedure sets one quit flag on `WM_CLOSE`, which the close button, Alt+F4 and the Exit Game popup's Yes all send. The main loop then ends and destroys the `CGame` object on the game thread; the loader hooks that destructor.

Done when: the callback's log line appears when the game is closed from its menu and from the window's close button. **Window close met** (a `WM_CLOSE` to the window). Exit Game sends the same message; a check from the menu is pending.

### Reliable key presses

**Today:** `Pressed` sees one new press per device type per frame. Two keys going down on the same frame lose one.
**After:** each pad's press is reported on its own, from its down state, alongside the game's own press detection. A press is reported once per hold.

Done when: pressing two bound keys on the same frame fires both. **Pending a gameplay check:** the code reads every pad's down state each frame, and needs real key presses to verify.

### More settings

**Today:** 32 mods and 128 settings across all mods.
**After:** 64 mods and 256 settings.

Done when: a test mod that registers 200 settings shows them all on the page. **Registration met** (200 of 200, plus 16 from the other mods). Showing them on the page is pending a gameplay check.

## M2: API v2, UI, events and helpers

Most of these wrap code the loader or VillageQoL already reverse engineered.

| Function | What it does | Built from |
|---|---|---|
| `AddButton` | A Mod Settings row that runs your callback when pressed. | The button rows the page already builds |
| `ShowToast` | A message in the game's own popup log. | Show BGM Information's toast |
| `ShowYesNo` | The game's Yes/No popup, with your callback receiving the answer. | Quick save's confirmation |
| `OnEvent` | Callbacks for game events: entering and leaving the field, a save finishing loading, reaching the title screen. | Field state from `PlayerInField`; small reverse engineering for loads and the title |
| `FindPattern` | Searches the game's code for a byte pattern with wildcards, so a mod finds functions by their bytes instead of fixed offsets and survives game updates. | New, no game knowledge needed |
| `GameBuild` | An id of the running `village.exe`, so a mod can tell which build it is on. | The executable's header |

VillageQoL moves onto these where it can: its toast and Yes/No popup go through the API, so the code lives in one place.

Done when: the example mod uses every M2 function, and the docs cover each one. **Met:** the example mod calls all six, and [API.md](API.md) and [MODDING.md](MODDING.md) cover each. VillageQoL's BGM toast and quick save prompt now go through the API. At the title screen, `GameBuild`, `FindPattern`, the title event, every rejection and an event callback crash were checked. Toasts in a game, the Yes/No popup, the button row, and the field and save loaded events are pending a gameplay check.

What the work turned up:

- **The game draws no popup log at the title screen.** A toast there would never show, so `ShowToast` is refused until a game runs: from a save load, or a new game's first field.
- **The Yes/No popup opens in two places only:** over the Mod Settings page, or in the field. Quick save's prompt is now field only, which it already was in practice.
- **Many game functions start with the same bytes.** A `FindPattern` pattern must be long enough to match one place; the guide says how to check.

## API v3: manifests and mod interfaces

A mod can name the mods it needs, and the loader starts them in that order. A mod can also provide a table of functions under a name, and another mod can get that table by name. A mod without a manifest loads as it did before.

| Part | What it adds |
|---|---|
| `CanopyManifest` | A mod's id, name, author, version and lowest API version, with the mods it needs. `CANOPY_MOD_MANIFEST` declares it. |
| Start order | Every DLL loads first. Each mod then starts after the mods it needs, and otherwise in file name order. A mod whose required mod is missing or too old is skipped, and the log says why. |
| `ProvideInterface`, `GetInterface` | A mod provides a table under a name, and another mod gets it with a minimum version. `GetInterface` returns `NULL` when no mod provides it. |
| `ModVersion` | The version of a started mod, found by its manifest id. |

Done when: a mod that needs another mod starts after it, a mod gets another mod's table by name, and a version 2 mod without a manifest still loads.

**Met offline:** the resolver passes 20 of 20 cases, run against the loader's own resolver code. The header compiles as C and as C++ with `/W4 /WX`, and both test DLLs built from it export `CanopyManifest` and `CanopyModInit`. The new v3 examples in the docs compile the same way. The clean build of the loader, VillageQoL and the example mod has no warnings, and ExampleMod.dll exports `CanopyManifest` and `CanopyModInit`.

**Pending a gameplay check:** the start order in a game with several mods installed, a consumer that gets `ExampleMod.Greeter` and one that finds it missing, and a version 2 mod without a manifest in the game log.

What the work turned up:

- **Every DLL loads before any mod starts.** Before version 3, each DLL ran its `DllMain` and static constructors just before its own `CanopyModInit`. Now they all run first, including for the mods a mod depends on.
- **Interface tables are read without a lock.** The loader writes them while it starts mods. The one later write is the teardown after a failed hook install, which disables every hook first. The header first said "any thread" for `GetInterface` and `ModVersion`, which was too broad for the startup window, so the comments now name it.
- **An optional dependency never skips a mod.** Two mods that name each other as optional both load, and the log says which one starts first.
- **The manifest type is a struct tag.** A typedef named `CanopyManifest` cannot share its name with the data symbol `CanopyManifest`, so the type is `struct CanopyManifest`.

## M3: Research spikes

Each spike ends with a written finding: how the game does it, what an API would look like, and a cost estimate. A spike may conclude that something is not worth building.

### Sliders

The game's options have volume sliders. If their setting record can be copied the way toggles and choices are, `AddSlider(min, max, step)` becomes a fourth setting kind.

Done in API v4. A setting record with no choices makes the game show its own number selector, keyed by the number itself:

- [x] `AddSlider(mod, key, label, help, min, max, step, defaultValue)`. A step of 1 uses the game's number selector; a larger step lists its numbers as choices the row cycles through.
- [x] At most 256 numbers. A range that does not fit is refused with a log line that says why.
- [x] Saved numbers are clamped to the range and moved to the nearest step.
- [x] The example mod's **Ping Value** is a slider.
- [ ] Gameplay check: both kinds of slider row on the Mod Settings page.

### Per-save data

Done in API v4. Mods keep values with the save being played, in `mods\CanopyData\slot<N>.ini`, never in the game's own save data:

- [x] Per-mod ints and strings, with has and delete, kept in memory while a game runs.
- [x] Written on a successful save to slot N, read on a successful load of slot N, deleted with the slot, and cleared at the title screen, so a new game starts empty.
- [x] Every finished save, load and delete passes the save utility's acknowledge, so one hook covers them all. The options and key config save is told apart by its smaller buffer and writes no file.
- [x] VillageQoL's quick save and quick load go through the same save code, so they carry the values.
- [x] The file is written to a temporary file and swapped in, so a failed write keeps the last good file.
- [ ] Gameplay check: save, load, quick save, quick load, delete and a new game.

### Loose file overrides

Done, with no API change. A file in `mods\<Folder>\data\` replaces the archived file with the same path, so mods replace textures, text and data tables without code:

- [x] Every content read asks one archive lookup first, and builds a loose file path only when no archive has the file. Two detours, on the lookup and the path builder, send every read of a shipped path to the mod's file, through the game's own loose file code.
- [x] Nothing to match: the archives store every file as is, with no compression or checksum, and retail runs with the file system's memory cache off.
- [x] Two mods that ship the same file: the later folder in name order wins, and the log names both mods and the path.
- [x] Each mod's files are counted by kind in the log. Scripts, progression, balance, map and core files also get a warning line; nothing is refused.
- [x] The loader now starts inside the game's init, on the main thread, so the files are found before the game reads its first one. Every other feature starts there too.
- [ ] Gameplay check: a text, texture, sound and table override, a conflict, and the loader thread fallback.

### Lua bridge

Done, with no API change: a folder with a `lua\main.lua` is a Lua mod, documented in [LUA.md](LUA.md).

- [x] The game runs every script in one Lua 5.4 state. Each Lua mod gets a script player of its own in it, updated once per frame, so the game's commands find the context they expect.
- [x] A per-mod sandbox: safe Lua functions, a read-only `string`, and the 932 commands classed as reading, presentation, temporary world changes or control, each only while in game. Commands that can start vanilla dialogue, events or reward screens, warp the player or switch collision off are left out. No `io`, `os`, `debug`, `load`, `package` or game globals, and nothing is written to them.
- [x] A guard on the 231 commands that write the save or start game scripts: refused for a sandboxed mod, logged once, and a plain call for the game's own scripts.
- [x] `canopy.ini`: name, version, toggle, slider and choice settings, and the `unsafe` opt-in, which is logged and shown as a warning row on the Mod Settings page.
- [x] `canopy.save` on the per-save store DLL mods use, events, a `frame` handler, an optional `main` coroutine and a `require` limited to the mod's own folder.
- [x] Every call is protected, a frame has an instruction budget, and 10 errors stop a mod, never the game. With no Lua mod installed, the script engine is not touched.
- [ ] Gameplay check: the LuaTest and LuaUnsafeTest mods at boot, title, load and on the map.

### Custom items

Done, with no C API change: a mod defines items in its `canopy.ini` and gives them through Lua, documented in [ITEMS.md](ITEMS.md).

- [x] `[item.<key>]` sections: a vanilla base item, a name and help in every language, and optionally another vanilla icon. A folder with only items is still a mod.
- [x] Ids from 1,500,000,000, kept for good in `mods\CanopyData\items.ini` and never given out twice.
- [x] The rows are appended to the game's own item table at boot, in its own format, and every item is checked against the game's own lookup at the title.
- [x] Getting a mod item never touches quests, letters, flags, the encyclopedia, collections or achievements. Vanilla items are unchanged.
- [x] `canopy.items.give`, `count` and `id` for Lua mods.
- [x] Mod items never reach the game's save: they are taken out while it is written and kept in `mods\CanopyData\slot<N>.items.ini`.
- [x] With no item defined, nothing is hooked.
- [ ] Gameplay check: give, save, load, quick save and load, delete, a new game, and loading after removing the mod.

### Overlay save and mod flags

Done, with no C API change: one save mechanism for every kind of mod state, and flags as its second user, documented in [FLAGS.md](FLAGS.md).

- [x] The overlay save core: each kind of mod state takes itself out of the save data before the write and goes back after it, in a fixed order, with its own `mods\CanopyData\slot<N>.<kind>.ini` written only when the save succeeds. A load applies once the files are read and the loaded save data is live, in either order. With nothing registered, nothing is hooked.
- [x] Custom items moved onto it unchanged, except that an item taken out of a list goes back to its old position.
- [x] `[flag.<key>]` sections with an optional `default = on`, each a real game flag from 95000 to 99998, kept for good in `mods\CanopyData\flags.ini`. No game table, script or native constant uses the band.
- [x] `canopy.flags.get`, `set` and `id` for Lua mods, only on the mod's own flags. `set` goes through the game's own flag write.
- [ ] Gameplay check: the FlagTest mod at boot, new game, save, load, quick save and load, delete, and loading after removing the mod.

### Custom villagers

Done, with no C API change: a mod defines villagers in its `canopy.ini`, each with an optional Lua AI script, documented in [VILLAGERS.md](VILLAGERS.md).

- [x] `[villager.<key>]` sections: a vanilla base villager, a name, job and help in every language, a starting place, and optionally a Lua script and routine. A folder with only villagers is still a mod.
- [x] Ids from 1,600,000,000, kept for good in `mods\CanopyData\villagers.ini` and never given out twice.
- [x] The rows are appended to the game's own character and schedule tables at boot, and every villager is checked against the game's own lookup at the title.
- [x] A villager's script is served to the game's script engine from the mod folder, and falls back to its base villager's script when it fails.
- [x] Mod villagers never reach the game's save: they are taken out while it is written, kept in `mods\CanopyData\slot<N>.villagers.ini`, and placed again after a load.
- [x] With no villager defined, nothing is hooked.
- [ ] Gameplay check: a new game, talking, friendship, save, load, continue, and loading after removing the mod.
- [ ] Own talk lines, gifts, birthday, portrait and art.

### Custom whistles

Done, with no C API change: a mod defines whistles in its `canopy.ini`, each with an optional Lua script, documented in [WHISTLES.md](WHISTLES.md).

- [x] `[whistle.<key>]` sections: a vanilla base whistle, a name in every language, a melody, a learn flag, an optional score icon, and optionally a Lua script and entry. A folder with only whistles is still a mod.
- [x] Ids from 1,800,000,000, kept for good in `mods\CanopyData\whistles.ini` and never given out twice.
- [x] The rows are appended to the game's own whistle table at boot, a melody that is already taken is refused, and every whistle is checked against the game's own lookup at the title.
- [x] A whistle's script is served to the game's script engine from the mod folder; one that fails does nothing.
- [ ] Gameplay check: learning a whistle through its flag, playing it, and its script running.
- [ ] Own score and guide art.

### Custom recipes

Done, with no C API change: a mod defines craft and cooking recipes in its `canopy.ini`, documented in [RECIPES.md](RECIPES.md).

- [x] `[craft.<key>]` and `[cooking.<key>]` sections: an output and up to five ingredients, by mod item key or vanilla item id, a count, an unlock flag, and a vanilla base recipe for the station or dish look. A folder with only recipes is still a mod.
- [x] Cooking ingredients can be a group, such as any egg, the way vanilla recipes use them.
- [x] Ids from 1,700,000,000, kept for good in `mods\CanopyData\recipes.ini` and never given out twice.
- [x] The rows are appended to the game's own craft and cooking tables at boot, and every recipe and the items it names are checked at the title.
- [x] Favorite mod recipes never reach the game's save: they are kept in `mods\CanopyData\slot<N>.recipes.ini` and put back after a load.
- [ ] Gameplay check: crafting and cooking with mod items, favorites, save and load.
- [ ] Own dish model and texture.

### Mod art and the table engine

Done, with no C API change: a mod adds textures, icons and model texture swaps in its `canopy.ini`, documented in [ART.md](ART.md).

- [x] One hook for every game table: Canopy detours the table manager's load and points each table's own load at one function, so a module appends rows to any table without a hook of its own.
- [x] Table layouts kept as plain data, with tests, so a new table is a schema and not new code.
- [x] `[texture.<key>]` sections: a PNG, or any image Windows reads, is converted to the game's texture format once and served from `mods\CanopyData\textures`; a ready `.nltx` is used as it is.
- [x] `[icon.<key>]` sections: a part of a mod or vanilla texture, used by mod items and mod whistles through `icon =`.
- [x] `[skin.<key>]` sections: a copy of a vanilla model texture swap set with some textures replaced.
- [x] `[row.<table>.<key>]` sections on the art tables: a raw row, field by field.
- [x] `[row.<table>.<key>]` sections on 99 more tables: a copy of a vanilla row with fields set and lists resized, ids from 2,100,000,000 in `mods\CanopyData\rows.ini`, refused on the tables whose ids the save keeps. See [ROWS.md](ROWS.md).
- [x] `[offering.<key>]`, `[catch.<key>]` and `[forage.<key>]` sections: an item in offering pools, a fish at fishing spots, a catch in traps, an item at forage spots and a wild crop in areas, added to the lists of vanilla rows as they load, after patches, with no id and nothing in the save. See [SHOPS.md](SHOPS.md) and [CATCHES.md](CATCHES.md).
- [x] `[sale.<key>]` and `[upgrade.<key>]` sections: a vanilla or mod item on a shop's list, with stock, seasons, flags and a discount, and a blacksmith upgrade between vanilla tools. Mod items take a `buy` price. Mod sales are kept out of the game's save, in `mods\CanopyData\slot<N>.sales.ini`. See [SHOPS.md](SHOPS.md).
- [x] `[crop.<key>]` sections: a copy of a vanilla crop with its own name, seasons, growth skins, harvest and drop swaps, planted by a mod seed item. Planted mod crops are saved as their base crop and kept in `mods\CanopyData\slot<N>.crops.ini`. See [CROPS.md](CROPS.md).
- [x] Treasure: `treasurebox` rows take a `weight` and an `unlock` pool field, so a `[row.treasurebox.<key>]` adds a vanilla or mod item to what treasure boxes give, and a patch changes or removes a vanilla entry. See [CATCHES.md](CATCHES.md#treasure).
- [x] Debug commands: what scripts print goes to `Canopy.log`, and the six debug configs shipped scripts read are settings in a Canopy debug entry of Mod Settings. See [DEBUG.md](DEBUG.md).
- [x] Ids from 1,900,000,000 (textures), 1,950,000,000 (icons) and 2,000,000,000 (skins), kept for good in `mods\CanopyData` and never given out twice, and every row checked at the title.
- [ ] Gameplay check: a mod icon in the inventory and the whistle menu, and a skin on a model.
- [ ] Gameplay check: a mod fish caught at a spot, a trap catch, a forage pick and an offering that gives a mod item.
- [ ] Gameplay check: a mod sale bought, saved and loaded, a blacksmith upgrade, and the debug settings.
- [ ] Skins on villagers and items, and more tables with schemas.

### Usable items and mod objects

Done, with no C API change: mod items built from scratch with a use, and mod furniture, documented in [ITEMS.md](ITEMS.md) and [OBJECTS.md](OBJECTS.md).

- [x] `base` is optional: an item is built from defaults, with a `category`.
- [x] `use = consume` (stamina and hp), `use = script` (the mod's `canopy.items.on_use` handler, with an optional `consume`) and `use = place` (a mod object).
- [x] `[object.<key>]` sections: a copy of a vanilla furniture object with its own name and optional skin, appended to the gimmick table, with ids from 1,650,000,000 kept in `mods\CanopyData\objects.ini`.
- [x] Placed mod objects are saved as their clones and kept in `mods\CanopyData\slot<N>.objects.ini`, so a save loads without the mod or Canopy.
- [ ] Gameplay check: eat, use and place the Showcase items, save and load a placed lamp, and load that save without Showcase.

### Linux and Steam Deck

The game is Windows only and runs on Linux through Proton. Canopy needs no code change there, only a launch option.

- [x] The README's install steps cover the Proton launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%`.
- [x] `Canopy.log` says when it runs under Wine or Proton, and which version.
- [ ] Gameplay check on Linux or a Steam Deck: the loader starts, Mod Settings opens, and a Lua mod and a file override work.

## M4: Content API, API v4

This is API v4: adding items, text and mechanics, built on M3's findings. A new item, for example, needs a row in the item data table, its name and description in every language, and an icon. Loose file overrides cover the text and icon, and the data table or the Lua bridge covers using the item. The API is designed once M3 says how the game stores these.

## M5: Developer tools

Reloading a mod while the game runs, in a developer mode the player never turns on by accident: the loader loads a copy of the DLL, and on reload removes the mod's hooks and callbacks, frees it, and loads the new copy. Its settings keep their values.

## Not planned

| Idea | Why not |
|---|---|
| Text input fields on the Mod Settings page | The game's options menu has no text entry to build on. |
| Unloading mods in normal play | A mod's hooks may be running on another thread at any moment; unloading safely needs the mod's cooperation, which only developer mode can assume. |
| A 32-bit build | The game is 64-bit only. |
