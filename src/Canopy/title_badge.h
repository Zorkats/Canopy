#pragma once

// Two lines under the game's version on the title screen: Canopy's version
// and how many mods are installed. Drawn by the game's own text widget, so
// they use its font and fade with the title.
namespace title_badge {

// Hooks the title's init. False on an unsupported game build, where the
// title shows only the game's version.
bool Install();

}  // namespace title_badge
