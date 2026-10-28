#pragma once
#include <cstdint>
#include <string>

// Mod flags: on/off flags a mod declares in its canopy.ini, each a real bit
// of the game's flag set in the band 95000 to 99998, read and set through
// the Lua API, and kept out of the game's save. See the design notes in
// mod_flags.cpp.
namespace mod_flags {

// Reads every mod folder's [flag.X] sections and, when there are any,
// gives each its bit and joins the overlay save. Startup thread, after the
// Lua host and before the settings store is sealed. ticking, saveData and
// beforeGameInit tell whether the frame tick, the per-save data and the
// early start exist; mod flags need all three. False, with nothing hooked,
// when there are no flags or they cannot work.
bool Install(bool ticking, bool saveData, bool beforeGameInit);

// One canopy.flags call from a Lua mod: action is get, set or id, value is
// "1" or "0" for set. The reply is "i<number>", "r<reason>" when the flag
// cannot be read or set now, or "e<message>" for a Lua error. Game thread.
std::string LuaRequest(const std::string& modKey, const char* action, const char* flag, const char* value);

// The bit of the mod flag Mod.Key, 0 when there is none. Startup thread,
// after Install.
int32_t IdOf(const std::string& fullKey);

}  // namespace mod_flags
