#pragma once
#include <string>
#include <vector>

// Loose file overrides: a mod replaces a file under the game's data folder by
// shipping it at mods\<ModFolder>\data\<same relative path>. The game then
// reads the mod's file instead of the one in its archives. The mod folders
// are indexed once at startup, so a change takes effect on the next start.
namespace file_overrides {

// Indexes every mod folder's data files, logs a summary and a warning for each
// risky class, and hooks the file system's archive lookup and path builder.
// Must run before the game initializes its file system. False when no mod
// ships a data file, or on an unsupported game build, where every override is
// ignored.
bool Install();

// The mod folders that override at least one data file, in load order.
// Empty when overrides are off.
std::vector<std::string> OverrideFolders();

// Serves a file Canopy made, such as a converted mod texture, at gamePath
// (data/..., lowercase ASCII) from diskPath, credited to modName. Creates
// the hooks when no mod ships a data file. Generated files do not count
// as overrides in OverrideFolders. Startup thread, after Install and
// before the hooks are enabled. False when Install did not run or failed,
// the path is taken or too long, or the hooks cannot be made.
bool AddGeneratedFile(const std::string& gamePath, const std::wstring& diskPath, const std::string& modName);

}  // namespace file_overrides
