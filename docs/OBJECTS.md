# Mod objects

A mod can add its own furniture to the game. Each object is a copy of a vanilla furniture object, with its own name and, optionally, its own skin, declared in the mod's `canopy.ini`. A [mod item](ITEMS.md) with `use = place` puts it down, and picking it up gives that item back. A save with a placed mod object still loads without Canopy: the object shows as the vanilla furniture it copies.

New to Canopy? Lua mods are explained in [LUA.md](LUA.md), items in [ITEMS.md](ITEMS.md), and skins in [ART.md](ART.md).

## Contents

- [Quick path](#quick-path)
- [Declaring an object](#declaring-an-object)
- [Picking a clone](#picking-a-clone)
- [What a mod object is](#what-a-mod-object-is)
- [Ids](#ids)
- [Saves](#saves)
- [Removing a mod](#removing-a-mod)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyLamp`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = My Lamp

   ; A copy of the vanilla desk lamp 242000004.
   [object.Lamp]
   clone = 242000004
   name = Reading lamp

   ; The item that places it. With no icon, it shows the vanilla lamp item's.
   [item.Lamp]
   name = Reading lamp
   help = A lamp for late nights. Use it to set it down.
   use = place
   object = Lamp
   ```

3. Give the item from a `lua\main.lua`, as in [ITEMS.md](ITEMS.md#quick-path).
4. Start the game and check `Canopy.log` for `Mod object MyLamp.Lamp is id ..., found in the gimmick table.` Select the item on the hotbar indoors and use it.

An object and its item may share a key: objects and items have separate keys.

## Declaring an object

One `[object.<key>]` section per object. The key is 1 to 32 letters, digits, `_` or `-`, and unique among the mod's objects.

| Key | Required | Meaning |
|---|---|---|
| `clone` | Yes | The id of the vanilla furniture object to copy, with its model, size and behavior. It must be furniture: an object its own vanilla item places. See [Picking a clone](#picking-a-clone). |
| `name` | Yes | The name, one line of up to 200 bytes. |
| `name.<lang>` | No | The name in one language: `ja`, `en`, `fr`, `es`, `zh-tw`, `ko` or `zh-cn`. Languages without one use `name`. |
| `skin` | No | A skin for the model: a vanilla AnimeReplaceTexture id, or a [mod skin](ART.md) by key or `Mod.Key`. Left out, the clone's skin stays. |

An object needs an item to be placed. An object no item places is still added, with a warning, and the player cannot put it down. At most 500 mod objects load in total. When two folders use the same mod key, only the first, in name order, gets its objects.

## Picking a clone

The clone is a row of the game's `gimmick.dat`. It must be furniture: its row names a vanilla item, and that item is a placeable item whose placed object is the clone. 1,783 vanilla objects qualify: lamps, chairs, tables, shelves, chests and so on.

- In Canopy Studio, the `clone` field's picker lists them by name, with the icon of the item that places each one. The Lists page shows them under Furniture. Studio's Furniture template starts a mod with one object and its item.
- `canopy validate --game <game folder>` checks the clone against the game's tables and says when it is not furniture.

The object behaves like its clone: same model, size, interaction and script. A chest stays a chest, and a lamp stays a lamp.

## What a mod object is

Canopy copies the clone's row in the game's gimmick table and changes these:

- a new id and an internal name, `GIMMICK_ID_CANOPY_<MOD>_<KEY>`;
- the name;
- the skin, when `skin` is set;
- the item picking it up gives: the mod item that places it.

Everything else comes from the clone.

## Ids

Each object gets an id from 1,650,000,000 up. The first time Canopy sees `<mod>.<key>`, it gives it the next free number and writes it to `mods\CanopyData\objects.ini`. The object keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete `objects.ini`: placed objects refer to these ids.

## Saves

The game's save never holds a mod object's id. While the game writes a save, Canopy turns each placed mod object into its clone, and turns it back right after. When the save succeeds, Canopy writes which placed objects were mod objects to `mods\CanopyData\slot<N>.objects.ini`. A failed save writes nothing.

Objects use the same save mechanism as [mod items](ITEMS.md#saves), the overlay save.

Loading slot N reads that file and turns each listed object back into the mod object, as long as it is still the clone. Deleting slot N deletes its file.

Each line of the file is one placed object: `object = <uid>, <mod>.<key>`, where the uid is the game's own id for that placed object. Lines for objects that are not installed or are off are kept and written back with the next save.

## Removing a mod

A save never holds a mod object id, so it always loads without the mod, and without Canopy. The placed objects show as their clones. Their lines stay in the slot's object file, and once the mod is installed again, the next load turns them back into mod objects. If the player saves without the mod, the lines are carried over.

Picking up a clone while the mod is gone gives the vanilla item, not the mod item.

## What the log says

| Line | Meaning |
|---|---|
| `No mod objects, so mod objects are off.` | No mod folder declares an object. Nothing is hooked. |
| `Mod <name> (<key>, folder <folder>) defines <n> objects...` | The folder's objects were read. |
| `Mod objects installed: <n> objects from <m> mods.` | The objects joined the table load and the overlay save. |
| `Mod objects: <n> objects from <m> mods appended to the gimmick table.` | The rows were added at boot. |
| `Mod object <mod>.<key> is id <id>, found in the gimmick table.` | At the title: the game finds the object, so it can be placed. |
| `Mod object <mod>.<key> is id <id>, but ...` | At the title: the object stays off, and the line says why, such as its item being off. |
| `Mod object <mod>.<key> skipped, ...` | The object was not added, and the line says why, such as a clone that is not furniture. |
| `Mod object <mod>.<key> keeps its clone's skin, ...` | Its mod skin was not in the skin table when the object was added. |
| `Mod objects: saving <n> placed objects as vanilla furniture.` | A save started. |
| `Mod objects: <n> placed objects are back after the save.` | The save was written and the objects went back. |
| `Mod objects: restored <n> placed objects from slot <N>...` | A load turned the objects back, and the line says what was kept or dropped. |

## Known limitations

- **Copies only.** An object is always a copy of vanilla furniture. A new model or new behavior is not supported.
- **Skins follow the model.** A skin must fit the clone's model, since it only swaps textures.
- **Clones in the save.** Without the mod, a placed object is its clone, and picking it up gives the clone's vanilla item.
- **Vanilla scripts.** An object runs its clone's script, and a vanilla script that checks object ids by number may not know the mod object.
- **Restart to reload.** Objects are read at start.
