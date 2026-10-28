#include "rules_mod.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "platform_files.h"

// =============================================================
// LUA MOD RULES
// =============================================================
// What a Lua mod's canopy.ini accepts: its [mod] section, and each
// [setting.<Key>] section, as the Lua host reads them at startup. Every
// message is the loader's log line, in the order the loader logs it.
//
// The settings store checks each setting it is given and logs the refusals it
// makes itself. It is shared with DLL mods, so those checks stay there. The
// loader's log lines for them come where the store meets the setting.

namespace mod_rules {
namespace {

constexpr const char* kSettingPrefix = "setting.";
constexpr size_t kSettingPrefixLength = 8;
constexpr const char* kDefaultVersion = "0.0.0";
constexpr const char* kDefaultAuthor = "unknown author";

const FieldInfo kModFields[] = {
    {"key", ValueKind::Text, false,
     "The mod's key: 1 to 32 letters, digits, '_' or '-'. Defaults to the folder name. A DLL mod with the same key is joined."},
    {"name", ValueKind::Text, false, "The name on the Mod Settings page and in the log. Defaults to the key."},
    {"author", ValueKind::Text, false, "Shown in the log.", "unknown author"},
    {"version", ValueKind::Text, false, "Shown in the log.", "0.0.0"},
    {"unsafe", ValueKind::Switch, false,
     "true gives the mod full game access: it can change story flags, quests and saves.", "false"},
};

const FieldInfo kSettingFields[] = {
    {"type", ValueKind::Choice, false,
     "The kind of setting: a toggle, a choice of values, or a slider of whole numbers.", "toggle", nullptr, 0, 0,
     "toggle|choice|slider"},
    {"label", ValueKind::Text, false, "The row's text on the Mod Settings page. Defaults to the setting's key."},
    {"help", ValueKind::Text, false, "The help line under the list."},
    {"default", ValueKind::Other, false,
     "The starting value: true or false for a toggle, a whole number for a slider, a choice's text or index for a choice. Defaults to off, the first choice, or min."},
    {"min", ValueKind::Integer, false, "A slider's lowest value, a whole number. Needed for a slider."},
    {"max", ValueKind::Integer, false, "A slider's highest value, a whole number. Needed for a slider."},
    {"step", ValueKind::Integer, false, "A slider's step, a whole number. Defaults to 1.", "1"},
    {"choices", ValueKind::Other, false, "A choice's values, separated by '|'. Two to eight are needed."},
};

// A Lua mod runs from its folder's lua\main.lua, which is a file.
bool HasLuaScript(const std::string& folderDir) {
    return platform::IsFile(platform::Join(platform::Join(folderDir, "lua"), "main.lua"));
}

// A whole number that reads back as it was written, so "007" and "-0" are not numbers.
// An optional '-' comes first, then digits, within the int32 range.
bool ParseWhole(const std::string& text, int32_t& value) {
    if (text.empty() || text.size() > 11) {
        return false;
    }
    size_t index = text[0] == '-' ? 1 : 0;
    if (index == text.size()) {
        return false;
    }
    int64_t number = 0;
    for (; index < text.size(); ++index) {
        if (text[index] < '0' || text[index] > '9') {
            return false;
        }
        number = number * 10 + (text[index] - '0');
    }
    if (text[0] == '-') {
        number = -number;
    }
    if (number < INT32_MIN || number > INT32_MAX || std::to_string(number) != text) {
        return false;
    }
    value = static_cast<int32_t>(number);
    return true;
}

// ParseWhole, or ParseWhole of the text after a leading '+'.
bool ParseNumber(const std::string& text, int32_t& value) {
    if (ParseWhole(text, value)) {
        return true;
    }
    return text.size() > 1 && text[0] == '+' && ParseWhole(text.substr(1), value);
}

// The values of a choice: separated by '|', each trimmed, with no empty ones.
std::vector<std::string> SplitChoices(const std::string& text) {
    std::vector<std::string> choices;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('|', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string choice = canopy_ini::Trim(text.substr(start, end - start));
        if (!choice.empty()) {
            choices.push_back(choice);
        }
        start = end + 1;
    }
    return choices;
}

// Whether the section is a setting. A section named just "setting." is one,
// and its key is empty.
bool IsSettingSection(const canopy_ini::IniSection& section) {
    return canopy_ini::Lower(section.name).compare(0, kSettingPrefixLength, kSettingPrefix) == 0;
}

// The key of a setting section: the text after "setting.".
std::string SettingKey(const canopy_ini::IniSection& section) {
    return section.name.substr(kSettingPrefixLength);
}

// Reads a slider's range, step and starting value. Reports and returns false
// when they are not whole numbers.
bool ReadSlider(
    const std::string& where, const std::string& folder, const canopy_ini::IniSection& section,
    const std::string& defaultText, Report& report, SettingSpec& setting
) {
    const char* broken = nullptr;
    if (!ParseNumber(canopy_ini::Value(&section, "min", ""), setting.min)) {
        broken = "min";
    } else if (!ParseNumber(canopy_ini::Value(&section, "max", ""), setting.max)) {
        broken = "max";
    } else if (!ParseNumber(canopy_ini::Value(&section, "step", "1"), setting.step)) {
        broken = "step";
    }
    if (broken) {
        report.Error(
            folder, canopy_ini::ValueLine(&section, broken),
            where + "a slider needs whole numbers for min, max and step."
        );
        return false;
    }
    setting.start = setting.min;
    if (!defaultText.empty() && !ParseNumber(defaultText, setting.start)) {
        report.Error(folder, canopy_ini::ValueLine(&section, "default"), where + "its default is not a whole number.");
        return false;
    }
    return true;
}

// Reads a choice's values and the index it starts at. The index may be any
// whole number that is not a value's text; the store checks the range.
bool ReadChoice(
    const std::string& where, const std::string& folder, const canopy_ini::IniSection& section,
    const std::string& defaultText, Report& report, SettingSpec& setting
) {
    setting.choices = SplitChoices(canopy_ini::Value(&section, "choices", ""));
    setting.start = 0;
    if (defaultText.empty()) {
        return true;
    }
    const auto found = std::find(setting.choices.begin(), setting.choices.end(), defaultText);
    if (found != setting.choices.end()) {
        setting.start = static_cast<int32_t>(found - setting.choices.begin());
        return true;
    }
    if (!ParseNumber(defaultText, setting.start)) {
        report.Error(
            folder, canopy_ini::ValueLine(&section, "default"), where + "its default is not one of its choices."
        );
        return false;
    }
    return true;
}

// Reads one [setting.<Key>] section. Reports, and returns false, when the
// setting breaks a rule here; the store then never sees it.
bool ReadSetting(
    const std::string& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    SettingSpec& setting
) {
    setting.key = SettingKey(section);
    setting.line = section.line;
    setting.label = canopy_ini::Value(&section, "label", setting.key);
    setting.help = canopy_ini::Value(&section, "help", "");
    const std::string where = "[" + modKey + "] Setting " + setting.key + " skipped, ";
    const std::string defaultText = canopy_ini::Value(&section, "default", "");
    const std::string type = canopy_ini::Lower(canopy_ini::Value(&section, "type", "toggle"));
    if (type == "toggle") {
        setting.type = SettingType::Toggle;
        setting.on = canopy_ini::Truthy(defaultText);
        return true;
    }
    if (type == "slider") {
        setting.type = SettingType::Slider;
        return ReadSlider(where, folder, section, defaultText, report, setting);
    }
    if (type == "choice") {
        setting.type = SettingType::Choice;
        return ReadChoice(where, folder, section, defaultText, report, setting);
    }
    report.Error(
        folder, canopy_ini::ValueLine(&section, "type"), where + "its type must be toggle, choice or slider."
    );
    return false;
}

// Accepts every mod and setting, for the desktop check.
ModAccept AcceptAllMods() {
    ModAccept accept;
    accept.attach = [](const ModSpec&) { return true; };
    accept.setting = [](const ModSpec&, const SettingSpec&) { return true; };
    accept.registered = [](const ModSpec&) {};
    return accept;
}

}  // namespace

const SectionInfo kModSection = {"mod", "Mod", "docs/LUA.md", kModFields, std::size(kModFields)};
const SectionInfo kSettingSection = {
    kSettingPrefix, "Setting", "docs/LUA.md", kSettingFields, std::size(kSettingFields),
};

std::vector<ModFolder> WithLuaScripts(const std::vector<ModFolder>& folders, const std::string& modsDir) {
    std::vector<ModFolder> wanted;
    for (const ModFolder& folder : folders) {
        if (HasLuaScript(FolderDir(modsDir, folder))) {
            wanted.push_back(folder);
        }
    }
    return wanted;
}

std::vector<ModSpec> ReadMods(const std::vector<ModFolder>& folders, Report& report, const ModAccept& accept) {
    std::vector<ModSpec> mods;
    for (const ModFolder& folder : folders) {
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        ModSpec mod;
        mod.folder = folder.name;
        mod.key = ModKey(folder);
        mod.name = ModName(folder, mod.key);
        mod.version = canopy_ini::Value(info, "version", kDefaultVersion);
        mod.author = canopy_ini::Value(info, "author", kDefaultAuthor);
        mod.unsafe = canopy_ini::Truthy(canopy_ini::Value(info, "unsafe", "false"));
        mod.line = info ? info->line : 0;
        const int keyLine = canopy_ini::ValueLine(info, "key");
        if (!KeyValid(mod.key)) {
            report.Error(
                folder.name, keyLine,
                "Lua mod in folder " + folder.name + " skipped, its key " + mod.key +
                    " must be 1 to 32 letters, digits, '_' or '-'. Set key in its canopy.ini."
            );
            continue;
        }
        const auto used = std::find_if(mods.begin(), mods.end(), [&mod](const ModSpec& other) {
            return platform::CompareNoCase(other.key.c_str(), mod.key.c_str()) == 0;
        });
        if (used != mods.end()) {
            report.Error(
                folder.name, keyLine,
                "Lua mod in folder " + folder.name + " skipped, its key " + mod.key +
                    " is already used by folder " + used->folder + "."
            );
            continue;
        }
        if (!accept.attach(mod)) {
            report.Error(
                folder.name, mod.line, "Lua mod in folder " + folder.name + " skipped, it could not be registered."
            );
            continue;
        }
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (!IsSettingSection(section)) {
                continue;
            }
            SettingSpec setting;
            if (!ReadSetting(folder.name, mod.key, section, report, setting) || !accept.setting(mod, setting)) {
                continue;
            }
            mod.settings.push_back(std::move(setting));
        }
        accept.registered(mod);
        mods.push_back(std::move(mod));
    }
    return mods;
}

void CheckMods(Context& context) {
    ReadMods(WithLuaScripts(context.folders, context.modsDir), context.report, AcceptAllMods());
}

}  // namespace mod_rules
