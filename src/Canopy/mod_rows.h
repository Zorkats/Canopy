#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "canopy_api.h"

// Mod rows: [row.<table>.<Key>] sections on the tables whose schema is
// generated from the game's serializers, each a copy of a vanilla row with
// fields set by path, appended to its table at boot through the table
// engine. The rules live in CanopyCore's rules_rows, shared with the desktop
// check. See the design notes in mod_rows.cpp and docs/ROWS.md.
namespace mod_rows {

// Reads every folder's generic rows, gives them their ids and, when there are
// any, has the table engine append them as their tables load. Startup
// thread, from the game init detour, after every module whose parts a row
// may name (art, objects, items, flags, villagers, whistles, recipes) and
// before patches. False, with nothing hooked, when there are no usable rows
// or they cannot work.
bool Install(bool ticking, bool beforeGameInit);

// True when Install took a [sale.] row, which mod_shops keeps out of the
// save. Startup thread, after Install.
bool HasSales();

// Leaves every [sale.] row out of its table, for when mod_shops cannot keep
// them out of the save. Startup thread, after Install, before tables load.
void DisableSales();

// A [crop.] section's crops and harvest rows, which share one id.
struct Crop {
    // Mod.Key.
    std::string fullKey;
    uint64_t id = 0;
    // The vanilla crop both rows copy.
    uint64_t baseId = 0;
    // The mod item that plants it, 0 for none.
    uint64_t seed = 0;
};

// Every crop Install took. Startup thread, after Install.
std::vector<Crop> Crops();

// True when Install took a crop. Startup thread, after Install.
bool HasCrops();

// Leaves the rows of the crop Mod.Key out of their tables, logging why.
// Startup thread, after Install, before tables load.
void DropCrop(const std::string& fullKey, const std::string& why);

// Leaves every crop out of its tables, for when mod crops cannot keep them
// out of the save. Startup thread, after Install, before tables load.
void DisableCrops();

// True when both rows of the crop id were appended. Game thread, at the
// title.
bool CropAppended(uint64_t id);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

}  // namespace mod_rows
