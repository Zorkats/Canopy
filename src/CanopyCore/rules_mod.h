#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mod_rules.h"

// The rules of a Lua mod's [mod] and [setting.<Key>] sections, read by the
// Lua host at startup and checked by the desktop tools. See docs/LUA.md.
namespace mod_rules {

// A toggle, a choice of values, or a slider of whole numbers.
enum class SettingType : uint8_t { Toggle, Choice, Slider };

// One [setting.<Key>] section, as the loader adds it to the settings store.
struct SettingSpec {
    // The text after "setting.", as written. Empty when the section has none,
    // which the store refuses.
    std::string key;
    int line = 0;
    SettingType type = SettingType::Toggle;
    std::string label;
    std::string help;
    // Toggle: whether it starts on.
    bool on = false;
    // Slider: its range and step. The store checks them.
    int32_t min = 0;
    int32_t max = 0;
    int32_t step = 1;
    // Slider: the value it starts at. Choice: the index it starts at, which
    // the store checks against the choices.
    int32_t start = 0;
    // Choice: its values, in order.
    std::vector<std::string> choices;
};

// One Lua mod: its folder, and the [mod] section's values with the loader's
// defaults. In the list ReadMods returns, settings holds the ones taken.
struct ModSpec {
    std::string folder;
    // key in [mod], or the folder name.
    std::string key;
    // name in [mod], or the key.
    std::string name;
    // version and author in [mod], or 0.0.0 and unknown author.
    std::string version;
    std::string author;
    bool unsafe = false;
    // The line of the [mod] header, 0 when the folder has no [mod] section.
    int line = 0;
    std::vector<SettingSpec> settings;
};

// What the loader does for each Lua mod, in the order ReadMods reads its
// folder: attach, then each setting that passed its checks, then registered.
// ReadMods calls them as it goes, so the loader's own log lines keep their
// place among the rules' reports. Every member must be set.
struct ModAccept {
    // Attaches the mod. False when it cannot be, and its folder is skipped.
    std::function<bool(const ModSpec& mod)> attach;
    // Adds one setting. False when the loader refuses it.
    std::function<bool(const ModSpec& mod, const SettingSpec& setting)> setting;
    // Every setting has been offered. mod.settings holds the ones taken.
    std::function<void(const ModSpec& mod)> registered;
};

extern const SectionInfo kModSection;
extern const SectionInfo kSettingSection;

// The folders among folders that have a lua\main.lua under modsDir, the game's
// mods folder, in the order given (FolderDir: under a folder's own dir when it
// has one). Those are the Lua mods the Lua host runs.
std::vector<ModFolder> WithLuaScripts(const std::vector<ModFolder>& folders, const std::string& modsDir);

// Reads the Lua mods among folders, as WithLuaScripts returns them. A mod's
// key must be usable and not already used by an earlier folder. Then the mod
// is attached, and each setting that passes its checks is offered to accept.
// Returns the mods, with the settings accept took.
std::vector<ModSpec> ReadMods(const std::vector<ModFolder>& folders, Report& report, const ModAccept& accept);

// The desktop check: reads the Lua mods of context.folders with everything
// accepted. Nothing else refers to a Lua mod or its settings, so no keys are
// registered in context.refs.
void CheckMods(Context& context);

}  // namespace mod_rules
