#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the table blob reader and builder and the table schemas, curated
// and generated. The extracted tables under _work/x are private research
// data, and the generated tables are read from the game's archives: a
// missing file or game prints a note and is never a failure. Nothing here
// prints game text: rows are named by index and fields by name.
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using table_schema::FieldSpec;
using table_schema::FieldType;
using table_schema::Location;
using table_schema::TableSchema;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED table_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

const fs::path kDatabaseDir = fs::u8path(CANOPY_SOURCE_DIR) / "_work" / "x" / "data" / "database";

Bytes ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

Bytes RowBytes(const blob::Row& row) {
    return Bytes(row.data, row.data + row.size);
}

std::string Text(const blob::Table& table, const blob::Row& row, size_t offset) {
    std::string text;
    return blob::ReadText(table, row, offset, text) ? text : std::string("<bad ref>");
}

// The one place path names, or a location with no field.
Location One(const table_schema::TableSchema& schema, const std::string& path, uint32_t count, size_t size) {
    std::vector<Location> locations;
    std::string error;
    if (!table_schema::Resolve(schema, path, count, size, locations, error) || locations.size() != 1) {
        return Location();
    }
    return locations[0];
}

// The one place path names in a row, or a location with no field.
Location Placed(const TableSchema& schema, const std::string& path, const Bytes& row) {
    std::vector<Location> locations;
    std::string error;
    if (!table_schema::ResolveRow(schema, path, row.data(), row.size(), locations, error) || locations.size() != 1) {
        return Location();
    }
    return locations[0];
}

// ---- Blobs ---------------------------------------------------------------

void TestBlobBuildAndParse() {
    std::printf("Table blobs\n");
    const std::string pool("\0alpha\0beta\0", 12);
    const std::vector<Bytes> rows = {Bytes(16, 1), Bytes(24, 2), Bytes()};
    Bytes data;
    blob::Build(rows, pool, data);
    CHECK(data.size() == 12 + 3 * 4 + 40 + pool.size());
    CHECK(blob::Get<uint32_t>(data.data(), 0) == 3);
    CHECK(blob::Get<uint32_t>(data.data(), 4) == 40);
    CHECK(blob::Get<uint32_t>(data.data(), 8) == pool.size());

    blob::Table table;
    std::string error;
    CHECK(blob::Parse(data.data(), data.size(), table, error));
    CHECK(table.rows.size() == 3);
    CHECK(RowBytes(table.rows[0]) == rows[0] && RowBytes(table.rows[1]) == rows[1] && table.rows[2].size == 0);
    CHECK(std::string(reinterpret_cast<const char*>(table.pool), table.poolBytes) == pool);
    CHECK(blob::RowId(table.rows[0]) == 0x0101010101010101ull && blob::RowId(table.rows[2]) == 0);

    // A text ref: offset 1 length 5 is "alpha"; one past the pool is refused.
    Bytes textRow(8);
    blob::Put<uint32_t>(textRow.data(), 0, 1);
    blob::Put<uint32_t>(textRow.data(), 4, 5);
    Bytes textData;
    blob::Build({textRow}, pool, textData);
    blob::Table textTable;
    CHECK(blob::Parse(textData.data(), textData.size(), textTable, error));
    CHECK(Text(textTable, textTable.rows[0], 0) == "alpha");
    std::string text;
    CHECK(!blob::ReadText(textTable, textTable.rows[0], 4, text));
    blob::Put<uint32_t>(textData.data(), 12 + 4 + 4, 12);
    CHECK(blob::Parse(textData.data(), textData.size(), textTable, error));
    CHECK(!blob::ReadText(textTable, textTable.rows[0], 0, text));

    // Damaged blobs.
    CHECK(!blob::Parse(data.data(), 8, table, error));
    CHECK(!blob::Parse(data.data(), data.size() - 1, table, error));
    Bytes badLength = data;
    blob::Put<uint32_t>(badLength.data(), 12, 17);
    CHECK(!blob::Parse(badLength.data(), badLength.size(), table, error));
    Bytes badCount = data;
    blob::Put<uint32_t>(badCount.data(), 0, blob::kMaxRows + 1);
    CHECK(!blob::Parse(badCount.data(), badCount.size(), table, error));
    Bytes empty;
    blob::Build({}, std::string(1, '\0'), empty);
    CHECK(blob::Parse(empty.data(), empty.size(), table, error) && table.rows.empty());
}

void TestPool() {
    std::printf("Text pool\n");
    blob::Pool pool(100);
    CHECK(pool.Base() == 100);
    CHECK(pool.Bytes() == std::string(1, '\0'));
    const blob::Text none = pool.Add("");
    CHECK(none.offset == 100 && none.length == 0);
    const blob::Text first = pool.Add("data/texture/a.nltx");
    CHECK(first.offset == 101 && first.length == 19);
    const blob::Text again = pool.Add("data/texture/a.nltx");
    CHECK(again.offset == first.offset && again.length == first.length);
    const blob::Text second = pool.Add("B");
    CHECK(second.offset == 101 + 20 && second.length == 1);
    CHECK(pool.Bytes() == std::string("\0data/texture/a.nltx\0B\0", 23));
    CHECK(pool.Add("").offset == 100);

    Bytes row(16, 0xEE);
    pool.Write(row, 8, "B");
    CHECK(blob::Get<uint32_t>(row.data(), 8) == second.offset && blob::Get<uint32_t>(row.data(), 12) == 1);
    CHECK(blob::Get<uint32_t>(row.data(), 0) == 0xEEEEEEEE);
}

void TestAppender() {
    std::printf("Appender\n");
    // A vanilla table whose pool, like the game's, starts with text.
    Bytes vanillaRow(16, 0);
    blob::Put<uint64_t>(vanillaRow.data(), 0, 7);
    blob::Put<uint32_t>(vanillaRow.data(), 8, 0);
    blob::Put<uint32_t>(vanillaRow.data(), 12, 4);
    Bytes data;
    blob::Build({vanillaRow}, std::string("SEVN\0", 5), data);
    blob::Table table;
    std::string error;
    CHECK(blob::Parse(data.data(), data.size(), table, error));

    blob::EnumNames enums(table, 8);
    CHECK(enums.Has("SEVN"));
    CHECK(enums.Unique("SEVN") == "SEVN_2");
    CHECK(enums.Unique("MOD") == "MOD");
    CHECK(enums.Unique("MOD") == "MOD_2");

    blob::Appender appender(table);
    Bytes modRow(16, 0);
    blob::Put<uint64_t>(modRow.data(), 0, 1900000001);
    appender.Texts().Write(modRow, 8, "MOD");
    appender.Add(modRow);
    Bytes blankRow(16, 0);
    blob::Put<uint64_t>(blankRow.data(), 0, 1900000002);
    appender.Texts().Write(blankRow, 8, "");
    appender.Add(blankRow);
    CHECK(appender.Count() == 2);

    Bytes merged;
    appender.Merge(merged);
    blob::Table back;
    CHECK(blob::Parse(merged.data(), merged.size(), back, error));
    CHECK(back.rows.size() == 3);
    CHECK(blob::Get<uint32_t>(merged.data(), 4) == 48);
    CHECK(back.poolBytes == 5 + 1 + 4);
    CHECK(Text(back, back.rows[0], 8) == "SEVN");
    CHECK(blob::RowId(back.rows[1]) == 1900000001 && Text(back, back.rows[1], 8) == "MOD");
    CHECK(blob::RowId(back.rows[2]) == 1900000002 && Text(back, back.rows[2], 8).empty());
    // The empty text points at the appended pool's NUL, past the vanilla pool.
    CHECK(blob::Get<uint32_t>(back.rows[2].data, 8) == 5);
}

// ---- Samples -------------------------------------------------------------

void TestSampleRoundTrips() {
    std::printf("Table samples\n");
    if (!fs::is_directory(kDatabaseDir)) {
        std::printf("  note: %s is missing, skipped\n", kDatabaseDir.u8string().c_str());
        return;
    }
    const struct {
        const char* file;
        const table_schema::TableSchema* schema;
    } samples[] = {
        {"texture.dat", &table_schema::kTexture},
        {"framework_texture.dat", &table_schema::kTexture},
        {"icon.dat", &table_schema::kIcon},
        {"framework_icon.dat", &table_schema::kIcon},
        {"animereplacetexture.dat", &table_schema::kAnimeReplaceTexture},
    };
    for (const auto& sample : samples) {
        const Bytes data = ReadBytes(kDatabaseDir / sample.file);
        if (data.empty()) {
            std::printf("  note: %s is missing, skipped\n", sample.file);
            continue;
        }
        blob::Table table;
        std::string error;
        if (!CHECK(blob::Parse(data.data(), data.size(), table, error))) {
            std::printf("  %s: %s\n", sample.file, error.c_str());
            continue;
        }
        std::vector<Bytes> rows;
        size_t fits = 0;
        size_t trailers = 0;
        for (const blob::Row& row : table.rows) {
            rows.push_back(RowBytes(row));
            fits += table_schema::RowFits(*sample.schema, row.data, row.size) ? 1 : 0;
            const size_t trailer = sample.schema->trailer == table_schema::TrailerKind::V20 ? 20 : 16;
            bool zero = true;
            for (size_t index = row.size - trailer + 4; index < row.size; ++index) {
                zero = zero && row.data[index] == 0;
            }
            trailers += zero ? 1 : 0;
        }
        CHECK(fits == table.rows.size());
        // Past the version, every trailer is zero.
        CHECK(trailers == table.rows.size());

        Bytes rebuilt;
        blob::Build(rows, std::string(reinterpret_cast<const char*>(table.pool), table.poolBytes), rebuilt);
        CHECK(rebuilt == data);

        // Appending nothing gives the file back.
        blob::Appender appender(table);
        Bytes merged;
        appender.Merge(merged);
        Bytes withPool = data;
        withPool.push_back(0);
        blob::Put<uint32_t>(withPool.data(), 8, table.poolBytes + 1);
        CHECK(merged == withPool);

        // A new row of the schema appends and reads back.
        blob::Appender adder(table);
        const uint32_t elements = table_schema::VectorField(*sample.schema) ? 2 : 0;
        Bytes row = table_schema::NewRow(*sample.schema, elements, adder.Texts());
        blob::Put<uint64_t>(row.data(), 0, sample.schema->idBandStart + 1);
        blob::EnumNames enums(table, static_cast<size_t>(sample.schema->enumOffset));
        const std::string name = enums.Unique(std::string(sample.schema->enumStem) + "TEST_ROW");
        CHECK(name == std::string(sample.schema->enumStem) + "TEST_ROW");
        std::vector<Location> locations;
        CHECK(table_schema::Resolve(*sample.schema, "comment", elements, row.size(), locations, error));
        CHECK(locations.size() == 1 && table_schema::Write(row, locations[0], "a comment", adder.Texts(), error));
        adder.Texts().Write(row, static_cast<size_t>(sample.schema->enumOffset), name);
        adder.Add(row);
        adder.Merge(merged);
        blob::Table back;
        CHECK(blob::Parse(merged.data(), merged.size(), back, error));
        CHECK(back.rows.size() == table.rows.size() + 1);
        const blob::Row& added = back.rows.back();
        CHECK(blob::RowId(added) == sample.schema->idBandStart + 1);
        CHECK(table_schema::RowFits(*sample.schema, added.data, added.size));
        CHECK(Text(back, added, 8) == name && Text(back, added, 16) == "a comment");
        CHECK(Text(back, back.rows[0], 8) == Text(table, table.rows[0], 8));
        std::printf(
            "  %-24s %5zu rows, rebuilt byte for byte, one row appended\n", sample.file, table.rows.size()
        );
    }
}

// ---- Schemas -------------------------------------------------------------

void TestTextureRow() {
    std::printf("texture.dat rows\n");
    const table_schema::TableSchema& schema = table_schema::kTexture;
    blob::Pool pool(1000);
    Bytes row = table_schema::NewRow(schema, 0, pool);
    CHECK(row.size() == 116);
    CHECK(table_schema::RowFits(schema, row.data(), row.size()));
    std::vector<Location> locations;
    std::string error;
    CHECK(table_schema::Resolve(schema, "fileName", 0, row.size(), locations, error));
    CHECK(locations.size() == 7);
    for (size_t language = 0; language < locations.size(); ++language) {
        CHECK(locations[language].offset == 24 + 8 * language && locations[language].type == FieldType::Text);
        CHECK(table_schema::Write(row, locations[language], "data/texture/canopy/m/k.nltx", pool, error));
    }
    CHECK(locations[4].path == "fileName.zh-tw");
    // The seven paths share one pool text.
    CHECK(blob::Get<uint32_t>(row.data(), 24) == blob::Get<uint32_t>(row.data(), 72));
    CHECK(pool.Bytes() == std::string("\0data/texture/canopy/m/k.nltx\0", 30));

    CHECK(One(schema, "FILENAME.en", 0, row.size()).offset == 32);
    CHECK(One(schema, "filename.1", 0, row.size()).offset == 32);
    CHECK(One(schema, "isresident", 0, row.size()).offset == 80);
    CHECK(One(schema, "height", 0, row.size()).offset == 96);
    const Location thumbnail = One(schema, "isThumnail", 0, row.size());
    CHECK(table_schema::Write(row, thumbnail, "true", pool, error) && blob::Get<uint32_t>(row.data(), 84) == 1);
    CHECK(!table_schema::Write(row, thumbnail, "2", pool, error));
    CHECK(!table_schema::Resolve(schema, "id", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "enumName", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "trailer", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "fileName.de", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "colour", 0, row.size(), locations, error));
    CHECK(error.find("no field colour") != std::string::npos);
    // The trailer stays zero.
    for (size_t index = 100; index < 116; ++index) {
        CHECK(row[index] == 0);
    }
}

void TestIconRow() {
    std::printf("icon.dat rows\n");
    const table_schema::TableSchema& schema = table_schema::kIcon;
    blob::Pool pool(0);
    Bytes row = table_schema::NewRow(schema, 0, pool);
    CHECK(row.size() == 272);
    std::string error;
    const Location texture = One(schema, "textureID", 0, row.size());
    CHECK(texture.type == FieldType::Ref64 && texture.offset == 24);
    CHECK(std::strcmp(texture.field->refTable, "texture.dat") == 0);
    CHECK(table_schema::Write(row, texture, "1900000001", pool, error));
    CHECK(table_schema::ReadNumber(row.data(), texture) == 1900000001ull);

    CHECK(One(schema, "drawInfo.0.w", 0, row.size()).offset == 40);
    CHECK(One(schema, "drawInfo.0.w", 0, row.size()).path == "drawInfo.0.w");
    CHECK(One(schema, "drawinfo.en.w", 0, row.size()).offset == 72);
    CHECK(One(schema, "drawInfo.6.offsetY", 0, row.size()).offset == 32 + 6 * 32 + 28);
    std::vector<Location> locations;
    CHECK(table_schema::Resolve(schema, "drawInfo.drawW", 0, row.size(), locations, error));
    CHECK(locations.size() == 7 && locations[3].offset == 32 + 3 * 32 + 16);
    for (const Location& location : locations) {
        CHECK(table_schema::Write(row, location, "64.5", pool, error));
    }
    CHECK(blob::Get<float>(row.data(), 32 + 16) == 64.5f && blob::Get<float>(row.data(), 32 + 6 * 32 + 16) == 64.5f);
    CHECK(!table_schema::Write(row, locations[0], "wide", pool, error));
    CHECK(!table_schema::Write(row, locations[0], "1e99", pool, error));
    CHECK(!table_schema::Resolve(schema, "drawInfo.7.w", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "drawInfo", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "drawInfo.0", 0, row.size(), locations, error));
    CHECK(!table_schema::Resolve(schema, "drawInfo.0.depth", 0, row.size(), locations, error));
    CHECK(!table_schema::Write(row, texture, "-1", pool, error));
}

void TestSkinRow() {
    std::printf("animereplacetexture.dat rows\n");
    const table_schema::TableSchema& schema = table_schema::kAnimeReplaceTexture;
    blob::Pool pool(0);
    Bytes row = table_schema::NewRow(schema, 1, pool);
    CHECK(row.size() == 60);
    CHECK(table_schema::VectorCount(schema, row.data(), row.size()) == 1);
    CHECK(table_schema::RowFits(schema, row.data(), row.size()));
    std::string error;
    CHECK(table_schema::Write(row, One(schema, "entries.0.name", 1, row.size()), "cap_01", pool, error));
    CHECK(table_schema::Write(row, One(schema, "entries.0.texture", 1, row.size()), "3833", pool, error));
    blob::Put<uint32_t>(row.data(), row.size() - 20, 0xAABBCCDD);

    CHECK(table_schema::Resize(schema, row, 3));
    CHECK(row.size() == 84 && table_schema::VectorCount(schema, row.data(), row.size()) == 3);
    CHECK(table_schema::RowFits(schema, row.data(), row.size()));
    CHECK(blob::Get<uint32_t>(row.data(), 36) == 3833);
    // The trailer moved with the row's end.
    CHECK(blob::Get<uint32_t>(row.data(), row.size() - 20) == 0xAABBCCDD);
    const Location third = One(schema, "entries.2.texture", 3, row.size());
    CHECK(third.offset == 24 + 4 + 2 * 12 + 8 && third.type == FieldType::Ref32);
    CHECK(table_schema::Write(row, third, "1900000001", pool, error));
    CHECK(!table_schema::Write(row, third, "4294967296", pool, error));
    std::vector<Location> locations;
    CHECK(!table_schema::Resolve(schema, "entries.3.texture", 3, row.size(), locations, error));
    CHECK(table_schema::Resolve(schema, "entries.name", 3, row.size(), locations, error) && locations.size() == 3);
    CHECK(!table_schema::Resolve(schema, "entries", 3, row.size(), locations, error));

    CHECK(table_schema::Resize(schema, row, 1));
    CHECK(row.size() == 60 && blob::Get<uint32_t>(row.data(), 36) == 3833);
    CHECK(blob::Get<uint32_t>(row.data(), 40) == 0xAABBCCDD);
    Bytes fixed = table_schema::NewRow(table_schema::kIcon, 0, pool);
    CHECK(!table_schema::Resize(table_schema::kIcon, fixed, 2));

    // Built into a blob, the row reads back.
    Bytes data;
    blob::Build({row}, pool.Bytes(), data);
    blob::Table table;
    CHECK(blob::Parse(data.data(), data.size(), table, error));
    CHECK(Text(table, table.rows[0], 28) == "cap_01");
}

void TestSchemaLookup() {
    std::printf("Schema lookup\n");
    CHECK(table_schema::All().size() == 3);
    CHECK(table_schema::Find("ICON") == &table_schema::kIcon);
    CHECK(table_schema::Find("texture.dat") == &table_schema::kTexture);
    CHECK(table_schema::Find("AnimeReplaceTexture") == &table_schema::kAnimeReplaceTexture);
    CHECK(table_schema::Find("gimmick") == nullptr);
    CHECK(table_schema::Find("") == nullptr);
    // Every band stays inside int32, since some references are i32.
    for (const table_schema::TableSchema* schema : table_schema::All()) {
        CHECK(schema->idBandStart + schema->idBandSize <= 2147483648ull);
        CHECK(table_schema::RowSize(*schema, 0) == schema->rowSize);
    }
}

// ---- Patch-only schemas --------------------------------------------------

std::string Lower(std::string text) {
    for (char& c : text) {
        c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return text;
}

bool IsRef(FieldType type) {
    return type == FieldType::Ref64 || type == FieldType::Ref32 || type == FieldType::FlagRef;
}

bool IsTextPlace(FieldType type) {
    return type == FieldType::Text || type == FieldType::Enum;
}

bool NeverEditable(FieldType type) {
    return type == FieldType::Id || type == FieldType::Enum || type == FieldType::Trailer || type == FieldType::Pad;
}

// The bytes a field takes in a row with every vector empty.
size_t Span(const FieldSpec& field) {
    return field.type == FieldType::Vector ? 4 : field.size;
}

bool HoldsVector(const FieldSpec& field) {
    for (uint32_t index = 0; field.type == FieldType::Array && index < field.elementCount; ++index) {
        if (field.element[index].type == FieldType::Vector) {
            return true;
        }
    }
    return false;
}

bool GroupTiles(const FieldSpec* fields, size_t count, size_t size);

// An Array's elements and a Vector's tile its stride, and a Struct's fields
// its size, all the way down; any other field has no element.
bool FieldTiles(const FieldSpec& field) {
    switch (field.type) {
    case FieldType::Array:
        return field.count > 0 && field.size == field.count * field.stride &&
               GroupTiles(field.element, field.elementCount, field.stride);
    case FieldType::Vector:
        return field.size == 4 && GroupTiles(field.element, field.elementCount, field.stride);
    case FieldType::Struct:
        return GroupTiles(field.element, field.elementCount, field.size);
    default:
        return field.element == nullptr && field.elementCount == 0;
    }
}

// A group's fields tile size bytes from 0 under unique names, with no id,
// enum name, trailer or pad among them. An unnamed field is the one value of
// its element, so it is the group's only field.
bool GroupTiles(const FieldSpec* fields, size_t count, size_t size) {
    if (!fields || count == 0 || size == 0) {
        return false;
    }
    size_t at = 0;
    std::vector<std::string> names;
    for (size_t index = 0; index < count; ++index) {
        const FieldSpec& part = fields[index];
        if (part.offset != static_cast<int32_t>(at) || NeverEditable(part.type) || (!*part.name && count != 1) ||
            (IsRef(part.type) && !part.refTable) || !FieldTiles(part)) {
            return false;
        }
        names.push_back(Lower(part.name));
        at += Span(part);
    }
    std::sort(names.begin(), names.end());
    return at == size && std::adjacent_find(names.begin(), names.end()) == names.end();
}

// The values a field holds, and how many of them can be edited.
void CountValues(const FieldSpec& field, bool editable, size_t& values, size_t& editableValues) {
    editable = editable && field.editable;
    if (field.type == FieldType::Array || field.type == FieldType::Vector || field.type == FieldType::Struct) {
        for (uint32_t index = 0; index < field.elementCount; ++index) {
            CountValues(field.element[index], editable, values, editableValues);
        }
        return;
    }
    ++values;
    editableValues += editable ? 1 : 0;
}

const Location* FindPlace(const std::vector<Location>& places, const std::string& path) {
    for (const Location& place : places) {
        if (place.path == path) {
            return &place;
        }
    }
    return nullptr;
}

// Each editable place is the one Resolve gives for its path, and Resolve
// refuses every other one. bad names the first place that differs.
bool SameAsResolve(
    const TableSchema& schema, const std::vector<Location>& places, uint32_t count, size_t size, std::string& bad
) {
    for (const Location& place : places) {
        std::vector<Location> resolved;
        std::string error;
        const bool found = table_schema::Resolve(schema, place.path, count, size, resolved, error);
        const bool same = found && resolved.size() == 1 && resolved[0].field == place.field &&
                          resolved[0].offset == place.offset && resolved[0].type == place.type &&
                          resolved[0].size == place.size && resolved[0].path == place.path;
        if (place.editable ? !same : found) {
            bad = place.path;
            return false;
        }
    }
    return true;
}

// Each editable place is the one ResolveRow gives for its path, and
// ResolveRow refuses every other one. bad names the first place that differs.
bool SameAsResolveRow(
    const TableSchema& schema, const std::vector<Location>& places, const uint8_t* row, size_t size, std::string& bad
) {
    for (const Location& place : places) {
        std::vector<Location> resolved;
        std::string error;
        const bool found = table_schema::ResolveRow(schema, place.path, row, size, resolved, error);
        const bool same = found && resolved.size() == 1 && resolved[0].field == place.field &&
                          resolved[0].offset == place.offset && resolved[0].type == place.type &&
                          resolved[0].size == place.size && resolved[0].path == place.path;
        if (place.editable ? !same : found) {
            bad = place.path;
            return false;
        }
    }
    return true;
}

void TestSchemaShapes() {
    std::printf("Patchable schemas\n");
    const std::vector<const TableSchema*>& schemas = table_schema::Patchable();
    CHECK(table_schema::All().size() == 3);
    CHECK(schemas.size() == table_schema::All().size() + table_schema::Generated().size());
    CHECK(schemas.size() == 133);
    // The curated schemas, then the generated ones by file name.
    const size_t curatedCount = table_schema::All().size();
    for (size_t index = 0; index < schemas.size(); ++index) {
        CHECK(schemas[index] == (index < curatedCount ? table_schema::All()[index]
                                                      : table_schema::Generated()[index - curatedCount]));
        CHECK(index <= curatedCount || std::string(schemas[index - 1]->file) < schemas[index]->file);
    }
    const TableSchema* item = table_schema::FindPatchable("ITEM");
    CHECK(item && item == table_schema::FindPatchable("item.dat") && item->recordSize == 1016);
    CHECK(table_schema::FindPatchable("storesales.dat") != nullptr);
    CHECK(table_schema::FindPatchable("talkdefine") == nullptr);
    CHECK(table_schema::Find("item") == nullptr);
    for (const TableSchema* schema : schemas) {
        const bool curated = table_schema::Curated(*schema);
        CHECK(table_schema::FindPatchable(schema->file) == schema);
        CHECK((table_schema::Find(schema->file) == schema) == curated);
        CHECK(table_schema::RowSize(*schema, 0) == schema->rowSize);
        if (!curated) {
            const std::string file = schema->file;
            CHECK(file.size() > 4 && file.substr(0, file.size() - 4) == schema->section);
            CHECK(schema->idBandStart == 0 && schema->idBandSize == 0 && schema->maxRows == 0);
            CHECK(schema->enumStem == nullptr && schema->registryFile == nullptr);
            blob::Pool pool(0);
            CHECK(table_schema::NewRow(*schema, 0, pool).empty());
        }

        CHECK(curated || schema->recordSize > 0);

        // The fields tile the row with every vector empty: the positive ones
        // from 0, the negative ones up to its end.
        size_t at = 0;
        bool tiled = true;
        bool vectorSeen = false;
        bool order = true;
        bool kinds = true;
        bool shapes = true;
        bool refs = true;
        bool enumFound = false;
        bool trailerFound = false;
        bool trailerFits = false;
        size_t fields = 0;
        size_t editable = 0;
        size_t pad = 0;
        std::vector<std::string> names;
        for (size_t index = 0; index < schema->fieldCount; ++index) {
            const FieldSpec& field = schema->fields[index];
            const int64_t start = field.offset < 0 ? static_cast<int64_t>(schema->rowSize) + field.offset : field.offset;
            tiled = tiled && start == static_cast<int64_t>(at);
            at = static_cast<size_t>(start) + Span(field);
            // In a curated schema no part of variable size comes before the
            // first Vector.
            vectorSeen = vectorSeen || field.type == FieldType::Vector;
            order = order && (!curated || vectorSeen || !HoldsVector(field));
            shapes = shapes && FieldTiles(field);
            kinds = kinds && (!NeverEditable(field.type) || !field.editable);
            refs = refs && (!IsRef(field.type) || field.refTable);
            names.push_back(Lower(field.name));
            if (field.type == FieldType::Enum) {
                enumFound = enumFound || field.offset == schema->enumOffset;
            }
            if (field.type == FieldType::Trailer) {
                trailerFound = true;
                const uint32_t size = schema->trailer == table_schema::TrailerKind::V20 ? 20 : 16;
                trailerFits = schema->trailer != table_schema::TrailerKind::None && field.size == size &&
                              field.offset == -static_cast<int32_t>(size);
            }
            if (field.type == FieldType::Pad) {
                pad += field.size;
            } else if (field.type != FieldType::Trailer) {
                CountValues(field, true, fields, editable);
            }
        }
        CHECK(tiled && at == schema->rowSize);
        CHECK(order);
        CHECK(kinds);
        CHECK(shapes);
        CHECK(refs);
        CHECK(enumFound == (schema->enumOffset >= 0));
        CHECK(trailerFound == (schema->trailer != table_schema::TrailerKind::None));
        CHECK(!trailerFound || trailerFits);
        std::sort(names.begin(), names.end());
        CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());
        std::printf(
            "  %-24s %3zu fields, %3zu editable, %3zu read only, %2zu bytes of pad\n", schema->file, fields, editable,
            fields - editable, pad
        );
    }
}

// ---- Parts of variable size ----------------------------------------------

// A schema with a part of variable size of every kind: the first Vector, an
// Array whose elements end with a Vector, and a second Vector.
constexpr FieldSpec kTestValueFields[] = {
    {"v", 0, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
};

constexpr FieldSpec kTestEffectFields[] = {
    {"kind", 0, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
    {"list", 4, 4, FieldType::Vector, nullptr, nullptr, nullptr, 0, 4, kTestValueFields, 1, false, -1},
};

constexpr FieldSpec kTestFields[] = {
    {"id", 0, 8, FieldType::Id, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, -1},
    {"first", 8, 4, FieldType::Vector, nullptr, nullptr, nullptr, 0, 4, kTestValueFields, 1, true, -1},
    {"middle", 12, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
    {"effects", 16, 16, FieldType::Array, nullptr, nullptr, nullptr, 2, 8, kTestEffectFields, 2, true, -1},
    {"after", 32, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
    {"second", 36, 4, FieldType::Vector, nullptr, nullptr, nullptr, 0, 4, kTestValueFields, 1, true, -1},
    {"tail", -4, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
};

const TableSchema kTestSchema = {
    "test.dat", "TestData", "test", 0, 44, -1, table_schema::TrailerKind::None, 0, 0, 0, nullptr, nullptr,
    kTestFields, std::size(kTestFields), 0,
};

void TestVariableRows() {
    std::printf("Rows with parts of variable size\n");
    std::string error;
    std::string bad;
    std::vector<Location> locations;
    std::vector<Location> places;

    // item.dat: the feature vector moves what follows it, and each effect of
    // the action ends with its own values.
    const TableSchema& item = *table_schema::FindPatchable("item");
    Bytes row(536, 0);
    blob::Put<uint32_t>(row.data(), row.size() - 20, 0xAABBCCDD);
    CHECK(table_schema::RowFits(item, row.data(), row.size()));
    CHECK(table_schema::VectorCount(item, row.data(), row.size()) == 0);
    CHECK(Placed(item, "action.id", row).offset == 388);
    CHECK(table_schema::Resize(item, row, 2));
    CHECK(row.size() == 552 && table_schema::VectorCount(item, row.data(), row.size()) == 2);
    CHECK(Placed(item, "action.id", row).offset == 404);
    CHECK(Placed(item, "feature.1", row).offset == 396);
    CHECK(One(item, "iconID", 2, row.size()).offset == 500);
    // The second effect gets one value: its count sits at 404 + 48 + 12 + 8.
    blob::Put<uint32_t>(row.data(), 472, 1);
    row.insert(row.begin() + 476, 8, 0);
    blob::Put<uint64_t>(row.data(), 476, 0x1122334455667788ull);
    CHECK(row.size() == 560 && table_schema::RowFits(item, row.data(), row.size()));
    CHECK(table_schema::RowSize(item, 2) == 552);
    CHECK(One(item, "iconID", 2, row.size()).offset == 508);
    CHECK(One(item, "buyingPrice", 2, row.size()).offset == 332);
    CHECK(One(item, "processing", 2, row.size()).offset == 344);
    CHECK(table_schema::Layout(item, row.data(), row.size(), places, error));
    const Location* value = FindPlace(places, "action.effect.1.value.0");
    CHECK(value && value->offset == 476 && value->editable && value->type == FieldType::I64);
    const Location* next = FindPlace(places, "action.effect.2.id");
    CHECK(next && next->offset == 484 && next->editable && next->type == FieldType::Ref64);
    const Location* count = FindPlace(places, "action.effect.1.value.count");
    CHECK(count && count->offset == 472 && !count->editable);
    const Location* icon = FindPlace(places, "iconID");
    CHECK(icon && icon->offset == 508 && icon->editable);
    const Location* feature = FindPlace(places, "feature.1");
    CHECK(feature && feature->offset == 396 && feature->editable);
    const Location* features = FindPlace(places, "feature.count");
    CHECK(features && features->offset == 384 && !features->editable);
    const Location* trailer = FindPlace(places, "trailer.disableVersion");
    CHECK(trailer && trailer->offset == 544 && !trailer->editable);
    const Location* name = FindPlace(places, "name.zh-cn");
    CHECK(name && name->offset == 32 + 48 && name->type == FieldType::Text && name->editable);
    CHECK(!FindPlace(places, "name"));
    CHECK(SameAsResolveRow(item, places, row.data(), row.size(), bad));
    CHECK(Placed(item, "action.effect.1.value.0", row).offset == 476);
    CHECK(!table_schema::Resolve(item, "action.effect.0.id", 2, row.size(), locations, error));
    CHECK(!table_schema::Resolve(item, "enumName", 2, row.size(), locations, error));
    CHECK(error.find("cannot be edited") != std::string::npos);
    // A count the row cannot hold.
    blob::Put<uint32_t>(row.data(), 472, 2);
    CHECK(!table_schema::RowFits(item, row.data(), row.size()));
    CHECK(!table_schema::Layout(item, row.data(), row.size(), places, error) && places.empty());
    CHECK(!table_schema::Resize(item, row, 0));
    blob::Put<uint32_t>(row.data(), 472, 1);
    blob::Put<uint32_t>(row.data(), 384, 0xFFFFFFFFu);
    CHECK(!table_schema::RowFits(item, row.data(), row.size()));
    blob::Put<uint32_t>(row.data(), 384, 2);
    CHECK(!table_schema::RowFits(item, row.data(), 40));
    // Dropping the features keeps the effect value and the trailer.
    CHECK(table_schema::Resize(item, row, 0));
    CHECK(row.size() == 544 && table_schema::RowFits(item, row.data(), row.size()));
    CHECK(blob::Get<uint64_t>(row.data(), 460) == 0x1122334455667788ull);
    CHECK(blob::Get<uint32_t>(row.data(), row.size() - 20) == 0xAABBCCDD);
    CHECK(Placed(item, "action.id", row).offset == 388);

    // map.dat: two vectors in a row.
    const TableSchema& map = *table_schema::FindPatchable("map");
    Bytes mapRow(140, 0);
    blob::Put<uint32_t>(mapRow.data(), 64, 1);
    mapRow.insert(mapRow.begin() + 68, 8, 0);
    blob::Put<uint32_t>(mapRow.data(), 76, 2);
    mapRow.insert(mapRow.begin() + 80, 16, 0);
    CHECK(mapRow.size() == 164 && table_schema::RowFits(map, mapRow.data(), mapRow.size()));
    CHECK(table_schema::VectorCount(map, mapRow.data(), mapRow.size()) == 1);
    CHECK(table_schema::Layout(map, mapRow.data(), mapRow.size(), places, error));
    const Location* gimmick = FindPlace(places, "connectGimmick.1");
    CHECK(gimmick && gimmick->offset == 88 && gimmick->editable && gimmick->type == FieldType::Ref64);
    const Location* after = FindPlace(places, "facilityID");
    CHECK(after && after->offset == 96 && after->editable);
    const Location* version = FindPlace(places, "trailer.version");
    CHECK(version && version->offset == 144);
    CHECK(!FindPlace(places, "connectGimmick"));
    CHECK(SameAsResolveRow(map, places, mapRow.data(), mapRow.size(), bad));
    CHECK(Placed(map, "fileName.3", mapRow).offset == 48);
    CHECK(One(map, "facilityID", 1, mapRow.size()).offset == 96);
    blob::Put<uint32_t>(mapRow.data(), 76, 3);
    CHECK(!table_schema::RowFits(map, mapRow.data(), mapRow.size()));

    // Every editable kind of place, in a schema made for it: id, first {1
    // value}, middle, effects {kind, list {2 values}} {kind, list {}}, after,
    // second {1 value}, tail.
    Bytes test(60, 0);
    blob::Put<uint32_t>(test.data(), 8, 1);
    blob::Put<uint32_t>(test.data(), 12, 101);
    blob::Put<uint32_t>(test.data(), 16, 102);
    blob::Put<uint32_t>(test.data(), 24, 2);
    blob::Put<uint32_t>(test.data(), 44, 103);
    blob::Put<uint32_t>(test.data(), 48, 1);
    blob::Put<uint32_t>(test.data(), 56, 104);
    CHECK(table_schema::RowFits(kTestSchema, test.data(), test.size()));
    CHECK(!table_schema::RowFits(kTestSchema, test.data(), test.size() - 4));
    CHECK(table_schema::VectorCount(kTestSchema, test.data(), test.size()) == 1);
    CHECK(table_schema::RowSize(kTestSchema, 1) == 48);
    CHECK(One(kTestSchema, "first.0.v", 1, test.size()).offset == 12);
    CHECK(One(kTestSchema, "middle", 1, test.size()).offset == 16);
    CHECK(One(kTestSchema, "tail", 1, test.size()).offset == 56);
    CHECK(!table_schema::Resolve(kTestSchema, "after", 1, test.size(), locations, error));
    CHECK(error.find("variable size") != std::string::npos);
    CHECK(!table_schema::Resolve(kTestSchema, "effects.0.kind", 1, test.size(), locations, error));
    CHECK(error.find("different sizes") != std::string::npos);
    CHECK(!table_schema::Resolve(kTestSchema, "second.0.v", 1, test.size(), locations, error));
    CHECK(table_schema::Layout(kTestSchema, test.data(), test.size(), places, error));
    CHECK(places.size() == 14);
    // The row places every field, whatever comes before it; only a count,
    // and what is inside a field that cannot be edited, stay read only.
    const struct {
        const char* path;
        size_t offset;
        bool editable;
    } expected[] = {
        {"first.count", 8, false},    {"first.0.v", 12, true},           {"middle", 16, true},
        {"effects.0.kind", 20, true}, {"effects.0.list.count", 24, false}, {"effects.0.list.1.v", 32, false},
        {"effects.1.kind", 36, true}, {"effects.1.list.count", 40, false}, {"after", 44, true},
        {"second.count", 48, false},  {"second.0.v", 52, true},          {"tail", 56, true},
    };
    for (const auto& place : expected) {
        const Location* found = FindPlace(places, place.path);
        CHECK(found && found->offset == place.offset && found->editable == place.editable);
    }
    CHECK(SameAsResolveRow(kTestSchema, places, test.data(), test.size(), bad));
    std::vector<Location> placed;
    CHECK(table_schema::ResolveRow(kTestSchema, "effects.kind", test.data(), test.size(), placed, error));
    CHECK(placed.size() == 2 && placed[0].offset == 20 && placed[1].offset == 36);
    CHECK(!table_schema::ResolveRow(kTestSchema, "second.1.v", test.data(), test.size(), placed, error));
    CHECK(error == "second has no element 1, it has 1" && placed.empty());
    CHECK(!table_schema::ResolveRow(kTestSchema, "effects.0.list.0.v", test.data(), test.size(), placed, error));
    CHECK(error == "effects.0.list cannot be edited");
    CHECK(table_schema::Resize(kTestSchema, test, 0));
    CHECK(test.size() == 56 && table_schema::RowFits(kTestSchema, test.data(), test.size()));
    CHECK(One(kTestSchema, "middle", 0, test.size()).offset == 12);
    CHECK(blob::Get<uint32_t>(test.data(), 12) == 102 && blob::Get<uint32_t>(test.data(), 52) == 104);
    CHECK(table_schema::Layout(kTestSchema, test.data(), test.size(), places, error));
    const Location* moved = FindPlace(places, "after");
    CHECK(moved && moved->offset == 40 && blob::Get<uint32_t>(test.data(), 40) == 103);

    // The curated schemas lay out the same way.
    blob::Pool pool(0);
    Bytes skin = table_schema::NewRow(table_schema::kAnimeReplaceTexture, 1, pool);
    CHECK(table_schema::Layout(table_schema::kAnimeReplaceTexture, skin.data(), skin.size(), places, error));
    CHECK(SameAsResolve(table_schema::kAnimeReplaceTexture, places, 1, skin.size(), bad));
    blob::Put<uint32_t>(skin.data(), 24, 5);
    CHECK(!table_schema::RowFits(table_schema::kAnimeReplaceTexture, skin.data(), skin.size()));
    CHECK(!table_schema::Layout(table_schema::kAnimeReplaceTexture, skin.data(), skin.size(), places, error));
    Bytes iconRow = table_schema::NewRow(table_schema::kIcon, 0, pool);
    CHECK(table_schema::Layout(table_schema::kIcon, iconRow.data(), iconRow.size(), places, error));
    CHECK(places.size() == 64);
    CHECK(std::count_if(places.begin(), places.end(), [](const Location& place) { return place.editable; }) == 58);
    const Location* offsetY = FindPlace(places, "drawInfo.6.offsetY");
    CHECK(offsetY && offsetY->offset == 32 + 6 * 32 + 28);
    CHECK(SameAsResolve(table_schema::kIcon, places, 0, iconRow.size(), bad));
}

// ---- Nesting -------------------------------------------------------------

// Vectors in a vector's elements, a struct, elements that are one value, a
// 1-byte Bool and a field after all of them: the shapes of crops.dat and
// storesales.dat rows.
constexpr FieldSpec kNestGrowFields[] = {
    {"days", 0, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
    {"skin", 4, 8, FieldType::Ref64, "texture.dat", nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
};

constexpr FieldSpec kNestFormFields[] = {
    {"kind", 0, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
    {"grow", 4, 4, FieldType::Vector, nullptr, nullptr, nullptr, 0, 12, kNestGrowFields, 2, true, -1},
    {"name", 8, 8, FieldType::Text, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
};

constexpr FieldSpec kNestInfoFields[] = {
    {"stock", 0, 4, FieldType::I32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 0},
    {"mode", 4, 4, FieldType::I32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 4},
};

constexpr FieldSpec kNestFeatureFields[] = {
    {"", 0, 8, FieldType::U64, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 0},
};

constexpr FieldSpec kNestSlotFields[] = {
    {"", 0, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 0},
};

constexpr FieldSpec kNestFields[] = {
    {"id", 0, 8, FieldType::Id, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, false, 0},
    {"form", 8, 4, FieldType::Vector, nullptr, nullptr, nullptr, 0, 16, kNestFormFields, 3, true, 8},
    {"info", 12, 8, FieldType::Struct, nullptr, nullptr, nullptr, 0, 0, kNestInfoFields, 2, true, 100},
    {"feature", 20, 4, FieldType::Vector, nullptr, nullptr, nullptr, 0, 8, kNestFeatureFields, 1, true, 40},
    {"slot", 24, 8, FieldType::Array, nullptr, nullptr, nullptr, 2, 4, kNestSlotFields, 1, true, 200, 16},
    {"flag", 32, 1, FieldType::Bool, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, 50},
    {"tail", -4, 4, FieldType::U32, nullptr, nullptr, nullptr, 0, 0, nullptr, 0, true, -1},
};

const TableSchema kNestSchema = {
    "nest.dat", "NestData", "nest", 0, 37, -1, table_schema::TrailerKind::None, 0, 0, 0, nullptr, nullptr,
    kNestFields, std::size(kNestFields), 0,
};

// Two forms, the first with two grow steps and the second with none, and
// one feature.
Bytes NestRow() {
    Bytes row(101, 0);
    blob::Put<uint32_t>(row.data(), 8, 2);
    blob::Put<uint32_t>(row.data(), 12, 7);
    blob::Put<uint32_t>(row.data(), 16, 2);
    blob::Put<uint32_t>(row.data(), 32, 5);
    blob::Put<uint64_t>(row.data(), 36, 1900000001);
    blob::Put<uint32_t>(row.data(), 52, 8);
    blob::Put<int32_t>(row.data(), 72, -1);
    blob::Put<uint32_t>(row.data(), 76, 1);
    blob::Put<uint64_t>(row.data(), 80, 0x1122334455667788ull);
    blob::Put<uint32_t>(row.data(), 92, 9);
    row[96] = 1;
    blob::Put<uint32_t>(row.data(), 97, 0xAABBCCDD);
    return row;
}

void TestNestedRows() {
    std::printf("Rows with nested parts\n");
    std::string error;
    std::string bad;
    std::vector<Location> places;
    std::vector<Location> found;
    Bytes row = NestRow();
    CHECK(table_schema::RowFits(kNestSchema, row.data(), row.size()));
    CHECK(!table_schema::RowFits(kNestSchema, row.data(), row.size() - 1));
    CHECK(table_schema::Layout(kNestSchema, row.data(), row.size(), places, error));
    const struct {
        const char* path;
        size_t offset;
        bool editable;
    } expected[] = {
        {"form.count", 8, false},         {"form.0.kind", 12, true},  {"form.0.grow.count", 16, false},
        {"form.0.grow.0.days", 20, true}, {"form.0.grow.1.skin", 36, true}, {"form.0.name", 44, true},
        {"form.1.kind", 52, true},        {"form.1.grow.count", 56, false}, {"form.1.name", 60, true},
        {"info.stock", 68, true},         {"info.mode", 72, true},    {"feature.count", 76, false},
        {"feature.0", 80, true},          {"slot.0", 88, true},       {"slot.1", 92, true},
        {"flag", 96, true},               {"tail", 97, true},         {"id", 0, false},
    };
    for (const auto& place : expected) {
        const Location* at = FindPlace(places, place.path);
        CHECK(at && at->offset == place.offset && at->editable == place.editable);
    }
    CHECK(places.size() == 20);
    CHECK(SameAsResolveRow(kNestSchema, places, row.data(), row.size(), bad));
    const Location* flag = FindPlace(places, "flag");
    CHECK(flag && flag->size == 1 && table_schema::ReadNumber(row.data(), *flag) == 1);

    // By shape: each path spelled out, or refused with why.
    CHECK(table_schema::ResolvePath(kNestSchema, "Info.Stock", found, error));
    CHECK(found.size() == 1 && found[0].path == "info.stock" && found[0].offset == 0);
    CHECK(table_schema::ResolvePath(kNestSchema, "slot.1", found, error) && found.size() == 1);
    CHECK(table_schema::ResolvePath(kNestSchema, "form.4095.grow.4095.skin", found, error));
    CHECK(found.size() == 1 && found[0].type == FieldType::Ref64);
    const struct {
        const char* path;
        const char* error;
    } refused[] = {
        {"form.grow.0.days", "form.grow.0.days names every element of form, and a path names one, such as form.0.kind"},
        {"form.0", "form.0 needs an element field, such as form.0.kind"},
        {"form.0.grow.days", "form.0.grow.days names every element of form.0.grow, and a path names one, such as "
                             "form.0.grow.0.days"},
        {"info", "info needs a field, such as info.stock"},
        {"info.nosuch", "info has no field nosuch"},
        {"feature.4096", "feature has no element 4096"},
        {"feature.0.x", "feature.0 has no part x"},
        {"feature", "feature needs an element, such as feature.0"},
        {"slot.2", "slot has no element 2, it has 2"},
        {"id", "id cannot be edited"},
        {"form.0.nosuch", "form elements have no field nosuch"},
        {"nosuch", "nest.dat has no field nosuch"},
    };
    for (const auto& path : refused) {
        CHECK(!table_schema::ResolvePath(kNestSchema, path.path, found, error) && found.empty());
        if (!CHECK(error == path.error)) {
            std::printf("    %s: %s\n", path.path, error.c_str());
        }
    }

    // In the row: a path names an element the row may lack.
    CHECK(table_schema::ResolveRow(kNestSchema, "FORM.0.GROW.1.SKIN", row.data(), row.size(), found, error));
    CHECK(found.size() == 1 && found[0].offset == 36 && found[0].path == "form.0.grow.1.skin");
    CHECK(!table_schema::ResolveRow(kNestSchema, "form.1.grow.0.days", row.data(), row.size(), found, error));
    CHECK(error == "form.1.grow has no element 0, it has 0");
    CHECK(!table_schema::ResolveRow(kNestSchema, "form.2.kind", row.data(), row.size(), found, error));
    CHECK(error == "form has no element 2, it has 2");
    CHECK(!table_schema::ResolveRow(kNestSchema, "info.stock", row.data(), row.size() - 1, found, error));

    // Where the record keeps a place.
    CHECK(table_schema::RecordOffset(kNestSchema, "info.mode") == 104);
    CHECK(table_schema::RecordOffset(kNestSchema, "slot.1") == 216);
    CHECK(table_schema::RecordOffset(kNestSchema, "flag") == 50);
    CHECK(table_schema::RecordOffset(kNestSchema, "form.0.kind") == -1);
    CHECK(table_schema::RecordOffset(kNestSchema, "feature.0") == -1);
    CHECK(table_schema::RecordOffset(kNestSchema, "tail") == -1);

    // Resizing a vector inside an element keeps every other byte.
    blob::Pool pool(1000);
    CHECK(table_schema::ResizeVector(kNestSchema, row, "Form.1.Grow", 2, pool, error));
    CHECK(row.size() == 125 && table_schema::RowFits(kNestSchema, row.data(), row.size()));
    CHECK(table_schema::ResolveRow(kNestSchema, "form.1.grow.1.skin", row.data(), row.size(), found, error));
    CHECK(found.size() == 1 && found[0].offset == 76);
    CHECK(table_schema::ResolveRow(kNestSchema, "feature.0", row.data(), row.size(), found, error));
    CHECK(found.size() == 1 && blob::Get<uint64_t>(row.data(), found[0].offset) == 0x1122334455667788ull);
    CHECK(blob::Get<uint32_t>(row.data(), row.size() - 4) == 0xAABBCCDD);
    // No text in a grow step, so the pool holds only its leading NUL.
    CHECK(pool.Bytes().size() == 1);
    // A new form gets its text written empty.
    CHECK(table_schema::ResizeVector(kNestSchema, row, "form", 3, pool, error));
    CHECK(row.size() == 141 && table_schema::RowFits(kNestSchema, row.data(), row.size()));
    CHECK(table_schema::ResolveRow(kNestSchema, "form.2.name", row.data(), row.size(), found, error));
    CHECK(found.size() == 1 && blob::Get<uint32_t>(row.data(), found[0].offset) == 1000);
    blob::Table empty;
    std::string text;
    CHECK(table_schema::Read(row.data(), row.size(), found[0], empty, &pool, text, error) && text.empty());
    // Shrinking drops the last forms whole.
    CHECK(table_schema::ResizeVector(kNestSchema, row, "form", 1, pool, error));
    CHECK(row.size() == 101 - 16 && table_schema::RowFits(kNestSchema, row.data(), row.size()));
    CHECK(table_schema::ResolveRow(kNestSchema, "info.mode", row.data(), row.size(), found, error));
    CHECK(found.size() == 1 && blob::Get<int32_t>(row.data(), found[0].offset) == -1);
    CHECK(!table_schema::ResizeVector(kNestSchema, row, "form.1.grow", 0, pool, error));
    CHECK(!table_schema::ResizeVector(kNestSchema, row, "info", 0, pool, error));
    CHECK(!table_schema::ResizeVector(kNestSchema, row, "feature", 4097, pool, error));
    // The curated Resize leaves vectors whose elements hold vectors alone.
    CHECK(!table_schema::Resize(kNestSchema, row, 0));
}

// ---- Reading values ------------------------------------------------------

void TestReadValues() {
    std::printf("Reading values back\n");
    Bytes row(64, 0);
    blob::Table none;
    blob::Pool pool(0);
    std::string value;
    std::string error;
    Location place;
    place.size = 4;
    place.path = "value";

    // A float reads as the shortest text that gives the same bits back.
    place.type = FieldType::F32;
    const struct {
        float number;
        const char* text;
    } floats[] = {{0.1f, "0.1"}, {64.5f, "64.5"}, {-0.0f, "-0"}, {1.0f, "1"}, {0.0f, "0"}, {1e-10f, "1e-10"}};
    for (const auto& sample : floats) {
        blob::Put<float>(row.data(), 0, sample.number);
        CHECK(table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error) && value == sample.text);
    }
    const float others[] = {1.0f / 3, 3.4028235e38f, -123456.789f, 16777216.0f, 2.5e-30f, 0.3f, 100.01f};
    for (const float number : others) {
        blob::Put<float>(row.data(), 0, number);
        Bytes copy = row;
        CHECK(table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error));
        CHECK(table_schema::Write(copy, place, value, pool, error) && copy == row);
    }
    // Values Write refuses: not a number, infinity, a Bool other than 0 or 1.
    blob::Put<uint32_t>(row.data(), 0, 0x7FC00000u);
    CHECK(!table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error));
    blob::Put<uint32_t>(row.data(), 0, 0x7F800000u);
    CHECK(!table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error));
    place.type = FieldType::Bool;
    blob::Put<uint32_t>(row.data(), 0, 1);
    CHECK(table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error) && value == "1");
    blob::Put<uint32_t>(row.data(), 0, 2);
    CHECK(!table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error) && value == "2");

    // Whole numbers and references in decimal.
    const struct {
        FieldType type;
        uint32_t size;
        uint64_t bits;
        const char* text;
    } numbers[] = {
        {FieldType::I32, 4, 0xFFFFFFFBull, "-5"},
        {FieldType::I32, 4, 0x80000000ull, "-2147483648"},
        {FieldType::FlagRef, 4, 0xFFFFFFFFull, "-1"},
        {FieldType::U32, 4, 7, "7"},
        {FieldType::Ref32, 4, 0xFFFFFFFFull, "4294967295"},
        {FieldType::U64, 8, 0xFFFFFFFFFFFFFFFFull, "18446744073709551615"},
        {FieldType::Ref64, 8, 1900000001, "1900000001"},
        {FieldType::Id, 8, 0, "0"},
    };
    for (const auto& sample : numbers) {
        place.type = sample.type;
        place.size = sample.size;
        place.offset = 8;
        std::fill(row.begin(), row.end(), static_cast<uint8_t>(0x5A));
        if (sample.size == 4) {
            blob::Put<uint32_t>(row.data(), 8, static_cast<uint32_t>(sample.bits));
        } else {
            blob::Put<uint64_t>(row.data(), 8, sample.bits);
        }
        Bytes copy = row;
        CHECK(table_schema::Read(row.data(), row.size(), place, none, nullptr, value, error) && value == sample.text);
        CHECK(table_schema::Write(copy, place, value, pool, error) && copy == row);
    }

    // Texts, from the table's pool or from the texts added to it.
    Bytes textRow(16, 0);
    blob::Put<uint32_t>(textRow.data(), 0, 1);
    blob::Put<uint32_t>(textRow.data(), 4, 5);
    Bytes data;
    blob::Build({textRow}, std::string("\0alpha\0", 7), data);
    blob::Table table;
    CHECK(blob::Parse(data.data(), data.size(), table, error));
    Location text;
    text.type = FieldType::Text;
    text.size = 8;
    text.path = "comment";
    CHECK(table_schema::Read(textRow.data(), textRow.size(), text, table, nullptr, value, error) && value == "alpha");
    blob::Pool added(table.poolBytes);
    Bytes copy = textRow;
    CHECK(table_schema::Write(copy, text, "beta", added, error));
    CHECK(table_schema::Read(copy.data(), copy.size(), text, table, &added, value, error) && value == "beta");
    CHECK(!table_schema::Read(copy.data(), copy.size(), text, table, nullptr, value, error));
    CHECK(table_schema::Write(copy, text, "", added, error));
    CHECK(table_schema::Read(copy.data(), copy.size(), text, table, &added, value, error) && value.empty());
    text.type = FieldType::Enum;
    CHECK(table_schema::Read(textRow.data(), textRow.size(), text, table, &added, value, error) && value == "alpha");
    // Places that hold no one value, or lie outside the row.
    Location outside = text;
    outside.offset = 12;
    CHECK(!table_schema::Read(textRow.data(), textRow.size(), outside, table, nullptr, value, error));
    Location whole = text;
    whole.type = FieldType::Array;
    CHECK(!table_schema::Read(textRow.data(), textRow.size(), whole, table, nullptr, value, error));
}

// ---- The game's tables ---------------------------------------------------

struct IdSet {
    bool loaded = false;
    std::unordered_set<uint64_t> ids;
};

// Every Patchable() table in the game: each row fits and lays out, each
// place reads, each editable place writes back the bytes it read, each
// confirmed reference resolves, and on every 16th row each editable place
// is the one its name resolves to.
void TestVanillaTables() {
    std::printf("Patchable tables in the game\n");
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return;
    }
    mod_rules::VanillaData vanilla;
    std::string error;
    if (!CHECK(vanilla.Open(folder.wstring(), error))) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    std::unordered_set<std::string> patchable;
    for (const TableSchema* schema : table_schema::Patchable()) {
        patchable.insert(Lower(schema->file));
    }
    // The ids of a table's rows. framework_<table> holds rows of the same
    // kind under ids the table does not use, and a reference may point
    // there: one shipped skin entry names a framework_texture.dat texture.
    std::unordered_map<std::string, IdSet> idSets;
    auto idsOf = [&](const char* file) -> const IdSet& {
        const std::string key = Lower(file);
        auto found = idSets.find(key);
        if (found == idSets.end()) {
            IdSet set;
            for (const std::string& name : {key, "framework_" + key}) {
                if (const blob::Table* table = vanilla.Table(name)) {
                    set.loaded = set.loaded || name == key;
                    for (const blob::Row& row : table->rows) {
                        set.ids.insert(blob::RowId(row));
                    }
                }
            }
            found = idSets.emplace(key, std::move(set)).first;
        }
        return found->second;
    };

    // A table and its framework_ twin share a schema, and some tables ship
    // their rows only in the twin.
    for (const TableSchema* schema : table_schema::Patchable()) {
        const std::string twin = std::string("framework_") + schema->file;
        const blob::Table* base = vanilla.Table(schema->file);
        const blob::Table* framework = vanilla.Table(twin);
        if (!CHECK((base && !base->rows.empty()) || (framework && !framework->rows.empty()))) {
            std::printf("  %s does not load\n", schema->file);
            continue;
        }
        for (const blob::Table* table : {base, framework}) {
            if (!table || table->rows.empty()) {
                continue;
            }
            const std::string file = table == base ? std::string(schema->file) : twin;
            size_t fits = 0;
            size_t laidOut = 0;
            size_t places = 0;
            size_t editable = 0;
            size_t unreadable = 0;
            size_t trips = 0;
            size_t refs = 0;
            size_t refsFound = 0;
            size_t otherRefs = 0;
            size_t otherFound = 0;
            size_t sampled = 0;
            size_t sampledSame = 0;
            size_t names = 0;
            size_t namesSame = 0;
            std::string failure;
            const auto fail = [&](const std::string& what, size_t index) {
                if (failure.empty()) {
                    failure = what + " in row " + std::to_string(index);
                }
            };
            for (size_t index = 0; index < table->rows.size(); ++index) {
                const blob::Row& row = table->rows[index];
                if (!table_schema::RowFits(*schema, row.data, row.size)) {
                    fail("the size", index);
                    continue;
                }
                ++fits;
                std::vector<Location> layout;
                if (!table_schema::Layout(*schema, row.data, row.size, layout, error)) {
                    fail("the layout", index);
                    continue;
                }
                ++laidOut;
                const Bytes original = RowBytes(row);
                Bytes copy = original;
                blob::Pool pool(table->poolBytes);
                for (const Location& place : layout) {
                    ++places;
                    std::string value;
                    const bool read = table_schema::Read(row.data, row.size, place, *table, nullptr, value, error);
                    const uint64_t id = IsRef(place.type) ? table_schema::ReadNumber(row.data, place) : 0;
                    if (id != 0 && place.field->refTable) {
                        const IdSet& set = idsOf(place.field->refTable);
                        const bool found = set.loaded && set.ids.count(id) != 0;
                        // A confirmed reference into a table these checks cover.
                        if (place.field->editable && patchable.count(Lower(place.field->refTable)) != 0) {
                            ++refs;
                            refsFound += found ? 1 : 0;
                            if (!found) {
                                fail(place.path + " reference", index);
                            }
                        } else {
                            ++otherRefs;
                            otherFound += found ? 1 : 0;
                        }
                    }
                    if (!place.editable) {
                        unreadable += read ? 0 : 1;
                        continue;
                    }
                    ++editable;
                    // The schema alone spells the place's path the way the row does.
                    std::vector<Location> shaped;
                    ++names;
                    if (table_schema::ResolvePath(*schema, place.path, shaped, error) && shaped.size() == 1 &&
                        shaped[0].field == place.field && shaped[0].type == place.type && shaped[0].path == place.path) {
                        ++namesSame;
                    } else {
                        fail(place.path + " by path", index);
                    }
                    if (!read) {
                        fail(place.path + " reading", index);
                        continue;
                    }
                    std::string again;
                    const bool back = table_schema::Write(copy, place, value, pool, error) &&
                                      table_schema::Read(copy.data(), copy.size(), place, *table, &pool, again, error) &&
                                      again == value;
                    bool kept = copy == original;
                    if (IsTextPlace(place.type)) {
                        // A text gets a new ref into the added texts; nothing else moves.
                        const size_t end = place.offset + place.size;
                        kept = std::equal(copy.begin(), copy.begin() + place.offset, original.begin()) &&
                               std::equal(copy.begin() + end, copy.end(), original.begin() + end);
                        std::copy(original.begin() + place.offset, original.begin() + end, copy.begin() + place.offset);
                    }
                    if (back && kept) {
                        ++trips;
                    } else {
                        fail(place.path + " round trip", index);
                    }
                }
                if (index % 16 != 0 && index + 1 != table->rows.size()) {
                    continue;
                }
                // Each place is the one its path resolves to in this row.
                std::string bad;
                ++sampled;
                if (SameAsResolveRow(*schema, layout, row.data, row.size, bad)) {
                    ++sampledSame;
                } else {
                    fail(bad + " resolving", index);
                }
            }
            const size_t rows = table->rows.size();
            CHECK(fits == rows);
            CHECK(laidOut == rows);
            CHECK(trips == editable);
            CHECK(refsFound == refs);
            CHECK(sampledSame == sampled);
            CHECK(namesSame == names);
            std::printf(
                "  %-24s %5zu rows fit, %7zu places, %6zu editable round trips, %4zu read only not readable, "
                "refs %5zu of %5zu, other refs %5zu of %5zu, names %6zu of %6zu\n",
                file.c_str(), fits, places, trips, unreadable, refsFound, refs, otherFound, otherRefs, namesSame, names
            );
            if (!failure.empty()) {
                std::printf("    first failure: %s\n", failure.c_str());
            }
        }
    }
}

}  // namespace

void TestTables(int& checks, int& failures) {
    TestBlobBuildAndParse();
    TestPool();
    TestAppender();
    TestSampleRoundTrips();
    TestTextureRow();
    TestIconRow();
    TestSkinRow();
    TestSchemaLookup();
    TestSchemaShapes();
    TestVariableRows();
    TestNestedRows();
    TestReadValues();
    TestVanillaTables();
    checks += g_checks;
    failures += g_failures;
}
