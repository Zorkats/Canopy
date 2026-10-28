#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "blob.h"
#include "game_browse.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the JSON the desktop tools browse tables with, against tables
// built here, and the grids of every table with a schema against the real
// install, read only.
namespace {

namespace fs = std::filesystem;

using Bytes = std::vector<uint8_t>;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED browse_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

bool Has(const std::string& json, const std::string& part) {
    return json.find(part) != std::string::npos;
}

size_t Count(const std::string& json, const std::string& part) {
    size_t count = 0;
    for (size_t at = json.find(part); at != std::string::npos; at = json.find(part, at + part.size())) {
        ++count;
    }
    return count;
}

// A table of rows and pool, parsed. bytes must outlive table.
void Make(const std::vector<Bytes>& rows, const std::string& pool, Bytes& bytes, blob::Table& table) {
    blob::Build(rows, pool, bytes);
    std::string error;
    CHECK(blob::Parse(bytes.data(), bytes.size(), table, error));
}

void TestUncuratedTable() {
    std::printf("Browse: a table without a schema\n");
    // Pool: "\0ITEM_ID_A\0sample text\0lower\0".
    const std::string pool("\0ITEM_ID_A\0sample text\0lower\0", 29);
    Bytes row(48, 0);
    blob::Put<uint64_t>(row.data(), 0, 7);
    blob::Put<uint32_t>(row.data(), 8, 1);
    blob::Put<uint32_t>(row.data(), 12, 9);
    blob::Put<uint32_t>(row.data(), 16, 11);
    blob::Put<uint32_t>(row.data(), 20, 11);
    // Points into the middle of a text: not a guess.
    blob::Put<uint32_t>(row.data(), 24, 13);
    blob::Put<uint32_t>(row.data(), 28, 9);
    blob::Put<uint32_t>(row.data(), 32, 42);
    Bytes second(16, 0);
    blob::Put<uint64_t>(second.data(), 0, 8);
    blob::Put<uint32_t>(second.data(), 8, 23);
    blob::Put<uint32_t>(second.data(), 12, 5);
    Bytes bytes;
    blob::Table table;
    Make({row, second}, pool, bytes, table);

    const std::string list = game_browse::TableJsonOf(table, "thing.dat");
    CHECK(Has(list, "\"ok\":true"));
    CHECK(Has(list, "\"schema\":false"));
    CHECK(Has(list, "\"struct\":null"));
    CHECK(Has(list, "{\"index\":0,\"id\":7,\"enum\":\"ITEM_ID_A\",\"size\":48}"));
    // "lower" at +8 is a text but not an enum name.
    CHECK(Has(list, "{\"index\":1,\"id\":8,\"enum\":null,\"size\":16}"));

    const std::string detail = game_browse::RowJsonOf(table, "thing.dat", 0);
    CHECK(Has(detail, "\"fields\":[]"));
    CHECK(Has(detail, "\"texts\":[{\"offset\":8,\"text\":\"ITEM_ID_A\"},{\"offset\":16,\"text\":\"sample text\"}]"));
    CHECK(Has(detail, "\"hex\":\"0700000000000000"));
    CHECK(Has(detail, "2a000000"));

    const std::string missing = game_browse::RowJsonOf(table, "thing.dat", 2);
    CHECK(Has(missing, "\"ok\":false"));
    CHECK(Has(missing, "thing.dat has 2 rows, so no row 2"));
}

void TestCuratedTable() {
    std::printf("Browse: texture.dat rows\n");
    blob::Pool pool(0);
    Bytes row = table_schema::NewRow(table_schema::kTexture, 0, pool);
    blob::Put<uint64_t>(row.data(), 0, 1900000001);
    pool.Write(row, 8, "TEX_ID_SAMPLE");
    for (size_t language = 0; language < 7; ++language) {
        pool.Write(row, 24 + 8 * language, "data/texture/sample.nltx");
    }
    blob::Put<uint32_t>(row.data(), 92, 64);
    blob::Put<uint32_t>(row.data(), 100, 130);
    Bytes bytes;
    blob::Table table;
    Make({row}, pool.Bytes(), bytes, table);

    const std::string list = game_browse::TableJsonOf(table, "texture.dat");
    CHECK(Has(list, "\"struct\":\"TextureData\",\"schema\":true"));
    CHECK(Has(list, "\"enum\":\"TEX_ID_SAMPLE\""));

    const std::string detail = game_browse::RowJsonOf(table, "texture.dat", 0);
    CHECK(Has(detail, "{\"path\":\"id\",\"type\":\"id\",\"value\":\"1900000001\",\"table\":null,\"editable\":false}"));
    CHECK(Has(detail,
              "{\"path\":\"enumName\",\"type\":\"enum\",\"value\":\"TEX_ID_SAMPLE\",\"table\":null,\"editable\":false}"));
    CHECK(Has(detail, "{\"path\":\"fileName.en\",\"type\":\"path\",\"value\":\"data/texture/sample.nltx\""));
    CHECK(Has(detail, "{\"path\":\"fileName.zh-cn\",\"type\":\"path\""));
    // Values are the text a patch takes: a Bool is 0 or 1.
    CHECK(Has(detail, "{\"path\":\"isResident\",\"type\":\"bool\",\"value\":\"0\",\"table\":null,\"editable\":true}"));
    CHECK(Has(detail, "{\"path\":\"width\",\"type\":\"u32\",\"value\":\"64\""));
    CHECK(Has(detail, "{\"path\":\"trailer.version\",\"type\":\"u32\",\"value\":\"130\""));
    CHECK(Has(detail, "{\"path\":\"trailer.platform\",\"type\":\"u32\",\"value\":\"0\""));
}

void TestSkinRow() {
    std::printf("Browse: animereplacetexture.dat rows\n");
    blob::Pool pool(0);
    Bytes row = table_schema::NewRow(table_schema::kAnimeReplaceTexture, 2, pool);
    blob::Put<uint32_t>(row.data(), 24, 2);
    pool.Write(row, 28, "body");
    blob::Put<uint32_t>(row.data(), 36, 899);
    pool.Write(row, 40, "face");
    blob::Put<uint32_t>(row.data(), 48, 900);
    Bytes bytes;
    blob::Table table;
    Make({row}, pool.Bytes(), bytes, table);

    const std::string detail = game_browse::RowJsonOf(table, "animereplacetexture", 0);
    CHECK(Has(detail, "{\"path\":\"entries.count\",\"type\":\"u32\",\"value\":\"2\""));
    CHECK(Has(detail, "{\"path\":\"entries.0.name\",\"type\":\"text\",\"value\":\"body\""));
    CHECK(Has(detail,
              "{\"path\":\"entries.1.texture\",\"type\":\"ref\",\"value\":\"900\",\"table\":\"texture.dat\","
              "\"editable\":true}"));
    CHECK(Has(detail, "\"trailer.disableVersion\""));
    CHECK(Has(detail, "{\"path\":\"entries.count\",\"type\":\"u32\",\"value\":\"2\",\"table\":null,\"editable\":false}"));
}

void TestGrids() {
    std::printf("Browse: grids\n");
    blob::Pool pool(0);
    Bytes first = table_schema::NewRow(table_schema::kTexture, 0, pool);
    blob::Put<uint64_t>(first.data(), 0, 1900000001);
    pool.Write(first, 8, "TEX_ID_SAMPLE");
    blob::Put<uint32_t>(first.data(), 92, 64);
    Bytes second = table_schema::NewRow(table_schema::kTexture, 0, pool);
    blob::Put<uint64_t>(second.data(), 0, 1900000002);
    blob::Put<uint32_t>(second.data(), 92, 32);
    // Too short for a texture row: shown, with no values.
    Bytes broken(40, 0);
    blob::Put<uint64_t>(broken.data(), 0, 3);
    Bytes bytes;
    blob::Table table;
    Make({first, second, broken}, pool.Bytes(), bytes, table);

    const std::string grid = game_browse::GridJsonOf(table, "texture.dat");
    CHECK(Has(grid, "\"ok\":true"));
    CHECK(Has(grid, "\"struct\":\"TextureData\""));
    CHECK(Has(grid, "{\"path\":\"id\",\"type\":\"id\",\"table\":null,\"editable\":false}"));
    CHECK(Has(grid, "{\"path\":\"width\",\"type\":\"u32\",\"table\":null,\"editable\":true}"));
    CHECK(Has(grid, "{\"path\":\"fileName.en\",\"type\":\"path\",\"table\":null,\"editable\":true}"));
    CHECK(Has(grid, "{\"index\":0,\"id\":1900000001,\"enum\":\"TEX_ID_SAMPLE\",\"fits\":true,\"values\":[\"1900000001\","
                    "\"TEX_ID_SAMPLE\","));
    CHECK(Has(grid, "{\"index\":2,\"id\":3,\"enum\":null,\"fits\":false,\"values\":[null,null,"));
    CHECK(Count(grid, "\"fits\":true") == 2);

    // A row with a longer vector adds its elements' columns beside the
    // others, and a shorter row has none there.
    blob::Pool skins(0);
    Bytes one = table_schema::NewRow(table_schema::kAnimeReplaceTexture, 1, skins);
    blob::Put<uint32_t>(one.data(), 24, 1);
    blob::Put<uint32_t>(one.data(), 36, 899);
    Bytes two = table_schema::NewRow(table_schema::kAnimeReplaceTexture, 2, skins);
    blob::Put<uint32_t>(two.data(), 24, 2);
    blob::Put<uint32_t>(two.data(), 48, 900);
    Bytes skinBytes;
    blob::Table skinTable;
    Make({one, two}, skins.Bytes(), skinBytes, skinTable);
    const std::string skinGrid = game_browse::GridJsonOf(skinTable, "animereplacetexture.dat");
    const size_t firstTexture = skinGrid.find("\"path\":\"entries.0.texture\"");
    const size_t secondName = skinGrid.find("\"path\":\"entries.1.name\"");
    const size_t trailer = skinGrid.find("\"path\":\"trailer.version\"");
    CHECK(firstTexture != std::string::npos && secondName != std::string::npos && trailer != std::string::npos);
    CHECK(firstTexture < secondName && secondName < trailer);
    CHECK(Has(skinGrid, "\"899\",null,null,"));
    CHECK(Has(skinGrid, "\"900\","));

    // A generated table, and one whose rows have no enum name.
    Bytes smith(table_schema::RowSize(*table_schema::FindPatchable("blacksmith"), 0), 0);
    blob::Put<uint64_t>(smith.data(), 0, 5);
    Bytes smithBytes;
    blob::Table smithTable;
    Make({smith}, std::string(1, '\0'), smithBytes, smithTable);
    CHECK(Has(game_browse::TableJsonOf(smithTable, "blacksmith.dat"), "{\"index\":0,\"id\":5,\"enum\":null,"));
    CHECK(Has(game_browse::TableJsonOf(smithTable, "blacksmith.dat"), "\"schema\":true"));
    CHECK(Has(game_browse::RowJsonOf(smithTable, "blacksmith.dat", 0), "\"editable\":true"));
    CHECK(Has(game_browse::GridJsonOf(smithTable, "blacksmith.dat"), "\"fits\":true"));

    // A framework_ twin takes its table's schema.
    CHECK(Has(game_browse::TableJsonOf(table, "framework_texture.dat"), "\"struct\":\"TextureData\",\"schema\":true"));
    CHECK(Has(game_browse::GridJsonOf(table, "thing.dat"), "Canopy has no schema for thing.dat"));
}

// Every table with a schema, from the real install: each row fits, and the
// grid has the rows the table has.
void TestRealGrids() {
    std::printf("Browse: grids of the real game\n");
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return;
    }
    const std::wstring game = folder.wstring();
    for (const table_schema::TableSchema* schema : table_schema::Patchable()) {
        // A table that ships its rows only in its framework_ twin is shown there.
        std::string file = schema->file;
        std::string grid = game_browse::GridJson(game, file);
        if (Count(grid, "\"fits\":") == 0) {
            file = "framework_" + file;
            grid = game_browse::GridJson(game, file);
        }
        const std::string rows = game_browse::TableJson(game, file);
        if (!CHECK(Has(grid, "\"ok\":true"))) {
            std::printf("  %s: no grid\n", file.c_str());
            continue;
        }
        const size_t gridRows = Count(grid, "\"fits\":");
        const size_t columns = Count(grid.substr(0, grid.find("\"rows\":")), "\"path\":");
        CHECK(gridRows == Count(rows, "\"index\":"));
        CHECK(Count(grid, "\"fits\":false") == 0);
        CHECK(columns > 0);
        std::printf("  %s: %zu rows, %zu columns\n", file.c_str(), gridRows, columns);
    }
}

void TestFailures() {
    std::printf("Browse: failures\n");
    CHECK(Has(game_browse::TableJson(L"", "../item.dat"), "is not a table file name"));
    CHECK(Has(game_browse::TableJson(L"", "a/b.dat"), "\"ok\":false"));
    const std::string game = game_browse::GameJson(L"");
    CHECK(Has(game, "\"ok\":false") && Has(game, "\"exe\":false") && Has(game, "\"archives\":[]"));
    CHECK(Has(game_browse::FailureJson("say \"why\""), "\"error\":\"say \\\"why\\\"\""));
}

}  // namespace

void TestBrowse(int& checks, int& failures) {
    TestUncuratedTable();
    TestCuratedTable();
    TestSkinRow();
    TestGrids();
    TestRealGrids();
    TestFailures();
    checks += g_checks;
    failures += g_failures;
}
