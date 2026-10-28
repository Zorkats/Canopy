#include "rules_patch.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

#include "platform_files.h"

// =============================================================
// PATCH RULES
// =============================================================
// A [patch.<table>.<id>] section changes named fields of one row the game
// already has, at load, without touching its files. The table is any one
// table_schema::Patchable() covers; the keys are the schema's field paths,
// the same names [row.] sections use. Reading resolves every key to the
// places it writes, spelled out (name becomes name.ja to name.zh-cn), so two
// mods that write one place two ways still meet in the merge. A patch never
// changes a row's id, its enum name or its size: Id and Enum fields are
// refused, and a vector element is named by its index and written only when
// the row has it. Each row places a path itself, so a field after any
// number of vectors, or inside a vector's element, can be patched.

namespace mod_rules {
namespace {

namespace ts = table_schema;
using FT = ts::FieldType;

const char* const kPrefix = "patch.";
const std::vector<const char*> kPrefixes = {kPrefix};
constexpr size_t kMaxEnumName = 128;
const char* const kFrameworkPrefix = "framework_";

int LineOf(const canopy_ini::IniSection& section, size_t index) {
    return index < section.valueLines.size() ? section.valueLines[index] : section.line;
}

bool Digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// An enum name as the game writes them: a letter or '_', then letters,
// digits and '_'. Matched with case, as the game does.
bool EnumNameValid(const std::string& text) {
    if (text.empty() || text.size() > kMaxEnumName || (text[0] >= '0' && text[0] <= '9')) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

std::vector<std::string> SplitPath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        const size_t dot = path.find('.', start);
        parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) {
            return parts;
        }
        start = dot + 1;
    }
}

const ts::FieldSpec* TopField(const ts::TableSchema& schema, const std::string& name) {
    for (size_t index = 0; index < schema.fieldCount; ++index) {
        if (platform::CompareNoCase(name.c_str(), schema.fields[index].name) == 0) {
            return &schema.fields[index];
        }
    }
    return nullptr;
}

// "a", "a and b", "a, b and c".
std::string JoinAnd(const std::vector<std::string>& parts) {
    std::string text;
    for (size_t index = 0; index < parts.size(); ++index) {
        text += (index == 0 ? "" : index + 1 == parts.size() ? " and " : ", ") + parts[index];
    }
    return text;
}

// The places path writes, each spelled out, checked as a patch may write
// them, by the schema alone: each row places them when the patch applies.
bool ResolveKey(
    const ts::TableSchema& schema, const std::string& path, std::vector<ts::Location>& locations, std::string& why
) {
    const std::vector<std::string> parts = SplitPath(path);
    const ts::FieldSpec* top = TopField(schema, parts[0]);
    if (top && (top->type == FT::Id || top->type == FT::Enum)) {
        why = std::string(top->name) + " cannot be patched, a patch never changes a row's id or enum name";
        return false;
    }
    if (!ts::ResolvePath(schema, path, locations, why)) {
        return false;
    }
    for (const ts::Location& location : locations) {
        if (location.type == FT::Id || location.type == FT::Enum) {
            why = location.path + " cannot be patched, a patch never changes a row's id or enum name";
            return false;
        }
    }
    return true;
}

bool RefKindOf(const ts::Location& location, RefKind& kind) {
    if (location.type == FT::FlagRef) {
        kind = RefKind::Flag;
        return true;
    }
    if ((location.type != FT::Ref64 && location.type != FT::Ref32) || !location.field || !location.field->refTable) {
        return false;
    }
    const char* table = location.field->refTable;
    if (platform::CompareNoCase(table, "gameflag.dat") == 0) {
        kind = RefKind::Flag;
        return true;
    }
    if (platform::CompareNoCase(table, "item.dat") == 0) {
        kind = RefKind::Item;
        return true;
    }
    if (platform::CompareNoCase(table, "icon.dat") == 0) {
        kind = RefKind::Icon;
        return true;
    }
    return false;
}

const char* RefNoun(RefKind kind) {
    switch (kind) {
    case RefKind::Flag:
        return "a flag";
    case RefKind::Item:
        return "an item";
    default:
        return "an icon";
    }
}

bool SameRow(const PatchSection& left, const PatchSection& right) {
    return left.schema == right.schema && left.id == right.id && left.enumName == right.enumName;
}

// Reads one section into patch. False when the section is skipped whole,
// which is reported.
bool ReadSection(
    const ModFolder& folder, const std::string& modKey, size_t order, const canopy_ini::IniSection& section,
    const std::vector<PatchSection>& earlier, const Refs& refs, Report& report, PatchSection& patch
) {
    const std::string rest = SectionKey(section, kPrefix);
    const size_t dot = rest.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == rest.size()) {
        report.Error(
            folder.name, section.line,
            "[" + modKey + "] Section " + section.name + " skipped, a patch is [patch.<table>.<id>]."
        );
        return false;
    }
    const std::string table = rest.substr(0, dot);
    const std::string row = rest.substr(dot + 1);
    const std::string written = "[" + modKey + "] Patch " + table + "." + row;
    patch.schema = ts::FindPatchable(table);
    if (!patch.schema) {
        report.Error(
            folder.name, section.line,
            written + " skipped, Canopy has no schema for the table " + table +
                ", so it cannot be patched. PATCHES.md lists the " + std::to_string(ts::Patchable().size()) +
                " tables that take patches."
        );
        return false;
    }
    if (Digits(row)) {
        if (!ParseU64(row, patch.id)) {
            report.Error(folder.name, section.line, written + " skipped, its id " + row + " is too large.");
            return false;
        }
    } else if (EnumNameValid(row)) {
        if (patch.schema->enumOffset < 0) {
            report.Error(
                folder.name, section.line,
                written + " skipped, " + patch.schema->file + " rows have no enum name, so name the row by its id."
            );
            return false;
        }
        patch.enumName = row;
    } else {
        report.Error(
            folder.name, section.line,
            written + " skipped, its row " + row + " must be a decimal id or an enum name."
        );
        return false;
    }
    patch.folder = folder.name;
    patch.modKey = modKey;
    patch.order = order;
    patch.line = section.line;
    const std::string where = "[" + modKey + "] Patch " + PatchLabel(*patch.schema, patch.id, patch.enumName);
    for (const PatchSection& other : earlier) {
        if (SameRow(other, patch)) {
            report.Error(
                folder.name, section.line,
                where + " skipped, the mod already patches that row on line " + std::to_string(other.line) + "."
            );
            return false;
        }
    }
    if (section.values.empty()) {
        report.Warning(folder.name, section.line, where + " skipped, it sets no field.");
        return false;
    }
    for (size_t index = 0; index < section.values.size(); ++index) {
        const auto& [key, value] = section.values[index];
        const int line = LineOf(section, index);
        std::vector<ts::Location> locations;
        std::string why;
        std::string resolved = value;
        bool fine = ResolveKey(*patch.schema, key, locations, why) && !locations.empty() &&
                    ResolvePatchValue(locations.front(), value, refs, resolved, why);
        for (size_t place = 0; fine && place < locations.size(); ++place) {
            fine = ts::Check(locations[place], resolved, why);
        }
        if (!fine) {
            report.Error(folder.name, line, where + ", " + key + " skipped, " + why + ".");
            continue;
        }
        // The first place an earlier key of the section already sets.
        const PatchField* again = nullptr;
        for (size_t place = 0; !again && place < locations.size(); ++place) {
            const auto set = std::find_if(patch.fields.begin(), patch.fields.end(), [&](const PatchField& field) {
                return field.path == locations[place].path;
            });
            again = set == patch.fields.end() ? nullptr : &*set;
        }
        if (again) {
            report.Error(
                folder.name, line,
                where + ", " + key + " skipped, the section already sets " + again->path + " on line " +
                    std::to_string(again->line) + "."
            );
            continue;
        }
        for (const ts::Location& location : locations) {
            PatchField field;
            field.path = location.path;
            field.value = resolved;
            field.folder = folder.name;
            field.modKey = modKey;
            field.line = line;
            field.order = order;
            patch.fields.push_back(std::move(field));
        }
    }
    return !patch.fields.empty();
}

// The rows of table, by id and by enum name, the first of each.
struct RowIndex {
    RowIndex(const blob::Table& table, const ts::TableSchema* schema) {
        for (size_t index = 0; index < table.rows.size(); ++index) {
            byId.emplace(blob::RowId(table.rows[index]), index);
        }
        if (!schema || schema->enumOffset < 0) {
            return;
        }
        std::string name;
        for (size_t index = 0; index < table.rows.size(); ++index) {
            if (blob::ReadText(table, table.rows[index], static_cast<size_t>(schema->enumOffset), name) && !name.empty()) {
                byName.emplace(name, index);
            }
        }
    }

    bool Find(const RowPatch& patch, size_t& row) const {
        if (patch.enumName.empty()) {
            const auto found = byId.find(patch.id);
            row = found == byId.end() ? 0 : found->second;
            return found != byId.end();
        }
        const auto found = byName.find(patch.enumName);
        row = found == byName.end() ? 0 : found->second;
        return found != byName.end();
    }

    std::unordered_map<uint64_t, size_t> byId;
    std::unordered_map<std::string, size_t> byName;
};

// The vanilla table of file and its framework_ twin, either null when the
// archives do not have it.
std::vector<const blob::Table*> VanillaTables(const VanillaData& vanilla, const ts::TableSchema& schema) {
    std::vector<const blob::Table*> tables;
    for (const std::string& file : {std::string(kFrameworkPrefix) + schema.file, std::string(schema.file)}) {
        if (const blob::Table* table = vanilla.Table(file)) {
            tables.push_back(table);
        }
    }
    return tables;
}

}  // namespace

const SectionInfo kPatchSection = {"patch.", "Patch", "docs/PATCHES.md", nullptr, 0};

std::string PatchLabel(const ts::TableSchema& schema, uint64_t id, const std::string& enumName) {
    return std::string(schema.file) + " " + (enumName.empty() ? std::to_string(id) : enumName);
}

std::string PatchLabel(const RowPatch& patch) {
    return PatchLabel(*patch.schema, patch.id, patch.enumName);
}

bool ResolvePatchValue(
    const ts::Location& location, const std::string& value, const Refs& refs, std::string& resolved, std::string& why
) {
    resolved = value;
    RefKind kind = RefKind::Item;
    const size_t dot = value.find('.');
    // A number with a point is a number, which Check then refuses.
    const bool number = value.find_first_not_of("0123456789.+-") == std::string::npos;
    if (number || dot == std::string::npos || dot == 0 || dot + 1 == value.size() || !RefKindOf(location, kind)) {
        return true;
    }
    const uint64_t id = refs.Find(kind, value);
    if (id == 0) {
        why = location.path + " names " + value + ", which is not " + RefNoun(kind) + " of an installed mod";
        return false;
    }
    resolved = std::to_string(id);
    return true;
}

std::vector<PatchSection> ReadPatches(const std::vector<ModFolder>& folders, const Refs& refs, Report& report) {
    std::vector<PatchSection> all;
    for (size_t order = 0; order < folders.size(); ++order) {
        const ModFolder& folder = folders[order];
        if (!HasSection(folder, kPrefixes)) {
            continue;
        }
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, canopy_ini::ValueLine(canopy_ini::FindSection(folder.sections, "mod"), "key"),
                "Patches in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        std::vector<PatchSection> own;
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (SectionKey(section, kPrefix).empty()) {
                continue;
            }
            PatchSection patch;
            if (ReadSection(folder, modKey, order, section, own, refs, report, patch)) {
                own.push_back(std::move(patch));
            }
        }
        for (PatchSection& patch : own) {
            all.push_back(std::move(patch));
        }
    }
    return all;
}

std::vector<RowPatch> MergePatches(
    const std::vector<PatchSection>& sections, const PatchEnumLookup& lookup, Report& report
) {
    std::vector<RowPatch> rows;
    std::map<std::pair<const ts::TableSchema*, std::string>, size_t> byKey;
    for (const PatchSection& section : sections) {
        uint64_t id = section.id;
        std::string enumName = section.enumName;
        uint64_t looked = 0;
        if (!enumName.empty() && lookup && lookup(*section.schema, enumName, looked)) {
            id = looked;
            enumName.clear();
        }
        const std::string key = enumName.empty() ? "#" + std::to_string(id) : enumName;
        const auto found = byKey.find({section.schema, key});
        if (found == byKey.end()) {
            byKey.emplace(std::make_pair(section.schema, key), rows.size());
            RowPatch row;
            row.schema = section.schema;
            row.id = id;
            row.enumName = enumName;
            rows.push_back(std::move(row));
        }
        RowPatch& row = rows[found == byKey.end() ? rows.size() - 1 : found->second];
        const std::string where = "[" + section.modKey + "] Patch " + PatchLabel(*section.schema, section.id, section.enumName);
        const auto same = std::find_if(row.origins.begin(), row.origins.end(), [&](const PatchOrigin& origin) {
            return origin.folder == section.folder;
        });
        if (same != row.origins.end()) {
            report.Error(
                section.folder, section.line,
                where + " skipped, the mod already patches that row on line " + std::to_string(same->line) + "."
            );
            continue;
        }
        row.origins.push_back({section.folder, section.modKey, section.line});
        // The places this section takes from an earlier folder, by folder.
        std::vector<std::pair<std::string, std::vector<const PatchField*>>> taken;
        for (const PatchField& field : section.fields) {
            const auto at = std::find_if(row.fields.begin(), row.fields.end(), [&](const PatchField& other) {
                return other.path == field.path;
            });
            if (at == row.fields.end()) {
                row.fields.push_back(field);
                continue;
            }
            auto loser = std::find_if(taken.begin(), taken.end(), [&](const auto& entry) {
                return entry.first == at->folder;
            });
            if (loser == taken.end()) {
                taken.push_back({at->folder, {}});
                loser = taken.end() - 1;
            }
            loser->second.push_back(&field);
            *at = field;
        }
        for (const auto& [folder, fields] : taken) {
            std::vector<std::string> paths;
            for (const PatchField* field : fields) {
                paths.push_back(field->path);
            }
            report.Warning(
                section.folder, fields.front()->line,
                "Patch conflict: " + folder + " and " + section.folder + " both set " +
                    PatchLabel(*row.schema, row.id, row.enumName) + " " + JoinAnd(paths) + ". " + section.folder +
                    " wins, it comes later in load order."
            );
        }
    }
    return rows;
}

bool ApplyPatches(
    const blob::Table& table, const std::vector<const RowPatch*>& patches, std::vector<uint8_t>& out,
    PatchResult& result
) {
    result = PatchResult();
    out.clear();
    const ts::TableSchema* named = nullptr;
    for (const RowPatch* patch : patches) {
        named = named ? named : (patch->enumName.empty() ? nullptr : patch->schema);
    }
    const RowIndex index(table, named);
    // Per row, each place once: the field of the highest order wins, and
    // of two with one order the later.
    std::map<size_t, size_t> rowOf;
    std::vector<std::map<std::string, std::pair<const PatchField*, ts::Location>>> places;
    for (const RowPatch* patch : patches) {
        size_t row = 0;
        if (!index.Find(*patch, row)) {
            result.missing.push_back(patch);
            continue;
        }
        PatchedRow patched;
        patched.patch = patch;
        patched.id = blob::RowId(table.rows[row]);
        patched.row = row;
        const auto known = rowOf.find(row);
        const size_t slot = known == rowOf.end() ? places.size() : known->second;
        if (known == rowOf.end()) {
            rowOf.emplace(row, slot);
            places.emplace_back();
        }
        std::vector<ts::Location> layout;
        std::string fits;
        const blob::Row& source = table.rows[row];
        const bool laidOut = ts::Layout(*patch->schema, source.data, source.size, layout, fits);
        for (const PatchField& field : patch->fields) {
            ts::Location location;
            std::string why = fits;
            if (!laidOut || !ts::Locate(layout, field.path, location, why)) {
                patched.failed.push_back({&field, why});
                continue;
            }
            auto& place = places[slot][field.path];
            if (!place.first || place.first->order <= field.order) {
                place = {&field, location};
            }
        }
        result.rows.push_back(std::move(patched));
    }
    for (PatchedRow& patched : result.rows) {
        for (const PatchField& field : patched.patch->fields) {
            const auto& slotPlaces = places[rowOf[patched.row]];
            const auto place = slotPlaces.find(field.path);
            if (place != slotPlaces.end() && place->second.first == &field) {
                patched.applied.push_back(&field);
                ++result.fields;
            }
        }
    }
    if (result.fields == 0) {
        return false;
    }

    // The header, every row and the pool as they are, then the new texts.
    const uint8_t* header = table.rowsStart - blob::kHeaderSize;
    out.assign(header, header + blob::kHeaderSize + table.rowsSize + table.poolBytes);
    blob::Pool pool(table.poolBytes);
    bool texts = false;
    for (const auto& [row, slot] : rowOf) {
        const blob::Row& source = table.rows[row];
        std::vector<uint8_t> bytes(source.data, source.data + source.size);
        for (const auto& [path, place] : places[slot]) {
            std::string why;
            if (ts::Write(bytes, place.second, place.first->value, pool, why)) {
                texts = texts || place.second.type == FT::Text;
            }
        }
        const size_t at = blob::kHeaderSize + static_cast<size_t>(source.data - table.rowsStart);
        std::memcpy(out.data() + at, bytes.data(), bytes.size());
    }
    if (texts) {
        out.insert(out.end(), pool.Bytes().begin(), pool.Bytes().end());
        blob::Put<uint32_t>(out.data(), 8, table.poolBytes + static_cast<uint32_t>(pool.Bytes().size()));
    }
    return true;
}

std::string CountPatches(const std::vector<RowPatch>& patches) {
    size_t fields = 0;
    std::set<const ts::TableSchema*> tables;
    std::set<std::string> mods;
    for (const RowPatch& patch : patches) {
        fields += patch.fields.size();
        tables.insert(patch.schema);
        for (const PatchOrigin& origin : patch.origins) {
            mods.insert(canopy_ini::Lower(origin.folder));
        }
    }
    return Plural(fields, "field", "fields") + " on " + Plural(patches.size(), "row", "rows") + " of " +
           Plural(tables.size(), "table", "tables") + ", from " + Plural(mods.size(), "mod", "mods");
}

int64_t PatchRecordOffset(const ts::TableSchema& schema, const std::string& path, ts::Location& location) {
    std::vector<ts::Location> locations;
    std::string why;
    if (!ResolveKey(schema, path, locations, why) || locations.size() != 1) {
        return -1;
    }
    location = locations[0];
    switch (location.type) {
    case FT::I32:
    case FT::U32:
    case FT::F32:
    case FT::Bool:
    case FT::U64:
    case FT::I64:
    case FT::Ref64:
    case FT::Ref32:
    case FT::FlagRef:
        break;
    default:
        return -1;
    }
    return ts::RecordOffset(schema, location.path);
}

PatchEnumLookup VanillaEnumLookup(const VanillaData& vanilla) {
    return [&vanilla](const ts::TableSchema& schema, const std::string& name, uint64_t& id) {
        if (schema.enumOffset < 0) {
            return false;
        }
        for (const blob::Table* table : VanillaTables(vanilla, schema)) {
            std::string text;
            for (const blob::Row& row : table->rows) {
                if (blob::ReadText(*table, row, static_cast<size_t>(schema.enumOffset), text) && text == name) {
                    id = blob::RowId(row);
                    return true;
                }
            }
        }
        return false;
    };
}

void CheckPatches(Context& context) {
    const std::vector<PatchSection> sections = ReadPatches(context.folders, context.refs, context.report);
    if (sections.empty()) {
        return;
    }
    const VanillaData* vanilla = context.vanilla;
    const PatchEnumLookup lookup = vanilla ? VanillaEnumLookup(*vanilla) : PatchEnumLookup();
    const std::vector<RowPatch> rows = MergePatches(sections, lookup, context.report);
    if (!vanilla) {
        return;
    }
    for (const ts::TableSchema* schema : ts::Patchable()) {
        std::vector<const RowPatch*> patches;
        for (const RowPatch& row : rows) {
            if (row.schema == schema) {
                patches.push_back(&row);
            }
        }
        const std::vector<const blob::Table*> tables = VanillaTables(*vanilla, *schema);
        if (patches.empty() || tables.empty()) {
            continue;
        }
        // As the loader applies them: to the twin first, then the file.
        std::set<const RowPatch*> found;
        std::set<const PatchField*> said;
        for (const blob::Table* table : tables) {
            PatchResult result;
            std::vector<uint8_t> out;
            ApplyPatches(*table, patches, out, result);
            for (const PatchedRow& patched : result.rows) {
                found.insert(patched.patch);
                for (const auto& [field, why] : patched.failed) {
                    if (said.insert(field).second) {
                        context.report.Error(
                            field->folder, field->line,
                            "[" + field->modKey + "] Patch " + PatchLabel(*patched.patch) + ", " + field->path +
                                " skipped, " + why + "."
                        );
                    }
                }
            }
        }
        for (const RowPatch* patch : patches) {
            if (found.count(patch)) {
                continue;
            }
            for (const PatchOrigin& origin : patch->origins) {
                context.report.Error(
                    origin.folder, origin.line, "Patch " + PatchLabel(*patch) + " skipped, the game's table has no such row."
                );
            }
        }
    }
}

}  // namespace mod_rules
