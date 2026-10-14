# Roadmap

Where Canopy and VillageQoL are going, in order. Each milestone lists what it delivers and how we know it is done.

| Milestone | Delivers | Status |
|---|---|---|
| [v1](#v1-the-framework) | The loader, API v1, VillageQoL as a mod, docs | Done |
| [M0](#m0-repository) | A publishable repository with automated builds and releases | Done, waiting for the first push |
| [M1](#m1-api-v2-robustness) | API v2, part 1: fixes for every v1 limitation | Done, waiting for gameplay checks |
| [M2](#m2-api-v2-ui-events-and-helpers) | API v2, part 2: buttons, popups, game events, signature search | Done, waiting for gameplay checks |
| [API v3](#api-v3-manifests-and-mod-interfaces) | Manifests with dependencies and load order, and interfaces between mods | Done, waiting for gameplay checks |
| [M3](#m3-research-spikes) | Research: sliders, loose file overrides, a Lua bridge | Planned |
| [M4](#m4-content-api-api-v4) | API v4: new items, text and mechanics | Depends on M3 |
| [M5](#m5-developer-tools) | Reload a mod without restarting the game | Planned |

## Compatibility rules

These hold for every milestone:

- **The API only grows.** New functions are appended to `CanopyApi`. Nothing is removed, reordered or changed in meaning. A v1 mod runs unchanged on every later loader.
- **The version number goes up once per released API.** API v2 covers M1 and M2, and ships when both are done. API v3 is the manifest and interfaces section after M2, and API v4 is M4. Each new function's comment names the version that added it, and mods check for it with `CANOPY_HAS`.
- **Behavior fixes need no version check.** M1's fixes change what existing functions do in cases that used to fail; a mod written for v1 only sees fewer failures.
- **Setting keys and file formats stay readable.** No change may lose a player's saved settings.

## v1: The framework

Done. `dinput8.dll` loads every DLL in `mods`, gives it the API v1 table, and shows its settings on the Mod Settings page. VillageQoL is the first mod. See [API.md](API.md) and [MODDING.md](MODDING.md).

## M0: Repository

One repository holds the loader, the SDK header, VillageQoL and the example mod. They change together and share the game's address table (`src/common/game_retail.h`), so splitting them would mean syncing that table between repositories. Mod authors only need `sdk/canopy_api.h` and the docs, which ship as their own download.

- [x] `LICENSE` (MIT).
- [x] `vcpkg.json` manifest, so MinHook installs on its own.
- [x] `CMakePresets.json`, so a build is two commands.
- [x] CMake install rules that lay out the player package: `dinput8.dll` at the top, mods in `mods\`.
- [x] A GitHub Actions workflow: build every push, and on a version tag publish two zips:
  - `VillageQoL-<version>.zip` for players: the loader, VillageQoL and the README.
  - `Canopy-SDK-<version>.zip` for modders: the header, the docs and the example mod's source.
- [x] `.gitattributes` for consistent line endings.
- [x] `THIRD_PARTY_NOTICES.txt` with MinHook's license, shipped in the player package.
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

### Loose file overrides

The game reads its data from `data.dat` archives, whose format we already read. Hooking the archive reader so a file in `mods\<mod>\data\` replaces the archived one would let mods replace textures, text and data tables without code. Questions to answer:

- Where does the game open and read archived files, and does every read go through one place?
- Are files cached, compressed or checked in a way an override must match?
- What happens when two mods override the same file?

### Lua bridge

The game drives its events with Lua script commands; event fast forward already uses one of them. If mods can reach the script engine, `RunScript` would expose the game's own commands: give items, set story flags, move the player. Questions to answer:

- Is there one script state, and can it run a command outside an event safely?
- Which commands are safe at any time, and which only inside events?

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
