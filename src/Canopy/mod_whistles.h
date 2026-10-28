#pragma once
#include <cstdint>
#include <string>

#include "canopy_api.h"

// Custom whistles: melodies a mod defines in its canopy.ini, appended to the
// game's whistle table at boot, learned through a flag and played with an
// optional text Lua script. See the design notes in mod_whistles.cpp.
namespace mod_whistles {

// Reads every mod folder's [whistle.X] sections and, when there are any,
// hooks the whistle table load and, for whistles with a script, the script
// module loader. Startup thread, after the Lua host, mod items and mod flags
// and before the settings store is sealed. ticking and beforeGameInit tell
// whether the frame tick and the early start exist; custom whistles need
// both. False, with nothing hooked, when there are no whistles or they
// cannot work.
bool Install(bool ticking, bool beforeGameInit);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

// True when name is the script module of a custom whistle, which this
// module serves. The Lua host's loader lets such names through. Game thread.
bool OwnsModule(const char* name);

// The id of the mod whistle fullKey (Mod.Key, without case), 0 when there is
// none. Startup thread, after Install.
uint64_t IdOf(const std::string& fullKey);

}  // namespace mod_whistles
