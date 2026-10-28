# Rows

A mod can add rows to the game's tables, declared in the mod's `canopy.ini`: a tip, a fishing spot, a blacksmith upgrade, a skill tree node, and rows of the other [99 tables](#tables) that take them. A row starts as a copy of a vanilla row of its table and sets the fields it changes, by the same field paths [patches](PATCHES.md) use.

Canopy adds the rows while the game loads each table, through the same hook that adds [mod art](ART.md) rows and applies patches. The game's files are never touched. A table whose ids the game's save keeps takes no rows, so a save never holds an id that would vanish with the mod: those tables are listed under [Tables that take no rows](#tables-that-take-no-rows), with the section to use instead when there is one.

## Contents

- [Quick path](#quick-path)
- [The section](#the-section)
- [Lists](#lists)
- [References](#references)
- [Ids and enum names](#ids-and-enum-names)
- [Tables](#tables)
- [Tables that take no rows](#tables-that-take-no-rows)
- [What the log says](#what-the-log-says)
- [Checking a row](#checking-a-row)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\LanternTips`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = Lantern Tips

   [flag.LanternTip]

   [row.tips.Lantern]
   base = 1
   name = Lanterns
   tips.count = 1
   tips.0 = A lantern lights the path home.
   beginFlag = LanternTip
   readFlag = 0
   ```

3. Start the game. At the title, `Canopy.log` says `Mod tips row LanternTips.Lantern is id 2100000001.`

The row copies tip 1, keeps one page of it with a new text, and waits for the mod's own flag `LanternTip`, which a Lua script of the mod turns on with `canopy.flags.set("LanternTip", true)`. Until then the tip never shows.

## The section

`[row.<table>.<Key>]`

- `<table>` is the table's file name without `.dat`, without case. The tables are listed under [Tables](#tables).
- `<Key>` names the row within the mod and table: 1 to 32 letters, digits, `_` or `-`. Two rows of one table in one mod cannot share a key; rows of different tables can.

| Key | Meaning |
|---|---|
| `base` | Required. The id of a vanilla row of the table, copied first. A row that names a [patched](PATCHES.md) row starts from the patched values. |
| `<list>.count` | The number of elements of a list, such as `tips.count`. See [Lists](#lists). |
| Any field path | The field's value, by the paths of [Field paths](PATCHES.md#field-paths): `name.en`, `captureInfo.0.rate`, `needItem.1.num`. |

Each value is checked against its field's type, as in a patch. A field path the table does not have, a value of the wrong type, a field set twice, or a reference that does not resolve skips the whole row, and the log says why. `id` and `enumName` are refused: Canopy gives every row its own, see [Ids and enum names](#ids-and-enum-names).

The build fields, such as `version`, `region` and `platform`, cannot be set, so a row is for the same builds as the row it copies.

## Lists

A list, such as a tip's `tips` or a fishing spot's `captureInfo`, has its own number of elements in each row. The copy starts with the base's elements; `<list>.count` sets how many the new row has:

```ini
[row.fishing.Pond]
base = 1
captureInfo.count = 2
captureInfo.1.rate = 0.5
```

- A shorter list drops the elements past the count.
- A longer list adds zero elements: every number 0 and every text empty, lists inside them empty.
- A list inside an element counts the same way: `language.1.shadowColor.count = 3`. Canopy resizes the outer lists first, whatever the order in the file.
- Most lists hold a fixed number of elements at most, since the game keeps them inside the row's record: 16 items in an offering, 32 catches in a bug-net row, 4 seasons in a condition. A count over that skips the row, and the line says the most the list takes. A list without such a limit, such as a fishing spot's `captureInfo`, takes up to 4096.
- A field of an element the row does not have, after the counts, skips the row.
- `.count` resizes only a list. A field named `count`, such as the number of items in an offering, is set like any field: `itemInfo.0.count = 3`.

A fixed-size array, such as a blacksmith upgrade's two `needItem` slots, has no count.

## References

A field that refers to another table, the same fields [patches](PATCHES.md#tables) check, takes any of:

- `0`, for no row;
- a row of that table from this mod by its key, `Pond`, or from any mod as `Mod.Key`;
- a mod part of that table by key or `Mod.Key`: an [item](ITEMS.md), a [flag](FLAGS.md), an [icon, texture or skin](ART.md), an [object](OBJECTS.md), a [villager](VILLAGERS.md) or a [whistle](WHISTLES.md);
- a vanilla id, as a number;
- the enum name of a vanilla row, for a table with enum names, with case.

A number in a mod id band, such as `2100000005` or a mod flag's `95000`, is refused: name the mod part by its key, so the reference follows it if its id ever changes. A row whose reference names a row that was skipped is skipped too, and the log names the row it waited on.

Mod references are checked at startup. A vanilla id is checked when the row is built, if its table has already loaded, and at the title otherwise: the row stays either way, as a vanilla row naming a missing id would, and the title line says which id is missing. An enum name is looked up when the row is built; a name its table does not have skips the row.

## Ids and enum names

Every row gets an id from one band shared by every table that takes rows:

| Rows | Ids from | Registry | Enum names |
|---|---|---|---|
| `[row.]` on any table below | 2,100,000,001 | `mods\CanopyData\rows.ini` | `CANOPY_<TABLE>_<MOD>_<KEY>` |

The first time Canopy sees `<table>.<Mod>.<Key>`, it gives it the next free number and writes it to the registry. The row keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete the registry. The band stays below 2,147,483,648, so a table that keeps ids in 32 bits takes them too.

A table with enum names gives the row `CANOPY_<TABLE>_<MOD>_<KEY>`, such as `CANOPY_TIPS_LANTERNTIPS_LANTERN`. A name the table already has gets a number on the end. Tables such as `blacksmith` and `fish` have no enum names.

## Tables

The tables that take rows, by file name without `.dat`:

`action`, `anime`, `animebank`, `animeinstancing`, `archive`, `attack`, `attackfeature`, `bgm`, `birddroptable`, `blacksmith`, `bu`, `bundlegroup`, `calc`, `capture`, `charactermove`, `characterpresent`, `characterschedule`, `cmd`, `construction`, `crafttab`, `creaturefeature`, `credit`, `cropsform`, `cropsharvest`, `cropsplacement`, `dailyresetvalue`, `debugconfig`, `debugconfigcategory`, `demofeature`, `dyeing`, `environmentse`, `eventfeature`, `facility`, `facilityrelease`, `fish`, `fishing`, `font`, `framebuffer`, `gachapon`, `gamedefine`, `gameeffectcondition`, `gameeffecteffect`, `gameeffectfeature`, `gameeffecttarget`, `gameeffectvalue`, `gameidvalue`, `gamesetting`, `gamesettinglist`, `gametablevalue`, `gamevalue`, `gimmickcompatibility`, `gimmickfeature`, `gimmickprocess`, `hsvreplacetexture`, `huntreward`, `intangeble`, `itemcategory`, `itemfeature`, `itemtype`, `latenightevent`, `lettersend`, `livestockfeature`, `livingtank`, `mapdiff`, `mapfeature`, `mapinterior`, `mapjudge`, `naturaloccurrence`, `networkdefine`, `number`, `offeringitem`, `pad`, `placementcreature`, `placementhorror`, `placementitem`, `randomsale`, `reminiscence`, `renderstep`, `scripttype`, `se`, `secretquest`, `shader`, `shootgamereward`, `skillspherecost`, `skilltree`, `string`, `talkcommand`, `talknameplate`, `tips`, `treasurebox`, `trophy`, `updatelevel`, `updatetext`, `vkey`, `vkeycategory`, `weatherfeature`, `weatherschedule`, `window` and `zdefine`.

`texture`, `icon` and `animereplacetexture` take rows through [mod art](ART.md), including its [raw rows](ART.md#raw-rows), with ids of their own.

A row in a table is not yet a row the game uses. A new tip shows by itself once its flag is on, but most rows only matter when another row names them: a new fishing spot needs a map that places it, a new skill tree node a node that leads to it. Canopy adds the row; what the game does with it is the game's.

To add a fish to the spots the game has, a catch to its traps, an item to its forage spots or offering pools, rather than a new spot, use the sections in [CATCHES.md](CATCHES.md) and [SHOPS.md](SHOPS.md): they add to the lists of vanilla rows.

## Tables that take no rows

| Table | Why | Instead |
|---|---|---|
| `item` | The save keeps item ids. | [`[item.<Key>]`](ITEMS.md) |
| `gimmick` | The save keeps object ids. | [`[object.<Key>]`](OBJECTS.md) |
| `character` | The save keeps villager ids. | [`[villager.<Key>]`](VILLAGERS.md) |
| `charactertalk` | The save keeps its ids. | `[talk.<Key>]` |
| `crops` | The save keeps crop ids. | [`[crop.<Key>]`](CROPS.md) |
| `storesales` | The save keeps shop entry ids. | [`[sale.<Key>]`](SHOPS.md#sales) |
| `letter`, `subquestitem` | The save keeps their ids. | `[letter.<Key>]`, `[request.<Key>]` |
| `playercrothing`, `playerfronthairstyle`, `playerbackhairstyle`, `playerheadaccessory`, `playerneckaccessory`, `playerbackaccessory` | The save keeps outfit ids. | `[outfit.<Key>]` |
| `gameflag` | Flags have a section of their own. | [`[flag.<Key>]`](FLAGS.md) |
| `whistle` | Whistles have a section of their own. | [`[whistle.<Key>]`](WHISTLES.md) |
| `craft`, `cooking` | Recipes have a section of their own. | [`[recipe.<Key>]`](RECIPES.md) |
| `bundle`, `creature`, `event`, `farmland`, `fertilizer`, `gameeffect`, `livestock`, `livestockplacement`, `map`, `mapgroup`, `quest`, `talk`, `weather` | The save keeps their ids. | None yet. Their vanilla rows still take [patches](PATCHES.md). |

A save made with a mod row of one of these tables would name an id that is gone once the mod is removed. The typed sections give their rows save-safe ids and keep them out of the save, the way mod items and objects do.

## What the log says

| Line | Meaning |
|---|---|
| `Mod <name> (<key>, folder <folder>) defines N rows, registered for its rows.` | A mod with rows and nothing else Canopy registers is now in the mod list. A mod that also has items, art or a script says `defines N rows.` instead. |
| `Mod rows installed: 1 fishing row and 1 tips row from 1 mod.` | The rows were read at startup, and every row has its id. |
| `Table engine: tips.dat gets N mod rows.` | The game loaded the table with the rows in it. |
| `Mod tips row <Mod>.<Key> is id N.` | At the title, the game's own lookup found the row. It works. |
| `Mod tips row <Mod>.<Key> is id N, but <field> names <table> <id>, which its table does not have.` | The row is in, but a vanilla id it names is missing. |
| `Mod tips row <Mod>.<Key> is id N, but the tips table does not have it.` | The row was built, but the game's table does not hold it. |
| `[<Mod>] Row <table>.<Key> skipped, <why>.` | The section was refused at startup: unknown table, a table that takes no rows, no `base`, a bad field or value, or a reference that does not resolve. |
| `Mod <table> row <Mod>.<Key> skipped, <why>.` | The row was refused as its table loaded: its base is not in the table, an element it sets is not in the row, an enum name its table lacks, or a vanilla id its table, already loaded, lacks. |
| `No usable mod rows, so mod rows are off.` | Every row was refused. |
| `Mod rows: the game tables were never seen loading, so mod rows are off.` | Canopy started after the tables loaded. No row was added. |

## Checking a row

`canopy validate` reads rows the way Canopy does at startup, without starting the game. With a game folder it also builds each row against the game's table, so a missing base, a missing vanilla id or an unknown enum name shows at its `canopy.ini` line:

```
canopy validate --game "D:\SteamLibrary\steamapps\common\Village in the Shade"
```

## Known limitations

- Only the [tables](#tables) Canopy has a generated layout for take rows. The tables listed under [Patches](PATCHES.md#known-limitations) as having none take neither.
- A vanilla id named by a row is checked against its table only when that table loads first; for any other, the title line reports it and the row stays.
- A row is added once per start. Changing a row needs a restart.
- Canopy must start before the game's init, the normal case. A late start leaves every table without mod rows.
