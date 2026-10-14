#pragma once

// The game's own UI that mods borrow through the API: the popup log, for
// toasts, and the Yes/No popup. Everything past Install runs on the game
// thread.
namespace game_ui {

// Hooks the popup log's text getter. False on an unsupported game build.
bool InstallToasts();
// Resolves the popup's render layer builder. False on an unsupported game
// build.
bool InstallPopups();

// Queues text in the game's popup log. Null on success, else why not.
const char* ShowToast(const char* text);

// Opens the Yes/No popup over the Mod Settings page when it is open, else
// over the field. Null on success, else why not. One popup at a time.
const char* OpenYesNo(const char* text);
// Advances the open popup. True once the engine let go of it, with yes the
// player's answer: false for No, Cancel, or a popup closed by something else.
bool PopupClosed(bool& yes);

}  // namespace game_ui
