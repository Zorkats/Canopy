#pragma once

#include "canopy_api.h"

// Applies the [patch.<table>.<id>] sections of the mods folder to the game's
// own rows as their tables load, through the table engine, and checks at
// the title that the game holds the patched values. The rules live in
// CanopyCore's rules_patch, shared with the desktop check. See the design
// notes in mod_patches.cpp and docs/PATCHES.md.
namespace mod_patches {

// Reads and merges every folder's patches and, when there are any, has the
// table engine hand their tables over as they load. Startup thread, from the
// game init detour, after mod art, items and flags, whose Mod.Key a patch
// may name. False, with nothing hooked, when there are no usable patches or
// they cannot work.
bool Install(bool ticking, bool beforeGameInit);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

}  // namespace mod_patches
