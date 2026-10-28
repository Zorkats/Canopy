#pragma once
#include "canopy_api.h"

// Custom recipes: craft and cooking recipes a mod defines in its
// canopy.ini, appended to the game's recipe tables at boot, with mod items
// or vanilla items on both sides, and kept out of the save's favorite lists.
// See the design notes in mod_recipes.cpp.
namespace mod_recipes {

// Reads every mod folder's [craft.X] and [cooking.X] sections and, when
// there are any, hooks the two recipe table loads and joins the overlay
// save. Startup thread, after mod items and mod flags and before the
// settings store is sealed. ticking, saveData and beforeGameInit tell
// whether the frame tick, the per-save data and the early start exist;
// custom recipes need all three. False, with nothing hooked, when there are
// no recipes or they cannot work.
bool Install(bool ticking, bool saveData, bool beforeGameInit);

// The frame's game events. Game thread, after mod items hear of them.
void OnEvent(CanopyEvent event);

}  // namespace mod_recipes
