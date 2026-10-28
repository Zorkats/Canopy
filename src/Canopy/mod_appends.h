#pragma once

#include "canopy_api.h"

// List additions: [offering.<Key>], [catch.<Key>] and [forage.<Key>]
// sections, each adding elements to lists inside vanilla rows (offering
// pools, fishing spots, traps, forage spots, wild crop areas) as their tables
// load, through the table engine. The rules live in CanopyCore's
// rules_append, shared with the desktop check. See the design notes in
// mod_appends.cpp, docs/SHOPS.md and docs/CATCHES.md.
namespace mod_appends {

// Reads every folder's list additions and, when there are any, has the table
// engine hand their tables over as they load. Startup thread, from the game
// init detour, after every module whose parts an addition may name (items,
// flags) and after patches, so a list grows after its vanilla elements are
// patched. False, with nothing hooked, when there are none or they cannot
// work.
bool Install(bool ticking, bool beforeGameInit);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

}  // namespace mod_appends
