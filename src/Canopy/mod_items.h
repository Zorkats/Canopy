#pragma once
#include <cstdint>
#include <string>

#include "canopy_api.h"

// Custom items: items a mod defines in its canopy.ini, appended to the
// game's item table at boot, given to the player through the Lua API, and
// kept out of the game's save. See the design notes in mod_items.cpp.
namespace mod_items {

// Reads every mod folder's [item.X] sections and, when there are any,
// hooks the item table load and the side effects of adding an item, and
// joins the overlay save. Startup thread, after the Lua host and before the settings store is
// sealed. ticking, saveData and beforeGameInit tell whether the frame tick,
// the per-save data and the early start exist; custom items need all three.
// False, with nothing hooked, when there are no items or they cannot work.
bool Install(bool ticking, bool saveData, bool beforeGameInit);

// Retries items that found no room. Game thread, once per frame, after the
// overlay save's tick and before the frame's events.
void Tick();

// The frame's game events. Game thread, after Tick.
void OnEvent(CanopyEvent event);

// One canopy.items call from a Lua mod: action is give, count, id or
// on_use, which checks that the item is a script item. The reply is
// "i<number>", "r<reason>" when give added nothing, or "e<message>" for a
// Lua error. Game thread.
std::string LuaRequest(const std::string& modKey, const char* action, const char* item, const char* count);

// The id of the mod item Mod.Key, 0 when there is none. Startup thread,
// after Install.
uint64_t IdOf(const std::string& fullKey);

// The place item of the mod object Mod.Key, 0 when no item places it.
// Startup thread, after Install.
uint64_t PlaceItemFor(const std::string& objectKey);

// Makes the mod item id a seed of the crop id: its row is built with the
// Plant action and the SEED category, unless it sets its own. False, with
// why, when id is not a mod item or has a use of its own. Startup thread,
// after Install, before the item table loads.
bool MakeSeed(uint64_t id, uint64_t crop, std::string& why);
// True when name is the module of a script item, which the script loader
// hook serves. Any thread.
bool OwnsModule(const char* name);

// True when id is in the mod item band, on or not. Any thread.
bool IsModItem(uint64_t id);

// False when id is a mod item id that is not on: unknown, or not found at
// the title. True for every other id. Game thread, after the title.
bool Usable(uint64_t id);

}  // namespace mod_items
