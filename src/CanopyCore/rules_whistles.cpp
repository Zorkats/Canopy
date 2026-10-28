#include "rules_whistles.h"

#include <algorithm>
#include <iterator>

#include "platform_files.h"

// =============================================================
// WHISTLE RULES
// =============================================================
// What the loader's custom whistles accept from canopy.ini, moved here from
// mod_whistles.cpp so the desktop tools check a mod the same way. Every
// message is the loader's log line.

namespace mod_rules {
namespace {

constexpr uint64_t kBandBase = 1800000000;
constexpr uint64_t kBandSize = 100000000;
constexpr size_t kMaxNameLength = 200;
constexpr size_t kMaxScriptBytes = 1024 * 1024;
constexpr int32_t kMinNotes = 2;
constexpr int32_t kMaxNotes = 10;
// ICON_ID_WHISTLE_SCORE_1 to _10.
constexpr uint64_t kFirstScoreIcon = 1153;
constexpr uint64_t kScoreIcons = 10;
constexpr const char* kDefaultEntry = "main";
constexpr const char* kModulePrefix = "whistle_";
constexpr const char* kPrefix = "whistle.";

constexpr size_t kRowNotes = 88;
constexpr size_t kRowNoteCount = 96;

const FieldInfo kFields[] = {
    {"name", ValueKind::LangText, true, "The name in the whistle menu, one line of up to 200 bytes."},
    {"notes", ValueKind::Melody, true, "The melody, 2 to 10 notes, each S or 0 for short and L or 1 for long."},
    {"learn", ValueKind::FlagRef, true, "The flag that makes the whistle show in the menu."},
    {"base", ValueKind::VanillaId, false, "The vanilla whistle to copy, 1 to 11.", "2", "whistle.dat", 1, 11},
    {"score", ValueKind::Integer, false, "Which vanilla score icon the menu shows, 1 to 10.", nullptr, nullptr, 1, 10},
    {"icon", ValueKind::IconRef, false, "A mod icon the menu shows in the score icon's place. Wins over score."},
    {"script", ValueKind::Path, false, "A .lua file inside the mod folder, run when the whistle is played."},
    {"entry", ValueKind::Other, false, "The function of the script the game runs.", "main"},
};

bool IsBand(uint64_t id) {
    return id >= kBandBase && id < kBandBase + kBandSize;
}

// Reads the whistle's script text from folderDir, its mod folder. Reports
// and returns false when it cannot.
bool ReadScript(WhistleSpec& whistle, const std::string& folderDir, const std::string& where, Report& report) {
    const int line = whistle.line;
    if (!ScriptPathValid(whistle.script)) {
        report.Warning(whistle.folder, line, where + " has no script, script must be a .lua path inside the mod folder.");
        return false;
    }
    const std::string path = platform::Join(folderDir, platform::NativeSeparators(whistle.script));
    std::string text;
    if (!ReadFileText(path, text)) {
        report.Warning(whistle.folder, line, where + " has no script, " + whistle.script + " could not be read.");
        return false;
    }
    if (text.empty() || text.size() > kMaxScriptBytes || text[0] == '\x1B') {
        report.Warning(whistle.folder, line, where + " has no script, " + whistle.script + " must be Lua text of up to 1 MB.");
        return false;
    }
    whistle.source = std::move(text);
    whistle.module = kModulePrefix + ModulePart(whistle.modKey) + "_" + ModulePart(whistle.key);
    whistle.chunkName = "=" + whistle.modKey + "/" + whistle.script;
    return true;
}

// Reads one [whistle.X] section. Reports and returns false when it is
// unusable.
bool ReadWhistle(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section,
    const std::string& modsDir, const Refs& refs, Report& report, WhistleSpec& whistle
) {
    whistle.modKey = modKey;
    whistle.folder = folder.name;
    whistle.key = SectionKey(section, kPrefix);
    whistle.fullKey = modKey + "." + whistle.key;
    whistle.line = section.line;
    const std::string where = "[" + modKey + "] Whistle " + whistle.key;
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    if (!KeyValid(whistle.key)) {
        report.Error(folder.name, section.line, where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'.");
        return false;
    }
    const std::string base = canopy_ini::Value(&section, "base", "");
    if (!base.empty() && (!ParseU64(base, whistle.baseId) || whistle.baseId == 0 || IsBand(whistle.baseId))) {
        report.Error(folder.name, at("base"), where + " skipped, base must be a vanilla whistle id.");
        return false;
    }
    ReadTexts(section, "name", whistle.names);
    for (const std::string& name : whistle.names) {
        if (name.empty() || !TextValid(name, kMaxNameLength)) {
            report.Error(folder.name, at("name"), where + " skipped, it needs name = <text>, one line of up to 200 bytes.");
            return false;
        }
    }
    if (!ParseMelody(canopy_ini::Value(&section, "notes", ""), whistle.notes, whistle.noteCount)) {
        report.Error(
            folder.name, at("notes"),
            where + " skipped, notes must be 2 to 10 notes, each S or 0 for short and L or 1 for long."
        );
        return false;
    }
    const std::string learn = canopy_ini::Value(&section, "learn", "");
    whistle.learn = FlagRef(refs, modKey, learn);
    if (whistle.learn == 0) {
        report.Error(
            folder.name, at("learn"),
            where + " skipped, learn must name a flag of an installed mod or a vanilla flag id" +
                (learn.empty() ? "." : ", and " + learn + " is neither.")
        );
        return false;
    }
    const std::string score = canopy_ini::Value(&section, "score", "");
    uint64_t number = 0;
    if (!score.empty()) {
        if (ParseU64(score, number) && number >= 1 && number <= kScoreIcons) {
            whistle.score = kFirstScoreIcon + number - 1;
        } else {
            report.Warning(folder.name, at("score"), where + " keeps its base whistle's score, score must be 1 to 10.");
        }
    }
    whistle.script = canopy_ini::Value(&section, "script", "");
    whistle.entry = canopy_ini::Value(&section, "entry", kDefaultEntry);
    if (!IdentifierValid(whistle.entry)) {
        report.Warning(folder.name, at("entry"), where + " runs entry main, entry must be a Lua function name.");
        whistle.entry = kDefaultEntry;
    }
    if (!whistle.script.empty()) {
        whistle.line = at("script");
        const bool read = ReadScript(whistle, FolderDir(modsDir, folder), where, report);
        whistle.line = section.line;
        if (!read) {
            whistle.script.clear();
        }
    }
    const std::string icon = canopy_ini::Value(&section, "icon", "");
    if (!icon.empty()) {
        whistle.icon = KeyRef(refs, RefKind::Icon, modKey, icon);
        if (whistle.icon == 0) {
            report.Warning(
                folder.name, at("icon"),
                where + " keeps its " + (whistle.score != 0 ? "score" : "base whistle's score icon") +
                    ", icon must be a mod icon by key or Mod.Key (score picks a vanilla one)."
            );
        }
    }
    if (!canopy_ini::Value(&section, "guide", "").empty()) {
        report.Warning(folder.name, at("guide"), where + ": guide is not supported yet, the whistle keeps its base whistle's.");
    }
    return true;
}

}  // namespace

const SectionInfo kWhistleSection = {kPrefix, "Whistle", "docs/WHISTLES.md", kFields, std::size(kFields)};

bool ParseMelody(const std::string& text, uint64_t& notes, int32_t& count) {
    notes = 0;
    count = 0;
    for (const char c : text) {
        if (c == ' ') {
            continue;
        }
        const bool longNote = c == '1' || c == 'L' || c == 'l';
        if (!longNote && c != '0' && c != 'S' && c != 's') {
            return false;
        }
        if (++count > kMaxNotes) {
            return false;
        }
        notes = (notes << 1) | (longNote ? 1 : 0);
    }
    return count >= kMinNotes;
}

std::string MelodyText(uint64_t notes, int32_t count) {
    std::string text;
    for (int32_t note = count - 1; note >= 0; --note) {
        text += ((notes >> note) & 1) ? 'L' : 'S';
    }
    return text;
}

std::vector<WhistleSpec> ReadWhistles(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
) {
    std::vector<WhistleSpec> all;
    std::vector<std::string> owners;
    for (const ModFolder& folder : folders) {
        if (!HasSection(folder, {kPrefix})) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Whistles in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Whistles in folder " + folder.name + " skipped, another folder already has whistles under the key " +
                    modKey + "."
            );
            continue;
        }
        std::vector<WhistleSpec> whistles;
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (SectionKey(section, kPrefix).empty()) {
                continue;
            }
            WhistleSpec whistle;
            if (!ReadWhistle(folder, modKey, section, modsDir, refs, report, whistle)) {
                continue;
            }
            const auto same = [&](const WhistleSpec& other) {
                return platform::CompareNoCase(other.key.c_str(), whistle.key.c_str()) == 0;
            };
            if (std::any_of(whistles.begin(), whistles.end(), same)) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Whistle " + whistle.key + " skipped, the mod already has a whistle with that key."
                );
                continue;
            }
            const auto sameMelody = [&](const WhistleSpec& other) {
                return other.noteCount == whistle.noteCount && other.notes == whistle.notes;
            };
            const auto earlier = std::find_if(all.begin(), all.end(), sameMelody);
            const auto local = std::find_if(whistles.begin(), whistles.end(), sameMelody);
            if (earlier != all.end() || local != whistles.end()) {
                const WhistleSpec& other = earlier != all.end() ? *earlier : *local;
                report.Error(
                    folder.name, canopy_ini::ValueLine(&section, "notes"),
                    "[" + modKey + "] Whistle " + whistle.key + " skipped, its melody " +
                        MelodyText(whistle.notes, whistle.noteCount) + " is already " + other.fullKey + "'s."
                );
                continue;
            }
            if (all.size() + whistles.size() >= kMaxWhistles) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Whistle " + whistle.key + " skipped, Canopy takes at most 100 mod whistles."
                );
                break;
            }
            whistles.push_back(std::move(whistle));
        }
        if (whistles.empty() || !accept(folder, modKey, whistles.size())) {
            continue;
        }
        owners.push_back(modKey);
        for (WhistleSpec& whistle : whistles) {
            all.push_back(std::move(whistle));
        }
    }
    return all;
}

Melody RowMelody(const uint8_t* row) {
    return {blob::Get<int32_t>(row, kRowNoteCount), blob::Get<uint64_t>(row, kRowNotes)};
}

bool CheckAgainstTable(const WhistleSpec& whistle, const blob::Row* base, std::set<Melody>& melodies, Report& report) {
    const std::string where = "Mod whistle " + whistle.fullKey;
    if (!base) {
        report.Error(
            whistle.folder, whistle.line,
            where + " skipped, its base " + std::to_string(whistle.baseId) + " is not in the whistle table."
        );
        return false;
    }
    if (base->size != kWhistleRowSize) {
        report.Error(whistle.folder, whistle.line, where + " skipped, its base row has an unexpected layout.");
        return false;
    }
    if (!melodies.insert({whistle.noteCount, whistle.notes}).second) {
        report.Error(
            whistle.folder, whistle.line,
            where + " skipped, its melody " + MelodyText(whistle.notes, whistle.noteCount) +
                " is already a vanilla whistle's."
        );
        return false;
    }
    return true;
}

void CheckWhistles(Context& context) {
    std::vector<WhistleSpec> whistles =
        ReadWhistles(context.folders, context.modsDir, context.refs, context.report, AcceptAll);
    for (const WhistleSpec& whistle : whistles) {
        context.refs.Add(RefKind::Whistle, whistle.fullKey);
    }
    const blob::Table* table = context.vanilla ? context.vanilla->Table("whistle.dat") : nullptr;
    if (!table) {
        return;
    }
    std::set<Melody> melodies;
    for (const blob::Row& row : table->rows) {
        if (row.size >= kWhistleRowSize) {
            melodies.insert(RowMelody(row.data));
        }
    }
    for (const WhistleSpec& whistle : whistles) {
        const auto base = std::find_if(table->rows.begin(), table->rows.end(), [&](const blob::Row& row) {
            return blob::RowId(row) == whistle.baseId;
        });
        CheckAgainstTable(whistle, base == table->rows.end() ? nullptr : &*base, melodies, context.report);
    }
}

}  // namespace mod_rules
