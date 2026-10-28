#pragma once
#include <string>

// The desktop check of one mod being made, as Canopy Studio's Create area
// runs it while the user types. The project folder may live anywhere, not
// only in the game's mods folder, and its canopy.ini is the text in the
// editor, saved or not. The mods folder's other mods are read as Canopy
// reads them, so the project can name their parts (Mod.Key) and two mods
// claiming one key are still caught.
namespace mod_rules {

// Checks projectFolder with its canopy.ini read from iniText. gameFolder
// may be empty: vanilla ids are then not checked. modsFolder may be empty:
// gameFolder\mods is used, or no other mods when gameFolder is empty too. A
// folder in modsFolder with the project folder's name stands for the
// project and is not read. The result has the layout of ValidationJson in
// rules_report.h, with only the project's diagnostics: {"abi", "ok",
// "error", "game", "mods", "vanilla", "folders", "errors", "warnings",
// "notes", "diagnostics": [{"severity", "folder", "file", "line",
// "message"}]}. folder is the project folder's name; file is "canopy.ini"
// or the file inside the project a message is about.
std::string ValidateProjectJson(
    const std::wstring& gameFolder,
    const std::wstring& modsFolder,
    const std::wstring& projectFolder,
    const std::string& iniText
);

}  // namespace mod_rules
