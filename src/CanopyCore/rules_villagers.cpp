#include "rules_villagers.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "platform_files.h"

// =============================================================
// VILLAGER RULES
// =============================================================
// What the loader's custom villagers accept from canopy.ini, moved here from
// mod_villagers.cpp so the desktop tools check a mod the same way. Every
// message is the loader's log line.

namespace mod_rules {
namespace {

constexpr uint64_t kBandBase = 1600000000;
constexpr uint64_t kBandSize = 100000000;
constexpr size_t kMaxNameLength = 200;
constexpr size_t kMaxHelpLength = 1000;
constexpr size_t kMaxScriptBytes = 1024 * 1024;
// save_data's reader refuses a file over 16 MB, so the script reader refuses
// it before reading it, as the loader always has.
constexpr uint64_t kMaxScriptFileBytes = 16 * 1024 * 1024;
constexpr const char* kDefaultRoutine = "none";
constexpr const char* kIgnoredHelp =
    "Not supported yet. The value is ignored and the villager keeps its base villager's.";
constexpr const char* kModulePrefix = "villager_";
constexpr const char* kPrefix = "villager.";
constexpr size_t kPrefixLength = 9;

// character.dat rows: 732 bytes plus 8 per forbidden feature, the count of
// them at 640, at most 4.
constexpr size_t kCharaRowBase = 732;
constexpr size_t kCharaRowForbidCount = 640;
constexpr size_t kForbidSize = 8;
constexpr uint32_t kMaxForbid = 4;
constexpr const char* kCharaFile = "character.dat";
constexpr const char* kScheduleFile = "characterschedule.dat";

const FieldInfo kFields[] = {
    {"base", ValueKind::VanillaId, true,
     "The vanilla villager to copy, by its id in character.dat. Its look, gifts and birthday stay the base's.",
     nullptr, kCharaFile},
    {"name", ValueKind::LangText, true,
     "The name the villager is shown with, one line of up to 200 bytes. name.<lang> sets it for one language."},
    {"job", ValueKind::LangText, false,
     "The job the villager is shown with, one line of up to 200 bytes, none when left out. job.<lang> sets it for one language."},
    {"help", ValueKind::LangText, false,
     "The villager's help text, one line of up to 1000 bytes. help.<lang> sets it for one language."},
    {"area", ValueKind::VanillaId, false, "The vanilla area the villager starts in.", "the base's"},
    {"x", ValueKind::Integer, false, "The starting x of the villager, added to its area's origin.", "the base's"},
    {"y", ValueKind::Integer, false, "The starting y of the villager, added to its area's origin.", "the base's"},
    {"enabled", ValueKind::Switch, false, "Whether the villager starts enabled in a new game.", "true"},
    {"routine", ValueKind::Other, false,
     "The Lua function of the villager's script that its schedule runs, none for no routine.", kDefaultRoutine},
    {"script", ValueKind::Path, false,
     "A .lua file in the mod folder, the villager's own script module. Left out, the villager runs its base villager's script."},
    {"birthday", ValueKind::Other, false, kIgnoredHelp},
    {"likes", ValueKind::Other, false, kIgnoredHelp},
    {"dislikes", ValueKind::Other, false, kIgnoredHelp},
    {"portrait", ValueKind::Other, false, kIgnoredHelp},
    {"anime", ValueKind::Other, false, kIgnoredHelp},
    {"icon", ValueKind::Other, false, kIgnoredHelp},
};

bool IsBand(uint64_t id) {
    return id >= kBandBase && id < kBandBase + kBandSize;
}

// A section whose name starts with villager., bare or not.
bool IsVillagerSection(const canopy_ini::IniSection& section) {
    return platform::CompareNoCase(section.name.c_str(), kPrefix, kPrefixLength) == 0;
}

bool HasVillagerSection(const ModFolder& folder) {
    return std::any_of(folder.sections.begin(), folder.sections.end(), &IsVillagerSection);
}

// Reads a script file, refusing one over 16 MB before reading it.
bool ReadScriptFile(const std::string& path, std::string& text) {
    return platform::ReadWholeFile(path, kMaxScriptFileBytes, text);
}

// The row of table whose id is id, null when there is none.
const blob::Row* FindRow(const blob::Table& table, uint64_t id) {
    const auto found = std::find_if(table.rows.begin(), table.rows.end(), [&](const blob::Row& row) {
        return blob::RowId(row) == id;
    });
    return found == table.rows.end() ? nullptr : &*found;
}

// Reads the villager's script text from folderDir, its mod folder. Reports
// and returns false when it cannot.
bool ReadScript(
    VillagerSpec& villager, const std::string& folderDir, const std::string& where, int line, Report& report
) {
    if (!ScriptPathValid(villager.script)) {
        report.Warning(
            villager.folder, line,
            where + " uses its base villager's script, script must be a .lua path inside the mod folder."
        );
        return false;
    }
    const std::string path = platform::Join(folderDir, platform::NativeSeparators(villager.script));
    std::string text;
    if (!ReadScriptFile(path, text)) {
        report.Warning(
            villager.folder, line,
            where + " uses its base villager's script, " + villager.script + " could not be read."
        );
        return false;
    }
    if (text.empty() || text.size() > kMaxScriptBytes || text[0] == '\x1B') {
        report.Warning(
            villager.folder, line,
            where + " uses its base villager's script, " + villager.script + " must be Lua text of up to 1 MB."
        );
        return false;
    }
    villager.source = std::move(text);
    villager.module = kModulePrefix + ModulePart(villager.modKey) + "_" + ModulePart(villager.key);
    villager.chunkName = "=" + villager.modKey + "/" + villager.script;
    return true;
}

// Reads one [villager.X] section. Reports and returns false when it is
// unusable.
bool ReadVillager(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section,
    const std::string& modsDir, Report& report, VillagerSpec& villager
) {
    villager.modKey = modKey;
    villager.folder = folder.name;
    villager.key = SectionKey(section, kPrefix);
    villager.fullKey = modKey + "." + villager.key;
    villager.line = section.line;
    const std::string where = "[" + modKey + "] Villager " + villager.key;
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    if (!KeyValid(villager.key)) {
        report.Error(
            folder.name, section.line, where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    if (!ParseU64(canopy_ini::Value(&section, "base", ""), villager.baseId) || villager.baseId == 0 ||
        IsBand(villager.baseId)) {
        report.Error(folder.name, at("base"), where + " skipped, it needs base = <vanilla villager id>.");
        return false;
    }
    ReadTexts(section, "name", villager.names);
    ReadTexts(section, "job", villager.jobs);
    ReadTexts(section, "help", villager.helps);
    for (int language = 0; language < kLanguageCount; ++language) {
        if (villager.names[language].empty()) {
            report.Error(folder.name, at("name"), where + " skipped, it needs name = <text>.");
            return false;
        }
        if (!TextValid(villager.names[language], kMaxNameLength) ||
            !TextValid(villager.jobs[language], kMaxNameLength) ||
            !TextValid(villager.helps[language], kMaxHelpLength)) {
            report.Error(
                folder.name, at("name"),
                where + " skipped, a name or job must be one line of up to 200 bytes and a help one line of up to 1000."
            );
            return false;
        }
    }
    const std::string area = canopy_ini::Value(&section, "area", "");
    if (!area.empty() && (!ParseU64(area, villager.area) || villager.area == 0)) {
        report.Warning(
            folder.name, at("area"), where + " keeps its base villager's area, area must be a vanilla area id."
        );
        villager.area = 0;
    }
    const std::string x = canopy_ini::Value(&section, "x", "");
    const std::string y = canopy_ini::Value(&section, "y", "");
    villager.hasX = !x.empty() && ParseI32(x, villager.x);
    villager.hasY = !y.empty() && ParseI32(y, villager.y);
    if ((!x.empty() && !villager.hasX) || (!y.empty() && !villager.hasY)) {
        report.Warning(
            folder.name, ((!x.empty() && !villager.hasX) ? at("x") : at("y")),
            where + ": x and y must be whole numbers, a bad one keeps its base villager's."
        );
    }
    villager.startEnabled = canopy_ini::Truthy(canopy_ini::Value(&section, "enabled", "true"));
    villager.routine = canopy_ini::Value(&section, "routine", kDefaultRoutine);
    if (!IdentifierValid(villager.routine)) {
        report.Warning(folder.name, at("routine"), where + " runs routine none, routine must be a Lua function name.");
        villager.routine = kDefaultRoutine;
    }
    villager.script = canopy_ini::Value(&section, "script", "");
    if (!villager.script.empty() && !ReadScript(villager, FolderDir(modsDir, folder), where, at("script"), report)) {
        villager.script.clear();
    }
    for (const char* ignored : {"birthday", "likes", "dislikes", "portrait", "anime", "icon"}) {
        if (!canopy_ini::Value(&section, ignored, "").empty()) {
            report.Warning(
                folder.name, at(ignored),
                where + ": " + ignored + " is not supported yet, the villager keeps its base villager's."
            );
        }
    }
    return true;
}

}  // namespace

const SectionInfo kVillagerSection = {kPrefix, "Villager", "docs/VILLAGERS.md", kFields, std::size(kFields)};

std::vector<VillagerSpec> ReadVillagers(
    const std::vector<ModFolder>& folders, const std::string& modsDir, [[maybe_unused]] const Refs& refs,
    Report& report, const FolderAccept& accept
) {
    std::vector<VillagerSpec> all;
    std::vector<std::string> owners;
    for (const ModFolder& folder : folders) {
        if (!HasVillagerSection(folder)) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Villagers in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Villagers in folder " + folder.name +
                    " skipped, another folder already has villagers under the key " + modKey + "."
            );
            continue;
        }
        std::vector<VillagerSpec> villagers;
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (!IsVillagerSection(section)) {
                continue;
            }
            VillagerSpec villager;
            if (!ReadVillager(folder, modKey, section, modsDir, report, villager)) {
                continue;
            }
            const bool duplicate = std::any_of(villagers.begin(), villagers.end(), [&](const VillagerSpec& other) {
                return platform::CompareNoCase(other.key.c_str(), villager.key.c_str()) == 0 ||
                       (!villager.module.empty() && platform::CompareNoCase(other.module.c_str(), villager.module.c_str()) == 0);
            });
            if (duplicate) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Villager " + villager.key +
                        " skipped, the mod already has a villager with that key or script module name."
                );
                continue;
            }
            if (all.size() + villagers.size() >= kMaxVillagers) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Villager " + villager.key + " skipped, Canopy takes at most 100 mod villagers."
                );
                break;
            }
            villagers.push_back(std::move(villager));
        }
        if (villagers.empty() || !accept(folder, modKey, villagers.size())) {
            continue;
        }
        owners.push_back(modKey);
        for (VillagerSpec& villager : villagers) {
            all.push_back(std::move(villager));
        }
    }
    return all;
}

std::vector<ModFolder> WithVillagers(const std::vector<ModFolder>& folders) {
    std::vector<ModFolder> wanted;
    for (const ModFolder& folder : folders) {
        if (HasVillagerSection(folder)) {
            wanted.push_back(folder);
        }
    }
    return wanted;
}

bool CheckAgainstCharacters(const VillagerSpec& villager, const blob::Row* base, Report& report) {
    const std::string where = "Mod villager " + villager.fullKey;
    if (!base) {
        report.Error(
            villager.folder, villager.line,
            where + " skipped, its base " + std::to_string(villager.baseId) + " is not in the character table."
        );
        return false;
    }
    // The forbid count lies inside a row of the base size only.
    const bool sized = base->size >= kCharaRowBase;
    const uint32_t forbid = sized ? blob::Get<uint32_t>(base->data, kCharaRowForbidCount) : 0u;
    if (!sized || forbid > kMaxForbid || base->size != kCharaRowBase + kForbidSize * forbid) {
        report.Error(villager.folder, villager.line, where + " skipped, its base row has an unexpected layout.");
        return false;
    }
    return true;
}

bool CheckAgainstSchedule(const VillagerSpec& villager, const blob::Row* base, Report& report) {
    if (!base) {
        report.Error(
            villager.folder, villager.line,
            "Mod villager " + villager.fullKey + " has no schedule, its base " + std::to_string(villager.baseId) +
                " is not in the schedule table."
        );
        return false;
    }
    return true;
}

void CheckVillagers(Context& context) {
    std::vector<VillagerSpec> villagers =
        ReadVillagers(context.folders, context.modsDir, context.refs, context.report, AcceptAll);
    for (const VillagerSpec& villager : villagers) {
        context.refs.Add(RefKind::Villager, villager.fullKey);
    }
    const blob::Table* characters = context.vanilla ? context.vanilla->Table(kCharaFile) : nullptr;
    const blob::Table* schedules = context.vanilla ? context.vanilla->Table(kScheduleFile) : nullptr;
    for (const VillagerSpec& villager : villagers) {
        if (characters) {
            CheckAgainstCharacters(villager, FindRow(*characters, villager.baseId), context.report);
        }
        if (schedules) {
            CheckAgainstSchedule(villager, FindRow(*schedules, villager.baseId), context.report);
        }
    }
}

}  // namespace mod_rules
