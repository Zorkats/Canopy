# Canopy

A mod loader and quality of life mods for the Steam version of Village in the Shade.

- **Canopy** (`dinput8.dll`), the mod loader, loads every mod in the game's `mods` folder and adds a **Mod Settings** page to the game's own options menu, where you can change each mod's settings.
- **VillageQoL** (`mods\VillageQoL.dll`) adds quality of life features: quick save, faster walking, event fast forward, mouse wheel zoom and more.

Everything is configured inside the game. There are no overlays or extra windows.

## Install

You need two files: `dinput8.dll` and `VillageQoL.dll`. They come in the release archive, or from your own build (see [Building from source](#building-from-source)).

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
Canopy starting, API version 3.
Options menu installed.
Loading 1 mod.
[VillageQoL] Quick save installed.
Mod VillageQoL (VillageQoL v1.0.0 by Zorkats, VillageQoL.dll) started: 12 settings, 18 hooks.
All hooks enabled.
```

| Symptom | Likely cause |
|---|---|
| No `Canopy.log` at all | `dinput8.dll` is not next to `village.exe`. |
| `Options menu disabled.` | A game update changed the options menu. Mods still load, but their settings cannot be shown. |
| `[VillageQoL] Walk speed disabled.` (or another feature) | A game update changed the code that feature relies on. The other features keep working. |
| `Mod X.dll did not start and was unloaded.` | That mod refused to start; the lines above it say why. |

When you report a problem, include `Canopy.log`.

## Making your own mod

A mod is a 64-bit DLL in the `mods` folder that exports one function. The loader gives it a table of functions to add settings and buttons to the Mod Settings page, run code every frame or on game events, read keys, show the game's toasts and Yes/No popup, and find and hook game functions.

- [docs/MODDING.md](docs/MODDING.md): build a working mod step by step.
- [docs/API.md](docs/API.md): every function, its rules, and the full key and button id list.
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
cmake --install build --component player --prefix dist\player
cmake --install build --component sdk --prefix dist\sdk
```

| Package | Contents |
|---|---|
| `player` | `dinput8.dll` and `mods\VillageQoL.dll`, in the game folder layout, with this README and the license. |
| `sdk` | `sdk\canopy_api.h`, the docs, and the example mod's source, which builds on its own. |

Pushing a tag such as `v1.2.0` makes GitHub Actions build both packages and publish them as a release.

| Folder | Contents |
|---|---|
| `sdk\` | `canopy_api.h`, the only file a mod needs. |
| `src\Canopy\` | The loader: proxy DLL, frame hook, input, settings store and the Mod Settings page. |
| `src\VillageQoL\` | The VillageQoL mod. |
| `src\common\` | Game addresses and helpers shared by the loader and VillageQoL. |
| `examples\ExampleMod\` | The example mod. It is built too; add `-DCANOPY_BUILD_EXAMPLES=OFF` to skip it. |
| `docs\` | The modding guide, the API reference and the [roadmap](docs/ROADMAP.md). |

## License

MIT. See [LICENSE](LICENSE). The loader includes MinHook, whose license is in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
