# Custom villagers

A mod can add its own villagers to the game. Each one is a copy of a vanilla villager with its own name, job, help text and starting place, declared in the mod's `canopy.ini`, and it can run its own Lua script. Mod villagers never enter the game's own save: Canopy keeps their state in a file next to it, so a save always loads without the mod.

New to Canopy? Lua mods are explained in [LUA.md](LUA.md), and DLL mods in [MODDING.md](MODDING.md).

## Contents

- [Quick path](#quick-path)
- [Declaring a villager](#declaring-a-villager)
- [What a mod villager is](#what-a-mod-villager-is)
- [The villager's script](#the-villagers-script)
- [Ids](#ids)
- [Saves](#saves)
- [Removing a mod](#removing-a-mod)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyVillagers`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = My Villagers

   [villager.Wanderer]
   base = 1020
   name = Wanderer
   job = Traveller
   help = Someone new in the village.
   script = villagers/Wanderer.lua
   x = 1540
   y = 1620
   ```

3. Put the script in `villagers\Wanderer.lua`:

   ```lua
   local MODULE_NAME = ...

   local M = {}
   setmetatable(M, { __index = require("ai_chara_template") })
   M.MODULE_NAME = MODULE_NAME

   function M:main(arg)
       self:unitEventStop_setup(arg)
       Collision2DAddContactBeginEvent(arg.objID, NPC_COLLISION_INDEX_SENSOR, self.MODULE_NAME, "talkInteraction", { arg })
       self:mainLoop(arg)
   end

   return M
   ```

4. Start the game and check `Canopy.log` for `Mod villager MyVillagers.Wanderer is id ..., found in the character and schedule tables`.

The script is optional. Without `script`, the villager runs its base villager's script.

## Declaring a villager

One `[villager.<key>]` section per villager. The key is 1 to 32 letters, digits, `_` or `-`, and unique within the mod. The mod key is `key` in `[mod]`, or the folder name.

| Key | Required | Meaning |
|---|---|---|
| `base` | Yes | The id of the vanilla villager to copy, such as `1020`. Its look, animations, gifts and birthday are kept. |
| `name` | Yes | The name, one line of up to 200 bytes. |
| `name.<lang>` | No | The name in one language: `ja`, `en`, `fr`, `es`, `zh-tw`, `ko` or `zh-cn`. Languages without one use `name`. |
| `job` | No | The job line, up to 200 bytes. Empty when left out. |
| `job.<lang>` | No | The job in one language, like `name.<lang>`. |
| `help` | No | The description, one line of up to 1000 bytes. |
| `help.<lang>` | No | The description in one language, like `name.<lang>`. |
| `script` | No | A `.lua` file inside the mod folder, such as `villagers/Wanderer.lua`, of up to 1 MB of Lua text. See [The villager's script](#the-villagers-script). |
| `routine` | No | The script function the villager's schedule runs, `none` when left out. A Lua name: letters, digits and `_`. |
| `area` | No | The vanilla area id the villager starts in. The base villager's area when left out. |
| `x`, `y` | No | The starting position in that area. The base villager's when left out. |
| `enabled` | No | `false` makes the villager start disabled, as some vanilla villagers do until the story brings them in. `true` when left out. |

`birthday`, `likes`, `dislikes`, `portrait`, `anime` and `icon` are not supported yet: a villager keeps its base villager's. Each one found is logged.

At most 100 mod villagers load in total. When two folders use the same mod key, only the first, in name order, gets its villagers.

## What a mod villager is

Canopy copies the base villager's rows in the game's character and schedule tables, and changes these:

- a new id and an internal name, `CHARA_ID_CANOPY_<MOD>_<KEY>`;
- the name, job and help;
- the script, when the villager has one;
- the starting area, position and enabled state;
- no story hooks: no love events, love flags, requests, detail or festival flags, which is how vanilla villagers without them are set up.

Its schedule is a single task with no conditions that runs `routine` from the villager's script, or from its base villager's script when it has none.

A new game creates the villager like every vanilla one. A save from before the mod was installed gets the villager placed at its starting position as soon as it loads.

## The villager's script

The script is a game script, the same kind as the game's own villager scripts, and it runs in the game's script engine.

- **It is not sandboxed.** Unlike a Lua mod in a `lua` folder, it can call every game command, including the ones that change story flags and saves. Canopy logs this when it starts.
- It is loaded the first time the game asks for it, as the module `villager_<mod>_<key>`, all lowercase, with `-` turned into `_`. `...` is that module name.
- It must return a table. The pattern in the quick path inherits everything from the game's shared villager template, `ai_chara_template`: walking the schedule, talking and the routine helpers. Override only what the villager does differently.
- `routine` names a function in that table. The template's `none` does nothing special.
- When the script does not load, fails, or returns no table, the villager runs its base villager's script instead, and `Canopy.log` says why, once per villager.

`canopy.*` is not available in a villager script. Settings and per-save values belong in a Lua mod, in the same folder if you like.

## Ids

Each villager gets an id from 1,600,000,000 up, above the item ids. The first time Canopy sees `<mod>.<key>`, it gives it the next free number and writes it to `mods\CanopyData\villagers.ini`. The villager keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete `villagers.ini`: the villager files of every save refer to these ids.

## Saves

Mod villagers never reach the game's save file. While the game writes a save, Canopy takes them out of the game's villager list and puts them back right after. When the save succeeds, Canopy writes each villager's friendship and enabled state to `mods\CanopyData\slot<N>.villagers.ini`. A failed save writes nothing.

Villagers use the same save mechanism as [custom items](ITEMS.md) and [mod flags](FLAGS.md), the overlay save.

Loading slot N places every installed villager the save lacks at its starting position, then reads that file back onto them. Deleting slot N deletes its file. VillageQoL's quick save and quick load go through the same code.

Each line of the file is one villager: `villager = <mod>.<key>, <friendship>, <enabled>`. Friendship is 0 to 2100, enabled is `1` or `0`. Lines for villagers that are not installed are kept and written back with the next save.

## Removing a mod

A save never holds a mod villager, so it always loads without the mod. The villager's friendship is not lost: its line stays in the slot's villager file and comes back once the mod is installed again.

## What the log says

| Line | Meaning |
|---|---|
| `No mod villagers, so custom villagers are off.` | No mod folder declares a villager. Nothing is hooked. |
| `Mod <name> (<key>, folder <folder>) defines <n> villagers...` | The folder's villagers were read. |
| `Custom villagers installed: <n> villagers from <m> mods, ...` | The hooks are in. |
| `Mod villagers: <n> villagers appended to the character table from <m> mods.` | The character rows were added at boot. |
| `Mod villagers: <n> villagers appended to the schedule table.` | The schedule rows were added at boot. |
| `Mod villager <mod>.<key> is id <id>, found in the character and schedule tables...` | At the title: the game finds the villager. |
| `Mod villager <mod>.<key> is id <id>, but ...` | At the title: the villager stays off, and the line says why. |
| `Mod villagers: the title's save data holds <n> mod villagers.` | The game created them along with the vanilla villagers. |
| `[<mod>] Villager <key>'s script ... failed, so it runs its base villager's script: ...` | The script did not load or run. The error follows. |
| `Mod villagers: moved <n> villagers out of the save while it is written.` | A save started. |
| `Mod villagers: <n> villagers are back after the save.` | The save was written and the villagers went back. |
| `Mod villagers: placed <n> villagers the loaded save did not have.` | A load placed the villagers. |
| `Mod villagers: restored <n> villagers from slot <N>.` | A load put the friendship and enabled state back. |
| `Custom villagers need ...` or `Custom villagers are off.` | Villagers cannot work on this setup or game build. The game runs without them. |

## Known limitations

- **No own talk lines yet.** A villager's talk table rows, gifts and birthday are not part of `canopy.ini` yet. What the template's `talkStart` shows for a villager with no talk rows is not checked yet.
- **Same look as the base.** The portrait, animations and minimap icon are the base villager's.
- **Back at the start after a load.** The position is not saved: a loaded villager starts at its starting position.
- **Friendship and enabled state only.** Other per-villager state the game may record, such as gifts given today, is not kept for mod villagers.
- **Not sandboxed.** A villager script can do anything a game script can.
- **Restart to reload.** Villagers and their scripts are read at start.
