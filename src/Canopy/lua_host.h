#pragma once

#include <string>

#include "canopy_api.h"

// Runs Lua mods: folders in the mods folder with a lua\main.lua, each in its
// own script player of the game's Lua state, behind a sandbox and a guard on
// the commands that write the save. See the design notes in lua_host.cpp.
namespace lua_host {

// Finds the Lua mods, registers them and their canopy.ini settings with the
// settings store, and creates the two hooks the host needs. With no Lua mod,
// no frame tick or an unsupported game build it creates nothing and returns
// false. Startup thread, after mod_loader::LoadAll and before
// mod_config::Seal.
bool Install(bool ticking);

// Queues one game event for every Lua mod. Game thread, from the frame tick.
void OnEvent(CanopyEvent event);

// Queues event for the Lua mod modKey alone, such as "use:Key" when one of
// its script items is used. False when the mod has no running Lua. Game
// thread.
bool PostEvent(const std::string& modKey, const std::string& event);

// Creates the Lua mods' script players once the game's script root exists,
// then updates them. Game thread, once per frame, after the event
// callbacks.
void Tick();

// The thread name, <module>-<entry>, of the script whose command is running
// on this thread, or empty when none is. From inside a script command.
std::string RunningScript();

// Stops every Lua mod's script player, once. Game thread, as the game
// closes.
void Exit();

}  // namespace lua_host
