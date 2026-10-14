#pragma once

#include "canopy_api.h"

// The game events mods can get callbacks for: the field gaining and losing
// the player, a save finishing loading, and reaching the title screen.
namespace game_events {

// The most events one frame can report.
constexpr int kMaxPerFrame = 4;

// Hooks the title screen and the save load. False on an unsupported game
// build, where only the field events are reported.
bool Install();

// The events since the last call, in the order they happened, into events.
// Returns how many. Once per frame on the game thread.
int Poll(CanopyEvent (&events)[kMaxPerFrame]);

// True while a game runs: from a save load, or the field first gaining the
// player, until the title screen. As of the last Poll. Game thread only.
bool InGame();

}  // namespace game_events
