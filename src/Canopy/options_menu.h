#pragma once
#include <cstdint>

// The options menu's Return to Title row and Mod Settings page. See the
// design notes in options_menu.cpp.
namespace options_menu {

// Checks its signatures and creates its detours. False, leaving nothing
// behind, when the game build does not match.
bool Install();

// Called once per frame on the game thread. Does nothing unless installed.
void Tick();

// The open Mod Settings page, or null when it is closed or closing. Game
// thread only.
uint8_t* ActivePage();

}  // namespace options_menu
