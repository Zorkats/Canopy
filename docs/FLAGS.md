# Mod flags

A mod can keep its own on/off flags in the game's flag set, declared in its `canopy.ini`. A Lua mod reads and sets them with `canopy.flags`. Mod flags never enter the game's own save: Canopy keeps them in a file next to it, per save slot.

New to Canopy? Lua mods are explained in [LUA.md](LUA.md).

## Contents

- [Quick path](#quick-path)
- [Declaring a flag](#declaring-a-flag)
- [Reading and setting](#reading-and-setting)
- [Ids](#ids)
- [Saves](#saves)
- [Removing a mod](#removing-a-mod)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyFlags`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = My Flags

   [flag.MetTheCat]

   [flag.TipsOn]
   default = on
   ```

3. Use them from `lua\main.lua`:

   ```lua
   canopy.on("control", function()
     if not canopy.flags.get("MetTheCat") then
       canopy.flags.set("MetTheCat", true)
     end
   end)
   ```

4. Start the game and check `Canopy.log` for `Mod flag MyFlags.MetTheCat is id 95000.`

## Declaring a flag

One `[flag.<key>]` section per flag. The key is 1 to 32 letters, digits, `_` or `-`, and unique within the mod. The mod key is `key` in `[mod]`, or the folder name.

| Key | Default | Meaning |
|---|---|---|
| `default` | `off` | `on` turns the flag on in a new game, and in a loaded save whose flag file has no line for it, such as a save made before the mod was installed. |

A folder with flags and no `lua` folder is still a mod. When two folders use the same mod key, only the first, in name order, gets its flags.

## Reading and setting

Only from a Lua mod, and only the mod's own flags. There is no C API for flags.

| Call | Returns |
|---|---|
| `canopy.flags.get(key)` | `true` or `false`. Outside a game, `false` and the reason. |
| `canopy.flags.set(key, on)` | `true` once set. `false` and the reason when it cannot be set now: not in a game, or the game is saving. `on` must be `true` or `false`. |
| `canopy.flags.id(key)` | The flag's id in the game's flag set. |

`set` goes through the game's own flag write, so anything the game watches on that id hears of the change. A key the mod did not declare is a Lua error.

## Ids

Each flag is a real game flag, with an id from 95000 to 99998. No game table, script or native code uses an id in that band, so a mod flag can never collide with a story flag. Canopy refuses any id outside the band.

The first time Canopy sees `<mod>.<key>`, it gives it the next free id and writes it to `mods\CanopyData\flags.ini`. The flag keeps that id for good, and an id is never given out twice, even after the mod is removed. Do not edit or delete `flags.ini`. When all 4999 ids are used, a new flag gets none and is logged.

## Saves

Mod flags never reach the game's save file. While the game writes a save, Canopy clears the whole band and sets it back right after. When the save succeeds, Canopy writes every declared flag as `on` or `off` to `mods\CanopyData\slot<N>.flags.ini`. A failed save writes nothing.

Loading slot N clears the band and sets the flags its file has on; a declared flag the file has no line for takes its default. A new game starts with the declared defaults. Neither tells the game's flag watchers. Deleting slot N deletes its file. VillageQoL's quick save and quick load go through the same code.

Flags and [custom items](ITEMS.md) share one save mechanism, the overlay save: every kind of mod state is taken out before the write and put back after it, and keeps its own `slot<N>.<kind>.ini`.

## Removing a mod

A save never holds a mod flag, so it always loads without the mod. Lines for flags no installed mod declares are kept and written back with the next save, so removing a mod for a while does not lose them.

## What the log says

| Line | Meaning |
|---|---|
| `No mod flags, so mod flags are off.` | No mod folder declares a flag. Nothing is hooked for flags. |
| `Mod <name> (<key>, folder <folder>) defines <n> flags...` | The folder's flags were read. |
| `Mod flag <mod>.<key> is id <id>.` | The flag's id. `, on in a new game` when its default is on. |
| `Mod flags installed: <n> flags from <m> mods.` | Flags are ready. |
| `[<mod>] Flag <key> (<id>) turned on.` | A `set` call changed the flag. |
| `Mod flags: took <n> flags out of the save while it is written.` | A save started. |
| `Mod flags: <n> flags are back after the save.` | The save was written and the flags went back. |
| `Mod flags: wrote slot <N>, <n> flags.` | The slot's flag file was written. |
| `Mod flags: set <n> flags from slot <N>.` | A load set the flags that were on. |
| `Mod flags: the flag band 95000 to 99998 is used up, ...` | A new flag got no id. |
| `Overlay save: ...` | The shared save mechanism, for example a save that was never written. |

## Known limitations

- **Lua only.** Game tables cannot name a mod flag yet.
- **No change notification on load.** Flags set by a load or a new game do not wake game scripts waiting on them.
- **Restart to reload.** Flags are read at start.
