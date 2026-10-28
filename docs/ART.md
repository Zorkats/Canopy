# Mod art

A mod can add its own textures, icons and model texture swaps to the game, declared in the mod's `canopy.ini`. A texture is a plain PNG in the mod folder: Canopy converts it to the game's texture format once and serves it to the game. An icon is a part of a texture the menus draw, and a [custom item](ITEMS.md) or a [custom whistle](WHISTLES.md) can show it. A skin is a copy of a vanilla model's texture swap set with some of its textures replaced.

Canopy adds these rows to the game's own tables while the game loads them, through one hook that serves every table. Nothing reaches the game's save.

New to Canopy? Custom items are explained in [ITEMS.md](ITEMS.md), and replacing game files in [Replace game files](MODDING.md#replace-game-files).

## Contents

- [Quick path](#quick-path)
- [Declaring a texture](#declaring-a-texture)
- [Declaring an icon](#declaring-an-icon)
- [Declaring a skin](#declaring-a-skin)
- [Raw rows](#raw-rows)
- [Naming textures](#naming-textures)
- [Using a mod icon](#using-a-mod-icon)
- [Ids](#ids)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\MyArt`.
2. Put a 128x128 PNG in it, such as `lantern.png`.
3. Put a `canopy.ini` next to it:

   ```ini
   [mod]
   name = My Art

   [texture.Lantern]
   file = lantern.png

   [icon.Lantern]
   texture = Lantern

   [item.Lantern]
   base = 230070
   name = Lantern
   icon = Lantern
   ```

4. Start the game and check `Canopy.log` for `Mod icon MyArt.Lantern is id ..., texture ..., rect 0, 0, 128, 128.`

The item now has the whole PNG as its icon.

## Declaring a texture

One `[texture.<key>]` section per texture. The key is 1 to 32 letters, digits, `_` or `-`, and unique among the mod's textures. The mod key is `key` in `[mod]`, or the folder name.

| Key | Required | Meaning |
|---|---|---|
| `file` | Yes | The image, as a path inside the mod folder such as `art/lantern.png`. A PNG, or any image Windows reads (BMP, JPEG, TIFF, GIF), up to 8192x8192. A `.nltx` file, the game's own texture format, is used as it is. |

An image is converted to `mods\CanopyData\textures\<Mod>\<Key>.nltx` the first time Canopy sees it, and again only when the image's size or modified time changes, so later starts skip the work. Canopy keeps a `<Key>.source` file next to it to know when. Both files can be deleted at any time: Canopy makes them again.

The game reads the texture as `data/texture/canopy/<mod>/<key>.nltx` (lowercase) in every language. If another mod ships a file at that path, the shipped file is kept and the texture is skipped.

## Declaring an icon

One `[icon.<key>]` section per icon, with the same key rules. An icon is a rectangle of a texture.

| Key | Required | Meaning |
|---|---|---|
| `texture` | Yes | The texture. See [Naming textures](#naming-textures). |
| `rect` | For a vanilla texture | `x, y, w, h` in pixels. The whole texture when left out, which works for a mod texture only, since Canopy knows its size. For a mod texture it must fit inside it. |
| `size` | No | `w, h`, the size the menus draw it at. The rect's `w, h` when left out. |
| `offset` | No | `x, y`, moves where it is drawn. `0, 0` when left out. |

The same rectangle is used in every language.

## Declaring a skin

One `[skin.<key>]` section per skin, with the same key rules. A skin is a row of the game's model texture swap table: a list of the model's texture names, each with the texture to draw instead.

| Key | Required | Meaning |
|---|---|---|
| `base` | No | The id of the vanilla swap row to copy. Its swaps are kept unless replaced. |
| `replace` | Yes | `<model texture name>: <texture>`, several separated by commas. The key can be repeated, one line per swap. A name the base has gets the new texture; a new name is added. |

```ini
[skin.Gold]
base = 171
replace = cap_01: Lantern
```

A skin is added to the table, but nothing in the game uses it yet: see [Known limitations](#known-limitations).

## Raw rows

A `[row.<table>.<key>]` section on `texture`, `icon` or `animereplacetexture` writes an art row field by field, for anything the sections above do not cover. Rows of the game's other tables are described in [ROWS.md](ROWS.md): they copy a vanilla row with `base`, take ids of their own, and some tables take none.

| Key | Meaning |
|---|---|
| `base` | Optional. The id of a vanilla row of the table to copy first. |
| Any field name | The field's value. The names are the game's own, without case: `textureID`, `drawInfo.x`, `entries.0.name`. |

```ini
[row.icon.Small]
textureID = Lantern
drawInfo.x = 0
drawInfo.y = 0
drawInfo.w = 64
drawInfo.h = 64
drawInfo.drawW = 64
drawInfo.drawH = 64
```

A field that names another table's row, such as `textureID`, takes a texture name like any other section. A field name the table does not have, a value of the wrong type, or a field that cannot be written skips the row, and the log says which. The id and enum name are always Canopy's. A raw row shares its table's keys and ids: `[row.icon.Small]` and `[icon.Small]` cannot both exist.

## Naming textures

A texture is one of:

- a texture of the same mod, such as `Lantern`;
- a texture of another mod, such as `OtherMod.Lantern`;
- a vanilla texture id, such as `1076`;
- a vanilla texture enum name, such as one starting with `TEX_ID_`.

A mod texture that is not installed skips the row when it is read. A vanilla texture is checked as soon as Canopy can: a skin's textures when the skin loads, since the texture table loads first, and an icon's texture at the title, since both tables load together. An icon whose vanilla texture is missing at the title is pointed at a vanilla texture that exists, so the game never draws a missing one, and the log says the icon stays off.

## Using a mod icon

- A [custom item](ITEMS.md): `icon = <icon>`, where the icon is a key of the same mod or `OtherMod.Key`. A number is still a vanilla icon id. The item uses the mod icon for both its icon and its outline.
- A [custom whistle](WHISTLES.md): `icon = <icon>` the same way. It takes the place of the score icon, and wins over `score` when both are set.

When the icon is off at load, the item or whistle keeps its base's icon and the log says so.

## Ids

Each row gets an id from a band of its table:

| Rows | Ids from | Registry | Enum names |
|---|---|---|---|
| Textures | 1,900,000,000 | `mods\CanopyData\textures.ini` | `TEX_ID_CANOPY_<MOD>_<KEY>` |
| Icons | 1,950,000,000 | `mods\CanopyData\icons.ini` | `ICON_ID_CANOPY_<MOD>_<KEY>` |
| Skins | 2,000,000,000 | `mods\CanopyData\skins.ini` | `ANIME_REP_ID_CANOPY_<MOD>_<KEY>` |

The first time Canopy sees a `<mod>.<key>`, it gives it the next free number of its table and writes it to the registry. The row keeps that id for good, and a number is never given out twice, even after the mod is removed. Do not edit or delete the registries. An enum name that is taken gets a number on the end.

## What the log says

| Line | Meaning |
|---|---|
| `Table engine installed.` | The one table hook is in. |
| `[<Mod>] Texture <Key>: converted <file> to mods\CanopyData\textures\<Mod>\<Key>.nltx, WxH.` | The image was converted. Missing on later starts while the image is unchanged. |
| `Mod art installed: N textures, M icons and K skins from J mods.` | The art was read, and every row has its id. |
| `Table engine: icon.dat gets N mod rows.` | The game loaded the rows at boot. Same for `texture.dat` and `animereplacetexture.dat`. |
| `Mod texture <Mod>.<Key> is id N, WxH, served from ...` | The game's own lookup found the texture with its file at the title. |
| `Mod icon <Mod>.<Key> is id N, texture T, rect x, y, w, h.` | The icon and its texture are in the game's tables. It works. |
| `Mod skin <Mod>.<Key> is id N, S texture swaps over E entries.` | The skin is in the game's table. |
| `Mod icon row <Mod>.<Key> is id N.` | A raw row is in the game's table. |
| `... skipped, ...` | The row was refused, and the line says why. |
| `... It stays off.` | The row failed its title check. |

## Known limitations

- No new 3D models. A skin only swaps textures on a vanilla model, and nothing in the game uses a new skin yet.
- A texture is read once per run: changing the PNG needs a restart.
- How the icons look in the game's menus has not been checked yet. The title check proves the rows are in the game's tables, not that they draw well.
- Canopy must start before the game's init, the normal case. A late start leaves mod art off.
