#pragma once
#include <cstdint>
#include <string>

#include "canopy_api.h"

// Custom villagers: villagers a mod defines in its canopy.ini, appended to
// the game's character and schedule tables at boot, each with an optional
// text Lua AI module, and kept out of the game's save. See the design notes
// in mod_villagers.cpp.
namespace mod_villagers {

// Reads every mod folder's [villager.X] sections and, when there are any,
// hooks the character and schedule table loads and, for villagers with a
// script, the script module loader, and joins the overlay save. Startup
// thread, after the Lua host and before the settings store is sealed.
// ticking, saveData and beforeGameInit tell whether the frame tick, the
// per-save data and the early start exist; custom villagers need all three.
// False, with nothing hooked, when there are no villagers or they cannot
// work.
bool Install(bool ticking, bool saveData, bool beforeGameInit);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

// True when name is the script module of a custom villager, which this
// module serves. The Lua host's loader lets such names through. Game thread.
bool OwnsModule(const char* name);

// The id of the mod villager fullKey (Mod.Key, without case), 0 when there
// is none. Startup thread, after Install.
uint64_t IdOf(const std::string& fullKey);

}  // namespace mod_villagers
