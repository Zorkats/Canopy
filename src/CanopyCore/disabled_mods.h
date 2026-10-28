#pragma once
#include <string>
#include <vector>

// The mods a player turned off without removing them, listed in
// mods\CanopyData\disabled.txt. Each line names one folder in mods or one
// DLL file in mods, such as VillageQoL.dll, matched without case. Blank
// lines and lines starting with # are ignored. A missing file turns
// nothing off. Canopy skips what the list names; Canopy Studio writes it.
//
// The file is UTF-8, with or without a byte order mark, and its lines end
// in CRLF or LF.
namespace disabled_mods {

// The names in modsDir\CanopyData\disabled.txt, as written, trimmed.
std::vector<std::string> Read(const std::string& modsDir);

// True when name, a folder or DLL file name in mods, is in list. name may be
// UTF-8 or in the system code page.
bool IsDisabled(const std::vector<std::string>& list, const std::string& name);

// The names in text, a whole disabled.txt: what Read does after it has the
// file's bytes.
std::vector<std::string> Parse(const std::string& text);

// Read, but the file is read again only when its size or time changed, so
// the loader's readers share one read per start and a desktop tool still
// sees an edit. Each mods folder has its own entry.
std::vector<std::string> Cached(const std::string& modsDir);

// The folders and DLL files in modsDir that the list names, as they are named
// on disk, in name order without case. A listed name with nothing behind it
// is not in the result.
std::vector<std::string> Existing(const std::string& modsDir);

}  // namespace disabled_mods
