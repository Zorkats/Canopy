# Custom recipes

A mod can add its own craft table recipes and cooking recipes to the game, declared in the mod's `canopy.ini`. The output and the ingredients can be [custom items](ITEMS.md) of any installed mod or vanilla items. Mod recipes never enter the game's own save: Canopy keeps the player's favorite mod recipes in a file next to it, so a save always loads without the mod.

New to Canopy? Custom items are explained in [ITEMS.md](ITEMS.md), and mod flags in [FLAGS.md](FLAGS.md).

## Contents

- [Quick path](#quick-path)
- [Declaring a craft recipe](#declaring-a-craft-recipe)
- [Declaring a cooking recipe](#declaring-a-cooking-recipe)
- [Naming items and flags](#naming-items-and-flags)
- [Ids](#ids)
- [Saves](#saves)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyRecipes`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = My Recipes

   [item.Lantern]
   base = 230070
   name = Lantern

   [craft.Lantern]
   output = Lantern
   needs = 230000 * 2, 230020
   ```

3. Start the game and check `Canopy.log` for `Mod craft recipe MyRecipes.Lantern is id ..., making 1 of Lantern from 2 ingredients, listed from the start.`

The craft table now lists the recipe: two wood and one stone make one Lantern.

## Declaring a craft recipe

One `[craft.<key>]` section per craft table recipe. The key is 1 to 32 letters, digits, `_` or `-`, and unique among the mod's craft recipes. The mod key is `key` in `[mod]`, or the folder name.

| Key | Required | Meaning |
|---|---|---|
| `output` | Yes | The item made. See [Naming items and flags](#naming-items-and-flags). |
| `needs` | Yes | 1 to 5 ingredients, separated by commas. Each is `item` or `item * count`, with a count of 1 to 99. |
| `count` | No | How many of the output one craft makes, 1 to 99. `1` when left out. |
| `unlock` | No | The flag that makes the recipe show. Listed from the start when left out. |
| `recipe` | No | An item whose use sets the unlock flag, like the vanilla recipe items. Needs `unlock`. |
| `base` | No | The id of the vanilla craft recipe to copy, `1` when left out. Its station and preview are kept. |

## Declaring a cooking recipe

One `[cooking.<key>]` section per cooking recipe, with the same key rules.

| Key | Required | Meaning |
|---|---|---|
| `output` | Yes | The item made. |
| `needs` | Yes | 1 to 5 ingredients, separated by commas. Each is `item`, `item * count`, `group:<number>` or `group:<number> * count`. |
| `count` | No | How many of the output one cook makes, 1 to 99. `1` when left out. |
| `unlock` | No | The flag that makes the recipe show. Listed from the start when left out. |
| `base` | No | The id of the vanilla cooking recipe whose dish look to borrow, `1` when left out. The cooking menu shows that dish as the preview. |

A group ingredient takes any item of a kind, the way vanilla recipes accept any egg. Canopy uses the item the vanilla recipes pair with that group:

| Group | Vanilla pairing |
|---|---|
| `group:11` | any egg |
| `group:12` | any milk |
| `group:13` | paired with game meat |
| `group:14`, `group:15` | paired with a river fish |

A group no vanilla recipe uses is refused at boot.

The output's name and icon in the menus come from the output item's own row, so a [custom item](ITEMS.md) shows its own name.

## Naming items and flags

An item is one of:

- an item of the same mod, such as `Lantern`;
- an item of another mod, such as `OtherMod.Lantern`;
- a vanilla item id, such as `230000`.

A flag, for `unlock`, is a flag of the same mod, `OtherMod.Flag`, or a vanilla flag id outside the mod flag band 95000 to 99998. See [FLAGS.md](FLAGS.md).

A mod item or flag that is not installed makes the recipe skipped when it is read. A vanilla item id that the item table lacks, or a mod item that is off, makes the recipe skipped at the title. The log says why.

## Ids

Each recipe gets an id from 1,700,000,000 up, below the whistle ids. The first time Canopy sees `craft.<mod>.<key>` or `cooking.<mod>.<key>`, it gives it the next free number and writes it to `mods\CanopyData\recipes.ini`. The recipe keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete `recipes.ini`: the recipe files of every save refer to these keys.

## Saves

The game keeps the player's favorite recipes in its save, by recipe id. Mod recipe ids never reach the game's save file: while the game writes a save, Canopy takes them out of both favorite lists and puts them back right after. When the save succeeds, Canopy writes the favorite mod recipes to `mods\CanopyData\slot<N>.recipes.ini`, one `favorite = craft, <mod>.<key>` or `favorite = cooking, <mod>.<key>` line each. A failed save writes nothing.

Loading slot N puts those favorites back. Lines of recipes that are not installed are kept in the file, so they come back when the mod does. Recipes use the same save mechanism as [custom items](ITEMS.md) and [mod flags](FLAGS.md), the overlay save.

## What the log says

| Line | Meaning |
|---|---|
| `Custom recipes installed: N craft recipes and M cooking recipes from K mods.` | The recipes were read and the hooks are in. |
| `Mod recipes: N recipes appended to the craft table.` | The game loaded the rows at boot. Same for the cooking table. |
| `Mod craft recipe <key> is id N, making ...` | The game's own lookup found the recipe and every item it names at the title. It works. |
| `... skipped, ...` | The recipe was refused, and the line says why. |
| `... It stays off.` | The recipe failed its title check, and it is taken out of the menu. |

## Known limitations

- A craft recipe uses its base recipe's station and preview, and a cooking recipe its base recipe's dish model and texture. Own dish art is not supported yet.
- A `recipe` item has not been checked in game yet. A custom item takes its use from its base item, so its base should be a vanilla recipe item.
- Crafting, cooking and favorites have been checked at the title only, not yet in a running game.
