#pragma once

#include "canopy_api.h"

// Mod crops: the [crop.<Key>] rows mod_rows appends, their seed items, and
// planted mod crops in the save. The rules live in CanopyCore's rules_crops.
// See the design notes in mod_crops.cpp and docs/CROPS.md.
namespace mod_crops {

// Joins the overlay save and turns each crop's seed item into its seed.
// Startup thread, after mod_rows::Install, before tables load. False when
// there are no crops or they cannot work; the caller then turns every crop
// off with mod_rows::DisableCrops.
bool Install(bool ticking, bool saveData);

// The frame's game events. Game thread.
void OnEvent(CanopyEvent event);

}  // namespace mod_crops
