#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "canopy_api.h"

// Loads the mods in the game's mods folder and serves them the API in
// sdk/canopy_api.h. See the design notes in mod_loader.cpp.
namespace mod_loader {

// Loads every DLL in the mods folder, then starts each one. A dependency in a
// manifest starts before the mod that names it. Otherwise file name order
// decides, and that is also the order for mods without a manifest.
// ticking and exitHooked tell whether the frame and exit hooks exist, so
// OnFrame and OnExit can work. Startup thread, before hooks are enabled.
void LoadAll(bool ticking, bool exitHooked);

// The hooks mods created and have not removed.
size_t HookCount();

// The name of every started mod: a DLL's file name without .dll, and the
// folder of a Lua, item, flag or villager mod. A folder that joined a DLL
// mod by key is not listed again. Startup or game thread, after LoadAll.
std::vector<std::string> StartedModNames();

// Marks startup as done: hooks a mod creates from now on are enabled at once.
void Started();

// Backs out every mod when hooks could not be enabled: calls each one's
// CanopyModShutdown and removes every hook mods created.
void ShutdownAll();

// Reports the frame's game events, a closed Yes/No popup's answer, then runs
// every mod's frame callbacks. Game thread, once per frame.
void Tick();

// Runs the callback of the button row showing setting. Game thread, from the
// options menu's tick.
void PressButton(int setting);

// Runs every mod's exit callbacks, once. Game thread, as the game closes.
void Exit();

// ---- For the Lua host ----------------------------------------------------

// Registers a Lua mod, or joins the DLL mod that already has its key, so its
// settings belong to that mod. source names it in the log, the way a DLL's
// file does. Startup thread, after LoadAll and before the settings store is
// sealed. CANOPY_NO_MOD when it cannot be registered.
CanopyMod AttachLuaMod(const char* key, const char* name, const char* source, bool& joined);

// Adds a row that only shows text, such as a warning, to a mod's page.
void AddNoticeRow(CanopyMod mod, const char* key, const char* text);

// The API table every mod gets, built by LoadAll.
const CanopyApi* Api();

}  // namespace mod_loader
