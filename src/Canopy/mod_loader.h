#pragma once
#include <cstddef>

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

}  // namespace mod_loader
