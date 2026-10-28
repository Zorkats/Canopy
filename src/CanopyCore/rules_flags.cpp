#include "rules_flags.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "platform_files.h"

// =============================================================
// FLAG RULES
// =============================================================
// What the loader's [flag.X] sections accept from canopy.ini, moved here from
// mod_flags.cpp so the desktop tools check a mod the same way. Every message
// is the loader's log line.

namespace mod_rules {
namespace {

constexpr const char* kPrefix = "flag.";

const FieldInfo kFields[] = {
    {"default", ValueKind::Switch, false,
     "on or off. On starts the flag on in a new game, and in a loaded save with no line for it, such as one made "
     "before the mod was installed.",
     "off"},
    {"id", ValueKind::Other, false,
     "Not used. Canopy gives each mod flag its id, and a value here is ignored with a warning."},
};

// A section named flag. or flag.<Key>. A bare flag. still counts, so that its
// error is reported.
bool IsFlagSection(const canopy_ini::IniSection& section) {
    return platform::CompareNoCase(section.name.c_str(), kPrefix, std::strlen(kPrefix)) == 0;
}

// Reads one [flag.X] section. Reports and returns false when it is unusable.
bool ReadFlag(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    FlagSpec& flag
) {
    flag.modKey = modKey;
    flag.key = section.name.substr(std::strlen(kPrefix));
    flag.fullKey = modKey + "." + flag.key;
    const std::string where = "[" + modKey + "] Flag " + flag.key;
    if (!KeyValid(flag.key)) {
        report.Error(
            folder.name, section.line, where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    if (!ParseSwitch(canopy_ini::Value(&section, "default", ""), flag.defaultOn)) {
        report.Warning(
            folder.name, canopy_ini::ValueLine(&section, "default"),
            where + ": default must be on or off, so it starts off."
        );
    }
    if (!canopy_ini::Value(&section, "id", "").empty()) {
        report.Warning(
            folder.name, canopy_ini::ValueLine(&section, "id"),
            where + ": id is ignored, Canopy gives each mod flag its id."
        );
    }
    return true;
}

}  // namespace

const SectionInfo kFlagSection = {kPrefix, "Flag", "docs/FLAGS.md", kFields, std::size(kFields)};

bool ParseSwitch(const std::string& text, bool& on) {
    const std::string value = canopy_ini::Lower(canopy_ini::Trim(text));
    if (value == "on" || value == "true" || value == "yes" || value == "1") {
        on = true;
        return true;
    }
    on = false;
    return value.empty() || value == "off" || value == "false" || value == "no" || value == "0";
}

std::vector<ModFolder> FlagFolders(const std::vector<ModFolder>& folders) {
    std::vector<ModFolder> wanted;
    for (const ModFolder& folder : folders) {
        if (std::any_of(folder.sections.begin(), folder.sections.end(), IsFlagSection)) {
            wanted.push_back(folder);
        }
    }
    return wanted;
}

std::vector<FlagSpec> ReadFlags(
    const std::vector<ModFolder>& folders, const std::string&, const Refs&, Report& report, const FolderAccept& accept
) {
    std::vector<FlagSpec> all;
    std::vector<std::string> owners;
    for (const ModFolder& folder : folders) {
        if (!std::any_of(folder.sections.begin(), folder.sections.end(), IsFlagSection)) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Flags in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Flags in folder " + folder.name + " skipped, another folder already has flags under the key " +
                    modKey + "."
            );
            continue;
        }
        std::vector<FlagSpec> flags;
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (!IsFlagSection(section)) {
                continue;
            }
            FlagSpec flag;
            if (!ReadFlag(folder, modKey, section, report, flag)) {
                continue;
            }
            const auto same = [&](const FlagSpec& other) {
                return platform::CompareNoCase(other.key.c_str(), flag.key.c_str()) == 0;
            };
            if (std::any_of(flags.begin(), flags.end(), same)) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Flag " + flag.key + " skipped, the mod already has a flag with that key."
                );
                continue;
            }
            flags.push_back(std::move(flag));
        }
        if (flags.empty() || !accept(folder, modKey, flags.size())) {
            continue;
        }
        owners.push_back(modKey);
        for (FlagSpec& flag : flags) {
            all.push_back(std::move(flag));
        }
    }
    return all;
}

void CheckFlags(Context& context) {
    const std::vector<FlagSpec> flags =
        ReadFlags(context.folders, context.modsDir, context.refs, context.report, AcceptAll);
    for (const FlagSpec& flag : flags) {
        context.refs.Add(RefKind::Flag, flag.fullKey);
    }
}

}  // namespace mod_rules
