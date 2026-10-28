# Custom items

A mod can add its own items to the game, declared in the mod's `canopy.ini`. An item can be built from scratch or copied from a vanilla item. It can do nothing, be eaten, run the mod's Lua when used, or place a [mod object](OBJECTS.md). A Lua mod gives it to the player with `canopy.items.give`. Mod items never enter the game's own save: Canopy keeps them in a file next to it.

New to Canopy? Lua mods are explained in [LUA.md](LUA.md), and DLL mods in [MODDING.md](MODDING.md).

## Contents

- [Quick path](#quick-path)
- [Declaring an item](#declaring-an-item)
- [Uses](#uses)
- [What a mod item is](#what-a-mod-item-is)
- [Giving and counting](#giving-and-counting)
- [Ids](#ids)
- [Saves](#saves)
- [Removing a mod](#removing-a-mod)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyItems`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = My Items

   ; Built from scratch, eaten for stamina. The icon is a vanilla icon id.
   [item.Berry]
   name = Glow Berry
   help = A berry that glows faintly. Restores some stamina.
   icon = 1000
   use = consume
   stamina = 25
   ```

3. To give it, add a `lua\main.lua`:

   ```lua
   canopy.on("control", function()
     if not canopy.save.has("got_berry") then
       local added = canopy.items.give("Berry", 3)
       if added > 0 then
         canopy.save.set("got_berry", true)
       end
     end
   end)
   ```

4. Start the game and check `Canopy.log` for `Mod item MyItems.Berry is id ..., found in the item table.` Select the berry on the hotbar and use it.

A folder with items and no `lua` folder is still a mod: its items exist, but nothing gives them.

## Declaring an item

One `[item.<key>]` section per item. The key is 1 to 32 letters, digits, `_` or `-`, and unique within the mod. The mod key is `key` in `[mod]`, or the folder name.

| Key | Required | Meaning |
|---|---|---|
| `name` | Yes | The name, one line of up to 200 bytes. |
| `name.<lang>` | No | The name in one language: `ja`, `en`, `fr`, `es`, `zh-tw`, `ko` or `zh-cn`. Languages without one use `name`. |
| `help` | No | The description, one line of up to 1000 bytes. |
| `help.<lang>` | No | The description in one language, like `name.<lang>`. |
| `base` | No | A vanilla item to copy. It must be a consumable raw good (categories 2 to 15), such as a crop or a material. Leave it out to build the item from defaults. |
| `icon` | Without a base | A vanilla item icon id, which must be an icon some vanilla item uses, or a [mod icon](ART.md#using-a-mod-icon) by key or `Mod.Key`. With a base, the base's icon stays when this is left out. A place item without a base or an icon shows the icon of the vanilla item that places its object's clone. |
| `use` | No | `none` (the default), `consume`, `script` or `place`. See [Uses](#uses). |
| `stamina` | No | For `use = consume`: the stamina eating one restores, 1 to 999. |
| `hp` | No | For `use = consume`: the health eating one restores, 1 to 999. |
| `consume` | No | For `use = script`: `on` takes one away each time it is used. `off` by default. |
| `object` | For `use = place` | The [mod object](OBJECTS.md) it places, by key or `Mod.Key`. |
| `category` | No | An item category id from `itemcategory.dat`. Defaults to the base's category, or for a place item to the category of the vanilla item that places its object's clone, or else to 2 (MATERIAL). |
| `buy` | No | What a shop asks for it, 0 to 999999, before a sale's discount. Defaults to the base's price, or 0 without a base. A [`[sale.]` section](SHOPS.md#selling-an-item) puts the item in a shop. |

`price`, `sell`, `energy`, `count` and `type` are not supported. Each one found is logged as ignored. A key that only goes with another use, such as `stamina` on a script item, is also logged as ignored.

At most 1000 mod items load in total. When two folders use the same mod key, only the first, in name order, gets its items.

## Uses

The player uses an item by selecting it on the hotbar, like a vanilla item.

| `use` | What happens |
|---|---|
| `none` | Nothing. The item is a material, an ingredient or a gift. |
| `consume` | The player eats it with the game's own eating: `stamina` and `hp` are restored, and one is gone. |
| `script` | The mod's `canopy.items.on_use` handler runs (see [LUA.md](LUA.md#canopyitemson_usekey-handler)). With `consume = on`, one is gone first. The mod needs a `lua\main.lua`. |
| `place` | The player places the mod object `object` names. Picking the object up gives the item back. |

An item with a base and no `use` keeps the base's use, which is none.

A script item, step by step:

```ini
[item.Compass]
name = Old compass
help = Points somewhere. Use it to find out where.
icon = 1000
use = script
```

```lua
canopy.items.on_use("Compass", function(key)
  canopy.log("The " .. key .. " spins and settles.")
end)
```

The handler runs on the mod's next frame, inside the Lua sandbox like any other handler.

## What a mod item is

Canopy writes a row in the game's item table: a copy of the base item's row, or a row of defaults. It then sets these:

- a new id and an internal name, `CANOPY_<MOD>_<KEY>`;
- the name and help;
- it cannot be sold, thrown away, stored or shown in the encyclopedia;
- no energy, lifespan or brand name;
- the use, category and icon described above.

Getting a mod item has no side effect on the game's progress: no quest or letter counts, no story flags, no encyclopedia entry, no collection or achievement progress. This includes the flag the game sets the first time the player ever gets an item, flag 700. Vanilla items are not affected at all.

## Giving and counting

Only from a Lua mod, and only the mod's own items. There is no C API for items.

| Call | Returns |
|---|---|
| `canopy.items.give(key, count)` | How many went in the bag. `count` is 1 to 999, 1 when left out. When none went in, `0` and the reason: the bag is full, the player is not on the map, the game is saving, or the item is off. |
| `canopy.items.count(key)` | How many the player has, in the bag and the tool slots. |
| `canopy.items.id(key)` | The item's id. |
| `canopy.items.on_use(key, handler)` | Nothing. Sets the handler a `use = script` item runs. |

`give` only works while `canopy.in_game()` is `true`. The item goes in like a found item: it joins a stack of the same item when there is room, takes a free bag slot otherwise, and shows the game's own "got an item" line. A key the mod did not define is a Lua error.

## Ids

Each item gets an id from 1,500,000,000 up. The first time Canopy sees `<mod>.<key>`, it gives it the next free number and writes it to `mods\CanopyData\items.ini`. The item keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete `items.ini`: the item files of every save refer to these ids.

## Saves

Mod items never reach the game's save file. While the game writes a save, Canopy takes them out of the bag, the tool slots and the other item lists, and puts them back right after, each in the slot or list position it came from. When the save succeeds, Canopy writes them to `mods\CanopyData\slot<N>.items.ini`. A failed save writes nothing.

Items use the same save mechanism as [mod flags](FLAGS.md), the overlay save. Items are taken out last and put back first.

Loading slot N reads that file and puts the items back where they were, with no side effect. An item whose slot is taken goes in the first free bag slot, and when the bag is full it waits until a slot frees up. Deleting slot N deletes its file, and a new game starts with none. VillageQoL's quick save and quick load go through the same code.

Each line of the file is one stack: `item = <mod>.<key>, <place>, <index>, <count>, <rank>`. Lines for items that are not installed are kept and written back with the next save, so removing a mod for a while does not lose them.

A placed object is saved separately, see [OBJECTS.md](OBJECTS.md#saves).

## Removing a mod

A save never holds a mod item, so it always loads without the mod. The items are not lost: their lines stay in the slot's item file and come back once the mod is installed again. If the player saves without the mod, the lines are carried over to the new file.

## What the log says

| Line | Meaning |
|---|---|
| `No mod items, so custom items are off.` | No mod folder declares an item. Nothing is hooked, and the game runs exactly as it does without Canopy's items. |
| `Mod <name> (<key>, folder <folder>) defines <n> items...` | The folder's items were read. |
| `Custom items installed: <n> items from <m> mods.` | The hooks are in. |
| `Mod items: <n> items from <m> mods appended to the item table.` | The rows were added at boot. |
| `Mod item <mod>.<key> is id <id>, found in the item table.` | At the title: the game finds the item, so it can be given. |
| `Mod item <mod>.<key> is id <id>, but ...` | At the title: the item stays off, and the line says why. |
| `Mod item <mod>.<key> skipped, ...` | The item was not added, and the line says why, such as an unsuitable base or no icon. |
| `Mod items: the script loader does not match, so script items are off.` | This game build is not supported for script items. The other items still work. |
| `[<mod>] Item <key> was used, but the mod's Lua is not running.` | A script item was used while the mod's Lua had stopped, so no handler ran. |
| `[<mod>] Gave <a> of <c> <mod>.<key>.` | A `give` call. |
| `Mod items: moved <n> items out of the save while it is written.` | A save started. |
| `Mod items: <n> items are back after the save.` | The save was written and the items went back. |
| `Mod items: wrote slot <N>, <n> items.` | The slot's item file was written. |
| `Mod items: restored <n> items from slot <N>.` | A load put the items back. |
| `Overlay save: a save started but was never written, so the mod state goes back now.` | A save never reached its write. After 30 seconds, once the player is on the map, the items go back anyway. |
| `Custom items need ...` or `Custom items are off.` | Items cannot work on this setup or game build. The game runs without them. |

## Known limitations

- **Bag and tool slots only.** Storage chests, shipping and the catalog are not supported. Mod items cannot be stored, sold or thrown away. A shop can sell one through a [`[sale.]` section](SHOPS.md#selling-an-item). If a mod item ever reaches another item list, it is kept out of the save and put back in the bag on load.
- **No selling price or energy.** A mod item can be bought, at its `buy` price, but never sold, and it has no energy.
- **Script items take from the first stack.** With `consume = on`, one is taken from the first bag stack of the item, which is the used one unless the bag holds two stacks of it. The hotbar's button hint for a script item may be blank.
- **No vanilla effects.** A mod item does not count for quests, letters, the encyclopedia, collections or achievements, and a game script that checks the bag by category may see its category.
- **No removing.** There is no `canopy.items.remove` yet. A mod item leaves the bag only when it is used up.
- **Restart to reload.** Items are read at start.
