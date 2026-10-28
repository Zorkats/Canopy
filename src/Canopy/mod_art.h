#pragma once
#include <cstdint>
#include <string>

#include "canopy_api.h"

// Mod art: textures, icons and model texture swaps a mod defines in its
// canopy.ini, plus raw rows for any table with a schema, appended to the
// game's tables at boot through the table engine. See the design notes in
// mod_art.cpp.
namespace mod_art {

// Reads every mod folder's [texture.X], [icon.X], [skin.X] and
// [row.<table>.X] sections, converts the textures, gives every row its id
// and, when there are any, installs the table engine. Startup thread, from
// the game init detour, after file overrides and the Lua host and before
// mod objects, mod items and mod whistles, which name mod skins and icons. False, with nothing
// hooked, when there is no art or it cannot work.
bool Install(bool ticking, bool beforeGameInit);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

// The id of a mod texture, a [texture.X] or a [row.texture.X]: Key of
// modKey or Mod.Key. 0 when there is no such texture. Startup thread, after
// Install.
uint64_t TextureId(const std::string& modKey, const std::string& ref);

// The id of a mod icon, a [icon.X] or a [row.icon.X]: Key of modKey or
// Mod.Key. 0 when there is no such icon. Startup thread, after Install.
uint64_t IconId(const std::string& modKey, const std::string& ref);

// True once the icon table holds the mod icon id. Any thread.
bool IconAdded(uint64_t id);

// The id of a mod skin, a [skin.X] or a [row.animereplacetexture.X]: Key of
// modKey or Mod.Key. 0 when there is no such skin. Startup thread, after
// Install.
uint64_t SkinId(const std::string& modKey, const std::string& ref);

// True once the AnimeReplaceTexture table holds the mod skin id. Any thread.
bool SkinAdded(uint64_t id);

}  // namespace mod_art
