# Debug output and debug configs

The game's scripts still call a few debugging commands, but the released game does nothing with them: a line a script prints goes nowhere, and every debug setting a script asks for reads as off. Canopy brings them back:

- What scripts print for debugging goes to `Canopy.log`.
- The debug settings shipped scripts read are on the game's **Mod Settings** page, in the **Canopy debug** entry.

Every setting starts as the released game has it, so nothing changes until you change one. A change applies at once, with no restart, and is saved in `mods\CanopyDebug.ini`.

## Contents

- [Quick path](#quick-path)
- [Settings](#settings)
- [What the log says](#what-the-log-says)
- [For Lua mods](#for-lua-mods)
- [Known limitations](#known-limitations)

## Quick path

1. Start the game. `Canopy.log` says `Debug hooks installed: script debug output and 6 debug configs are in the Canopy debug entry of Mod Settings.`
2. Open **Options**, then **Mod Settings**, and open **Canopy debug**.
3. Turn on **Script debug output** and **Staff roll info**.
4. Watch the staff roll. `Canopy.log` gets one line per thing the staff roll script reports, such as `Script <module>-<entry>: <text>`.

## Settings

| Setting | In `CanopyDebug.ini` | What it does |
|---|---|---|
| Script debug output | `ScriptOutput` | Writes what scripts print or show for debugging to `Canopy.log`. |
| Debug mode | `DebugMode` | Scripts that ask whether the game is a debug build are told yes. No shipped script asks; a [Lua mod](#for-lua-mods) can. |
| Open villager houses | `NpcHouse` | Villagers' houses can be entered at any time. |
| Friendship events at any level | `LikabilityEvents` | Villagers' friendship events can start whatever their friendship level. |
| Late night events | `LateNight` | `0` Normal: the usual days pass between late night events. `1` Every night: one can start each night. `2` Never: none starts. |
| Environment sound info | `EnvironmentSound` | The environment sound script reports the sounds it plays. Logged while Script debug output is on. |
| Staff roll info | `StaffRoll` | The staff roll script reports its progress. Logged while Script debug output is on. |
| Warp on failed route | `RouteWarp` | A villager that cannot find a path warps to where it was going. |

The file holds one line per setting, `1` for on and `0` for off, and the late night choice as its number:

```ini
[CanopyDebug]
ScriptOutput=1
LateNight=1
```

The last six settings are the game's own debug configs, which shipped scripts read by name: `DEBUG_CONFIG_ENABLE_NPC_HOUSE` (229), `DEBUG_CONFIG_NPC_LIKABILITY_EVENT_ABS` (230), `DEBUG_CONFIG_LATE_NIGHT_ABSOLUTE` (240), `DEBUG_CONFIG_DISP_ENVIROMENT_SE` (244), `DEBUG_CONFIG_DISP_STAFFROLL_INFO` (246) and `DEBUG_CONFIG_ROUTE__FAILED_WARP` (249), with their `debugconfig.dat` ids. The settings change what those scripts do, so a save played with them on can reach events sooner, or later, than it would have.

## What the log says

| Line | Meaning |
|---|---|
| `Debug hooks installed: script debug output and 6 debug configs are in the Canopy debug entry of Mod Settings.` | The debug commands are hooked and the settings are on the page. It works. |
| `Script <module>-<entry>: <text>` | A script printed a line. A script's name is its module and the function it runs. |
| `Script <module>-<entry> displays: <text>` | A script displayed a line, which the debug build drew on screen. |
| `Script <module>-<entry> shows <first>: <second>` | A script gave on-screen debug text, as its two arguments. |
| `Script <module>-<entry> read DEBUG_CONFIG_<NAME>, and Canopy answered N.` | The first time in a session a script reads a debug config: the value it got. |
| `Script <module>-<entry> asked whether debug mode is on, and Canopy answered yes.` | The first time in a session a script asks while Debug mode is on. |
| `Script debug output: 2000 lines this session, so the rest is not logged.` | Script output is cut after 2000 lines a session, so a chatty script cannot fill the disk. Restart the game for 2000 more. |
| `Debug hooks are off.` | A game update moved code Canopy could not find again, after a `Game address <name> was not found in this game build.` line. Every debug command stays as the released game has it. |
| `Debug hooks are off, their settings entry CanopyDebug could not be registered.` | Another mod already uses the key `CanopyDebug`. |

Text a script prints is written as it is, on one line: line breaks become spaces, and a text longer than 1000 bytes is cut.

## For Lua mods

The [Lua sandbox](LUA.md) lets a mod call the game's `DebugIsOn`, `DebugGetConfig` and `DebugPrint` directly, and they answer the same way:

```lua
if DebugIsOn() then
    DebugPrint("Only while Debug mode is on.")
end
```

`canopy.log` is the usual way for a Lua mod to log, and works whatever the settings. `DebugPrint` writes only while Script debug output is on, which suits output meant for debugging.

## Known limitations

- `debugconfig.dat` has 52 configs. Only these six are read by scripts the game ships. The others, such as infinite health or every shop open, were read by code the released game does not contain, so no setting can bring them back.
- A Lua mod's `DebugGetConfig` with any other config id answers 0, as the released game does.
- `DebugSetConfig` is still the released game's: a script that calls it changes nothing. No shipped script but a test one calls it.
- Debug text that a debug build drew on screen is only logged.
