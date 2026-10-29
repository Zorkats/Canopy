# Shops, the blacksmith and offerings

A mod can add to what the village trades in, declared in the mod's `canopy.ini`:

- A **sale** puts an item on a shop's list, a vanilla item or a [mod item](ITEMS.md).
- An **upgrade** has the blacksmith turn one tool into another.
- An **offering** adds an item to the pools offerings draw from.

A sale and an upgrade are new rows of the game's tables, like a [mod row](ROWS.md). An offering adds to a list inside a row the game already has: it adds no row and gives out no id. None of the three reaches the game's save file, so a mod that goes away leaves nothing behind.

## Contents

- [Quick path](#quick-path)
- [Sales](#sales)
- [Selling an item](#selling-an-item)
- [Upgrades](#upgrades)
- [Offerings](#offerings)
- [Naming pools and items](#naming-pools-and-items)
- [Full pools](#full-pools)
- [Ids and saves](#ids-and-saves)
- [What the log says](#what-the-log-says)
- [Checking a section](#checking-a-section)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\ShrineGems`.
2. Put a `canopy.ini` in it, with a vanilla item's id:

   ```ini
   [mod]
   name = Shrine Gems

   [sale.Gem]
   item = 230000
   shop = 0
   stock = 3

   [offering.Gem]
   pools = all
   item = 230000
   count = 2
   ```

3. Start the game. At the title, `Canopy.log` says `Mod sale ShrineGems.Gem is id 2100000001.` and `Mod offering ShrineGems.Gem is in 2 offeringitem rows, 2 elements.`

Shop 0 now sells three of that item a day, and every offering pool can give two of it. To sell or offer a [mod item](ITEMS.md), name it by its key: `item = Gem` for the mod's own `[item.Gem]`.

## Sales

`[sale.<Key>]` puts an item on a shop's list, as a row of `storesales.dat`. Every day the game rebuilds each shop's list from the rows of that shop whose seasons and flags hold, so the sale shows from the next list the game builds: the next day, or a new game.

| Key | Meaning |
|---|---|
| `item` | Required. A mod item by key or `Mod.Key`, or a vanilla item id or enum name. |
| `shop` | Required. The shop, by the `storeID` its vanilla sales have: `0`, `1`, `2`, `3`, `4` or `99`. [Canopy Studio](https://github.com/Zorkats/CanopyStudio)'s game browser lists each shop's `storesales` rows. |
| `stock` | How many the shop has, 1 to 99, or `unlimited`. Default `unlimited`. |
| `restock` | With a stock: `daily` fills it again every day, `never` leaves what was bought gone for good. Default `daily`. |
| `seasons` | The seasons it is listed in: `spring`, `summer`, `autumn` and `winter`, or `all`. Default `all`. |
| `flag` | Flags that must all be on for it to be listed, up to 16: [mod flags](FLAGS.md) by key or `Mod.Key`, or vanilla flag ids. |
| `unless` | Flags that must all be off for it to be listed, up to 16. |
| `discount` | Percent off the item's price, 0 to 99. Default 0. |
| `priority` | Its place in the list, 0 to 100: higher comes first. Default 50. |
| `silhouette` | `on`: while its seasons or flags do not hold, the shop shows it as a dark shape. Default `off`. |

`<Key>` names the sale within the mod: 1 to 32 letters, digits, `_` or `-`. Two `[sale.]` sections of one mod cannot share a key.

The row starts as a copy of the shop's first vanilla row, which also proves the shop exists, and then takes every key above. The rest of the copy is cleared:

- **The traveling stall's rotation.** A sale is always listed while its seasons and flags hold.
- **The release flags.** The game turns them on only for a fixed list of vanilla items it knows by id, and one shop refuses an item once they are all on. A mod sale would never see them turned on, so it has none. A `sets` key is ignored with a warning; to stop listing a sale once something happened, give it `unless` with a flag the mod turns on.

## Selling an item

A shop asks the item's own price, less the sale's `discount`:

- A vanilla item has its price in `item.dat`. A [patch](PATCHES.md) such as `[patch.item.<id>]` with `buyingPrice = 500` changes it in every shop.
- A [mod item](ITEMS.md) takes its price from its `buy` key, 0 to 999999. Without `buy`, it keeps the price of its `base` item, and an item with neither is free.

Mod items cannot be sold back to a shop, as with every other way of [getting rid of one](ITEMS.md#known-limitations).

## Upgrades

`[upgrade.<Key>]` adds a job to the blacksmith, as a row of `blacksmith.dat`. The player brings a tool and two materials, and after a wait the blacksmith hands back a better tool in a box.

| Key | Meaning |
|---|---|
| `from` | Required. The tool the player brings: a vanilla item id or enum name. |
| `to` | Required. The tool the blacksmith hands back: a vanilla item that a vanilla upgrade makes. |
| `needs` | Required. The two materials it takes, each `item` or `item * count` with a count of 1 to 99: mod items by key or `Mod.Key`, or vanilla item ids or enum names. For example `needs = 220100, 210010 * 5`. |
| `time` | How long the blacksmith works on it, 1 to 1000, in the game's own units. Every vanilla upgrade has 10. Default 10. |

The box the blacksmith hands the tool over in is an object made for that tool, and only the tools vanilla upgrades make have one. So `to` must be such a tool: the row is a copy of the vanilla upgrade that makes it, box included, with `from`, `needs` and `time` written over it. `from` cannot be `to`.

`<Key>` names the upgrade within the mod, like a sale's. A sale and an upgrade may share a key, but an upgrade and a [`[row.blacksmith.]`](ROWS.md) of the same mod cannot.

## Offerings

`[offering.<Key>]` adds an item to offering pools, the `itemInfo` list of `offeringitem.dat`.

| Key | Meaning |
|---|---|
| `pools` | Required. The pools: `all`, or `offeringitem.dat` ids or enum names, such as `1, 2`. |
| `item` | Required. A mod item by key or `Mod.Key`, or a vanilla item id or enum name. |
| `rate` | The entry's weight against the pool's other entries, 1 to 1000. Default 1, as every vanilla entry has. |
| `count` | How many of the item the entry gives, 1 to 99. Default 1. |

`<Key>` names the offering within the mod: 1 to 32 letters, digits, `_` or `-`. Two `[offering.]` sections of one mod cannot share a key.

## Naming pools and items

- A pool, a shop's item or a vanilla tool is named by its id, as a number, or by its enum name as the game writes it, with case.
- [Canopy Studio](https://github.com/Zorkats/CanopyStudio)'s game browser shows every table's rows with their ids and fields.
- A number in a mod id band, such as a mod item's `1500000001`, is refused: name a mod item or flag by its key, so the section follows it if its id ever changes.
- A vanilla item id is checked against `item.dat` as the table loads, if `item.dat` has already loaded, and at the title otherwise. An enum name is looked up as the table loads.

## Full pools

A pool holds 16 entries at most, since the game keeps them inside the row's record. A pool that is full keeps its vanilla entries, the log names it, and the other pools still get the offering. Offerings go in folder and file order, so when several mods add to one pool, the first ones fit.

[Patches](PATCHES.md) apply before offerings, so a patch's element index, such as `itemInfo.2.dropRate`, always names a vanilla entry.

## Ids and saves

A sale and an upgrade get their ids the way every [mod row](ROWS.md#ids-and-enum-names) does, from 2,100,000,001 up, kept in `mods\CanopyData\rows.ini`. Do not edit or delete `rows.ini`.

The game's save keeps each shop's list: the sales on it and the stock left. While the game writes a save, Canopy takes out the entries of mod sales, and of any sale of a mod item, and puts them back right after. When the save succeeds, Canopy writes them to `mods\CanopyData\slot<N>.sales.ini`. A failed save writes nothing. Loading slot N puts them back where they were, with the stock that was left. An entry whose sale or item is gone, because its mod was removed, is left out, and the next day's list is built without it.

The blacksmith's rows are never saved.

## What the log says

| Line | Meaning |
|---|---|
| `Mod rows installed: 1 sale and 1 upgrade from 1 mod.` | The sales and upgrades were read at startup, and every row has its id. |
| `Mod shops installed: shop lists keep mod sales out of the save.` | Shop lists are watched while the game saves. |
| `Mod sale <Mod>.<Key> is id N.` | At the title, the game's own lookup found the row. It works. `Mod upgrade` for an upgrade. |
| `Mod sale <Mod>.<Key> skipped, no vanilla sale is in shop 50, so the game has no such shop.` | The shop does not exist. |
| `Mod upgrade <Mod>.<Key> skipped, no vanilla upgrade makes 1234, so the blacksmith has no box to hand it over in.` | `to` is not a tool a vanilla upgrade makes. |
| `[<Mod>] Sale <Key> skipped, <why>.` | The section was refused at startup: a missing or bad key, or a mod item or flag that does not resolve. `Upgrade` for an upgrade. |
| `[<Mod>] Sale <Key>: sets is ignored, buying a mod sale turns no flag on. Use unless to stop listing it once a flag is on.` | See [Sales](#sales). |
| `Mod shops: moved N sales out of the save while it is written.` | A save is being written. |
| `Mod shops: N sales are back after the save.` | The save is written and the lists are whole again. |
| `Mod shops: put back N sales from slot N.` | A save was loaded. |
| `Mod shops: put back N sales from slot N, and left out N sales whose row or item is gone or already listed; the next day lists the shops again.` | Some entries had no sale or item to go back to. |
| `Mod rows: N sales are off, mod shops could not start.` | Canopy could not keep the shop lists out of the save, so it adds no sale rather than write mod ids into saves. Upgrades still work. |
| `Mod offering <Mod>.<Key> is in N offeringitem rows, M elements.` | At the title: the pools the offering went into. It works. |
| `Mod offering <Mod>.<Key> skipped on offeringitem 2, itemInfo can have 16 elements at most.` | That pool was full. |
| `Mod offering <Mod>.<Key> skipped on pools 9, which offeringitem.dat does not have.` | A pool the section names is not in the table. |
| `Mod offering <Mod>.<Key> skipped, item: it names item 1234, which is not in item.dat.` | The item did not resolve, so the offering went nowhere. |
| `[<Mod>] Offering <Key> skipped, <why>.` | The section was refused at startup: a missing or bad key, or a mod item that does not resolve. |

Sales and upgrades share the rest of their log lines with [mod rows](ROWS.md#what-the-log-says). Offerings share theirs with the [catch and forage](CATCHES.md#what-the-log-says) sections, which add to lists the same way.

## Checking a section

`canopy validate` reads sales, upgrades and offerings the way Canopy does at startup, without starting the game. With a game folder it also builds the rows and adds the offerings to the game's tables, so a missing shop, a tool no vanilla upgrade makes, a full pool or an item the game lacks shows at its `canopy.ini` line:

```
canopy validate --game "D:\SteamLibrary\steamapps\common\Village in the Shade"
```

## Known limitations

- Sales, upgrades and offerings are added once per start. Changing one needs a restart.
- Canopy must start before the game's init, the normal case. A late start leaves the shops, the blacksmith and every pool as vanilla.
- A new sale shows from the next list the game builds: the next day, or a new game.
- A sale is always listed while its seasons and flags hold. It cannot join the traveling stall's rotation.
- Buying a mod sale turns no flag on. A [Lua mod](LUA.md) can turn a [mod flag](FLAGS.md) on when the item's count in the bag goes up.
- Mod items cannot be sold back to a shop.
- An upgrade makes only a tool that a vanilla upgrade makes, and takes only a vanilla tool.
- A tool that a vanilla upgrade already takes as `from` has two upgrades to offer. Which one the blacksmith shows is the game's choice and has not been checked in game.
