# Crops

A mod can add its own crops, declared in the mod's `canopy.ini`. A mod crop is a copy of a vanilla crop: it grows and looks like it, and the mod can change its name, its seasons, its growth skins, what it gives at harvest, and the seed that plants it. The seed is a [mod item](ITEMS.md).

A crop is two new rows of the game's tables, `crops` and `cropsharvest`, like a [mod row](ROWS.md). A planted mod crop is written into the game's save as the crop it copies, so a save made with it still loads without the mod.

## Contents

- [Quick path](#quick-path)
- [Crop keys](#crop-keys)
- [Seeds](#seeds)
- [Harvest and swaps](#harvest-and-swaps)
- [Other fields](#other-fields)
- [Ids and saves](#ids-and-saves)
- [What the log says](#what-the-log-says)
- [Checking a section](#checking-a-section)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\Moonberry`.
2. Put a `canopy.ini` in it, with a seed item, a produce item and the crop:

   ```ini
   [mod]
   name = Moonberry

   [item.Seed]
   base = 10010
   name = Moonberry seed
   help = Plants a moonberry.

   [item.Berry]
   base = 230070
   name = Moonberry
   help = A berry that grows all year.

   [crop.Berry]
   base = 1
   name = Moonberry
   seed = Seed
   seasons = all
   harvest = Berry * 2-3
   ```

3. Start the game. At the title, `Canopy.log` says `Mod crop Moonberry.Berry is id 2100000001, found in the crops and harvest tables, planted by item 1500000001.`

The seed copies crop 1's vanilla seed and now plants the moonberry, in any season. It grows as crop 1 does, and its harvest gives two to three berries. A Lua mod gives the seed with `canopy.items.give("Seed")`, or a [sale](SHOPS.md#sales) puts it in a shop.

## Crop keys

`[crop.<Key>]` copies a vanilla crop.

| Key | Meaning |
|---|---|
| `base` | Required. The vanilla crop to copy, by its `crops.dat` id: how it grows and looks, its seasons and what it drops. |
| `name` | Required. The name the game shows for the crop and its harvest, one line of up to 200 bytes. `name.<lang>` sets it for one language. |
| `seed` | The mod item that plants it, by key or `Mod.Key`. See [Seeds](#seeds). Without one, only a script can plant the crop. |
| `seasons` | The seasons it grows in: `spring`, `summer`, `autumn` and `winter`, or `all`. Where the base limits when it is sown or harvested, those limits follow. Default: the base's. |
| `skins` | Up to 5 skins for its growth stages, in the base's order: [mod skins](ART.md) by key or `Mod.Key`, or vanilla `animereplacetexture` ids. Stages past the last one named keep the base's. |
| `harvest` | What it gives instead of the base's produce. See [Harvest and swaps](#harvest-and-swaps). |
| `swap` | Other drops to change, up to 16. See [Harvest and swaps](#harvest-and-swaps). |
| `crops.<path>`, `cropsharvest.<path>` | Any other field of the crop's row or its harvest row, by engine path. See [Other fields](#other-fields). |

A key the section does not know is a warning, and it is ignored.

## Seeds

`seed` names a mod item, and Canopy turns that item into the crop's seed: using it plants the crop, as a vanilla seed plants its own.

- The item must have no `use` of its own. A seed only plants.
- It may copy a vanilla seed with `base`, as the quick path does, which gives it the seed's look and price. Any other base the [item rules](ITEMS.md) take works too.
- Its category is the game's seed category unless the item sets one.
- One item plants one crop. Two crops that name the same seed are an error.
- A vanilla item cannot be a seed here. A vanilla seed already plants its own crop.
- A base crop the game never plants from a seed (type 2) cannot take a seed.

When the crop is off at the title, because its rows are missing, Canopy takes the plant action off its seed again, so the seed plants nothing rather than a crop the game does not have.

## Harvest and swaps

A crop's harvest row lists its drops. The first drop's item is the produce, and the base's other drops (seeds, extras) follow it.

`harvest` changes the produce. Every drop of the produce item gives the new item instead, with the new counts:

| Form | Meaning |
|---|---|
| `harvest = Berry` | One berry per drop. |
| `harvest = Berry * 3` | Three per drop. |
| `harvest = Berry * 2-3` | Two to three per drop. |

The item is a mod item by key or `Mod.Key`, or a vanilla item id or enum name. Counts are 1 to 99.

`swap` changes any other drop, as a comma-separated list of `<vanilla item> -> <item>`:

```ini
swap = 100011 -> 100010, 100012 -> Berry
```

Every drop of the first item gives the second instead, with the base's counts. The first item must be a vanilla item the base crop drops, named once. A swap of the produce item while `harvest` is set is an error, because `harvest` already changes it.

## Other fields

`crops.<path>` and `cropsharvest.<path>` set any other field of the two rows by its engine path, as a [patch](PATCHES.md) does, such as `cropsharvest.form.0.growInfo.0.harvest.0.dropRate = 50`. The paths are listed in [Canopy Studio](../README.md#canopy-studio)'s game browser.

These are refused:

| Path | Why | Instead |
|---|---|---|
| `form.count`, any `.growInfo.count` | The forms and growth stages stay as the base has them, so the crop and its harvest agree. | Pick a base with the stages you want. |
| `name`, `harvestCropsName` | The `name` key sets the crop's name and its harvest names. | `name` |
| `suitableSeason`, `sowingSeason`, `harvestSeason` | Only when `seasons` is set, which already sets them. | `seasons` |
| `changeCrops` set to a mod crop | The save keeps the crop a stage changes into as a number. | A vanilla crop id. |

## Ids and saves

A crop's two rows share one id, given the way every [mod row](ROWS.md#ids-and-enum-names) gets one, from 2,100,000,001 up, and kept in `mods\CanopyData\rows.ini`. Do not edit or delete `rows.ini`.

The game's save keeps every planted crop. While the game writes a save, Canopy turns each planted mod crop into its base crop, and turns it back right after. When the save succeeds, Canopy writes each one, with its map, cell and slot, to `mods\CanopyData\slot<N>.crops.ini`. A failed save writes nothing. Loading slot N turns each base crop at those spots back into the mod crop, with its growth kept.

- Without Canopy, or with the mod removed, the save loads with the base crop in its place.
- A line whose crop is off or not installed is kept in the file, so putting the mod back restores the crop at the next load.
- A line whose spot no longer holds the base crop, because it was harvested or cleared, is dropped.

## What the log says

| Line | Meaning |
|---|---|
| `Mod rows installed: 1 crop from 1 mod.` | The crops were read at startup, and every row has its id. |
| `Mod crops installed: 1 crop, 1 seed.` | Planted crops are watched while the game saves, and the seeds are set. |
| `Mod crop <Mod>.<Key> is id N, found in the crops and harvest tables, planted by item M.` | At the title, the game's own lookups found both rows and the seed. It works. |
| `Mod crop <Mod>.<Key> is id N, found in the crops and harvest tables, but no item plants it.` | The crop has no `seed`. A script can still plant it. |
| `Mod crop <Mod>.<Key> is id N, but the harvest table does not have it. It stays off and its seed plants nothing.` | A row is missing, or the base crop is. The seed is turned back into a plain item. |
| `Mod crop <Mod>.<Key> skipped, <why>.` | The seed could not be set: it is not a mod item that loaded, it has a use, or it already plants another crop. |
| `[<Mod>] Crop <Key> skipped, <why>.` | The section was refused at startup: a missing or bad key, a seed or item that does not resolve, or a refused path. |
| `[<Mod>] Crop <Key> has no seed, so only a script can plant it.` | A note from `canopy validate` only. |
| `Mod crops: saving N planted crops as their base crops.` | A save is being written. |
| `Mod crops: N planted crops are back after the save.` | The save is written and the crops are whole again. |
| `Mod crops: restored N planted crops from slot N.` | A save was loaded. The line also counts kept and dropped lines when there are any. |
| `Mod crops: N planted crops have no base crop to save as, so this save needs Canopy and the mod to load.` | A base crop is missing from the game's table. This should not happen with vanilla data. |
| `Mod rows: N crops are off, mod crops could not start.` | Canopy could not watch the save, so it adds no crop rather than write mod ids into saves. |

Crops share the rest of their log lines with [mod rows](ROWS.md#what-the-log-says).

## Checking a section

`canopy validate` reads crops the way Canopy does at startup, without starting the game. With a game folder it also builds both rows against the game's tables, so a base the game does not have, a seed that has a use, a swap of an item the base never drops, or a base of type 2 with a seed shows at its `canopy.ini` line:

```
canopy validate --game "D:\SteamLibrary\steamapps\common\Village in the Shade"
```

## Known limitations

- Crops are added once per start. Changing one needs a restart.
- Canopy must start before the game's init, the normal case. A late start leaves the crop tables as vanilla.
- A crop has its base's forms and growth stages. Their count cannot change.
- A stage that changes into another crop (`changeCrops`) names a vanilla crop only.
- The harvest shows the crop's own name. It cannot have a name of its own.
- A crop that is off, or a save loaded without the mod, grows as its base.
- A seed is a mod item. A vanilla item cannot be turned into a seed for a mod crop.
- The game's crop brand records are keyed by crop id. A brand earned on a mod crop is kept in the save under its id, and does nothing once the mod is gone.
