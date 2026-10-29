# Canopy

The mod loader for the Steam version of Village in the Shade.

**Canopy** (`dinput8.dll`) loads every mod in the game's `mods` folder and adds a **Mod Settings** page to the game's own options menu, where you can change each mod's settings. Mods can also replace the game's own data files, such as text and textures, by shipping them in `mods\<Mod>\data\`. The title screen shows Canopy's version and how many mods are installed in its bottom-right corner.

Everything is configured inside the game. There are no overlays or extra windows.

Two companions have their own repositories and releases:

- **[VillageQoL](https://github.com/Zorkats/VillageQoL)** (`mods\VillageQoL.dll`), a Canopy mod with quality of life features: quick save, faster walking, event fast forward, mouse wheel zoom and more.
- **[Canopy Studio](https://github.com/Zorkats/CanopyStudio)**, a desktop app for Windows and Linux that installs and updates Canopy and VillageQoL for you, turns mods off and on, makes new mods, edits their Lua with completion of the Canopy API, and browses the game's files.

## Install

You need `dinput8.dll`, from `Canopy-<version>.zip` on this repository's [releases page](https://github.com/Zorkats/Canopy/releases/latest) or from your own build (see [Building from source](#building-from-source)). For VillageQoL you also need `VillageQoL.dll`, from `VillageQoL-<version>.zip` on [its releases page](https://github.com/Zorkats/VillageQoL/releases/latest). Canopy works without VillageQoL, so you can install the loader alone. [Canopy Studio](https://github.com/Zorkats/CanopyStudio) does all of this for you.

1. Open the game folder. In Steam, right-click **Village in the Shade**, then pick **Manage > Browse local files**.
2. Copy `dinput8.dll` into that folder, next to `village.exe`.
3. Create a folder named `mods` in that same folder.
4. Copy `VillageQoL.dll` into `mods`.
5. Start the game. Open **Options**: a **Mod Settings** row means the loader is working.

The game folder should now look like this:

```text
Village in the Shade\
├── village.exe
├── dinput8.dll              the loader
├── Canopy.log               written by the loader, useful when something goes wrong
└── mods\
    ├── VillageQoL.dll
    └── VillageQoL.ini       your VillageQoL settings, created on first start
```

### Linux and Steam Deck

The game runs there through Proton, and Canopy runs inside it. Proton uses its own `dinput8.dll` unless told otherwise, so tell it once:

1. In Steam, right-click **Village in the Shade**, then pick **Properties**.
2. Under **General**, set **Launch Options** to:

   ```text
   WINEDLLOVERRIDES="dinput8=n,b" %command%
   ```

3. Install as above, in the folder **Manage > Browse local files** opens. On a Steam Deck, do this in Desktop Mode.

`Canopy.log` then has the line `Running under Wine or Proton, version ...`. No `Canopy.log` at all means the launch option is missing or mistyped.

### Updating from an older VillageQoL

Older versions were a single `dinput8.dll` that kept its settings in `VillageQoL.ini` next to the game. Install as above, replacing the old `dinput8.dll`. Your settings are copied to `mods\VillageQoL.ini` on the first start. After that, the old `VillageQoL.ini` and `VillageQoL.log` are no longer used and you can delete them.

### Uninstall

Delete `dinput8.dll` and the `mods` folder. The mods never change your save files outside of the game's own saving.

## Change settings

Open **Options** from the title screen or the in-game menu, then pick **Mod Settings**.

| What you see | What to do |
|---|---|
| `+ VillageQoL` | Select it to show that mod's settings. It turns into `- VillageQoL`; select it again to hide them. |
| An on/off or a list setting | Change it like any of the game's own options. |
| A key or button setting, such as `Quick Save Key: F5` | Select it, then press the key or button you want. **Esc** cancels, **Delete** sets it to none. |
| **Back** | Returns to the options menu. Cancel does the same. |

Changes apply at once and are saved right away to the mod's file in `mods`.

The in-game options menu also gets a **Return to Title** row above **Exit Game**. It asks for confirmation first, since unsaved progress is lost.

## When something goes wrong

Open `Canopy.log` in the game folder. Each start adds lines like these:

```text
Canopy starting, API version 4.
Game build 0x6ABD400D is checked: Ver. 1.30.
Starting on the main thread, before game init.
Options menu installed.
Loading 1 mod.
[VillageQoL] Quick save installed.
Mod VillageQoL (VillageQoL v1.0.0 by Zorkats, VillageQoL.dll) started: 12 settings, 18 hooks.
All hooks enabled, before game init.
```

| Symptom | Likely cause |
|---|---|
| No `Canopy.log` at all | `dinput8.dll` is not next to `village.exe`. |
| No `Canopy.log` on Linux or a Steam Deck | Proton used its own `dinput8.dll`. Set the launch option in [Linux and Steam Deck](#linux-and-steam-deck). |
| `Game build 0x... is not checked. Searched ...: N of M addresses and fields found.` | The game was updated after this Canopy release. Canopy found what it could on its own, so most features and mods still work. Features that write saves stay off until a Canopy update checks the new build. |
| `Per-save data needs a checked game build, ...` or `[VillageQoL] Quick save needs a checked game build.` | Same cause: on an unchecked build, nothing that writes to a save runs, so a wrong address cannot damage one. |
| `Game address <name> was not found in this game build.` | A game update changed code Canopy could not find again. Only the feature that needs it is off, and the next line names it. |
| `Game field <name> is 0x... in this game build, not 0x...` or `Game field <name> was not found in this game build.`, possibly after `Game field <name> is checked through <other>.` | A game update changed, or may have changed, the layout of something Canopy reads, such as where a save keeps its group. Reading it at the old place could crash the game, so only the feature that needs it is off, and the next line names it. |
| `Game build 0x... is checked, but mods\CanopyData\force_search.txt asks for a search. ...` | Someone is testing how Canopy copes with a game update. Delete `mods\CanopyData\force_search.txt` to go back to the checked addresses. |
| `Options menu disabled.` | A game update changed the options menu. Mods still load, but their settings cannot be shown. |
| `[VillageQoL] Walk speed disabled.` (or another feature) | A game update changed the code that feature relies on. The other features keep working. |
| `Mod X.dll did not start and was unloaded.` | That mod refused to start; the lines above it say why. |
| `Starting on a loader thread, which races game init: ...` | A game update changed how the game starts. Mods still load, but replaced game files are not used. |
| `Override conflict: ...` | Two mods replace the same game file. The one named last wins. |

When you report a problem, include `Canopy.log`.

## What Canopy cannot do yet

The main limits are below. Each guide under [docs](docs) lists its own in full, in its **Known limitations** section.

**For players:**

- **Mods are read when the game starts.** Settings on the Mod Settings page apply at once, but a new or changed mod, its files, its `canopy.ini` or its Lua take a restart.
- **Settings have no text fields.** A setting is a toggle, a choice, a slider, a key or button, or a button row. The game's options menu has no text entry to build on.
- **After a game update, features that write saves stay off** until a Canopy release checks the new build. See [When something goes wrong](#when-something-goes-wrong).

**For mod authors:**

| Area | Not possible yet |
|---|---|
| Reloading | A mod cannot be unloaded or reloaded while the game runs. Restart the game to test a new build. Reloading in a developer mode is [planned](docs/ROADMAP.md#m5-developer-tools). |
| Models and art | No new 3D models. Furniture is a copy of vanilla furniture, a skin only swaps textures, and a villager keeps its base's portrait and animations. Dish art and whistle guide art are the base's too. |
| Items | Mod items live in the bag and tool slots only. They cannot go in storage chests, be shipped or sold, or appear in the catalog, and they do not count for quests, letters, the encyclopedia, collections or achievements. See [ITEMS.md](docs/ITEMS.md#known-limitations). |
| Villagers | No talk lines, gifts or birthday of their own. A villager's position is not saved, so after a load it starts at its starting place. See [VILLAGERS.md](docs/VILLAGERS.md#known-limitations). |
| Flags | Mod flags are for Lua only. The game's tables cannot name one. |
| Tables | 14 of the game's tables take neither patches nor rows, because Canopy has no layout for them. [PATCHES.md](docs/PATCHES.md#known-limitations) names them. |
| Lua | The Lua API covers a mod's own items and flags only, not vanilla items, flags or shops. Game commands are not documented, and Canopy does not check their arguments. |

Some features work in Canopy's checks but have not been tried in a running game yet: crafting and cooking with mod recipes, `recipe` items, how mod icons look in the game's menus, and the whistle menu with many more than the vanilla eleven whistles.

## Making your own mod

A mod is a 64-bit DLL in the `mods` folder that exports one function. The loader gives it a table of functions to add settings, sliders and buttons to the Mod Settings page, keep data with each save, run code every frame or on game events, read keys, show the game's toasts and Yes/No popup, and find and hook game functions. A mod can also be only a folder of replacement game files, with no code: see [Replace game files](docs/MODDING.md#replace-game-files). Or it can be a Lua script, with no DLL or compiler, that runs in a sandbox inside the game's own script engine: see [docs/LUA.md](docs/LUA.md). A mod can also add its own items, furniture, villagers, whistles, recipes and art: see [docs/ITEMS.md](docs/ITEMS.md), [docs/OBJECTS.md](docs/OBJECTS.md), [docs/VILLAGERS.md](docs/VILLAGERS.md), [docs/WHISTLES.md](docs/WHISTLES.md), [docs/RECIPES.md](docs/RECIPES.md) and [docs/ART.md](docs/ART.md).

- [docs/MODDING.md](docs/MODDING.md): build a working mod step by step.
- [docs/API.md](docs/API.md): every function, its rules, and the full key and button id list.
- [docs/LUA.md](docs/LUA.md): Lua mods, their API, the sandbox and the log lines.
- [docs/ITEMS.md](docs/ITEMS.md): custom items a mod defines in its `canopy.ini` and gives through Lua, which can be eaten, run Lua, or place furniture.
- [docs/OBJECTS.md](docs/OBJECTS.md): mod furniture, copies of vanilla furniture that a mod item places and that survive saves without the mod.
- [docs/VILLAGERS.md](docs/VILLAGERS.md): custom villagers a mod defines in its `canopy.ini`, with their own Lua scripts.
- [docs/WHISTLES.md](docs/WHISTLES.md): custom whistles a mod defines in its `canopy.ini`, with their own Lua scripts.
- [docs/RECIPES.md](docs/RECIPES.md): custom craft and cooking recipes a mod defines in its `canopy.ini`.
- [docs/ART.md](docs/ART.md): textures, icons and model texture swaps a mod defines in its `canopy.ini`, from plain PNG files.
- [docs/CATCHES.md](docs/CATCHES.md): fish, trap catches, forage items and wild crops a mod adds to the game's own spots.
- [docs/SHOPS.md](docs/SHOPS.md): items a mod sells in shops, blacksmith upgrades and items in offering pools.
- [docs/CROPS.md](docs/CROPS.md): mod crops copied from vanilla ones, with their own seed, seasons, skins and harvest.
- [docs/DEBUG.md](docs/DEBUG.md): the game's debug script output in `Canopy.log`, and its debug configs on the Mod Settings page.
- [examples/ExampleMod](examples/ExampleMod/example_mod.cpp): the finished mod from the walkthrough.
- [docs/ROADMAP.md](docs/ROADMAP.md): what the framework does next.

## Building from source

You need Visual Studio 2022 with the **Desktop development with C++** workload, which includes CMake and Ninja, and a [vcpkg](https://vcpkg.io) checkout. vcpkg installs MinHook on its own from `vcpkg.json`.

From an **x64 Native Tools Command Prompt for VS 2022**, in the repository folder:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg
cmake --preset release
cmake --build --preset release
```

To lay out the packages a release ships:

```bat
cmake --install build --component canopy --prefix dist\canopy
cmake --install build --component sdk --prefix dist\sdk
```

| Package | Contents |
|---|---|
| `canopy` | `dinput8.dll`, the loader, with this README, the license and the third-party notices. |
| `sdk` | `sdk\canopy_api.h`, the docs, and the example mod's source, which builds on its own. |

### On Linux

Linux builds the parts that run outside the game: `libcanopy_core.so`, which Canopy Studio reads the game with, and the `canopy` command line tool. The loader is a Windows DLL and is built on Windows only; under Proton it runs as it is. You need GCC 12 or newer and CMake. vcpkg is not needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests that read the game look for it in `~/.local/share/Steam`; set `CANOPY_GAME_DIR` to its folder for another install.

### As a subproject

VillageQoL and Canopy Studio take Canopy as a git submodule in `external/Canopy` and add it with `add_subdirectory`, or configure it on its own for `canopy_core`. Only the loader needs vcpkg, so they turn it off with `-DCANOPY_BUILD_LOADER=OFF`; `CANOPY_BUILD_TOOLS`, `CANOPY_BUILD_TESTS` and `CANOPY_BUILD_EXAMPLES` turn off the rest. Install rules are only added when Canopy is the top project.

### When the game updates

Canopy finds every game address by name. On a build listed in `src\common\game_builds.txt` it uses that build's checked addresses; on any other it searches the exe with the patterns in `src\common\game_addresses.txt`, and features that write saves stay off. Field lines in the same file read struct offsets and sizes, such as the `0x208` of `mov rax,[rcx+0x208]`, so a feature whose layout moved stays off too. Offsets with no such line can be checked through a field line of the same struct with `GAME_FIELD_VIA`, which fails when that struct's size or a later field moves. The rest are plain `constexpr` in `game_retail.h` and are not checked; the generator lists both.

To make a new build a checked one, run from the repository folder, with the build folder configured:

```bat
scripts\add_game_build.bat "C:\path\to\new\village.exe" "Ver. 1.xx (Steam build 12345678)" "_work\bin\village 1.30.exe"
```

The last argument, a listed build, is optional. The script prints what the search found and what to check by hand: names only some of their lines found, functions whose length changed, and fields whose value changed. It asks before it adds the section, then copies the exe to `_work\bin` for the tests, rebuilds and runs the tests. A name it cannot find stops it; once you have checked the new build has no such site, add `--none <name>`. When the game's code moved a lot, regenerate the patterns from every sample, newest first, with `python scripts\gen_address_patterns.py "_work\bin\village <new>.exe" "_work\bin\village 1.30.exe"`.

VillageQoL's feature lists live in `game_retail.h` too, so its sources count as well. Give the generator its source folder with `--sources ..\VillageQoL\src`, which pairs the names its files use together. The tests check that each game address a source file uses is in a list that file checks; `-DCANOPY_MOD_SOURCE_DIRS=<folder>` adds a mod's folder to that check, and VillageQoL's own build sets it.

To try the search on a checked build in the game, put a file `mods\CanopyData\force_search.txt` in the game folder. Canopy then searches `village.exe`, or the loaded game when the file's first word is `loaded`, logs how the result compares with the checked addresses, and keeps features that write saves off.

### Releases

GitHub Actions builds and tests every push and pull request on Windows and on Linux. Pushing a tag such as `v1.2.0` publishes a release with every package:

| Package | Contents |
|---|---|
| `Canopy-1.2.0.zip` | The loader. |
| `Canopy-SDK-1.2.0.zip` | The header, the docs and the example mod. |
| `Canopy-Tools-1.2.0-linux-x64.tar.gz` | The `canopy` tool and `libcanopy_core.so` for Linux. |

VillageQoL and Canopy Studio are released from their own repositories, with their own versions. The tag's version is the one the title screen shows and the loader's version resource carries. Every day the tip of `main` is also published as the `nightly` prerelease, when it has changed. Canopy Studio installs from the latest tagged release only, never from a nightly.

| Folder | Contents |
|---|---|
| `sdk\` | `canopy_api.h`, the only file a mod needs. |
| `src\Canopy\` | The loader: proxy DLL, frame hook, input, settings store and the Mod Settings page. |
| `src\common\` | Game addresses and helpers shared by the loader and mods built against them, such as VillageQoL. |
| `src\CanopyCore\`, `src\CanopyTools\` | The game archive and mod rules library behind `canopy_core` and the `canopy` tool, for Windows and Linux. |
| `examples\ExampleMod\` | The example mod. It is built too; add `-DCANOPY_BUILD_EXAMPLES=OFF` to skip it. |
| `docs\` | The modding guide, the API reference, the Lua guide and the [roadmap](docs/ROADMAP.md). |

## License

MIT. See [LICENSE](LICENSE). The loader includes MinHook, whose license is in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
