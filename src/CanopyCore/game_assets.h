#pragma once
#include <cstdint>
#include <string>
#include <vector>

// The game's own assets as the desktop tools show them: icons cut out of
// their textures, the names a picker lists in each language, and sounds as
// plain Ogg Vorbis. Everything is read from the user's install; nothing is
// written. Each JSON result has "abi", "ok" and "error". Each call opens the
// archives for itself and keeps nothing, so a tool can call from any thread.
namespace game_assets {

// The cell of icon.dat row iconId (the row's id, not its index), cut out of
// its texture: width x height BGRA8 pixels, rows top first. The texture is
// the one the game loads for the row's textureID, texture.dat's file for
// the language, and the cell is drawInfo's x, y, w and h rounded to whole
// pixels and clamped to the texture. Both are taken in English: every
// vanilla row has the same rect in all seven languages, and all but nine
// textures the same file. False, with error set, when the row, its texture
// or a non-empty cell cannot be found.
bool Icon(
    const std::wstring& gameFolder,
    uint32_t iconId,
    uint32_t& width,
    uint32_t& height,
    std::vector<uint8_t>& pixels,
    std::string& error
);

// Every icon in ids cut as Icon cuts it and saved as outFolder\<id>.png,
// replacing any file there, with each texture decoded once, so a tool can
// cache a whole picker in one call: {"abi", "ok", "error", "written",
// "problems"}. written counts the files saved; problems holds a sentence for
// each icon that could not be cut or saved, and the rest go on. A repeated
// id is done once. ok is false only when the game, its icon or texture
// table, or outFolder cannot be opened or made.
std::string IconsJson(const std::wstring& gameFolder, const std::vector<uint32_t>& ids, const std::wstring& outFolder);

// What a picker lists for kind, in language: {"abi", "ok", "error", "kind",
// "language", "entries": [{"id", "enum", "name", "icon", "group"}]}, one
// entry per table row in table order (gimmick lists only some rows).
// language is one of the canopy.ini codes "ja", "en", "fr", "es", "zh-tw",
// "ko", "zh-cn", which is also the order of the game's seven per-language
// texts. name is empty when the game has none in that language; icon is the
// icon.dat id or null; group is a number or null. Where each kind comes from:
//   item      item.dat: name at +32, icon at end-52, group the category
//             (itemcategory.dat id) at +320.
//   villager  character.dat: name at +24; no icon or group.
//   whistle   whistle.dat: name at +16, icon the score icon at +80.
//   craft     craft.dat (output item at +92) and cooking.dat (output at
//   cooking   +24): the game names and draws a recipe by its output item,
//             so name, icon and group are the output item's.
//   gimmick   gimmick.dat, furniture only: the rows whose item (read by
//             walking the row) places them. Name at +24, or the item's name
//             when the row has none; icon and group are the item's. Entries
//             are the furniture rows, not every row.
//   area      not supported: map.dat, the area table, has no per-language
//             name, so ok is false.
// An icon id is given only when icon.dat has it.
std::string NamesJson(const std::wstring& gameFolder, const std::string& kind, const std::string& language);

// The sound file gamePath, such as "data/bgm/x.nlsd", as the Ogg Vorbis
// stream inside it: the bytes from its first OggS page to the end of the
// file, every page checked. False, with error set, when it is not one.
//
// An .nlsd starts with a 24-byte header, read from every file the game
// ships (38 under data/bgm, 1068 under data/se):
//   +0   u32  the codec: 7 for Ogg Vorbis, 5 for a RIFF WAVE of MS ADPCM
//             (975 of the sound effects), which Sound refuses.
//   +4   u32  the bytes after the header.
//   +8   u16  the sample rate, 48000 in every file.
//   +10  u16  0 for mono and 1 for stereo, matching the stream's channels.
//   +12  u32  the samples per channel of the whole sound.
//   +16  u32  where the loop part starts, as a byte offset into the stream.
//   +20  u32  where the loop part ends, the same way; 0 and the stream size
//             when the sound does not loop.
// A looping Ogg sound is a chained stream of three links, intro, loop and
// outro, and +16 and +20 point at the first pages of the last two. Their
// lengths in samples add up to +12, and the intro's and the loop's end
// match the loop start and end samples in bgm.dat (+52 and +56).
bool Sound(
    const std::wstring& gameFolder,
    const std::string& gamePath,
    std::vector<uint8_t>& ogg,
    std::string& error
);

}  // namespace game_assets
