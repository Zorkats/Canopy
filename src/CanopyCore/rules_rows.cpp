#include "rules_rows.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "platform_files.h"
#include "rules_crops.h"
#include "rules_shops.h"

// =============================================================
// ROW RULES
// =============================================================
// A [row.<table>.<Key>] section on a table whose schema is generated from
// the game's serializers adds a row to it: a copy of the vanilla row named by
// base, with its lists resized by <list>.count keys and fields set by path,
// the names [patch.] sections use. The row gets an id in the row band, one
// band and one registry for every such table, and, when its table has enum
// names, CANOPY_<TABLE>_<MOD>_<KEY> made unique as the table loads.
//
// Not every table may take one. A table whose ids the game's save keeps would
// leave a save that names a row no longer there once the mod is gone, so it
// is refused, and so is a table a typed section owns, which keeps those
// rules: the message names the section to use. The curated tables (texture,
// icon, animereplacetexture) are art, read by rules_art.

namespace mod_rules {
namespace {

namespace ts = table_schema;
using FT = ts::FieldType;

const char* const kPrefix = "row.";
const std::vector<const char*> kPrefixes = {kPrefix, kSalePrefix, kUpgradePrefix, kCropPrefix};
const char* const kFrameworkPrefix = "framework_";
constexpr size_t kMaxEnumName = 128;

// The tables a typed section owns: rows come from that section only.
struct Owned {
    const char* stem;
    const char* section;
    const char* doc;
};

const Owned kOwned[] = {
    {"gameflag", "flag", "FLAGS.md"},
    {"whistle", "whistle", "WHISTLES.md"},
    {"craft", "recipe", "RECIPES.md"},
    {"cooking", "recipe", "RECIPES.md"},
    {"item", "item", "ITEMS.md"},
    {"character", "villager", "VILLAGERS.md"},
    {"gimmick", "object", "OBJECTS.md"},
    {"storesales", "sale", "SHOPS.md"},
    {"crops", "crop", "CROPS.md"},
    {"cropsharvest", "crop", "CROPS.md"},
    {"charactertalk", "talk", "VILLAGERS.md"},
    {"letter", "letter", "LETTERS.md"},
    {"subquestitem", "request", "LETTERS.md"},
    {"playerbackaccessory", "outfit", "OUTFITS.md"},
    {"playerbackhairstyle", "outfit", "OUTFITS.md"},
    {"playercrothing", "outfit", "OUTFITS.md"},
    {"playerfronthairstyle", "outfit", "OUTFITS.md"},
    {"playerheadaccessory", "outfit", "OUTFITS.md"},
    {"playerneckaccessory", "outfit", "OUTFITS.md"},
};

// The record types whose ids the game's save keeps (its CDataPtr types).
const char* const kSaveKept[] = {
    "AnimeReplaceTextureData", "CharaData", "CharaTalkData", "CreatureData", "CropsData",
    "DemoData", "DungeonData", "EventData", "FarmlandData", "FertilizerData",
    "GameEffectData", "GimmickData", "ItemData", "LivestockData", "LivestockPlacementData",
    "MapData", "MapGroupData", "PlayerBackAccessoryData", "PlayerBackHairStyleData", "PlayerCrothingData",
    "PlayerFrontHairStyleData", "PlayerHeadAccessoryData", "PlayerNeckAccessoryData", "StoreSalesData", "WeatherData",
};

// The tables whose ids the save keeps as plain numbers in lists of its own.
const char* const kRawIds[] = {"letter", "quest", "subquestitem", "bundle", "talk"};

int LineOf(const canopy_ini::IniSection& section, size_t index) {
    return index < section.valueLines.size() ? section.valueLines[index] : section.line;
}

bool Digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// An enum name as the game writes them: capitals, digits and '_', with one
// '_' at least.
bool LooksLikeEnum(const std::string& text) {
    return text.size() <= kMaxEnumName && text.find('_') != std::string::npos &&
           std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}

bool SameText(const char* left, const char* right) {
    return platform::CompareNoCase(left, right) == 0;
}

std::string Stem(const char* file) {
    const std::string name = file;
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".dat") == 0 ? name.substr(0, name.size() - 4) : name;
}

size_t Depth(const std::string& path) {
    return static_cast<size_t>(std::count(path.begin(), path.end(), '.'));
}

// The mod part kind a reference into file names by key, false for a table
// whose rows no other section adds.
bool KindOf(const char* file, RefKind& kind) {
    struct Entry {
        const char* file;
        RefKind kind;
    };
    const Entry entries[] = {
        {"gameflag.dat", RefKind::Flag},         {"item.dat", RefKind::Item},
        {"icon.dat", RefKind::Icon},             {"texture.dat", RefKind::Texture},
        {"animereplacetexture.dat", RefKind::Skin}, {"gimmick.dat", RefKind::Object},
        {"character.dat", RefKind::Villager},    {"whistle.dat", RefKind::Whistle},
    };
    for (const Entry& entry : entries) {
        if (SameText(file, entry.file)) {
            kind = entry.kind;
            return true;
        }
    }
    return false;
}

const char* KindNoun(RefKind kind) {
    switch (kind) {
    case RefKind::Flag:
        return "flag";
    case RefKind::Item:
        return "item";
    case RefKind::Icon:
        return "icon";
    case RefKind::Texture:
        return "texture";
    case RefKind::Skin:
        return "skin";
    case RefKind::Object:
        return "object";
    case RefKind::Villager:
        return "villager";
    default:
        return "whistle";
    }
}

// "a sale", "an upgrade", or "a blacksmith row" for a [row.] section.
std::string Noun(const RowSpec& row) {
    const std::string noun = row.kind ? std::string(row.kind) : std::string(row.schema->section) + " row";
    return (std::string("aeiou").find(noun[0]) != std::string::npos ? "an " : "a ") + noun;
}

// The row of schema's table with modKey and key, with an id or not.
const RowSpec* FindAny(
    const std::vector<RowSpec>& rows, const ts::TableSchema& schema, const std::string& modKey, const std::string& key
) {
    for (const RowSpec& row : rows) {
        if (row.schema == &schema && SameText(row.modKey.c_str(), modKey.c_str()) &&
            SameText(row.key.c_str(), key.c_str())) {
            return &row;
        }
    }
    return nullptr;
}

// ---- Reading -------------------------------------------------------------

// The table and key of a [row.<table>.<Key>] section, false when it has no
// dot, which rules_art reports.
bool RowSection(const canopy_ini::IniSection& section, std::string& table, std::string& key) {
    const std::string rest = SectionKey(section, kPrefix);
    const size_t dot = rest.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    table = rest.substr(0, dot);
    key = rest.substr(dot + 1);
    return true;
}

// Checks one key = value and adds it to row. False, reported, when the row
// must be skipped.
bool ReadValueOf(const std::string& key, const std::string& value, int line, Report& report, RowSpec& row) {
    RowValue entry;
    entry.value = value;
    entry.line = line;
    const std::string countSuffix = ".count";
    std::string why;
    std::vector<ts::Location> locations;
    // <list>.count resizes a list; a key whose part before .count is not a
    // list may be a field named count, such as itemInfo.0.count.
    const ts::FieldSpec* vector = nullptr;
    const bool countKey = key.size() > countSuffix.size() &&
                          SameText(key.substr(key.size() - countSuffix.size()).c_str(), countSuffix.c_str());
    const std::string listPath = countKey ? key.substr(0, key.size() - countSuffix.size()) : std::string();
    if (countKey && !ts::VectorPath(*row.schema, listPath, why, &vector)) {
        std::string ignored;
        if (!ts::ResolvePath(*row.schema, key, locations, ignored) || locations.empty()) {
            report.Error(row.folder, line, row.where + " skipped, " + key + ": " + why + ".");
            return false;
        }
    }
    if (vector) {
        entry.count = true;
        entry.path = listPath;
        uint64_t elements = 0;
        // A list the game keeps inside its record holds no more than it has room for.
        const uint32_t most = ts::MaxElements(*vector);
        if (!ParseU64(value, elements) || elements > most) {
            report.Error(
                row.folder, line, row.where + " skipped, " + key + " must be a count from 0 to " + std::to_string(most) + "."
            );
            return false;
        }
        entry.elements = static_cast<uint32_t>(elements);
        const bool again = std::any_of(row.counts.begin(), row.counts.end(), [&](const RowValue& other) {
            return SameText(other.path.c_str(), entry.path.c_str());
        });
        if (again) {
            report.Error(row.folder, line, row.where + " skipped, it sets " + key + " twice.");
            return false;
        }
        row.counts.push_back(std::move(entry));
        return true;
    }
    if (locations.empty() && (!ts::ResolvePath(*row.schema, key, locations, why) || locations.empty())) {
        report.Error(row.folder, line, row.where + " skipped, " + (why.empty() ? key + " names nothing" : why) + ".");
        return false;
    }
    for (const ts::Location& location : locations) {
        if (location.type == FT::Id || location.type == FT::Enum) {
            report.Error(
                row.folder, line,
                row.where + " skipped, " + location.path + " is set by Canopy, which gives the row its id and enum name."
            );
            return false;
        }
        const auto again = std::find_if(row.values.begin(), row.values.end(), [&](const RowValue& other) {
            std::vector<ts::Location> others;
            std::string ignored;
            if (!ts::ResolvePath(*row.schema, other.path, others, ignored)) {
                return false;
            }
            return std::any_of(others.begin(), others.end(), [&](const ts::Location& place) {
                return place.path == location.path;
            });
        });
        if (again != row.values.end()) {
            report.Error(
                row.folder, line,
                row.where + " skipped, " + key + " sets " + location.path + ", which line " +
                    std::to_string(again->line) + " already sets."
            );
            return false;
        }
    }
    const ts::Location& first = locations.front();
    const bool reference = first.type == FT::FlagRef ||
                           ((first.type == FT::Ref64 || first.type == FT::Ref32) && first.field && first.field->refTable);
    if (reference) {
        entry.refTable = first.field && first.field->refTable ? first.field->refTable : "gameflag.dat";
        entry.flag = first.type == FT::FlagRef || SameText(entry.refTable, "gameflag.dat");
    } else {
        for (const ts::Location& location : locations) {
            if (!ts::Check(location, value, why)) {
                report.Error(row.folder, line, row.where + " skipped, " + key + ": " + why + ".");
                return false;
            }
        }
    }
    entry.path = key;
    row.values.push_back(std::move(entry));
    return true;
}

// Reads a section whose table has no curated schema into row. False when it
// is not one, or is skipped, which is reported.
bool ReadSection(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    RowSpec& row
) {
    std::string table;
    if (!RowSection(section, table, row.key) || ts::Find(table)) {
        return false;
    }
    row.modKey = modKey;
    row.folder = folder.name;
    row.line = section.line;
    row.where = "[" + modKey + "] Row " + table + "." + row.key;
    row.schema = ts::FindPatchable(table);
    if (!row.schema) {
        report.Error(
            folder.name, section.line,
            row.where + " skipped, Canopy has no schema for the table " + table +
                ", so it takes no rows. ROWS.md lists the tables that take rows."
        );
        return false;
    }
    const std::string refused = RowsRefused(*row.schema);
    if (!refused.empty()) {
        report.Error(folder.name, section.line, row.where + " skipped, " + refused + ".");
        return false;
    }
    row.fullKey = modKey + "." + row.key;
    row.label = std::string("Mod ") + row.schema->section + " row " + row.fullKey;
    if (!KeyValid(row.key)) {
        report.Error(
            folder.name, section.line, row.where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    bool based = false;
    for (size_t index = 0; index < section.values.size(); ++index) {
        const auto& [key, value] = section.values[index];
        const int line = LineOf(section, index);
        if (key == "base") {
            if (based || !ParseU64(value, row.baseId) || row.baseId == 0 ||
                (row.baseId >= ts::kRowBandStart && row.baseId < ts::kRowBandStart + ts::kRowBandSize)) {
                report.Error(
                    folder.name, line,
                    row.where + " skipped, base must be one vanilla id of " + row.schema->file + "."
                );
                return false;
            }
            based = true;
            continue;
        }
        if (!ReadValueOf(key, value, line, report, row)) {
            return false;
        }
    }
    if (!based) {
        report.Error(
            folder.name, section.line,
            row.where + " skipped, it needs base = <the id of a vanilla " + row.schema->section + " row to copy>."
        );
        return false;
    }
    // Outer lists first: form.2.growInfo needs form to have element 2.
    std::stable_sort(row.counts.begin(), row.counts.end(), [](const RowValue& left, const RowValue& right) {
        return Depth(left.path) < Depth(right.path);
    });
    return true;
}

// ---- References ----------------------------------------------------------

// Resolves value, a reference of row into value.refTable. Rows must have
// their ids. False, with why, when it does not resolve.
bool ResolveValue(
    const std::vector<RowSpec>& rows, const RowSpec& row, const Refs& refs, RowValue& value, std::string& why
) {
    value.id = 0;
    value.enumName.clear();
    value.vanilla = false;
    const std::string& text = value.value;
    const ts::TableSchema* target = ts::FindPatchable(value.refTable);
    RefKind kind = RefKind::Item;
    const bool keyed = KindOf(value.refTable, kind);
    const std::string noun = target ? target->section : Stem(value.refTable);
    if (Digits(text)) {
        uint64_t number = 0;
        if (!ParseU64(text, number)) {
            why = ValueName(value) + " " + text + " is not an id";
            return false;
        }
        value.id = number;
        value.vanilla = number != 0;
        if (number == 0) {
            return true;
        }
        if (keyed && kind == RefKind::Flag) {
            if (FlagRef(refs, row.modKey, text) == 0) {
                why = ValueName(value) + " " + text + " is not a vanilla flag id; name a mod flag by its key";
                return false;
            }
            return true;
        }
        if (keyed && kind == RefKind::Item) {
            if (ItemRef(refs, row.modKey, text) == 0) {
                why = ValueName(value) + " " + text + " is a mod item id; name a mod item by its key";
                return false;
            }
            return true;
        }
        if (target && number >= ts::BandStart(*target) && number < ts::BandStart(*target) + ts::BandSize(*target)) {
            why = ValueName(value) + " " + text + " is a mod id; name a mod " + noun + " by its key instead";
            return false;
        }
        return true;
    }
    const size_t dot = text.find('.');
    const std::string modKey = dot == std::string::npos ? row.modKey : text.substr(0, dot);
    const std::string key = dot == std::string::npos ? text : text.substr(dot + 1);
    if (target && !modKey.empty() && !key.empty()) {
        if (const RowSpec* other = FindRow(rows, *target, modKey, key)) {
            value.id = other->id;
            return true;
        }
        if (FindAny(rows, *target, modKey, key)) {
            why = value.path + " names the " + noun + " row " + modKey + "." + key + ", which was skipped";
            return false;
        }
    }
    if (keyed && !key.empty()) {
        value.id = kind == RefKind::Flag ? static_cast<uint64_t>(FlagRef(refs, row.modKey, text))
                                         : KeyRef(refs, kind, row.modKey, text);
        if (value.id != 0) {
            return true;
        }
    }
    if (dot == std::string::npos && LooksLikeEnum(text)) {
        if (!target || target->enumOffset < 0) {
            why = value.path + " names " + text + ", but " + value.refTable + " rows have no enum name; use an id";
            return false;
        }
        value.enumName = text;
        return true;
    }
    if (dot != std::string::npos) {
        why = ValueName(value) + " " + text + " is not a " + noun + " row" + (keyed ? std::string(" or ") + KindNoun(kind) : "") +
              " of an installed mod";
        return false;
    }
    why = ValueName(value) + " " + text + " is not a " + noun + " row" +
          (keyed ? std::string(" or ") + KindNoun(kind) : "") + " of this mod, a Mod.Key, a vanilla id or an enum name";
    return false;
}

// Resolves every reference of row. False, reported, at the first that does
// not.
bool ResolveRowRefs(const std::vector<RowSpec>& rows, RowSpec& row, const Refs& refs, Report& report) {
    std::vector<RowValue*> values;
    for (RowValue& value : row.values) {
        values.push_back(&value);
    }
    if (!row.basePath.empty()) {
        values.push_back(&row.baseValue);
    }
    for (RowValue& value : row.inputs) {
        values.push_back(&value);
    }
    for (RowValue* value : values) {
        if (!value->refTable) {
            continue;
        }
        std::string why;
        if (!ResolveValue(rows, row, refs, *value, why)) {
            report.Error(row.folder, value->line, row.where + " skipped, " + why + ".");
            return false;
        }
    }
    return true;
}

// ---- Building ------------------------------------------------------------

// The first vanilla row of table whose field path holds value, null for
// none.
const blob::Row* FirstRowWith(
    const ts::TableSchema& schema, const blob::Table& table, const std::string& path, uint64_t value
) {
    for (const blob::Row& row : table.rows) {
        const uint64_t id = blob::RowId(row);
        std::vector<ts::Location> places;
        std::string ignored;
        if ((id < ts::kRowBandStart || id >= ts::kRowBandStart + ts::kRowBandSize) &&
            ts::ResolveRow(schema, path, row.data, row.size, places, ignored) && places.size() == 1 &&
            ts::ReadNumber(row.data, places[0]) == value) {
            return &row;
        }
    }
    return nullptr;
}

// Sets every byte of the fields names, and of what they hold, to zero,
// wherever row's lists put them.
bool ZeroFields(
    const ts::TableSchema& schema, const std::vector<std::string>& names, std::vector<uint8_t>& row, std::string& why
) {
    std::vector<ts::Location> layout;
    if (!ts::Layout(schema, row.data(), row.size(), layout, why)) {
        return false;
    }
    for (const std::string& name : names) {
        size_t cleared = 0;
        for (const ts::Location& place : layout) {
            const bool inside = place.path.size() > name.size() && place.path[name.size()] == '.' &&
                                SameText(place.path.substr(0, name.size()).c_str(), name.c_str());
            if ((inside || SameText(place.path.c_str(), name.c_str())) && place.offset + place.size <= row.size()) {
                std::fill_n(row.begin() + static_cast<std::ptrdiff_t>(place.offset), place.size, uint8_t{0});
                ++cleared;
            }
        }
        if (cleared == 0) {
            why = name + " is not a field of " + std::string(schema.file);
            return false;
        }
    }
    return true;
}

// ---- Desktop check -------------------------------------------------------

// The vanilla table of file and its framework_ twin, either null when the
// archives do not have it.
std::vector<const blob::Table*> TablesOf(const VanillaData& vanilla, const std::string& file) {
    std::vector<const blob::Table*> tables;
    for (const std::string& name : {std::string(kFrameworkPrefix) + file, file}) {
        if (const blob::Table* table = vanilla.Table(name)) {
            tables.push_back(table);
        }
    }
    return tables;
}

}  // namespace

VanillaRowTables::VanillaRowTables(const VanillaData& vanilla) : vanilla_(vanilla) {}

bool VanillaRowTables::EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const {
    const ts::TableSchema* schema = ts::FindPatchable(file);
    const std::vector<const blob::Table*> tables = TablesOf(vanilla_, file);
    if (name.rfind("CANOPY_", 0) == 0 || (schema && schema->enumStem && name.rfind(schema->enumStem, 0) == 0) ||
        !schema || schema->enumOffset < 0 || tables.empty()) {
        id = schema ? ts::BandStart(*schema) : ts::kRowBandStart;
        return true;
    }
    for (const blob::Table* table : tables) {
        std::string text;
        for (const blob::Row& row : table->rows) {
            if (blob::ReadText(*table, row, static_cast<size_t>(schema->enumOffset), text) && text == name) {
                id = blob::RowId(row);
                return true;
            }
        }
    }
    why = std::string(file) + " has no " + name;
    return false;
}

bool VanillaRowTables::VanillaFine(const char* file, uint64_t id, std::string& why) const {
    if (SameText(file, "gameflag.dat")) {
        // Flags are slots, 1 to kFlagCount - 1, not all of them rows.
        return true;
    }
    const std::vector<const blob::Table*> tables = TablesOf(vanilla_, file);
    if (tables.empty() || vanilla_.HasRow(file, id) || vanilla_.HasRow(kFrameworkPrefix + std::string(file), id)) {
        return true;
    }
    why = "it names " + Stem(file) + " " + std::to_string(id) + ", which is not in " + file;
    return false;
}

std::string RowsRefused(const ts::TableSchema& schema) {
    const std::string stem = Stem(schema.file);
    const Owned* owned = nullptr;
    for (const Owned& entry : kOwned) {
        if (SameText(stem.c_str(), entry.stem)) {
            owned = &entry;
        }
    }
    const bool saveKept = std::any_of(std::begin(kSaveKept), std::end(kSaveKept), [&](const char* name) {
        return schema.structName && SameText(schema.structName, name);
    });
    const bool rawIds = std::any_of(std::begin(kRawIds), std::end(kRawIds), [&](const char* name) {
        return SameText(stem.c_str(), name);
    });
    std::string why;
    if (saveKept || rawIds) {
        why = "the game's save keeps " + stem + " ids, so a save made with a mod row would break once the mod is gone";
    } else if (owned) {
        why = stem + " rows are added through their own sections";
    } else {
        return "";
    }
    if (owned) {
        why += std::string(". Use [") + owned->section + ".<Key>] sections instead (docs/" + owned->doc + ")";
    }
    return why;
}

std::vector<RowSpec> ReadRows(const std::vector<ModFolder>& folders, Report& report, const FolderAccept& accept) {
    std::vector<RowSpec> all;
    std::vector<std::string> owners;
    // The seeds a crop already has, by Mod.Key: an item plants one crop.
    std::vector<std::string> seeds;
    const auto seeded = [](const std::vector<std::string>& keys, const std::string& seed) {
        return std::any_of(keys.begin(), keys.end(), [&](const std::string& other) {
            return SameText(other.c_str(), seed.c_str());
        });
    };
    for (const ModFolder& folder : folders) {
        if (!HasSection(folder, kPrefixes)) {
            continue;
        }
        const std::string modKey = ModKey(folder);
        std::vector<RowSpec> rows;
        std::vector<std::string> folderSeeds;
        bool any = false;
        for (const canopy_ini::IniSection& section : folder.sections) {
            std::string table;
            std::string key;
            const bool shop = IsShopSection(section);
            const bool crop = IsCropSection(section);
            if (!shop && !crop &&
                (SectionKey(section, kPrefix).empty() || !RowSection(section, table, key) || ts::Find(table))) {
                continue;
            }
            if (!any) {
                any = true;
                const int infoLine = canopy_ini::ValueLine(canopy_ini::FindSection(folder.sections, "mod"), "key");
                if (!KeyValid(modKey)) {
                    report.Error(
                        folder.name, infoLine,
                        "Rows in folder " + folder.name + " skipped, its key " + modKey +
                            " must be 1 to 32 letters, digits, '_' or '-'."
                    );
                    break;
                }
                if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                        return SameText(owner.c_str(), modKey.c_str());
                    })) {
                    report.Error(
                        folder.name, infoLine,
                        "Rows in folder " + folder.name + " skipped, another folder already has rows under the key " +
                            modKey + "."
                    );
                    break;
                }
            }
            // A [crop.] section makes two rows, its crops row first.
            std::vector<RowSpec> made(crop ? 2 : 1);
            if (shop ? !ReadShop(folder, modKey, section, report, made[0])
                : crop ? !ReadCrop(folder, modKey, section, report, made[0], made[1])
                       : !ReadSection(folder, modKey, section, report, made[0])) {
                continue;
            }
            const RowSpec& row = made[0];
            // Keys are per table: [upgrade.X] and [row.blacksmith.X] would
            // share one registry key, so one id.
            const RowSpec* taken = nullptr;
            for (const RowSpec& one : made) {
                for (const RowSpec& other : rows) {
                    if (!taken && other.schema == one.schema && SameText(other.key.c_str(), one.key.c_str())) {
                        taken = &other;
                    }
                }
            }
            if (taken) {
                const std::string why =
                    taken->kind == row.kind
                        ? "the mod already has " + Noun(row) + " with that key"
                        : "the mod already has " + Noun(*taken) + " " + taken->key + ", and both are " +
                              taken->schema->section + " rows, which share their keys";
                report.Error(folder.name, section.line, row.where + " skipped, " + why + ".");
                continue;
            }
            const std::string seed = crop ? CropSeedKey(row) : std::string();
            if (!seed.empty() && (seeded(seeds, seed) || seeded(folderSeeds, seed))) {
                report.Error(
                    folder.name, canopy_ini::ValueLine(&section, "seed"),
                    row.where + " skipped, another crop already grows from the seed " + seed +
                        ", and an item plants one crop."
                );
                continue;
            }
            if (all.size() + rows.size() + made.size() > kMaxRows) {
                report.Error(folder.name, section.line, row.where + " skipped, Canopy takes at most 10000 mod rows.");
                break;
            }
            if (!seed.empty()) {
                folderSeeds.push_back(seed);
            }
            for (RowSpec& one : made) {
                rows.push_back(std::move(one));
            }
        }
        // A crop's harvest row goes with its crops row, so it is not counted.
        const size_t count = static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [](const RowSpec& row) {
            return !row.kind || std::strcmp(row.kind, kCropHarvestKind) != 0;
        }));
        if (rows.empty() || (accept && !accept(folder, modKey, count))) {
            continue;
        }
        owners.push_back(modKey);
        seeds.insert(seeds.end(), folderSeeds.begin(), folderSeeds.end());
        for (RowSpec& row : rows) {
            all.push_back(std::move(row));
        }
    }
    return all;
}

std::string RowRegistryKey(const RowSpec& row) {
    return row.registryKey.empty() ? Stem(row.schema->file) + "." + row.fullKey : row.registryKey;
}

void ResolveRows(std::vector<RowSpec>& rows, const RowNumbers& number, const Refs& refs, Report& report) {
    if (rows.empty()) {
        return;
    }
    // Each registry key once, in row order: rows that share one share its
    // number.
    std::vector<std::string> keys;
    std::vector<size_t> keyOf;
    for (const RowSpec& row : rows) {
        const std::string key = RowRegistryKey(row);
        const auto found = std::find(keys.begin(), keys.end(), key);
        keyOf.push_back(static_cast<size_t>(found - keys.begin()));
        if (found == keys.end()) {
            keys.push_back(key);
        }
    }
    std::vector<uint64_t> numbers;
    if (number) {
        if (!number(keys, numbers)) {
            numbers.clear();
        }
    } else {
        for (size_t index = 0; index < keys.size(); ++index) {
            numbers.push_back(index + 1);
        }
    }
    for (size_t index = 0; index < rows.size(); ++index) {
        const uint64_t at = keyOf[index] < numbers.size() ? numbers[keyOf[index]] : 0;
        rows[index].id = at != 0 && at < ts::kRowBandSize ? ts::kRowBandStart + at : 0;
    }
    // A row that names a row skipped on an earlier pass is skipped too, and
    // so is every row that shares its key.
    for (bool changed = true; changed;) {
        changed = false;
        for (RowSpec& row : rows) {
            if (row.id != 0 && !ResolveRowRefs(rows, row, refs, report)) {
                DropRow(rows, row);
                changed = true;
            }
        }
        for (const RowSpec& row : rows) {
            if (row.id == 0 && !row.registryKey.empty() &&
                std::any_of(rows.begin(), rows.end(), [&](const RowSpec& other) {
                    return other.id != 0 && other.registryKey == row.registryKey;
                })) {
                DropRow(rows, row);
                changed = true;
            }
        }
    }
}

const RowSpec* FindRow(
    const std::vector<RowSpec>& rows, const ts::TableSchema& schema, const std::string& modKey, const std::string& key
) {
    const RowSpec* row = FindAny(rows, schema, modKey, key);
    return row && row->id != 0 ? row : nullptr;
}

const std::string& ValueName(const RowValue& value) {
    return value.name.empty() ? value.path : value.name;
}

bool ReadRowValue(const std::string& key, const std::string& value, int line, Report& report, RowSpec& row) {
    return ReadValueOf(key, value, line, report, row);
}

void DropRow(std::vector<RowSpec>& rows, const RowSpec& row) {
    const std::string key = RowRegistryKey(row);
    for (RowSpec& other : rows) {
        if (other.id != 0 && RowRegistryKey(other) == key) {
            other.id = 0;
        }
    }
}

bool RowValueText(const RowValue& value, const RowTables& tables, std::string& text, std::string& why) {
    if (!value.refTable) {
        text = value.value;
        return true;
    }
    uint64_t id = value.id;
    if (!value.enumName.empty() && !tables.EnumId(value.refTable, value.enumName, id, why)) {
        why = ValueName(value) + ": " + why;
        return false;
    }
    if (value.vanilla && !tables.VanillaFine(value.refTable, id, why)) {
        why = ValueName(value) + ": " + why;
        return false;
    }
    text = std::to_string(id);
    return true;
}


std::string CountRows(const std::vector<const RowSpec*>& rows) {
    std::vector<std::string> parts;
    for (const ts::TableSchema* schema : ts::Patchable()) {
        const size_t count = static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [&](const RowSpec* row) {
            return row->schema == schema && !row->kind;
        }));
        if (count == 0) {
            continue;
        }
        const std::string one = std::string(schema->section) + " row";
        parts.push_back(Plural(count, one.c_str(), (one + "s").c_str()));
    }
    for (const char* kind : {kSaleKind, kUpgradeKind, kCropKind}) {
        const size_t count = static_cast<size_t>(std::count_if(rows.begin(), rows.end(), [&](const RowSpec* row) {
            return row->kind && std::strcmp(row->kind, kind) == 0;
        }));
        if (count > 0) {
            parts.push_back(Plural(count, kind, (std::string(kind) + "s").c_str()));
        }
    }
    std::string text;
    for (size_t index = 0; index < parts.size(); ++index) {
        text += (index == 0 ? "" : index + 1 == parts.size() ? " and " : ", ") + parts[index];
    }
    return text;
}

std::string RowEnumName(const RowSpec& row) {
    return "CANOPY_" + EnumPart(row.schema->section) + "_" + EnumPart(row.modKey) + "_" + EnumPart(row.key);
}

bool BuildRow(
    const RowSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId,
    const RowTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
) {
    const ts::TableSchema& schema = *mod.schema;
    const blob::Row* source = nullptr;
    if (mod.basePath.empty()) {
        const auto base = byId.find(mod.baseId);
        if (base == byId.end()) {
            why = "its base " + std::to_string(mod.baseId) + " is not in " + schema.file;
            return false;
        }
        source = &table.rows[base->second];
    } else {
        std::string text;
        uint64_t wanted = 0;
        if (!RowValueText(mod.baseValue, tables, text, why)) {
            return false;
        }
        source = ParseU64(text, wanted) ? FirstRowWith(schema, table, mod.basePath, wanted) : nullptr;
        if (!source) {
            why = mod.baseMissing;
            return false;
        }
    }
    if (!ts::RowFits(schema, source->data, source->size)) {
        why = "its base row has an unexpected layout";
        return false;
    }
    row.assign(source->data, source->data + source->size);
    for (const RowValue& count : mod.counts) {
        if (!ts::ResizeVector(schema, row, count.path, count.elements, pool, why)) {
            why = ValueName(count) + ": " + why;
            return false;
        }
    }
    if (!mod.zero.empty() && !ZeroFields(schema, mod.zero, row, why)) {
        return false;
    }
    for (const RowValue& value : mod.values) {
        std::string text;
        if (!RowValueText(value, tables, text, why)) {
            return false;
        }
        std::vector<ts::Location> locations;
        if (!ts::ResolveRow(schema, value.path, row.data(), row.size(), locations, why)) {
            why = value.path + ": " + why;
            return false;
        }
        for (const ts::Location& location : locations) {
            if (!ts::Write(row, location, text, pool, why)) {
                why = value.path + ": " + why;
                return false;
            }
        }
    }
    return !mod.finish || mod.finish(mod, row, tables, pool, why);
}

void CheckRows(Context& context) {
    std::vector<RowSpec> rows = ReadRows(context.folders, context.report, AcceptAll);
    ResolveRows(rows, RowNumbers(), context.refs, context.report);
    CheckCropSeeds(context, rows);
    if (!context.vanilla) {
        return;
    }
    const VanillaRowTables tables(*context.vanilla);
    for (const ts::TableSchema* schema : ts::Patchable()) {
        const blob::Table* table = context.vanilla->Table(schema->file);
        const bool any = std::any_of(rows.begin(), rows.end(), [&](const RowSpec& row) {
            return row.schema == schema && row.id != 0;
        });
        if (!any || !table) {
            continue;
        }
        std::unordered_map<uint64_t, size_t> byId;
        for (size_t index = 0; index < table->rows.size(); ++index) {
            byId.emplace(blob::RowId(table->rows[index]), index);
        }
        blob::Pool pool(table->poolBytes);
        for (const RowSpec& mod : rows) {
            if (mod.schema != schema || mod.id == 0) {
                continue;
            }
            std::vector<uint8_t> row;
            std::string why;
            if (!BuildRow(mod, *table, byId, tables, pool, row, why)) {
                context.report.Error(mod.folder, mod.line, mod.label + " skipped, " + why + ".");
            }
        }
    }
}

}  // namespace mod_rules
