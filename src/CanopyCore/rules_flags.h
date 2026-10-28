#pragma once
#include <string>
#include <vector>

#include "mod_rules.h"

// The rules of [flag.<Key>] sections. See docs/FLAGS.md.
namespace mod_rules {

struct FlagSpec {
    std::string modKey;
    std::string key;
    // Mod.Key, as the registry and the files write it.
    std::string fullKey;
    // Starts on in a new game, and in a loaded save with no line for it.
    bool defaultOn = false;
};

extern const SectionInfo kFlagSection;

// on, true, yes and 1 are on; off, false, no, 0 and empty are off. Any other
// text returns false and leaves on set to off.
bool ParseSwitch(const std::string& text, bool& on);
// The folders that have a [flag.X] section, in their order. A bare [flag.]
// counts too, so that its error is reported.
std::vector<ModFolder> FlagFolders(const std::vector<ModFolder>& folders);
// Reads every [flag.X] section of folders, in folder and file order, as the
// loader installs them: per mod key, the first folder owns it; per mod, keys
// are unique. modsDir and refs are unused, since a flag names no file and no
// other part; they keep the shape of the other readers. accept sees each
// folder with usable flags.
std::vector<FlagSpec> ReadFlags(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
);

// The desktop check: reads the flags, then registers each Mod.Key in
// context.refs as a flag.
void CheckFlags(Context& context);

}  // namespace mod_rules
