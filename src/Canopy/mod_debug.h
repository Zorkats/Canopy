#pragma once

// The game's debug script commands, brought back: what scripts print for
// debugging goes to Canopy.log, and the debug configs shipped scripts read
// take their values from a Canopy debug entry on the Mod Settings page.
namespace mod_debug {

// Hooks the five debug commands and registers their settings. False on an
// unsupported game build, or when the settings entry cannot be registered,
// where every command stays as retail. Startup thread, after the mods
// register and before mod_config::Seal.
bool Install();

}  // namespace mod_debug
