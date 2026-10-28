#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "mod_rules.h"

// The rules of [villager.<Key>] sections. See docs/VILLAGERS.md.
namespace mod_rules {

// A villager as canopy.ini says it. The loader adds what installing it needs.
struct VillagerSpec {
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as the registry and the sidecars write it.
    std::string fullKey;
    int line = 0;
    uint64_t baseId = 0;
    std::string names[kLanguageCount];
    std::string jobs[kLanguageCount];
    std::string helps[kLanguageCount];
    // 0 keeps the base villager's starting area.
    uint64_t area = 0;
    bool hasX = false;
    bool hasY = false;
    int32_t x = 0;
    int32_t y = 0;
    bool startEnabled = true;
    // The Lua function the schedule runs, none for no routine.
    std::string routine = "none";
    // The script's path in the mod folder and its text; empty for none.
    std::string script;
    std::string source;
    // villager_<mod>_<key> when the villager has a script.
    std::string module;
    std::string chunkName;
};

constexpr size_t kMaxVillagers = 100;

extern const SectionInfo kVillagerSection;

// Reads every [villager.X] section of folders, in folder and file order, as
// the loader installs them: per mod key, the first folder owns it; per mod,
// keys and script module names are unique; across all mods, at most
// kMaxVillagers load. modsDir is the game's mods folder, where scripts are
// read from. accept sees each folder with usable villagers.
std::vector<VillagerSpec> ReadVillagers(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
);
// The folders with a [villager.X] section, bare or not. A bare section is
// refused with its message, so it has to reach the reader.
std::vector<ModFolder> WithVillagers(const std::vector<ModFolder>& folders);

// ---- The vanilla tables --------------------------------------------------

// The check against character.dat that the loader runs when it appends the
// rows: the base row is there with the expected layout. base is null when
// the table has no row baseId. False, reported, when the villager is skipped.
bool CheckAgainstCharacters(const VillagerSpec& villager, const blob::Row* base, Report& report);
// The check against characterschedule.dat: the base row is there. False,
// reported, when the villager has no schedule.
bool CheckAgainstSchedule(const VillagerSpec& villager, const blob::Row* base, Report& report);

// The desktop check: reads the villagers, registers their keys in
// context.refs, then checks them against the vanilla tables when there are.
void CheckVillagers(Context& context);

}  // namespace mod_rules
