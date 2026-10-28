#include "game_browse.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "archive.h"
#include "canopy_ini.h"
#include "game_addresses.h"
#include "json_out.h"
#include "nltx.h"
#include "platform_files.h"
#include "rules_report.h"
#include "table_schema.h"

// =============================================================
// GAME BROWSE
// =============================================================
// What the desktop tools show of a game install. Each call opens the
// archives for itself and keeps nothing, so a tool can call from any thread.

namespace game_browse {
namespace {

using json_out::PutKey;
using json_out::PutOptional;
using json_out::PutString;
using table_schema::FieldSpec;
using FT = table_schema::FieldType;

constexpr const char* kDatabaseFolder = "data/database/";
// The longest text a guessed ref may point at.
constexpr uint32_t kMaxGuessedText = 4096;

bool Exists(const std::wstring& path, bool folder) {
    const platform::FileInfo info = platform::Stat(path);
    return info.exists && info.folder == folder;
}

// The PE header of the exe at path, zeros when it has none.
game_addresses::Header HeaderOf(const std::wstring& path) {
    std::vector<uint8_t> start;
    game_addresses::Header header;
    platform::ReadFileStart(path, 4096, start);
    game_addresses::ReadHeader({start.data(), start.size(), 0}, header);
    return header;
}

// An archive's path from the game folder, such as data\x_1_00.dat.
std::string Relative(const std::wstring& path, const std::wstring& folder) {
    size_t start = path.compare(0, folder.size(), folder) == 0 ? folder.size() : 0;
    while (start < path.size() && (path[start] == L'\\' || path[start] == L'/')) {
        ++start;
    }
    return mod_rules::ToUtf8(path.substr(start));
}

// The table file a name means: lowercase, ".dat" added when missing.
// False for a name that is not a bare file name.
bool TableFile(const std::string& name, std::string& file) {
    file = canopy_ini::Lower(name);
    if (file.empty() || file.find_first_of("/\\:") != std::string::npos || file.find("..") != std::string::npos) {
        return false;
    }
    if (file.size() < 4 || file.compare(file.size() - 4, 4, ".dat") != 0) {
        file += ".dat";
    }
    return true;
}

void Open(std::string& out) {
    out = "{";
    PutKey(out, "abi");
    out += std::to_string(mod_rules::kJsonAbi) + ",";
}

void PutOk(std::string& out, bool ok, const std::string& error) {
    PutKey(out, "ok");
    out += ok ? "true," : "false,";
    PutKey(out, "error");
    PutOptional(out, ok ? nullptr : error.c_str());
}

std::string Number(uint64_t value) {
    return std::to_string(value);
}

std::string Real(float value) {
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%.9g", static_cast<double>(value));
    return text;
}

// The schema that describes file: a patchable one, and for a framework_
// twin its table's, since both files hold rows of one record. Null when
// Canopy has none.
const table_schema::TableSchema* SchemaOf(const std::string& file) {
    static const std::string kTwin = "framework_";
    const table_schema::TableSchema* schema = table_schema::FindPatchable(file);
    if (!schema && file.compare(0, kTwin.size(), kTwin) == 0) {
        schema = table_schema::FindPatchable(file.substr(kTwin.size()));
    }
    return schema;
}

// The row's enum name: the text at the schema's enum offset, or for a table
// without a schema the text at +8 when it reads as one (A-Z, 0-9 and _).
// None for a schema whose rows have no enum name.
std::string EnumOf(const blob::Table& table, const blob::Row& row, const table_schema::TableSchema* schema) {
    if (schema && schema->enumOffset < 0) {
        return {};
    }
    const size_t at = schema ? static_cast<size_t>(schema->enumOffset) : 8;
    std::string text;
    if (!blob::ReadText(table, row, at, text) || text.empty()) {
        return {};
    }
    if (!schema) {
        for (const char c : text) {
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
                return {};
            }
        }
    }
    return text;
}

// ---- Fields --------------------------------------------------------------

const char* TypeName(FT type) {
    switch (type) {
        case FT::Id:
            return "id";
        case FT::Enum:
            return "enum";
        case FT::Text:
        case FT::MLText:
            return "text";
        case FT::MLPath:
            return "path";
        case FT::I32:
            return "i32";
        case FT::U32:
            return "u32";
        case FT::F32:
            return "f32";
        case FT::Bool:
            return "bool";
        case FT::U64:
            return "u64";
        case FT::I64:
            return "i64";
        case FT::Ref64:
        case FT::Ref32:
            return "ref";
        case FT::FlagRef:
            return "flag";
        case FT::Array:
        case FT::Vector:
        case FT::Trailer:
        case FT::Pad:
        case FT::Struct:
            break;
    }
    return "bytes";
}

// The type a tool shows for a place: a per-language path stays a path,
// though each language of it is a text.
const char* PlaceType(const table_schema::Location& place) {
    return place.field && place.field->type == FT::MLPath ? "path" : TypeName(place.type);
}

// The table a reference place points into, null for any other place.
const char* PlaceTable(const table_schema::Location& place) {
    const bool reference = place.type == FT::Ref64 || place.type == FT::Ref32;
    return reference && place.field ? place.field->refTable : nullptr;
}

// What a row shows of a table its schema does not describe: every field
// the schema places within the row's bytes, by fixed offset, none of them
// editable.
class FieldWriter {
public:
    FieldWriter(std::string& out, const blob::Table& table, const blob::Row& row)
        : out_(out), table_(table), row_(row) {}

    void Table(const table_schema::TableSchema& schema) {
        for (size_t index = 0; index < schema.fieldCount; ++index) {
            const FieldSpec& field = schema.fields[index];
            const size_t base = field.offset < 0 ? row_.size - std::min<size_t>(row_.size, -field.offset)
                                                 : static_cast<size_t>(field.offset);
            switch (field.type) {
                case FT::Array:
                    Elements(field, base, field.count);
                    break;
                case FT::Vector:
                    if (base + 4 <= row_.size) {
                        Put(std::string(field.name) + ".count", "u32", Number(blob::Get<uint32_t>(row_.data, base)),
                            nullptr);
                        Elements(field, base + 4, blob::Get<uint32_t>(row_.data, base));
                    }
                    break;
                case FT::Trailer:
                    Trailer(field, base);
                    break;
                case FT::Pad:
                    break;
                default:
                    Leaf(field, base, "");
                    break;
            }
        }
    }

private:
    void Put(const std::string& path, const char* type, const std::string& value, const char* table) {
        out_ += first_ ? "{" : ",{";
        first_ = false;
        PutKey(out_, "path");
        PutString(out_, path);
        out_ += ',';
        PutKey(out_, "type");
        PutString(out_, type);
        out_ += ',';
        PutKey(out_, "value");
        PutString(out_, value);
        out_ += ',';
        PutKey(out_, "table");
        PutOptional(out_, table);
        out_ += ',';
        PutKey(out_, "editable");
        out_ += "false}";
    }

    // Stops at the row's end: a short or damaged row shows what it has.
    void Elements(const FieldSpec& field, size_t start, uint32_t count) {
        for (uint32_t element = 0; element < count; ++element) {
            const size_t at = start + static_cast<size_t>(field.stride) * element;
            if (at + field.stride > row_.size) {
                return;
            }
            for (uint32_t inner = 0; inner < field.elementCount; ++inner) {
                const FieldSpec& part = field.element[inner];
                Leaf(part, at + static_cast<size_t>(part.offset),
                     std::string(field.name) + "." + std::to_string(element) + ".");
            }
        }
    }

    void Trailer(const FieldSpec& field, size_t base) {
        static const char* const kV16[] = {"version", "region", "product", "platform"};
        static const char* const kV20[] = {"version", "disableVersion", "region", "product", "platform"};
        const bool v20 = field.size == 20;
        const char* const* names = v20 ? kV20 : kV16;
        const size_t count = v20 ? 5 : 4;
        for (size_t index = 0; index < count; ++index) {
            const size_t at = base + 4 * index;
            if (at + 4 <= row_.size) {
                Put(std::string(field.name) + "." + names[index], "u32",
                    Number(blob::Get<uint32_t>(row_.data, at)), nullptr);
            }
        }
    }

    void Leaf(const FieldSpec& field, size_t at, const std::string& prefix) {
        // An element that is one value has an unnamed field: feature.3.
        std::string name = prefix + field.name;
        if (field.name[0] == '\0' && !name.empty() && name.back() == '.') {
            name.pop_back();
        }
        if (field.type == FT::Struct) {
            for (uint32_t inner = 0; inner < field.elementCount; ++inner) {
                const FieldSpec& part = field.element[inner];
                Leaf(part, at + static_cast<size_t>(part.offset), name + ".");
            }
            return;
        }
        if (field.type == FT::MLText || field.type == FT::MLPath) {
            for (int language = 0; language < table_schema::kLanguageCount; ++language) {
                Text(name + "." + table_schema::kLanguageKeys[language], TypeName(field.type),
                     at + blob::kTextSize * static_cast<size_t>(language));
            }
            return;
        }
        if (field.type == FT::Id || field.type == FT::Enum || field.type == FT::Text) {
            if (field.type == FT::Id) {
                if (at + 8 <= row_.size) {
                    Put(name, "id", Number(blob::Get<uint64_t>(row_.data, at)), nullptr);
                }
                return;
            }
            Text(name, TypeName(field.type), at);
            return;
        }
        if (at + field.size > row_.size) {
            return;
        }
        std::string value;
        switch (field.type) {
            case FT::I32:
            case FT::FlagRef:
                value = std::to_string(blob::Get<int32_t>(row_.data, at));
                break;
            case FT::U32:
            case FT::Ref32:
                value = Number(blob::Get<uint32_t>(row_.data, at));
                break;
            case FT::F32:
                value = Real(blob::Get<float>(row_.data, at));
                break;
            case FT::Bool:
                value = (field.size == 1 ? row_.data[at] : blob::Get<uint32_t>(row_.data, at)) != 0 ? "true" : "false";
                break;
            case FT::U64:
            case FT::Ref64:
                value = Number(blob::Get<uint64_t>(row_.data, at));
                break;
            case FT::I64:
                value = std::to_string(blob::Get<int64_t>(row_.data, at));
                break;
            default:
                return;
        }
        Put(name, TypeName(field.type), value, field.refTable);
    }

    void Text(const std::string& path, const char* type, size_t at) {
        std::string text;
        if (blob::ReadText(table_, row_, at, text)) {
            Put(path, type, text, nullptr);
        }
    }

    std::string& out_;
    const blob::Table& table_;
    const blob::Row& row_;
    bool first_ = true;
};

// The row's places as the schema lays them out, each with the text
// table_schema::Read gives, the one a patch takes. False, with nothing
// written, when the row does not fit the schema.
bool PutPlaces(
    std::string& out, const blob::Table& table, const blob::Row& row, const table_schema::TableSchema& schema
) {
    std::vector<table_schema::Location> places;
    std::string error;
    if (!table_schema::Layout(schema, row.data, row.size, places, error)) {
        return false;
    }
    bool first = true;
    for (const table_schema::Location& place : places) {
        // A value Read refuses is still held, for display.
        std::string value;
        table_schema::Read(row.data, row.size, place, table, nullptr, value, error);
        out += first ? "{" : ",{";
        first = false;
        PutKey(out, "path");
        PutString(out, place.path);
        out += ',';
        PutKey(out, "type");
        PutString(out, PlaceType(place));
        out += ',';
        PutKey(out, "value");
        PutString(out, value);
        out += ',';
        PutKey(out, "table");
        PutOptional(out, PlaceTable(place));
        out += ',';
        PutKey(out, "editable");
        out += place.editable ? "true}" : "false}";
    }
    return true;
}

// Each 8 bytes of the row that point at a whole text of the pool: at its
// start, ending at its NUL, with no NUL inside, and valid UTF-8.
void PutTexts(std::string& out, const blob::Table& table, const blob::Row& row) {
    bool first = true;
    size_t offset = 0;
    while (offset + blob::kTextSize <= row.size) {
        const uint32_t start = blob::Get<uint32_t>(row.data, offset);
        const uint32_t length = blob::Get<uint32_t>(row.data, offset + 4);
        bool hit = length > 0 && length <= kMaxGuessedText &&
                   static_cast<uint64_t>(start) + length < table.poolBytes && table.pool[start + length] == 0 &&
                   (start == 0 || table.pool[start - 1] == 0);
        if (hit) {
            const std::string text(reinterpret_cast<const char*>(table.pool + start), length);
            hit = text.find('\0') == std::string::npos && !text.empty() && platform::IsUtf8(text);
            if (hit) {
                out += first ? "{" : ",{";
                first = false;
                PutKey(out, "offset");
                out += std::to_string(offset) + ",";
                PutKey(out, "text");
                PutString(out, text);
                out += '}';
            }
        }
        offset += hit ? blob::kTextSize : 4;
    }
}

void PutHex(std::string& out, const blob::Row& row) {
    static const char kDigits[] = "0123456789abcdef";
    out += '"';
    for (uint32_t index = 0; index < row.size; ++index) {
        out += kDigits[row.data[index] >> 4];
        out += kDigits[row.data[index] & 15];
    }
    out += '"';
}

// ---- Loading -------------------------------------------------------------

struct LoadedTable {
    std::vector<uint8_t> bytes;
    blob::Table table;
};

bool LoadTable(const std::wstring& gameFolder, const std::string& file, LoadedTable& loaded, std::string& error) {
    archive::GameData data;
    if (!data.Open(gameFolder, error)) {
        return false;
    }
    if (!data.FindEntry(kDatabaseFolder + file)) {
        error = file + " is not in the game's archives";
        return false;
    }
    if (!data.Read(kDatabaseFolder + file, loaded.bytes, error)) {
        return false;
    }
    if (!blob::Parse(loaded.bytes.data(), loaded.bytes.size(), loaded.table, error)) {
        error = file + " is not a table: " + error;
        return false;
    }
    return true;
}

}  // namespace

std::string FailureJson(const std::string& error) {
    std::string out;
    Open(out);
    PutOk(out, false, error);
    out += '}';
    return out;
}

std::string GameJson(const std::wstring& gameFolder) {
    std::wstring folder = gameFolder;
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) {
        folder.pop_back();
    }
    // Nothing is there without a folder, not even at the root of the drive.
    const auto there = [&](const wchar_t* name, bool isFolder) {
        return !folder.empty() && Exists(platform::Join(folder, name), isFolder);
    };
    const bool exe = there(L"village.exe", false);
    const game_addresses::Header header = exe ? HeaderOf(platform::Join(folder, L"village.exe")) : game_addresses::Header();
    const uint32_t build = header.timestamp;
    archive::GameData data;
    std::string error;
    const bool ok = !folder.empty() && data.Open(folder, error);
    if (folder.empty()) {
        error = "No game folder was given.";
    }
    std::map<std::string, size_t> names;
    if (ok) {
        for (size_t index = 0; index < data.ArchiveCount(); ++index) {
            for (const archive::Entry& entry : data.ArchiveAt(index).Entries()) {
                ++names[entry.name];
            }
        }
    }
    size_t tables = 0;
    const std::string database = kDatabaseFolder;
    for (const auto& name : names) {
        const std::string& path = name.first;
        if (path.compare(0, database.size(), database) == 0 && path.size() > database.size() + 4 &&
            path.find('/', database.size()) == std::string::npos &&
            path.compare(path.size() - 4, 4, ".dat") == 0) {
            ++tables;
        }
    }

    std::string out;
    Open(out);
    PutOk(out, ok, error);
    out += ',';
    PutKey(out, "folder");
    PutString(out, mod_rules::ToUtf8(folder));
    out += ',';
    PutKey(out, "exe");
    out += exe ? "true," : "false,";
    PutKey(out, "build");
    out += Number(build) + ",";
    PutKey(out, "supported");
    out += exe && game_addresses::IsChecked(header) ? "true," : "false,";
    PutKey(out, "mods");
    out += there(L"mods", true) ? "true," : "false,";
    PutKey(out, "canopy");
    out += there(L"dinput8.dll", false) ? "true," : "false,";
    PutKey(out, "log");
    out += there(L"Canopy.log", false) ? "true," : "false,";
    PutKey(out, "archives");
    out += '[';
    for (size_t index = 0; ok && index < data.ArchiveCount(); ++index) {
        const archive::Archive& part = data.ArchiveAt(index);
        out += index == 0 ? "{" : ",{";
        PutKey(out, "name");
        PutString(out, Relative(part.Path(), folder));
        out += ',';
        PutKey(out, "version");
        out += Number(part.Version()) + ",";
        PutKey(out, "entries");
        out += Number(part.Entries().size()) + "}";
    }
    out += "],";
    PutKey(out, "files");
    out += Number(names.size()) + ",";
    PutKey(out, "tables");
    out += Number(tables) + "}";
    return out;
}

std::string FilesJson(const std::wstring& gameFolder) {
    archive::GameData data;
    std::string error;
    if (!data.Open(gameFolder, error)) {
        return FailureJson(error);
    }
    std::map<std::string, size_t> copies;
    for (size_t index = 0; index < data.ArchiveCount(); ++index) {
        for (const archive::Entry& entry : data.ArchiveAt(index).Entries()) {
            ++copies[entry.name];
        }
    }
    std::string out;
    Open(out);
    PutOk(out, true, error);
    out += ',';
    PutKey(out, "files");
    out += '[';
    bool first = true;
    for (const auto& name : copies) {
        const archive::Location location = data.Find(name.first);
        if (!location.Found()) {
            // Too long for the game to look up, so never read.
            continue;
        }
        const archive::Archive& part = data.ArchiveAt(location.archive);
        const archive::Entry& entry = part.Entries()[location.entry];
        out += first ? "{" : ",{";
        first = false;
        PutKey(out, "path");
        PutString(out, name.first);
        out += ',';
        PutKey(out, "size");
        out += Number(entry.size) + ",";
        PutKey(out, "time");
        out += Number(entry.timestamp) + ",";
        PutKey(out, "archive");
        PutString(out, Relative(part.Path(), data.Folder()));
        out += ',';
        PutKey(out, "copies");
        out += Number(name.second) + "}";
    }
    out += "]}";
    return out;
}

std::string TableJsonOf(const blob::Table& table, const std::string& file) {
    const table_schema::TableSchema* schema = SchemaOf(file);
    std::string out;
    Open(out);
    PutOk(out, true, "");
    out += ',';
    PutKey(out, "file");
    PutString(out, file);
    out += ',';
    PutKey(out, "struct");
    PutOptional(out, schema ? schema->structName : nullptr);
    out += ',';
    PutKey(out, "schema");
    out += schema ? "true," : "false,";
    PutKey(out, "poolBytes");
    out += Number(table.poolBytes) + ",";
    PutKey(out, "rows");
    out += '[';
    for (size_t index = 0; index < table.rows.size(); ++index) {
        const blob::Row& row = table.rows[index];
        out += index == 0 ? "{" : ",{";
        PutKey(out, "index");
        out += Number(index) + ",";
        PutKey(out, "id");
        out += Number(blob::RowId(row)) + ",";
        PutKey(out, "enum");
        const std::string name = EnumOf(table, row, schema);
        PutOptional(out, name.empty() ? nullptr : name.c_str());
        out += ',';
        PutKey(out, "size");
        out += Number(row.size) + "}";
    }
    out += "]}";
    return out;
}

std::string RowJsonOf(const blob::Table& table, const std::string& file, size_t index) {
    if (index >= table.rows.size()) {
        return FailureJson(file + " has " + std::to_string(table.rows.size()) + " rows, so no row " +
                           std::to_string(index));
    }
    const table_schema::TableSchema* schema = SchemaOf(file);
    const blob::Row& row = table.rows[index];
    std::string out;
    Open(out);
    PutOk(out, true, "");
    out += ',';
    PutKey(out, "file");
    PutString(out, file);
    out += ',';
    PutKey(out, "index");
    out += Number(index) + ",";
    PutKey(out, "id");
    out += Number(blob::RowId(row)) + ",";
    PutKey(out, "enum");
    const std::string name = EnumOf(table, row, schema);
    PutOptional(out, name.empty() ? nullptr : name.c_str());
    out += ',';
    PutKey(out, "size");
    out += Number(row.size) + ",";
    PutKey(out, "fields");
    out += '[';
    if (schema && !PutPlaces(out, table, row, *schema)) {
        FieldWriter(out, table, row).Table(*schema);
    }
    out += "],";
    PutKey(out, "texts");
    out += '[';
    PutTexts(out, table, row);
    out += "],";
    PutKey(out, "hex");
    PutHex(out, row);
    out += '}';
    return out;
}

std::string GridJsonOf(const blob::Table& table, const std::string& file) {
    const table_schema::TableSchema* schema = SchemaOf(file);
    if (!schema) {
        return FailureJson("Canopy has no schema for " + file + ", so it has no columns to show.");
    }
    struct Column {
        std::string path;
        const char* type;
        const char* table;
        bool editable;
    };
    // The columns: each path once, in the order the rows give them. A path
    // a later row adds goes after the path before it in that row, so the
    // elements of a longer vector sit with the vector's other elements.
    std::vector<Column> columns;
    std::map<std::string, size_t> position;
    std::vector<table_schema::Location> places;
    std::string error;
    for (const blob::Row& row : table.rows) {
        places.clear();
        if (!table_schema::Layout(*schema, row.data, row.size, places, error)) {
            continue;
        }
        size_t next = 0;
        for (const table_schema::Location& place : places) {
            const auto found = position.find(place.path);
            if (found != position.end()) {
                Column& column = columns[found->second];
                column.editable = column.editable && place.editable;
                next = found->second + 1;
                continue;
            }
            columns.insert(columns.begin() + static_cast<std::ptrdiff_t>(next),
                           Column{place.path, PlaceType(place), PlaceTable(place), place.editable});
            for (auto& entry : position) {
                if (entry.second >= next) {
                    ++entry.second;
                }
            }
            position[place.path] = next;
            ++next;
        }
    }

    std::string out;
    Open(out);
    PutOk(out, true, "");
    out += ',';
    PutKey(out, "file");
    PutString(out, file);
    out += ',';
    PutKey(out, "struct");
    PutOptional(out, schema->structName);
    out += ',';
    PutKey(out, "columns");
    out += '[';
    for (size_t index = 0; index < columns.size(); ++index) {
        const Column& column = columns[index];
        out += index == 0 ? "{" : ",{";
        PutKey(out, "path");
        PutString(out, column.path);
        out += ',';
        PutKey(out, "type");
        PutString(out, column.type);
        out += ',';
        PutKey(out, "table");
        PutOptional(out, column.table);
        out += ',';
        PutKey(out, "editable");
        out += column.editable ? "true}" : "false}";
    }
    out += "],";
    PutKey(out, "rows");
    out += '[';
    std::vector<std::string> values;
    std::vector<bool> present;
    for (size_t index = 0; index < table.rows.size(); ++index) {
        const blob::Row& row = table.rows[index];
        places.clear();
        const bool fits = table_schema::Layout(*schema, row.data, row.size, places, error);
        values.assign(columns.size(), std::string());
        present.assign(columns.size(), false);
        for (const table_schema::Location& place : places) {
            const size_t at = position[place.path];
            table_schema::Read(row.data, row.size, place, table, nullptr, values[at], error);
            present[at] = true;
        }
        out += index == 0 ? "{" : ",{";
        PutKey(out, "index");
        out += Number(index) + ",";
        PutKey(out, "id");
        out += Number(blob::RowId(row)) + ",";
        PutKey(out, "enum");
        const std::string name = EnumOf(table, row, schema);
        PutOptional(out, name.empty() ? nullptr : name.c_str());
        out += ',';
        PutKey(out, "fits");
        out += fits ? "true," : "false,";
        PutKey(out, "values");
        out += '[';
        for (size_t column = 0; column < columns.size(); ++column) {
            if (column > 0) {
                out += ',';
            }
            if (present[column]) {
                PutString(out, values[column]);
            } else {
                out += "null";
            }
        }
        out += "]}";
    }
    out += "]}";
    return out;
}

std::string GridJson(const std::wstring& gameFolder, const std::string& name) {
    std::string file;
    if (!TableFile(name, file)) {
        return FailureJson(name + " is not a table file name");
    }
    LoadedTable loaded;
    std::string error;
    if (!LoadTable(gameFolder, file, loaded, error)) {
        return FailureJson(error);
    }
    return GridJsonOf(loaded.table, file);
}

std::string TableJson(const std::wstring& gameFolder, const std::string& name) {
    std::string file;
    if (!TableFile(name, file)) {
        return FailureJson(name + " is not a table file name");
    }
    LoadedTable loaded;
    std::string error;
    if (!LoadTable(gameFolder, file, loaded, error)) {
        return FailureJson(error);
    }
    return TableJsonOf(loaded.table, file);
}

std::string RowJson(const std::wstring& gameFolder, const std::string& name, size_t index) {
    std::string file;
    if (!TableFile(name, file)) {
        return FailureJson(name + " is not a table file name");
    }
    LoadedTable loaded;
    std::string error;
    if (!LoadTable(gameFolder, file, loaded, error)) {
        return FailureJson(error);
    }
    return RowJsonOf(loaded.table, file, index);
}

bool Texture(
    const std::wstring& gameFolder, const std::string& gamePath, uint32_t& width, uint32_t& height,
    std::vector<uint8_t>& pixels, std::string& error
) {
    std::vector<uint8_t> bytes;
    if (!File(gameFolder, gamePath, bytes, error)) {
        return false;
    }
    return nltx::Decode(bytes.data(), bytes.size(), width, height, pixels, error);
}

bool File(
    const std::wstring& gameFolder, const std::string& gamePath, std::vector<uint8_t>& bytes, std::string& error
) {
    archive::GameData data;
    if (!data.Open(gameFolder, error)) {
        return false;
    }
    if (!data.FindEntry(gamePath)) {
        error = gamePath + " is not in the game's archives";
        return false;
    }
    return data.Read(gamePath, bytes, error);
}

}  // namespace game_browse
