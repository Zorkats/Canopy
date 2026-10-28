# Patches

A mod can change fields of rows the game already has, declared in the mod's `canopy.ini`: an item's price, a shop entry's stock, a recipe's ingredients, an icon's rectangle, and any other field of the [133 tables](#tables) Canopy has a layout for. A patch names one row of one game table and the fields to change. Everything else in the row keeps its vanilla value.

Canopy writes the patched values into the table while the game loads it, through the same hook that adds [mod art](ART.md) rows. The game's files are never touched, nothing reaches the game's save, and removing the mod puts the vanilla values back on the next start.

Two mods can patch the same row. When they change different fields, both apply. When they change the same field, the mod later in load order wins and the log says so.

## Contents

- [Quick path](#quick-path)
- [The section](#the-section)
- [Field paths](#field-paths)
- [Tables](#tables)
- [What cannot be patched](#what-cannot-be-patched)
- [Several mods](#several-mods)
- [What the log says](#what-the-log-says)
- [Checking a patch](#checking-a-patch)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\WideTestIcon`.
2. Put a `canopy.ini` in it:

   ```ini
   [mod]
   name = Wide Test Icon

   [patch.icon.1000]
   drawInfo.drawW = 72
   drawInfo.drawH = 72
   ```

3. Start the game. At the title, `Canopy.log` says `Patch self-check: 14 fields match, 0 differ, 0 cannot be checked.`

Icon 1000 is the game's test icon. The patch sets the size it is drawn at, in all seven languages.

## The section

`[patch.<table>.<row>]`

- `<table>` is the table's file name, with or without `.dat`, without case: `icon`, `icon.dat` and `Icon.DAT` are one table. The tables that take patches are listed under [Tables](#tables). Any other table is refused, and the log says so.
- `<row>` is the row's id, in decimal, or its enum name as the game writes it, with case: `[patch.icon.1000]`, or `[patch.icon.<enum name>]`. Some tables, such as `blacksmith`, have no enum names, so name their rows by id. The row must be one the game has: a patch never adds a row. To add one, see [Rows](ROWS.md).

Every other line is `<field path> = <value>`. A value is checked against the field's type: a whole number in range for ids and counts (some, such as an item's action value, can be negative), any number for decimals, `0`, `1`, `true` or `false` for a flag, and one line of text for a text. A field that refers to the item, icon or flag table, such as `itemID`, `iconID` or `condOnFlag.0`, also takes a mod item, mod icon or mod flag as `Mod.Key`.

```ini
[patch.texture.<id>]
isNoCompress = 1
comment = Kept uncompressed by My Mod
```

A row appears once per mod. A second section for the same row in the same mod is refused, also when one names the row by id and the other by enum name.

## Field paths

The names are the game's own, read from the game's data definitions, the same ones [rows](ROWS.md) use, without case. A path names one place in the row, or several at once:

| Path | Places |
|---|---|
| `width` | The field. |
| `comment` | The text. |
| `fileName.en` | One language of a per-language text, by key (`ja`, `en`, `fr`, `es`, `zh-tw`, `ko`, `zh-cn`) or by index (`0` to `6`). |
| `fileName` | Every language of it. |
| `drawInfo.0.w` | One element of an array: the field `w` of element 0. |
| `drawInfo.w` | That field in every element of the array. |
| `help.1` | An element that is one value, such as the second of an item's three help texts, has no field name. |
| `stockInfo.restock` | A field of a group of fields, here a shop entry's restock. |
| `entries.2.texture` | One element of a list. A list always needs the index. |
| `feature.0` | The first value of a list of single values, such as an item's features. |
| `action.effect.1.value.0` | Paths go as deep as the row: the first value of the second effect of an item's action. |

A path that names several places sets them all to the value. Each place is then patched on its own: two mods that write `drawInfo.w` and `drawInfo.0.w` meet on `drawInfo.0.w`, and the log names that place.

A list, such as a skin's `entries` or a shop entry's `condOnFlag`, holds a different number of elements in each row. A patch only reaches the elements the row already has: `entries.5.name` on a row with three entries is skipped for that row, and the log says so. A list's element count, such as `entries.count`, cannot be patched.

Some parts of a row have no name in the game's definitions, such as the four words of a shop entry's `randomInfo`. Canopy lists them by their byte offset, `randomInfo.value4`, so a row can be read in full, and refuses to patch them.

## Tables

The tables that take patches, by file name without `.dat`:

`action`, `anime`, `animebank`, `animeinstancing`, `animereplacetexture`, `archive`, `attack`, `attackfeature`, `bgm`, `birddroptable`, `blacksmith`, `bu`, `bundle`, `bundlegroup`, `calc`, `capture`, `character`, `charactermove`, `characterpresent`, `characterschedule`, `charactertalk`, `cmd`, `construction`, `cooking`, `craft`, `crafttab`, `creature`, `creaturefeature`, `credit`, `crops`, `cropsform`, `cropsharvest`, `cropsplacement`, `dailyresetvalue`, `debugconfig`, `debugconfigcategory`, `demofeature`, `dyeing`, `environmentse`, `event`, `eventfeature`, `facility`, `facilityrelease`, `farmland`, `fertilizer`, `fish`, `fishing`, `font`, `framebuffer`, `gachapon`, `gamedefine`, `gameeffect`, `gameeffectcondition`, `gameeffecteffect`, `gameeffectfeature`, `gameeffecttarget`, `gameeffectvalue`, `gameflag`, `gameidvalue`, `gamesetting`, `gamesettinglist`, `gametablevalue`, `gamevalue`, `gimmick`, `gimmickcompatibility`, `gimmickfeature`, `gimmickprocess`, `hsvreplacetexture`, `huntreward`, `icon`, `intangeble`, `item`, `itemcategory`, `itemfeature`, `itemtype`, `latenightevent`, `letter`, `lettersend`, `livestock`, `livestockfeature`, `livestockplacement`, `livingtank`, `map`, `mapdiff`, `mapfeature`, `mapgroup`, `mapinterior`, `mapjudge`, `naturaloccurrence`, `networkdefine`, `number`, `offeringitem`, `pad`, `placementcreature`, `placementhorror`, `placementitem`, `playerbackaccessory`, `playerbackhairstyle`, `playercrothing`, `playerfronthairstyle`, `playerheadaccessory`, `playerneckaccessory`, `quest`, `randomsale`, `reminiscence`, `renderstep`, `scripttype`, `se`, `secretquest`, `shader`, `shootgamereward`, `skillspherecost`, `skilltree`, `storesales`, `string`, `subquestitem`, `talk`, `talkcommand`, `talknameplate`, `texture`, `tips`, `treasurebox`, `trophy`, `updatelevel`, `updatetext`, `vkey`, `vkeycategory`, `weather`, `weatherfeature`, `weatherschedule`, `whistle`, `window` and `zdefine`.

[Canopy Studio](../README.md#canopy-studio)'s game browser shows every one of them with its fields, and `canopy validate` checks a patch against them.

`texture`, `icon` and `animereplacetexture` also take [mod art](ART.md) rows; their layouts are written by hand and carry the extra checks art needs. Every other layout is generated from the game's own data definitions, and every row the game ships, in both files of each table, is read through it end to end before Canopy accepts it.

A field refers to another table, and is checked against it, when its name names that table and the game's rows agree: `itemID` refers to `item`, `type` in `item` to `itemtype`, a name ending in `Flag` to `gameflag`. Every other number is a plain number.

## What cannot be patched

- **Ids and enum names.** `id` and `enumName` are refused: other tables, saves and scripts find the row by them.
- **A row's build fields.** `version`, `disableVersion`, `region`, `product` and `platform` tell the game which builds a row is for. Changing them could hide the row, so they stay as the game has them.
- **Row sizes.** A patch never adds or removes list elements, and never grows a row. Element counts stay as the game has them.
- **Fields Canopy does not know.** Only the fields of a table's layout can be named. The parts the game's definitions leave unnamed, listed as `value<offset>`, and the trailer every row ends with are refused. Canopy does not guess at bytes the game does not name.
- **Rows the game does not have.** A patch whose row is in no table is skipped, and the log says so once.

## Several mods

Mods load in folder name order, without case, and turned-off mods are left out. For each place in a row, the last mod that sets it wins:

```ini
; mods\BigIcons\canopy.ini
[patch.icon.1000]
drawInfo.0.drawW = 96

; mods\TidyIcons\canopy.ini
[patch.icon.1000]
drawInfo.0.drawW = 80
drawInfo.0.offsetX = 4
```

`TidyIcons` comes after `BigIcons`, so the icon is drawn 80 wide with an offset of 4, and the log says:

`Patch conflict: BigIcons and TidyIcons both set icon.dat 1000 drawInfo.0.drawW. TidyIcons wins, it comes later in load order.`

When the two mods set different fields of a row, every field applies and nothing is logged. To change which mod wins, rename a folder; this is the same rule as [replacing game files](MODDING.md#replace-game-files).

A patch applies to whichever of the table's two files holds the row: a table can come in a small `framework_<table>.dat`, read early, and the main `<table>.dat`.

Anything else that starts from a vanilla row sees the patched one. A [mod row](ROWS.md) or a [custom item](ITEMS.md) that copies a patched row as its base starts from the patched values. Elements that [catches, forage entries](CATCHES.md) and [offerings](SHOPS.md) add to a list come after the patch, so an element index in a patch always names a vanilla element.

## What the log says

| Line | Meaning |
|---|---|
| `Patches: N fields on M rows of T tables, from J mods.` | The patches were read and merged at startup. |
| `Mod <name> (<key>, folder <folder>) patches N fields on M rows, registered for its patches.` | A mod with patches and nothing else Canopy registers is now in the mod list. A mod that also has items, art or a script says `patches N fields on M rows.` instead. |
| `Patch conflict: A and B both set <table> <row> <field>. B wins, it comes later in load order.` | Two mods set one place. B's value is used. |
| `Patches: icon.dat gets N fields on M rows.` | The game loaded the table with the patches written in. `framework_icon.dat` is the table's early file. |
| `Patch <table> <row> skipped, the game's table has no such row.` | Neither file of the table has the row. Logged once. |
| `[<Mod>] Patch <table> <row>, <field> skipped, <why>.` | The field was refused, the rest of the section still applies. |
| `[<Mod>] Patch <table>.<row> skipped, ...` | The whole section was refused: unknown table, a row that is neither an id nor an enum name, or the same row twice in one mod. |
| `Patch self-check: N fields match, M differ, K cannot be checked.` | At the title, Canopy read the patched fields back from the game's own records. `match` means the game holds the patched value. |
| `Patch self-check: <table> <id> <field> is X in the game, not Y.` | The game holds another value than the patch. Something else changed the field after the load, or the field's place in the game's record is wrong. |
| `Patches: the game tables were never seen loading, so no patch applied.` | Canopy started after the tables loaded. Nothing was patched. |
| `No patches, so patches are off.` | No mod has a `[patch.]` section. |

Texts, list elements and fields whose place in the game's record is not mapped count as `cannot be checked`. They are still patched; the title check just cannot read them back.

## Checking a patch

`canopy validate` checks patches the way Canopy reads them at startup, without starting the game. With a game folder it also checks every row against the game's tables and every list element against the row's own count:

```
canopy validate --game "D:\SteamLibrary\steamapps\common\Village in the Shade"
```

Each problem is reported at its `canopy.ini` line, with the same text the log would show.

## Known limitations

- Only the [tables](#tables) Canopy has a layout for take patches. Of the tables the game ships with rows, these have none yet: `actioneffect`, `cropsfeature`, `demolist`, `discover`, `footselist`, `livestockstyle`, `logtext`, `mapgroundelement`, `padtable` and `talkdefine` have no data definition by their name, and the rows of `debuggameprogress`, `dungeon`, `lostbook` and `multilinetext` do not read through theirs end to end.
- To match a row named by its enum name with the same row named by id, Canopy reads the game's tables from its archives at startup, only when some patch names a row by enum name. If the archives cannot be read, the log says so and such a row is found as its table loads: when one mod names it by enum name and another by id and both set the same field, the later mod still wins, but without a conflict line.
- Canopy must start before the game's init, the normal case. A late start leaves every table vanilla.
- A patched value takes effect on the next start. Nothing is changed while the game runs.
