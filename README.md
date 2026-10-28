# Canopy

A mod loader and quality of life mods for the Steam version of Village in the Shade.

- **Canopy** (`dinput8.dll`), the mod loader, loads every mod in the game's `mods` folder and adds a **Mod Settings** page to the game's own options menu, where you can change each mod's settings. Mods can also replace the game's own data files, such as text and textures, by shipping them in `mods\<Mod>\data\`. The title screen shows Canopy's version and how many mods are installed in its bottom-right corner.
- **VillageQoL** (`mods\VillageQoL.dll`) adds quality of life features: quick save, faster walking, event fast forward, mouse wheel zoom and more.

Everything is configured inside the game. There are no overlays or extra windows.

**Canopy Studio**, a separate desktop app for Windows and Linux, installs and updates both for you, turns mods off and on, makes new mods, edits their Lua with completion of the Canopy API, and browses the game's files. See [Canopy Studio](#canopy-studio).

## Install

You need two files: `dinput8.dll` and `VillageQoL.dll`. They come in two release archives, `Canopy-<version>.zip` and `VillageQoL-<version>.zip`, or from your own build (see [Building from source](#building-from-source)). Canopy works without VillageQoL, so you can install the loader alone.

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

### Canopy Studio

Canopy Studio is optional. It finds the game in your Steam libraries, installs Canopy and VillageQoL from the latest release (each download checked against the SHA-256 GitHub lists for it), and changes the game folder only when you ask it to.

| System | Download | Start |
|---|---|---|
| Windows 10 or 11, 64-bit | `CanopyStudio-<version>-windows-x64.zip` | Extract anywhere, run `CanopyStudio.exe`. |
| Linux, 64-bit (x86-64) | `CanopyStudio-<version>-linux-x64.tar.gz` | Extract anywhere, run `CanopyStudio`. |

Both carry everything they need: no .NET or other runtime to install. On Linux the game still runs through Proton, so set the launch option above; Canopy Studio shows it with a **Copy** button, but never changes Steam's files itself. It finds Steam installed natively or as a Flatpak, and keeps its own files in `~/.config/CanopyStudio` (`%APPDATA%\CanopyStudio` on Windows).

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

## VillageQoL features

| Feature | What it does | Default |
|---|---|---|
| Show BGM Information | Shows each song's title in the game's popup log the first time it plays. | On |
| Quick Save | Press the key in the field to save and return to the title screen. The save loads from an extra row in the load menu, and is deleted once you load it. | F5, no controller button |
| Settings Hotkey | Press the key in the field to open the options. | F10, no controller button |
| Walking Speed | How fast you walk and run in the field: Normal, 1.25x, 1.5x, 1.75x or 2x. Movement during events does not change. | 1.5x |
| Event Fast Forward | Hold the key or button during an event the game lets you skip, to play it faster. Events that cannot be skipped are never sped up. | Left Ctrl or RT, 4x |
| Advance Text | While fast forwarding, talk advances on its own. | On |
| Beta Event Skip | Press Cancel and the fast forward button together (B + RT, or Circle + R2) during an event the game lets you skip, to run it at 32x like the beta build's skip. It stops at every choice and popup and hands control back, so it never answers for you. | Off |
| Mouse Wheel Zoom | Scroll the mouse wheel in the field to step the camera through its three zoom levels. | On |
| Back Up Key Bindings | Keeps a copy of your game key bindings in `mods\VillageQoL.ini` and restores them if they ever load back at their defaults. | On |

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
cmake --install build --component villageqol --prefix dist\villageqol
cmake --install build --component sdk --prefix dist\sdk
```

| Package | Contents |
|---|---|
| `canopy` | `dinput8.dll`, the loader, with this README, the license and the third-party notices. |
| `villageqol` | `mods\VillageQoL.dll` in the game folder layout, with the license. |
| `sdk` | `sdk\canopy_api.h`, the docs, and the example mod's source, which builds on its own. |

### On Linux

Linux builds the parts that run outside the game: `libcanopy_core.so`, which Canopy Studio reads the game with, and the `canopy` command line tool. The loader and VillageQoL are Windows DLLs and are built on Windows only; under Proton they run as they are. You need GCC 12 or newer, CMake and, for Canopy Studio, the .NET 10 SDK. vcpkg is not needed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
dotnet publish src/CanopyStudio/CanopyStudio/CanopyStudio.csproj -c Release -r linux-x64 --self-contained
```

The tests that read the game look for it in `~/.local/share/Steam`; set `CANOPY_GAME_DIR` to its folder for another install. Canopy Studio copies `libcanopy_core.so` from `build/src/CanopyTools` (on Windows `canopy_core.dll`); add `-p:CanopyCoreDir=<folder>`, with the folder's full path, to take it from elsewhere.

### When the game updates

Canopy finds every game address by name. On a build listed in `src\common\game_builds.txt` it uses that build's checked addresses; on any other it searches the exe with the patterns in `src\common\game_addresses.txt`, and features that write saves stay off. Field lines in the same file read struct offsets and sizes, such as the `0x208` of `mov rax,[rcx+0x208]`, so a feature whose layout moved stays off too. Offsets with no such line can be checked through a field line of the same struct with `GAME_FIELD_VIA`, which fails when that struct's size or a later field moves. The rest are plain `constexpr` in `game_retail.h` and are not checked; the generator lists both.

To make a new build a checked one, run from the repository folder, with the build folder configured:

```bat
scripts\add_game_build.bat "C:\path\to\new\village.exe" "Ver. 1.xx (Steam build 12345678)" "_work\bin\village 1.30.exe"
```

The last argument, a listed build, is optional. The script prints what the search found and what to check by hand: names only some of their lines found, functions whose length changed, and fields whose value changed. It asks before it adds the section, then copies the exe to `_work\bin` for the tests, rebuilds and runs the tests. A name it cannot find stops it; once you have checked the new build has no such site, add `--none <name>`. When the game's code moved a lot, regenerate the patterns from every sample, newest first, with `python scripts\gen_address_patterns.py "_work\bin\village <new>.exe" "_work\bin\village 1.30.exe"`.

To try the search on a checked build in the game, put a file `mods\CanopyData\force_search.txt` in the game folder. Canopy then searches `village.exe`, or the loaded game when the file's first word is `loaded`, logs how the result compares with the checked addresses, and keeps features that write saves off.

### Releases

GitHub Actions builds and tests every push and pull request on Windows and on Linux. Pushing a tag such as `v1.2.0` publishes a release with every package:

| Package | Contents |
|---|---|
| `Canopy-1.2.0.zip` | The loader. |
| `VillageQoL-1.2.0.zip` | The VillageQoL mod. |
| `Canopy-SDK-1.2.0.zip` | The header, the docs and the example mod. |
| `CanopyStudio-1.2.0-windows-x64.zip`, `CanopyStudio-1.2.0-linux-x64.tar.gz` | Canopy Studio. |
| `Canopy-Tools-1.2.0-linux-x64.tar.gz` | The `canopy` tool and `libcanopy_core.so` for Linux. |

The tag's version is the one the title screen shows and the DLLs' version resources carry. Every day the tip of `main` is also published as the `nightly` prerelease, when it has changed. Canopy Studio installs from the latest tagged release only, never from a nightly.

| Folder | Contents |
|---|---|
| `sdk\` | `canopy_api.h`, the only file a mod needs. |
| `src\Canopy\` | The loader: proxy DLL, frame hook, input, settings store and the Mod Settings page. |
| `src\VillageQoL\` | The VillageQoL mod. |
| `src\common\` | Game addresses and helpers shared by the loader and VillageQoL. |
| `src\CanopyCore\`, `src\CanopyTools\` | The game archive and mod rules library behind `canopy_core` and the `canopy` tool, for Windows and Linux. |
| `src\CanopyStudio\` | Canopy Studio. |
| `examples\ExampleMod\` | The example mod. It is built too; add `-DCANOPY_BUILD_EXAMPLES=OFF` to skip it. |
| `docs\` | The modding guide, the API reference, the Lua guide and the [roadmap](docs/ROADMAP.md). |

## License

MIT. See [LICENSE](LICENSE). The loader includes MinHook, whose license is in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
