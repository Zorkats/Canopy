#pragma once
#include <cstdint>
#include <string>

#include "canopy_api.h"

// Mod objects: furniture a mod defines in its canopy.ini as a copy of a
// vanilla one, appended to the game's gimmick table at boot, placed by the
// mod item that names it, and saved as its vanilla clone. See the design
// notes in mod_objects.cpp.
namespace mod_objects {

// Reads every mod folder's [object.X] sections and, when there are any,
// gives each its id, adds the rows through the table engine and joins the
// overlay save. Startup thread, after mod art, whose skins objects name,
// and before mod items, whose place items name objects. ticking, saveData
// and beforeGameInit tell whether the frame tick, the per-save data and the
// early start exist; mod objects need all three. False, with nothing
// hooked, when there are no objects or they cannot work.
bool Install(bool ticking, bool saveData, bool beforeGameInit);

// Learns which mod item places each object. Startup thread, after mod items'
// Install.
void LinkItems();

// The frame's game events. Game thread, after mod items hear them.
void OnEvent(CanopyEvent event);

// The id of the mod object Mod.Key, 0 when there is none. Startup thread,
// after Install.
uint64_t IdOf(const std::string& fullKey);

// The vanilla gimmick a mod object copies, 0 when id is no mod object. Any
// thread, after Install.
uint64_t CloneOf(uint64_t id);

}  // namespace mod_objects
