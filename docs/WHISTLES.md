# Custom whistles

A mod can add its own whistles to the game. Each one is a copy of a vanilla whistle with its own name, melody and learn flag, declared in the mod's `canopy.ini`, and it can run its own Lua script when it is played. Nothing in the game's save names a whistle, so a save always loads without the mod.

New to Canopy? Lua mods are explained in [LUA.md](LUA.md), and mod flags in [FLAGS.md](FLAGS.md).

## Contents

- [Quick path](#quick-path)
- [Declaring a whistle](#declaring-a-whistle)
- [Melodies](#melodies)
- [Learning a whistle](#learning-a-whistle)
- [The whistle's script](#the-whistles-script)
- [Ids](#ids)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyWhistles`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = My Whistles

   [flag.KnowsRise]
   default = on

   [whistle.Rise]
   name = Rise
   notes = S S L L
   learn = KnowsRise
   script = whistles/Rise.lua
   ```

3. Put the script in `whistles\Rise.lua`:

   ```lua
   local MODULE_NAME = ...

   local M = {}
   M.MODULE_NAME = MODULE_NAME

   function M.main()
       -- What the whistle does.
   end

   return M
   ```

4. Start the game and check `Canopy.log` for `Mod whistle MyWhistles.Rise is id ..., melody SSLL, learned by flag ...`.

The script is optional. Without `script`, the whistle can be played but does nothing.

## Declaring a whistle

One `[whistle.<key>]` section per whistle. The key is 1 to 32 letters, digits, `_` or `-`, and unique within the mod. The mod key is `key` in `[mod]`, or the folder name.

| Key | Required | Meaning |
|---|---|---|
| `name` | Yes | The name in the whistle menu, one line of up to 200 bytes. |
| `name.<lang>` | No | The name in one language: `ja`, `en`, `fr`, `es`, `zh-tw`, `ko` or `zh-cn`. Languages without one use `name`. |
| `notes` | Yes | The melody, 2 to 10 notes. See [Melodies](#melodies). |
| `learn` | Yes | The flag that makes the whistle show in the menu. See [Learning a whistle](#learning-a-whistle). |
| `base` | No | The id of the vanilla whistle to copy, 1 to 11. `2` when left out. Its guide animation and its other texts are kept. |
| `score` | No | Which vanilla score icon the menu shows, 1 to 10. The base whistle's when left out. |
| `icon` | No | A [mod icon](ART.md#using-a-mod-icon) the menu shows in the score icon's place, by key or `Mod.Key`. Wins over `score`. |
| `script` | No | A `.lua` file inside the mod folder, such as `whistles/Rise.lua`, of up to 1 MB of Lua text. |
| `entry` | No | The function of the script the game runs, `main` when left out. A Lua name: letters, digits and `_`. |

`guide` is not supported yet: a whistle keeps its base whistle's. Each one found is logged.

At most 100 mod whistles load in total. When two folders use the same mod key, only the first, in name order, gets its whistles.

## Melodies

Write each note as `S` or `0` for a short note and `L` or `1` for a long one. Spaces are ignored, so `S S L L`, `SSLL` and `0011` are the same melody.

The game runs the whistle whose note count and notes both match exactly what the player played, so every melody must be unique:

- a melody that a vanilla whistle already uses is refused at boot, and the log names the melody;
- a melody that an earlier mod whistle uses is refused when it is read, and the log names that whistle.

The vanilla melodies hold 2 to 10 notes, and the game listens for them in a short window, so long melodies are harder to play.

## Learning a whistle

The whistle menu lists every whistle whose learn flag is set. `learn` is one of:

- a flag of the same mod, such as `KnowsRise`;
- a flag of another mod, such as `OtherMod.Flag`;
- a vanilla flag id, outside the mod flag band 95000 to 99998.

Canopy never sets the flag itself. Give the flag `default = on` to have the whistle known from the start of a new game, or set it from a Lua mod with `canopy.flags.set` when the player earns it. See [FLAGS.md](FLAGS.md).

A whistle whose flag cannot be found is skipped, and the log says why.

## The whistle's script

The script is a game script, the same kind as the game's own whistle scripts, and it runs in the game's script engine.

- **It is not sandboxed.** Unlike a Lua mod in a `lua` folder, it can call every game command, including the ones that change story flags and saves. Canopy logs this when it starts.
- It is loaded the first time the game asks for it, as the module `whistle_<mod>_<key>`, all lowercase, with `-` turned into `_`. `...` is that module name.
- It must return a table, and the game calls the `entry` function of that table when the melody is played.
- When the script does not load, fails, or returns no table, the whistle does nothing, and `Canopy.log` says why, once per whistle.

`canopy.*` is not available in a whistle script. Settings and per-save values belong in a Lua mod, in the same folder if you like; a whistle script and a Lua mod can share state through a mod flag.

## Ids

Each whistle gets an id from 1,800,000,000 up. The first time Canopy sees `<mod>.<key>`, it gives it the next free number and writes it to `mods\CanopyData\whistles.ini`. The whistle keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete `whistles.ini`.

## What the log says

| Line | Meaning |
|---|---|
| `Custom whistles installed: N whistles from M mods` | The whistles were read and the hooks are in. |
| `Mod whistles: N whistles appended to the whistle table.` | The game loaded the rows at boot. |
| `Mod whistle <key> is id N, melody ..., learned by flag F` | The game's own lookup found the whistle at the title. It works. |
| `Mod whistle <key> skipped, its melody ... is already a vanilla whistle's.` | Pick another melody. |
| `... but the whistle table does not have it as written. It stays off.` | The row was not loaded as Canopy wrote it, and it is taken out of the menu. Report it. |
| `Whistle <key>'s script ... failed, so the whistle does nothing: ...` | The script has an error, shown after the colon. |

## Known limitations

- The score icon is one of the ten vanilla ones or a mod icon, and the guide animation is the base whistle's. Own guide art is not supported yet.
- The whistle menu has not been checked with many more whistles than the vanilla eleven.
