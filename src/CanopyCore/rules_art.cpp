#include "rules_art.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "image_io.h"
#include "nltx.h"
#include "platform_files.h"

// =============================================================
// ART RULES
// =============================================================
// What the loader's mod art accepts from canopy.ini, moved here from
// mod_art.cpp so the desktop tools check a mod the same way: reading each
// section, resolving references between rows, and building a row the way the
// table load does. Every message is the loader's log line. The loader keeps
// what needs the game: converting and serving texture files, numbering rows
// in its registries, and its table and title checks, which it plugs in
// through ArtHost, ArtNumbers and ArtTables.

namespace mod_rules {
namespace {

namespace ts = table_schema;
using FT = ts::FieldType;

constexpr uint32_t kMaxTextureSide = 8192;
constexpr size_t kMaxTextureBytes = 512u * 1024 * 1024;
constexpr size_t kMaxNameLength = 64;
constexpr const char* kGameFolder = "data/texture/canopy/";

const FieldInfo kTextureFields[] = {
    {"file", ValueKind::Path, true,
     "The image inside the mod folder: a PNG, or any image Windows reads, up to 8192x8192. A .nltx is used as "
     "it is."},
};

const FieldInfo kIconFields[] = {
    {"texture", ValueKind::TextureRef, true,
     "The texture: a texture of this mod by key, Mod.Key, a vanilla texture id, or a TEX_ID_ name."},
    {"rect", ValueKind::Other, false,
     "x, y, w, h in pixels. The whole texture when left out, which works for a mod texture only."},
    {"size", ValueKind::Other, false, "w, h, the size the menus draw it at. The rect's w, h when left out."},
    {"offset", ValueKind::Other, false, "x, y, moves where it is drawn.", "0, 0"},
};

const FieldInfo kSkinFields[] = {
    {"base", ValueKind::VanillaId, false, "The vanilla swap row to copy. Its swaps are kept unless replaced.",
     nullptr, "animereplacetexture.dat"},
    {"replace", ValueKind::List, true,
     "<model texture name>: <texture>, several separated by commas. The key can be repeated. At most 64 swaps."},
};

const FieldInfo kRowFields[] = {
    {"base", ValueKind::VanillaId, false,
     "A vanilla row of the table to copy first, needed on every table but texture, icon and "
     "animereplacetexture. Every other key sets a field by path, such as drawInfo.x, and <list>.count "
     "resizes a list."},
};

const char* Noun(ArtKind kind) {
    switch (kind) {
    case ArtKind::Texture:
        return "texture";
    case ArtKind::Icon:
        return "icon";
    case ArtKind::Skin:
        return "skin";
    default:
        return "row";
    }
}

bool Digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// An enum name as the game writes them: capitals, digits and '_'.
bool LooksLikeEnum(const std::string& text) {
    return text.find('_') != std::string::npos && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}

// A relative path inside the mod folder, without "..".
bool PathValid(const std::string& path) {
    if (path.empty() || path.size() > 200 || path[0] == '/' || path[0] == '\\' ||
        path.find(':') != std::string::npos) {
        return false;
    }
    std::string part;
    for (const char c : path + "/") {
        if (c == '/' || c == '\\') {
            if (part.empty() || part == "..") {
                return false;
            }
            part.clear();
        } else {
            part += c;
        }
    }
    return TextValid(path, 200);
}

bool ParseFloat(const std::string& text, float& value) {
    if (text.empty() || text.size() > 32) {
        return false;
    }
    char* end = nullptr;
    value = std::strtof(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(value);
}

// count numbers split at commas. Empty text leaves parts empty.
bool ReadNumbers(const std::string& text, size_t count, std::vector<std::string>& parts, std::vector<float>& values) {
    parts.clear();
    values.clear();
    if (text.empty()) {
        return true;
    }
    parts = SplitList(text);
    if (parts.size() != count) {
        return false;
    }
    for (const std::string& part : parts) {
        float value = 0;
        if (!ParseFloat(part, value)) {
            return false;
        }
        values.push_back(value);
    }
    return true;
}

int LineOf(const canopy_ini::IniSection& section, size_t index) {
    return index < section.valueLines.size() ? section.valueLines[index] : section.line;
}

// ---- Reading -------------------------------------------------------------

// The table and key of a [row.<table>.<Key>] section.
bool RawSection(const canopy_ini::IniSection& section, std::string& table, std::string& key);

// True when folder has a section art reads: a texture, icon or skin, a row
// of a curated table, or a [row.] section too short to name a table.
bool HasArt(const ModFolder& folder) {
    for (const canopy_ini::IniSection& section : folder.sections) {
        std::string table;
        std::string key;
        if (!SectionKey(section, "texture.").empty() || !SectionKey(section, "icon.").empty() ||
            !SectionKey(section, "skin.").empty()) {
            return true;
        }
        if (!SectionKey(section, "row.").empty() && (!RawSection(section, table, key) || ts::Find(table))) {
            return true;
        }
    }
    return false;
}

// Reads a [texture.X] row whose file is inside folderDir, its mod folder.
bool ReadTexture(
    const canopy_ini::IniSection& section, const std::string& folderDir, const ArtHost& host, Report& report,
    ArtSpec& row
) {
    const int line = canopy_ini::ValueLine(&section, "file");
    row.file = canopy_ini::Value(&section, "file", "");
    if (!PathValid(row.file)) {
        report.Error(row.folder, line, row.where + " skipped, it needs file = <image or .nltx path inside the mod folder>.");
        return false;
    }
    // The folder is in the narrow form the loader opens files with, the
    // file is UTF-8 as canopy.ini holds it.
    row.source = platform::Join(
        platform::AnsiToWide(folderDir), platform::Utf8ToWideLossy(platform::NativeSeparators(row.file))
    );
    const size_t dot = row.file.find_last_of('.');
    row.nltx = dot != std::string::npos && canopy_ini::Lower(row.file.substr(dot)) == ".nltx";
    if (row.source.size() >= platform::kWindowsMaxPath) {
        report.Error(
            row.folder, line, row.where + " skipped, the full path of " + row.file + " is longer than Windows allows here."
        );
        return false;
    }
    std::string why;
    if (!(host.load ? host.load(row, why) : LoadTexture(row, why))) {
        report.Error(row.folder, line, row.where + " skipped, " + why + ".");
        return false;
    }
    if (row.width == 0 || row.height == 0) {
        report.Error(row.folder, line, row.where + " skipped, " + row.file + " has no pixels.");
        return false;
    }
    row.gamePath = kGameFolder + canopy_ini::Lower(row.modKey) + "/" + canopy_ini::Lower(row.key) + ".nltx";
    if (host.serve && !host.serve(row)) {
        report.Error(
            row.folder, row.line,
            row.where + " skipped, Canopy could not serve its file at " + row.gamePath +
                " (file overrides are off on this game build, or the path is taken)."
        );
        return false;
    }
    return true;
}

bool ReadIcon(const canopy_ini::IniSection& section, Report& report, ArtSpec& row) {
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    row.texture.text = canopy_ini::Value(&section, "texture", "");
    row.texture.line = at("texture");
    if (row.texture.text.empty()) {
        report.Error(
            row.folder, row.texture.line,
            row.where + " skipped, it needs texture = <texture key, Mod.Key, vanilla id or TEX_ID_ name>."
        );
        return false;
    }
    std::vector<float> rect;
    std::vector<float> size;
    std::vector<float> offset;
    row.rectLine = at("rect");
    if (!ReadNumbers(canopy_ini::Value(&section, "rect", ""), 4, row.rect, rect) ||
        (!rect.empty() && (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0))) {
        report.Error(row.folder, row.rectLine, row.where + " skipped, rect must be x, y, w, h in pixels, with w and h above 0.");
        return false;
    }
    if (!ReadNumbers(canopy_ini::Value(&section, "size", ""), 2, row.size, size) ||
        (!size.empty() && (size[0] <= 0 || size[1] <= 0))) {
        report.Error(row.folder, at("size"), row.where + " skipped, size must be w, h above 0.");
        return false;
    }
    if (!ReadNumbers(canopy_ini::Value(&section, "offset", ""), 2, row.offset, offset)) {
        report.Error(row.folder, at("offset"), row.where + " skipped, offset must be x, y.");
        return false;
    }
    return true;
}

bool ReadSkin(const canopy_ini::IniSection& section, Report& report, ArtSpec& row) {
    const std::string base = canopy_ini::Value(&section, "base", "");
    if (!base.empty() && (!ParseU64(base, row.baseId) || row.baseId == 0 || InBand(*row.schema, row.baseId))) {
        report.Error(
            row.folder, canopy_ini::ValueLine(&section, "base"),
            row.where + " skipped, base must be a vanilla AnimeReplaceTexture id."
        );
        return false;
    }
    for (size_t index = 0; index < section.values.size(); ++index) {
        const auto& [key, value] = section.values[index];
        if (key != "replace") {
            continue;
        }
        const int line = LineOf(section, index);
        for (const std::string& part : SplitList(value)) {
            const size_t colon = part.find(':');
            ArtSwap swap;
            swap.name = canopy_ini::Trim(part.substr(0, colon));
            swap.texture.text = colon == std::string::npos ? "" : canopy_ini::Trim(part.substr(colon + 1));
            swap.texture.line = line;
            if (swap.name.empty() || swap.texture.text.empty() || !TextValid(swap.name, kMaxNameLength)) {
                report.Error(
                    row.folder, line,
                    row.where + " skipped, each replace must be <model texture name>: <texture>, and " + part +
                        " is not."
                );
                return false;
            }
            const bool again = std::any_of(row.swaps.begin(), row.swaps.end(), [&](const ArtSwap& other) {
                return platform::CompareNoCase(other.name.c_str(), swap.name.c_str()) == 0;
            });
            if (again) {
                report.Error(row.folder, line, row.where + " skipped, it replaces " + swap.name + " twice.");
                return false;
            }
            row.swaps.push_back(std::move(swap));
        }
    }
    if (row.swaps.empty()) {
        report.Error(row.folder, row.line, row.where + " skipped, it needs replace = <model texture name>: <texture>.");
        return false;
    }
    // The game keeps a skin's swaps inside its record, 32 at most.
    const uint32_t most = ts::MaxElements(*ts::VectorField(ts::kAnimeReplaceTexture));
    if (row.swaps.size() > most) {
        report.Error(
            row.folder, row.line, row.where + " skipped, a skin takes at most " + std::to_string(most) + " swaps."
        );
        return false;
    }
    return true;
}

// The element count the raw row's keys need: entries.2.name needs 3.
bool RawElements(const ArtSpec& row, uint32_t& count) {
    count = 0;
    const ts::FieldSpec* vector = ts::VectorField(*row.schema);
    if (!vector) {
        return true;
    }
    for (const ArtField& field : row.fields) {
        const size_t dot = field.path.find('.');
        if (dot == std::string::npos || platform::CompareNoCase(field.path.substr(0, dot).c_str(), vector->name) != 0) {
            continue;
        }
        const size_t next = field.path.find('.', dot + 1);
        const std::string index = field.path.substr(dot + 1, next == std::string::npos ? std::string::npos : next - dot - 1);
        uint64_t value = 0;
        if (Digits(index) && ParseU64(index, value)) {
            if (value >= ts::MaxElements(*vector)) {
                return false;
            }
            count = std::max(count, static_cast<uint32_t>(value + 1));
        }
    }
    return true;
}

bool ReadRaw(const canopy_ini::IniSection& section, Report& report, ArtSpec& row) {
    for (size_t index = 0; index < section.values.size(); ++index) {
        const auto& [key, value] = section.values[index];
        if (key == "base") {
            if (!ParseU64(value, row.baseId) || row.baseId == 0 || InBand(*row.schema, row.baseId)) {
                report.Error(
                    row.folder, LineOf(section, index),
                    row.where + " skipped, base must be a vanilla id of " + row.schema->file + "."
                );
                return false;
            }
            continue;
        }
        ArtField field;
        field.path = key;
        field.value = value;
        field.line = LineOf(section, index);
        row.fields.push_back(std::move(field));
    }
    if (!RawElements(row, row.elements)) {
        report.Error(
            row.folder, row.line,
            row.where + " skipped, a raw row of " + row.schema->file + " takes at most " +
                std::to_string(ts::MaxElements(*ts::VectorField(*row.schema))) + " elements."
        );
        return false;
    }
    // Checked against a row with at least one element, so a key that sets
    // every element of a base's vector can be checked before the base is.
    const uint32_t elements = std::max<uint32_t>(row.elements, ts::VectorField(*row.schema) ? 1 : 0);
    const size_t size = ts::RowSize(*row.schema, elements);
    for (ArtField& field : row.fields) {
        std::vector<ts::Location> locations;
        std::string why;
        if (!ts::Resolve(*row.schema, field.path, elements, size, locations, why)) {
            report.Error(row.folder, field.line, row.where + " skipped, " + why + ".");
            return false;
        }
        const ts::Location& location = locations.front();
        if (location.type == FT::Ref64 || location.type == FT::Ref32) {
            field.refTable = location.field->refTable;
            field.ref.text = field.value;
            field.ref.line = field.line;
            continue;
        }
        for (const ts::Location& each : locations) {
            if (!ts::Check(each, field.value, why)) {
                report.Error(row.folder, field.line, row.where + " skipped, " + why + ".");
                return false;
            }
        }
    }
    return true;
}

// The table and key of a [row.<table>.<Key>] section.
bool RawSection(const canopy_ini::IniSection& section, std::string& table, std::string& key) {
    const std::string rest = SectionKey(section, "row.");
    const size_t dot = rest.find('.');
    if (dot == std::string::npos) {
        return false;
    }
    table = rest.substr(0, dot);
    key = rest.substr(dot + 1);
    return true;
}

// Starts a row from a section. False when the section is not art, or its key
// or table is unusable, which is reported.
bool StartFromSection(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    ArtSpec& row
) {
    row.modKey = modKey;
    row.folder = folder.name;
    row.line = section.line;
    struct Curated {
        const char* prefix;
        ArtKind kind;
        const ts::TableSchema* schema;
        const char* title;
    };
    const Curated curated[] = {
        {"texture.", ArtKind::Texture, &ts::kTexture, "Texture"},
        {"icon.", ArtKind::Icon, &ts::kIcon, "Icon"},
        {"skin.", ArtKind::Skin, &ts::kAnimeReplaceTexture, "Skin"},
    };
    for (const Curated& entry : curated) {
        const std::string key = SectionKey(section, entry.prefix);
        if (!key.empty()) {
            row.kind = entry.kind;
            row.schema = entry.schema;
            row.key = key;
            row.where = "[" + modKey + "] " + entry.title + " " + key;
            row.label = std::string("Mod ") + Noun(entry.kind) + " " + modKey + "." + key;
        }
    }
    std::string table;
    if (!row.schema) {
        if (SectionKey(section, "row.").empty()) {
            return false;
        }
        row.kind = ArtKind::Raw;
        if (!RawSection(section, table, row.key)) {
            report.Error(
                folder.name, section.line,
                "[" + modKey + "] Section " + section.name + " skipped, a raw row is [row.<table>.<Key>]."
            );
            return false;
        }
        row.where = "[" + modKey + "] Row " + table + "." + row.key;
        // Rows of any other table are rules_rows'.
        row.schema = ts::Find(table);
        if (!row.schema) {
            return false;
        }
        row.label = std::string("Mod ") + row.schema->section + " row " + modKey + "." + row.key;
    }
    row.fullKey = modKey + "." + row.key;
    if (!KeyValid(row.key)) {
        report.Error(
            folder.name, section.line, row.where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    return true;
}

bool ReadRow(
    const canopy_ini::IniSection& section, const std::string& folderDir, const ArtHost& host, Report& report,
    ArtSpec& row
) {
    switch (row.kind) {
    case ArtKind::Texture:
        return ReadTexture(section, folderDir, host, report, row);
    case ArtKind::Icon:
        return ReadIcon(section, report, row);
    case ArtKind::Skin:
        return ReadSkin(section, report, row);
    default:
        return ReadRaw(section, report, row);
    }
}

// Reads one folder's art into rows. before is the count of rows read from
// earlier folders.
void ReadFolder(
    const ModFolder& folder, const std::string& modKey, const std::string& modsDir, size_t before,
    const ArtHost& host, Report& report, std::vector<ArtSpec>& rows
) {
    for (const canopy_ini::IniSection& section : folder.sections) {
        ArtSpec row;
        if (!StartFromSection(folder, modKey, section, report, row)) {
            continue;
        }
        const bool taken = std::any_of(rows.begin(), rows.end(), [&](const ArtSpec& other) {
            return other.schema == row.schema && platform::CompareNoCase(other.key.c_str(), row.key.c_str()) == 0;
        });
        if (taken) {
            report.Error(
                folder.name, section.line,
                row.where + " skipped, the mod already has a " + row.schema->section + " row with that key."
            );
            continue;
        }
        if (before + rows.size() >= kMaxArtRows) {
            report.Error(folder.name, section.line, row.where + " skipped, Canopy takes at most 10000 mod art rows.");
            break;
        }
        if (ReadRow(section, FolderDir(modsDir, folder), host, report, row)) {
            rows.push_back(std::move(row));
        }
    }
}

// ---- References ----------------------------------------------------------

// Resolves ref, written in a row of modKey, into target's table. Mod rows
// of target must have their ids already.
bool ResolveRef(
    const std::vector<ArtSpec*>& rows, const std::string& modKey, const ts::TableSchema& target, ArtRef& ref,
    std::string& why
) {
    const std::string text = ref.text;
    const int line = ref.line;
    ref = ArtRef();
    ref.text = text;
    ref.line = line;
    const std::string noun = target.section;
    if (Digits(text)) {
        if (!ParseU64(text, ref.id) || ref.id == 0) {
            why = "its " + noun + " " + text + " is not an id";
            return false;
        }
        if (InBand(target, ref.id)) {
            why = "its " + noun + " " + text + " is a mod id; name a mod " + noun + " by its key instead";
            return false;
        }
        return true;
    }
    const size_t dot = text.find('.');
    if (dot != std::string::npos) {
        ref.row = FindArt(rows, target, text.substr(0, dot), text.substr(dot + 1));
        if (!ref.row) {
            why = "its " + noun + " " + text + " is not a " + noun + " of an installed mod";
        }
        return ref.row != nullptr;
    }
    ref.row = FindArt(rows, target, modKey, text);
    if (ref.row) {
        return true;
    }
    if (LooksLikeEnum(text)) {
        ref.enumName = text;
        return true;
    }
    why = "its " + noun + " " + text + " is not a " + noun + " of this mod, a Mod.Key, a vanilla id or an enum name";
    return false;
}

// Resolves every reference of row. False, reported, when one does not
// resolve or the row cannot be completed.
bool ResolveRow(const std::vector<ArtSpec*>& rows, ArtSpec& row, Report& report) {
    std::string why;
    if (row.kind == ArtKind::Icon) {
        if (!ResolveRef(rows, row.modKey, ts::kTexture, row.texture, why)) {
            report.Error(row.folder, row.texture.line, row.where + " skipped, " + why + ".");
            return false;
        }
        const ArtSpec* texture = row.texture.row;
        const bool sized = texture && texture->width > 0 && texture->height > 0;
        if (row.rect.empty()) {
            if (!sized) {
                report.Error(row.folder, row.line, row.where + " skipped, it needs rect = x, y, w, h for a vanilla texture.");
                return false;
            }
            row.rect = {"0", "0", std::to_string(texture->width), std::to_string(texture->height)};
        } else if (sized) {
            const float x = std::strtof(row.rect[0].c_str(), nullptr);
            const float y = std::strtof(row.rect[1].c_str(), nullptr);
            const float w = std::strtof(row.rect[2].c_str(), nullptr);
            const float h = std::strtof(row.rect[3].c_str(), nullptr);
            if (x + w > static_cast<float>(texture->width) || y + h > static_cast<float>(texture->height)) {
                report.Error(
                    row.folder, row.rectLine,
                    row.where + " skipped, its rect runs past the " + std::to_string(texture->width) + "x" +
                        std::to_string(texture->height) + " texture " + texture->fullKey + "."
                );
                return false;
            }
        }
        if (row.size.empty()) {
            row.size = {row.rect[2], row.rect[3]};
        }
        if (row.offset.empty()) {
            row.offset = {"0", "0"};
        }
        return true;
    }
    if (row.kind == ArtKind::Skin) {
        for (ArtSwap& swap : row.swaps) {
            if (!ResolveRef(rows, row.modKey, ts::kTexture, swap.texture, why)) {
                report.Error(row.folder, swap.texture.line, row.where + " skipped, " + swap.name + ": " + why + ".");
                return false;
            }
        }
        return true;
    }
    if (row.kind == ArtKind::Raw) {
        for (ArtField& field : row.fields) {
            const ts::TableSchema* target = field.refTable ? ts::Find(field.refTable) : nullptr;
            if (field.refTable && (!target || !ResolveRef(rows, row.modKey, *target, field.ref, why))) {
                report.Error(
                    row.folder, field.line,
                    row.where + " skipped, " + field.path + ": " + (target ? why : "it names a table with no schema") +
                        "."
                );
                return false;
            }
        }
    }
    return true;
}

// ---- Rows ----------------------------------------------------------------

// Writes value to every place path names in row.
bool Set(
    const ts::TableSchema& schema, std::vector<uint8_t>& row, const std::string& path, const std::string& value,
    blob::Pool& pool, std::string& why
) {
    std::vector<ts::Location> locations;
    if (!ts::Resolve(schema, path, ts::VectorCount(schema, row.data(), row.size()), row.size(), locations, why)) {
        return false;
    }
    for (const ts::Location& location : locations) {
        if (!ts::Write(row, location, value, pool, why)) {
            return false;
        }
    }
    return true;
}

// The id a reference names, looking an enum name up in its table.
bool RefId(const ArtRef& ref, const char* file, const ArtTables& tables, uint64_t& id, std::string& why) {
    if (ref.row) {
        id = ref.row->id;
        return true;
    }
    if (ref.enumName.empty()) {
        id = ref.id;
        return true;
    }
    return tables.EnumId(file, ref.enumName, id, why);
}

// A copy of the base row, or a new row with elements elements.
bool StartRow(
    const ArtSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId, uint32_t elements,
    blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
) {
    if (mod.baseId == 0) {
        row = ts::NewRow(*mod.schema, elements, pool);
        return true;
    }
    const auto base = byId.find(mod.baseId);
    if (base == byId.end()) {
        why = "its base " + std::to_string(mod.baseId) + " is not in " + mod.schema->file;
        return false;
    }
    const blob::Row& source = table.rows[base->second];
    if (!ts::RowFits(*mod.schema, source.data, source.size)) {
        why = "its base row has an unexpected layout";
        return false;
    }
    row.assign(source.data, source.data + source.size);
    if (elements > ts::VectorCount(*mod.schema, row.data(), row.size())) {
        ts::Resize(*mod.schema, row, elements);
    }
    return true;
}

bool BuildTexture(const ArtSpec& mod, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why) {
    row = ts::NewRow(ts::kTexture, 0, pool);
    return Set(ts::kTexture, row, "fileName", mod.gamePath, pool, why);
}

bool BuildIcon(
    const ArtSpec& mod, const ArtTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
) {
    uint64_t texture = 0;
    if (!RefId(mod.texture, ts::kTexture.file, tables, texture, why)) {
        return false;
    }
    row = ts::NewRow(ts::kIcon, 0, pool);
    const char* const names[] = {"x", "y", "w", "h", "drawW", "drawH", "offsetX", "offsetY"};
    const std::string values[] = {mod.rect[0], mod.rect[1], mod.rect[2], mod.rect[3],
                                  mod.size[0], mod.size[1], mod.offset[0], mod.offset[1]};
    for (size_t index = 0; index < 8; ++index) {
        if (!Set(ts::kIcon, row, std::string("drawInfo.") + names[index], values[index], pool, why)) {
            return false;
        }
    }
    return Set(ts::kIcon, row, "textureID", std::to_string(texture), pool, why);
}

bool BuildSkin(
    const ArtSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId,
    const ArtTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
) {
    if (!StartRow(mod, table, byId, 0, pool, row, why)) {
        return false;
    }
    // The base's names, read before any swap is added.
    const uint32_t baseCount = ts::VectorCount(ts::kAnimeReplaceTexture, row.data(), row.size());
    std::vector<std::string> names(baseCount);
    for (uint32_t element = 0; element < baseCount; ++element) {
        std::vector<ts::Location> locations;
        if (ts::Resolve(
                ts::kAnimeReplaceTexture, "entries." + std::to_string(element) + ".name", baseCount, row.size(),
                locations, why
            )) {
            const blob::Row copy{row.data(), static_cast<uint32_t>(row.size())};
            blob::ReadText(table, copy, locations.front().offset, names[element]);
        }
    }
    for (const ArtSwap& swap : mod.swaps) {
        uint64_t texture = 0;
        if (!RefId(swap.texture, ts::kTexture.file, tables, texture, why)) {
            return false;
        }
        uint32_t element = 0;
        while (element < baseCount && platform::CompareNoCase(names[element].c_str(), swap.name.c_str()) != 0) {
            ++element;
        }
        if (element == baseCount) {
            element = ts::VectorCount(ts::kAnimeReplaceTexture, row.data(), row.size());
            ts::Resize(ts::kAnimeReplaceTexture, row, element + 1);
            if (!Set(ts::kAnimeReplaceTexture, row, "entries." + std::to_string(element) + ".name", swap.name, pool,
                     why)) {
                return false;
            }
        }
        if (!Set(ts::kAnimeReplaceTexture, row, "entries." + std::to_string(element) + ".texture",
                 std::to_string(texture), pool, why)) {
            return false;
        }
    }
    return true;
}

bool BuildRaw(
    const ArtSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId,
    const ArtTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
) {
    if (!StartRow(mod, table, byId, mod.elements, pool, row, why)) {
        return false;
    }
    for (const ArtField& field : mod.fields) {
        std::string value = field.value;
        if (field.refTable) {
            uint64_t id = 0;
            if (!RefId(field.ref, field.refTable, tables, id, why)) {
                return false;
            }
            value = std::to_string(id);
        }
        if (!Set(*mod.schema, row, field.path, value, pool, why)) {
            return false;
        }
    }
    return true;
}

// ---- Desktop check -------------------------------------------------------

RefKind RefKindOf(const ts::TableSchema& schema) {
    if (&schema == &ts::kIcon) {
        return RefKind::Icon;
    }
    return &schema == &ts::kTexture ? RefKind::Texture : RefKind::Skin;
}

// The game's tables as the vanilla archives have them. A mod row's id is
// always fine, since the reader resolved it, and a reference the title
// checks is left to TitleNotes.
class VanillaTables : public ArtTables {
public:
    explicit VanillaTables(const VanillaData& vanilla) : vanilla_(vanilla) {}

    bool EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const override {
        const ts::TableSchema* schema = ts::Find(file);
        // A mod row's enum name is made as the table loads, so only the game
        // can look it up: any id of the band stands in for it.
        if (schema && schema->enumStem && name.rfind(schema->enumStem, 0) == 0) {
            id = schema->idBandStart;
            return true;
        }
        const blob::Table* table = vanilla_.Table(file);
        if (!schema || !table) {
            id = schema ? schema->idBandStart : 0;
            return true;
        }
        for (const blob::Row& row : table->rows) {
            std::string text;
            if (blob::ReadText(*table, row, static_cast<size_t>(schema->enumOffset), text) && text == name) {
                id = blob::RowId(row);
                return true;
            }
        }
        why = std::string(file) + " has no " + name;
        return false;
    }

    bool RefFine(const ts::TableSchema&, const char* file, uint64_t id, bool titleChecks, std::string& why)
        const override {
        const ts::TableSchema* target = ts::Find(file);
        if (titleChecks || (target && InBand(*target, id)) || !vanilla_.Table(file) || vanilla_.HasRow(file, id)) {
            return true;
        }
        why = std::string("it names ") + (target ? target->section : file) + " " + std::to_string(id) +
              ", which is not in " + file;
        return false;
    }

private:
    const VanillaData& vanilla_;
};

// The references the game checks at the title, where a missing one turns the
// row off. The loader says so with the row's id, so a desktop check notes it.
void TitleNotes(const ArtSpec& mod, const std::vector<uint8_t>& row, const VanillaData& vanilla, Report& report) {
    const ts::TableSchema& schema = *mod.schema;
    for (size_t index = 0; index < schema.fieldCount; ++index) {
        const ts::FieldSpec& field = schema.fields[index];
        if ((field.type != FT::Ref64 && field.type != FT::Ref32) || !field.refTable || field.recordOffset < 0) {
            continue;
        }
        const ts::TableSchema* target = ts::Find(field.refTable);
        const size_t at = static_cast<size_t>(field.offset);
        const uint64_t id = field.type == FT::Ref64 ? blob::Get<uint64_t>(row.data(), at)
                                                    : blob::Get<uint32_t>(row.data(), at);
        if (!target || InBand(*target, id) || !vanilla.Table(field.refTable) || vanilla.HasRow(field.refTable, id)) {
            continue;
        }
        report.Note(
            mod.folder, mod.line,
            mod.label + " stays off at the title, its " + target->section + " " + std::to_string(id) +
                " is not in the " + target->section + " table."
        );
    }
}

}  // namespace

const SectionInfo kTextureSection = {"texture.", "Texture", "docs/ART.md", kTextureFields, std::size(kTextureFields)};
const SectionInfo kIconSection = {"icon.", "Icon", "docs/ART.md", kIconFields, std::size(kIconFields)};
const SectionInfo kSkinSection = {"skin.", "Skin", "docs/ART.md", kSkinFields, std::size(kSkinFields)};
const SectionInfo kRowSection = {"row.", "Raw row", "docs/ROWS.md", kRowFields, std::size(kRowFields)};

bool InBand(const ts::TableSchema& schema, uint64_t id) {
    return id >= ts::BandStart(schema) && id < ts::BandStart(schema) + ts::BandSize(schema);
}

std::vector<ArtSpec> ReadArt(
    const std::vector<ModFolder>& folders, const std::string& modsDir, Report& report, const ArtHost& host
) {
    std::vector<ArtSpec> all;
    std::vector<std::string> owners;
    for (const ModFolder& folder : folders) {
        if (!HasArt(folder)) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Art in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Art in folder " + folder.name + " skipped, another folder already has art under the key " + modKey +
                    "."
            );
            continue;
        }
        std::vector<ArtSpec> rows;
        ReadFolder(folder, modKey, modsDir, all.size(), host, report, rows);
        if (rows.empty() || (host.accept && !host.accept(folder, modKey, rows))) {
            continue;
        }
        owners.push_back(modKey);
        for (ArtSpec& row : rows) {
            all.push_back(std::move(row));
        }
    }
    return all;
}

void ResolveArt(const std::vector<ArtSpec*>& rows, const ArtNumbers& number, Report& report) {
    for (const ts::TableSchema* schema : ts::All()) {
        std::vector<ArtSpec*> kept;
        for (ArtSpec* row : rows) {
            if (row->schema == schema && ResolveRow(rows, *row, report)) {
                kept.push_back(row);
            }
        }
        if (kept.empty()) {
            continue;
        }
        std::vector<std::string> keys;
        for (const ArtSpec* row : kept) {
            keys.push_back(row->fullKey);
        }
        std::vector<uint64_t> numbers;
        if (number) {
            if (!number(*schema, keys, numbers)) {
                continue;
            }
        } else {
            for (size_t index = 0; index < keys.size(); ++index) {
                numbers.push_back(index + 1);
            }
        }
        for (size_t index = 0; index < kept.size() && index < numbers.size(); ++index) {
            kept[index]->id = numbers[index] ? ts::BandStart(*schema) + numbers[index] : 0;
        }
    }
}

const ArtSpec* FindArt(
    const std::vector<ArtSpec*>& rows, const ts::TableSchema& schema, const std::string& modKey, const std::string& key
) {
    for (const ArtSpec* row : rows) {
        if (row->schema == &schema && row->id != 0 && platform::CompareNoCase(row->modKey.c_str(), modKey.c_str()) == 0 &&
            platform::CompareNoCase(row->key.c_str(), key.c_str()) == 0) {
            return row;
        }
    }
    return nullptr;
}

std::string CountArt(const std::vector<const ArtSpec*>& rows) {
    std::vector<std::string> parts;
    for (const ts::TableSchema* schema : ts::All()) {
        const size_t count = static_cast<size_t>(
            std::count_if(rows.begin(), rows.end(), [&](const ArtSpec* row) { return row->schema == schema; })
        );
        if (count == 0) {
            continue;
        }
        const std::string one = schema == &ts::kAnimeReplaceTexture ? "skin" : schema->section;
        parts.push_back(Plural(count, one.c_str(), (one + "s").c_str()));
    }
    std::string text;
    for (size_t index = 0; index < parts.size(); ++index) {
        text += (index == 0 ? "" : index + 1 == parts.size() ? " and " : ", ") + parts[index];
    }
    return text;
}

// ---- Texture files -------------------------------------------------------

bool ReadArtFile(const std::wstring& path, std::vector<uint8_t>& bytes) {
    // An empty file is no texture.
    if (!platform::ReadWholeFile(path, kMaxTextureBytes, bytes) || bytes.empty()) {
        bytes.clear();
        return false;
    }
    return true;
}

bool LoadNltx(ArtSpec& row, std::string& why) {
    std::vector<uint8_t> bytes;
    nltx::Header header;
    std::string error;
    if (!ReadArtFile(row.source, bytes)) {
        why = row.file + " could not be read";
        return false;
    }
    if (!nltx::ParseHeader(bytes.data(), bytes.size(), header, error)) {
        why = row.file + " is not a texture the game reads (" + error + ")";
        return false;
    }
    row.width = header.width;
    row.height = header.height;
    return true;
}

bool DecodeImage(
    const ArtSpec& row, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& why
) {
    std::string error;
    if (!image_io::Load(row.source, width, height, pixels, error)) {
        why = row.file + " could not be read as an image (" + error + ")";
        return false;
    }
    if (width > kMaxTextureSide || height > kMaxTextureSide) {
        why = row.file + " is " + std::to_string(width) + "x" + std::to_string(height) +
              ", larger than the 8192x8192 Canopy takes";
        return false;
    }
    return true;
}

bool LoadTexture(ArtSpec& row, std::string& why) {
    if (row.nltx) {
        return LoadNltx(row, why);
    }
    const platform::FileInfo info = platform::Stat(row.source);
    if (!info.exists || info.folder) {
        why = row.file + " could not be found";
        return false;
    }
    std::vector<uint8_t> pixels;
    return DecodeImage(row, row.width, row.height, pixels, why);
}

// ---- Rows ----------------------------------------------------------------

bool BuildArtRow(
    const ArtSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId,
    const ArtTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
) {
    switch (mod.kind) {
    case ArtKind::Texture:
        return BuildTexture(mod, pool, row, why);
    case ArtKind::Icon:
        return BuildIcon(mod, tables, pool, row, why);
    case ArtKind::Skin:
        return BuildSkin(mod, table, byId, tables, pool, row, why);
    default:
        return BuildRaw(mod, table, byId, tables, pool, row, why);
    }
}

bool CheckArtRefs(
    const ts::TableSchema& schema, const std::vector<uint8_t>& row, const ArtTables& tables, std::string& why
) {
    for (size_t index = 0; index < schema.fieldCount; ++index) {
        const ts::FieldSpec& field = schema.fields[index];
        if ((field.type == FT::Ref64 || field.type == FT::Ref32) && field.refTable) {
            const size_t at = static_cast<size_t>(field.offset);
            const uint64_t id = field.type == FT::Ref64 ? blob::Get<uint64_t>(row.data(), at)
                                                        : blob::Get<uint32_t>(row.data(), at);
            if (!tables.RefFine(schema, field.refTable, id, field.recordOffset >= 0, why)) {
                return false;
            }
        }
        if (field.type != FT::Vector) {
            continue;
        }
        const uint32_t count = ts::VectorCount(schema, row.data(), row.size());
        for (uint32_t element = 0; element < count; ++element) {
            for (uint32_t inner = 0; inner < field.elementCount; ++inner) {
                const ts::FieldSpec& part = field.element[inner];
                if ((part.type != FT::Ref64 && part.type != FT::Ref32) || !part.refTable) {
                    continue;
                }
                const size_t at = static_cast<size_t>(field.offset) + 4 + static_cast<size_t>(field.stride) * element +
                                  static_cast<size_t>(part.offset);
                const uint64_t id = part.type == FT::Ref64 ? blob::Get<uint64_t>(row.data(), at)
                                                           : blob::Get<uint32_t>(row.data(), at);
                if (!tables.RefFine(schema, part.refTable, id, false, why)) {
                    why = std::string(field.name) + " " + std::to_string(element) + ": " + why;
                    return false;
                }
            }
        }
    }
    return true;
}

void CheckArt(Context& context) {
    std::vector<ArtSpec> rows = ReadArt(context.folders, context.modsDir, context.report, ArtHost());
    std::vector<ArtSpec*> pointers;
    for (ArtSpec& row : rows) {
        pointers.push_back(&row);
    }
    ResolveArt(pointers, ArtNumbers(), context.report);
    for (ArtSpec* row : pointers) {
        if (row->id != 0) {
            row->where = row->label;
            context.refs.Add(RefKindOf(*row->schema), row->fullKey);
        }
    }
    if (!context.vanilla) {
        return;
    }
    const VanillaTables tables(*context.vanilla);
    for (const ts::TableSchema* schema : ts::All()) {
        const blob::Table* table = context.vanilla->Table(schema->file);
        if (!table) {
            continue;
        }
        std::unordered_map<uint64_t, size_t> byId;
        for (size_t index = 0; index < table->rows.size(); ++index) {
            byId.emplace(blob::RowId(table->rows[index]), index);
        }
        blob::Pool pool(table->poolBytes);
        for (const ArtSpec* mod : pointers) {
            if (mod->schema != schema || mod->id == 0) {
                continue;
            }
            std::vector<uint8_t> row;
            std::string why;
            if (!BuildArtRow(*mod, *table, byId, tables, pool, row, why) || !CheckArtRefs(*schema, row, tables, why)) {
                context.report.Error(mod->folder, mod->line, mod->label + " skipped, " + why + ".");
                continue;
            }
            TitleNotes(*mod, row, *context.vanilla, context.report);
        }
    }
}

}  // namespace mod_rules
