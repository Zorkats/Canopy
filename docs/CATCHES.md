# Catches and forage

A mod can add what the player catches and finds, declared in the mod's `canopy.ini`: a fish at fishing spots, an item or a creature in traps, an item at forage spots, and a wild crop in the areas where crops grow on their own. Each takes a vanilla item, creature or crop, or a [mod item](ITEMS.md).

These sections add to lists inside rows the game already has, such as the catches of a fishing spot. They add no row and give out no id, and the game's save keeps none of these lists, only what the player caught or picked. A mod that goes away leaves nothing behind: the lists are as vanilla again at the next start, and what the player already has stays theirs.

## Contents

- [Quick path](#quick-path)
- [Fish](#fish)
- [Trap catches](#trap-catches)
- [Forage items](#forage-items)
- [Wild crops](#wild-crops)
- [Treasure](#treasure)
- [Naming rows and items](#naming-rows-and-items)
- [Full lists](#full-lists)
- [Patches and rows](#patches-and-rows)
- [What the log says](#what-the-log-says)
- [Checking a section](#checking-a-section)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\RiverBounty`.
2. Put a `canopy.ini` in it, with a vanilla fish's item id:

   ```ini
   [mod]
   name = River Bounty

   [catch.Night]
   spots = all
   fish = 1234
   times = 18:00-0:00
   rate = 2
   ```

3. Start the game. At the title, `Canopy.log` says `Mod catch RiverBounty.Night is in 8 fishing rows, 32 elements.`

Every fishing spot now lists that fish at night in every season, with twice the weight of a rate 1 catch.

## Fish

`[catch.<Key>]` with `fish` adds a fish to fishing spots, the `captureInfo` list of `fishing.dat`.

| Key | Meaning |
|---|---|
| `fish` | Required. The fish, by its item id or the item's enum name. It must be a fish: an item `fish.dat` has. |
| `spots` | Required. The fishing spots: `all`, or `fishing.dat` ids or enum names, such as `1, 3`. |
| `rate` | Its weight against the spot's other catches, a number over 0 and up to 1000, such as `0.5` or `3`. Default 1. Vanilla fish have 0.5 to 6. |
| `seasons` | `spring`, `summer`, `autumn` and `winter`, as a list, or `all`. Default `all`. |
| `times` | Time windows as `H:MM-H:MM`, such as `7:00-12:00, 18:00-0:00`, or `all`. Default `all`, the four windows vanilla fish use: `0:00-6:00`, `7:00-12:00`, `12:00-18:00` and `18:00-0:00`. |

The game lists a fish once per season and time window, so Canopy adds one element per pair: two seasons and two windows are four elements in each spot. A window is written back the way the game writes it, without a leading zero: `07:00-12:00` becomes `7:00-12:00`.

`<Key>` names the entry within the mod: 1 to 32 letters, digits, `_` or `-`. Two `[catch.]` sections of one mod cannot share a key.

## Trap catches

`[catch.<Key>]` with `item` or `creature` adds a catch to traps, the `captureInfo` list of `capture.dat`.

| Key | Meaning |
|---|---|
| `item` | An item: a mod item by key or `Mod.Key`, or a vanilla item id or enum name. |
| `creature` | Or a creature: a vanilla creature id or enum name. |
| `traps` | Required. The traps: `all`, or `capture.dat` ids or enum names. |
| `rate` | Its weight against the trap's other catches, a whole number from 1 to 1000. Default 1. Vanilla catches have 1 to 60. |
| `bait` | `yes` when the trap must be baited, `no` when it must not be, `both` for either. Default `both`. |

A section has one of `fish`, `item` and `creature`. The game lists a catch once per bait state, so `both` adds two elements to each trap, one baited and one not.

## Forage items

`[forage.<Key>]` with `item` adds an item to forage spots, the objects that give an item when picked. Each spot's items are a list in `placementitem.dat`, under the spot object's id.

| Key | Meaning |
|---|---|
| `item` | Required. A mod item by key or `Mod.Key`, or a vanilla item id or enum name. |
| `spots` | Required. The spot objects: `all`, or `gimmick.dat` ids or enum names. |
| `weight` | Its weight against the spot's other items, a whole number from 1 to 1000. Default 10. |
| `weather` | The weathers it shows in: `weather.dat` ids or enum names, or `all`. Default `all`. |
| `flag` | A flag that must be on: a [mod flag](FLAGS.md) by key or `Mod.Key`, or a vanilla flag id. |
| `unless` | A flag that must be off, the same way. |

A spot named in `spots` that no row of `placementitem.dat` lists has no item list to add to: the log says so, and the other spots still get the item.

## Wild crops

`[forage.<Key>]` with `crop` adds a crop to the areas where crops grow on their own, the `cropsInfo` list of `naturaloccurrence.dat`.

| Key | Meaning |
|---|---|
| `crop` | Required. A vanilla crop id or enum name. |
| `areas` | Required. The areas: `all`, or `naturaloccurrence.dat` ids or enum names. |
| `seasons` | `spring`, `summer`, `autumn` and `winter`, as a list, or `all`. Default `all`. |
| `daily` | How many grow each day, as `min-max` from 0 to 100. Default `1-2`. |
| `most` | The most the area holds at once, 1 to 100. Default 5. |
| `flag` | A flag that must be on, as for a forage item. |

A section has one of `item` and `crop`. `all` seasons is one element, the way vanilla crops that grow all year are listed; a list of seasons is one element per season.

## Treasure

A treasure box gives one item, drawn from every row of `treasurebox.dat`. Treasure has no section of its own: a [mod row](ROWS.md) adds an entry, and a [patch](PATCHES.md) changes or removes a vanilla one.

```ini
[row.treasurebox.Gem]
base = 55
itemID = Gem
weight = 1
unlock = 0
```

| Field | Meaning |
|---|---|
| `itemID` | The item: a vanilla item id, or a [mod item](ITEMS.md) as `Key` or `Mod.Key`. |
| `weight` | Its share of the draw. Every vanilla entry has 1. An entry of 0 or less is never drawn, so `weight = 0` in a patch takes a vanilla entry out. |
| `unlock` | `0` for a plain entry. Entries with any other value form a second pool for items that unlock something, such as a recipe or an outfit, and are only drawn while their item still has something to unlock. |

The game draws from the unlock pool 80 times in 100 while it holds an entry that can still unlock something, and from the plain entries otherwise, by weight. Copy a row whose `unlock` is 0, as `base` does above, or set `unlock = 0`, for an item that is plain treasure. The row's other numbers are read only.

## Naming rows and items

- A row or a vanilla part is named by its id, as a number, or by its enum name as the game writes it, with case: `spots = 1, 3`. `fish.dat` has no enum names, so a fish is named by its item's id or enum name.
- [Canopy Studio](../README.md#canopy-studio)'s game browser shows every table's rows with their ids and fields.
- A number in a mod id band, such as a mod item's `1500000001`, is refused: name a mod item by its key, so the section follows it if its id ever changes. A [mod row](ROWS.md)'s id or `CANOPY_` enum name is refused too.
- Mod references are checked at startup. A vanilla id is checked against its table as the table loads, if that table has already loaded, and at the title otherwise. An enum name is looked up as the table loads.

## Full lists

Most of these lists hold a fixed number of elements, since the game keeps them inside the row's record: 32 catches in a trap, 16 items at a forage spot, 32 crops in an area. A fishing spot's list has no such limit.

A section goes into a row with all its elements or none. A row whose list would grow past its limit keeps its vanilla list, the log names the row, and the other rows still get the section. Sections go in folder and file order, so when several mods add to one full list, the first ones fit.

## Patches and rows

[Patches](PATCHES.md) apply before these sections, so a patch's element index, such as `captureInfo.2.rate`, always names a vanilla element. A [mod row](ROWS.md) of `fishing`, `capture`, `placementitem` or `naturaloccurrence` is not a vanilla row: these sections take vanilla rows only.

## What the log says

| Line | Meaning |
|---|---|
| `Mod <name> (<key>, folder <folder>) defines N list additions, registered for its list additions.` | A mod with these sections and nothing else Canopy registers is now in the mod list. A mod that also has items, rows or a script says `defines N list additions.` instead. |
| `List additions installed: 2 catches and 1 forage entry from 1 mod.` | The sections were read at startup. |
| `List additions: fishing.dat gets N elements on M rows.` | The game loaded the table with the elements in it. |
| `Mod catch <Mod>.<Key> is in N fishing rows, M elements.` | At the title: what the section added. It works. |
| `Mod catch <Mod>.<Key> is in N fishing rows, M elements, but it names fish 1234 (not in fish.dat).` | The elements are in, but a vanilla id they name is missing. |
| `Mod catch <Mod>.<Key> went into no row.` | Nothing was added, and the lines before it say why. |
| `Mod catch <Mod>.<Key> skipped on capture 3 and 7, captureInfo can have 32 elements at most.` | Those rows' lists were full. |
| `Mod catch <Mod>.<Key> skipped on spots 9, which fishing.dat does not have.` | A row the section names is not in the table. |
| `Mod forage entry <Mod>.<Key> skipped on spots 500000009, which no placementitem.dat row lists.` | A spot object has no item list. |
| `Mod catch <Mod>.<Key> skipped, fish: it names fish 1234, which is not in fish.dat.` | A reference did not resolve, so the section went nowhere. |
| `[<Mod>] Catch <Key> skipped, <why>.` | The section was refused at startup: a missing or bad key, or a mod reference that does not resolve. |
| `No usable list additions, so list additions are off.` | Every section was refused. |
| `List additions: the game tables were never seen loading, so none was added.` | Canopy started after the tables loaded. |

## Checking a section

`canopy validate` reads these sections the way Canopy does at startup, without starting the game. With a game folder it also adds them to the game's tables, so a full list, a missing row or a reference that does not resolve shows at its `canopy.ini` line:

```
canopy validate --game "D:\SteamLibrary\steamapps\common\Village in the Shade"
```

## Known limitations

- No new species: a fish must be one `fish.dat` has, with its own model and size, and a creature or a crop one the game has. A mod item caught in a trap or found at a spot is an item, with no creature of its own.
- A section is added once per start. Changing one needs a restart.
- Treasure has no section of its own: see [Treasure](#treasure). Bird drops take [patches](PATCHES.md) and [rows](ROWS.md) too.
- Canopy must start before the game's init, the normal case. A late start leaves every list as vanilla.
