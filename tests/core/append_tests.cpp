#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "mod_rules.h"
#include "rules_append.h"
#include "rules_rows.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the list additions: AppendElement on a row, reading
// [offering.], [catch.] and [forage.] sections and every error, applying
// them to synthetic tables, and the desktop check and a whole-table apply
// against the game when it is installed. Mods are written to a temporary
// folder; the game is read, never written. Only ids, counts and sizes are
// printed, never game text.
namespace {

namespace fs = std::filesystem;
namespace ts = table_schema;
using mod_rules::AppendOutcome;
using mod_rules::AppendResult;
using mod_rules::AppendSpec;
using mod_rules::Diagnostic;
using mod_rules::RefKind;
using mod_rules::Severity;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED append_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

class TempMods {
public:
    explicit TempMods(const char* name) {
        root_ = fs::temp_directory_path() /
                ("canopy_append_tests_" + std::to_string(test_support::ProcessId()) + "_" + name);
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_, error);
    }
    ~TempMods() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    void Write(const std::string& relative, const std::string& text) const {
        const fs::path path = root_ / fs::u8path(relative);
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    std::string Dir() const {
        return root_.string();
    }

private:
    fs::path root_;
};

// The lines joined into a file: line n of the file is lines[n - 1].
std::string Lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    return text;
}

// The diagnostic at line whose message holds part, null when there is none.
const Diagnostic* Said(const mod_rules::Report& report, int line, const std::string& part) {
    for (const Diagnostic& item : report.Items()) {
        if (item.line == line && item.message.find(part) != std::string::npos) {
            return &item;
        }
    }
    return nullptr;
}

const AppendSpec* Spec(const std::vector<AppendSpec>& specs, const char* key) {
    for (const AppendSpec& spec : specs) {
        if (spec.key == key) {
            return &spec;
        }
    }
    return nullptr;
}

// The value of the element field path, or "" when the element sets none.
std::string ElementAt(const AppendSpec& spec, size_t element, const char* path) {
    if (element >= spec.elements.size()) {
        return "<no element>";
    }
    for (const mod_rules::ElementValue& value : spec.elements[element]) {
        if (value.path == path) {
            return value.ref >= 0 ? "ref " + std::to_string(value.ref) : value.value;
        }
    }
    return "";
}

// Every enum name is id 42; vanilla id 13 is in no table.
class StubTables : public mod_rules::RowTables {
public:
    bool EnumId(const char*, const std::string&, uint64_t& id, std::string&) const override {
        id = 42;
        return true;
    }
    bool VanillaFine(const char* file, uint64_t id, std::string& why) const override {
        if (id == 13) {
            why = std::string("13 is not in ") + file;
            return false;
        }
        return true;
    }
};

// A table built from rows, with the bytes it points into.
struct Built {
    std::vector<uint8_t> bytes;
    blob::Table table;
};

std::unique_ptr<Built> BuildTable(const std::vector<std::vector<uint8_t>>& rows) {
    auto built = std::make_unique<Built>();
    blob::Build(rows, std::string(1, '\0'), built->bytes);
    std::string error;
    blob::Parse(built->bytes.data(), built->bytes.size(), built->table, error);
    return built;
}

std::unique_ptr<Built> ParseTable(const std::vector<uint8_t>& bytes) {
    auto built = std::make_unique<Built>();
    built->bytes = bytes;
    std::string error;
    if (!blob::Parse(built->bytes.data(), built->bytes.size(), built->table, error)) {
        built->table = blob::Table();
    }
    return built;
}

std::vector<uint8_t> EmptyRow(const ts::TableSchema& schema, uint64_t id) {
    std::vector<uint8_t> row(schema.rowSize, 0);
    blob::Put<uint64_t>(row.data(), 0, id);
    return row;
}

// The value at path in row, as Read gives it, texts from table's pool or
// from added.
std::string ReadAt(const ts::TableSchema& schema, const uint8_t* row, size_t size, const blob::Table& table,
                   const blob::Pool* added, const std::string& path) {
    std::vector<ts::Location> layout;
    std::string error;
    ts::Location location;
    std::string value;
    if (!ts::Layout(schema, row, size, layout, error) || !ts::Locate(layout, path, location, error) ||
        !ts::Read(row, size, location, table, added, value, error)) {
        return "<" + error + ">";
    }
    return value;
}

std::string ReadAt(const ts::TableSchema& schema, const blob::Table& table, size_t row, const std::string& path) {
    return ReadAt(schema, table.rows[row].data, table.rows[row].size, table, nullptr, path);
}

uint32_t CountAt(const ts::TableSchema& schema, const uint8_t* row, size_t size, const std::string& path) {
    uint32_t count = 0;
    std::string error;
    return ts::ElementCount(schema, row, size, path, count, error) ? count : 99999;
}

bool Write(const ts::TableSchema& schema, std::vector<uint8_t>& row, const std::string& path, const std::string& value,
           blob::Pool& pool) {
    std::vector<ts::Location> places;
    std::string error;
    return ts::ResolveRow(schema, path, row.data(), row.size(), places, error) && places.size() == 1 &&
           ts::Write(row, places[0], value, pool, error);
}

// ---- AppendElement -------------------------------------------------------

void TestAppendElement() {
    std::printf("Append element\n");
    const ts::TableSchema& offering = *ts::FindPatchable("offeringitem");
    std::vector<uint8_t> row = EmptyRow(offering, 1);
    const auto empty = BuildTable({row});
    blob::Pool pool(empty->table.poolBytes);
    std::string error;
    uint32_t index = 99;
    CHECK(ts::AppendElement(offering, row, "itemInfo", {{"itemID", "5"}, {"itemName", "A.B"}, {"dropRate", "3"}}, pool,
                            index, error));
    CHECK(index == 0 && CountAt(offering, row.data(), row.size(), "itemInfo") == 1);
    CHECK(ts::AppendElement(offering, row, "ITEMINFO", {{"count", "2"}}, pool, index, error) && index == 1);
    CHECK(ReadAt(offering, row.data(), row.size(), empty->table, &pool, "itemInfo.0.itemID") == "5");
    CHECK(ReadAt(offering, row.data(), row.size(), empty->table, &pool, "itemInfo.0.itemName") == "A.B");
    CHECK(ReadAt(offering, row.data(), row.size(), empty->table, &pool, "itemInfo.0.dropRate") == "3");
    CHECK(ReadAt(offering, row.data(), row.size(), empty->table, &pool, "itemInfo.1.itemID") == "0");
    CHECK(ReadAt(offering, row.data(), row.size(), empty->table, &pool, "itemInfo.1.count") == "2");
    CHECK(ts::RowFits(offering, row.data(), row.size()));

    // A value that does not fit, or a field the element lacks, leaves the
    // row as it was.
    const std::vector<uint8_t> before = row;
    CHECK(!ts::AppendElement(offering, row, "itemInfo", {{"dropRate", "x"}}, pool, index, error) &&
          error.rfind("itemInfo.2.dropRate: ", 0) == 0 && row == before);
    CHECK(!ts::AppendElement(offering, row, "itemInfo", {{"nosuch", "1"}}, pool, index, error) && row == before);
    CHECK(!ts::AppendElement(offering, row, "name", {}, pool, index, error) &&
          error == "offeringitem.dat rows have no vector name here" && row == before);
    uint32_t count = 0;
    CHECK(!ts::ElementCount(offering, row.data(), row.size(), "rarity", count, error));

    // A full k_vector takes no more.
    CHECK(ts::ResizeVector(offering, row, "itemInfo", 16, pool, error));
    const std::vector<uint8_t> full = row;
    CHECK(!ts::AppendElement(offering, row, "itemInfo", {{"itemID", "5"}}, pool, index, error) &&
          error == "itemInfo can have 16 elements at most" && row == full);

    // A list inside the new element is sized by <list>.count first.
    const ts::TableSchema& fishing = *ts::FindPatchable("fishing");
    std::vector<uint8_t> spot = EmptyRow(fishing, 1);
    CHECK(ts::AppendElement(fishing, spot, "captureInfo",
                            {{"id", "300"}, {"rate", "0.5"}, {"condSeason.count", "2"}, {"condSeason.1", "3"},
                             {"condTime", "7:00-12:00"}},
                            pool, index, error));
    CHECK(CountAt(fishing, spot.data(), spot.size(), "captureInfo.0.condSeason") == 2);
    CHECK(ReadAt(fishing, spot.data(), spot.size(), empty->table, &pool, "captureInfo.0.condSeason.1") == "3");
    CHECK(ReadAt(fishing, spot.data(), spot.size(), empty->table, &pool, "captureInfo.0.rate") == "0.5");
    CHECK(ReadAt(fishing, spot.data(), spot.size(), empty->table, &pool, "captureInfo.0.condTime") == "7:00-12:00");
    CHECK(!ts::AppendElement(fishing, spot, "captureInfo", {{"condSeason.count", "5"}}, pool, index, error) &&
          error == "captureInfo.1.condSeason can have 4 elements at most");
    CHECK(!ts::AppendElement(fishing, spot, "captureInfo", {{"condSeason.count", "two"}}, pool, index, error) &&
          error == "captureInfo.1.condSeason.count: two is not a count");
    CHECK(CountAt(fishing, spot.data(), spot.size(), "captureInfo") == 1);
}

// ---- Reading -------------------------------------------------------------

void TestReading() {
    std::printf("List addition sections\n");
    TempMods mods("read");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                          // 1
            "key = Alpha",                    // 2
            "[offering.Gift]",                // 3
            "pools = all",                    // 4
            "item = Gem",                     // 5  this mod's item
            "rate = 2",                       // 6
            "[offering.NoPools]",             // 7
            "item = 300",                     // 8
            "[catch.Bass]",                   // 9
            "spots = 1, 2, 1",                // 10 named twice
            "fish = 300",                     // 11
            "seasons = Spring, winter",       // 12
            "times = 7:00-12:00, 18:00-0:00", // 13
            "rate = 0.5",                     // 14
            "traps = 3",                      // 15 goes with traps
            "[catch.BadTime]",                // 16
            "spots = all",                    // 17
            "fish = 300",                     // 18
            "times = 25:00-1:00",             // 19
            "[catch.Trap]",                   // 20
            "traps = all",                    // 21
            "item = 300",                     // 22
            "bait = yes",                     // 23
            "[catch.Bug]",                    // 24
            "traps = 4, CAPTURE_ID_X",        // 25
            "creature = 12",                  // 26
            "[catch.Both]",                   // 27
            "fish = 300",                     // 28
            "item = 300",                     // 29
            "[catch.None]",                   // 30
            "rate = 1",                       // 31
            "[forage.Shroom]",                // 32
            "spots = 500000000",              // 33
            "item = 300",                     // 34
            "weather = 1, 2",                 // 35
            "flag = Flagged",                 // 36 this mod's flag
            "unless = 100",                   // 37
            "colour = red",                   // 38 no such key
            "[forage.Berry]",                 // 39
            "areas = all",                    // 40
            "crop = CROPS_ID_X",              // 41
            "seasons = all",                  // 42
            "daily = 2-4",                    // 43
            "most = 7",                       // 44
            "[forage.BadDaily]",              // 45
            "areas = all",                    // 46
            "crop = 5",                       // 47
            "daily = 5-2",                    // 48
            "[offering.ModId]",               // 49
            "pools = all",                    // 50
            "item = 1500000001",              // 51 a mod item's id
            "[offering.Row]",                 // 52
            "pools = CANOPY_OFFERINGITEM_X",  // 53 a mod row
            "item = 300",                     // 54
            "[offering.Gift]",                // 55 key taken
            "pools = all",                    // 56
            "item = 300",                     // 57
            "[catch.Rate]",                   // 58
            "spots = all",                    // 59
            "fish = 300",                     // 60
            "rate = 0",                       // 61
        })
    );
    mods.Write("Beta/canopy.ini", Lines({"[mod]", "key = Alpha", "[offering.Other]", "pools = all", "item = 300"}));

    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Item, "Alpha.Gem");
    refs.Add(RefKind::Flag, "Alpha.Flagged");
    mod_rules::Report report;
    std::vector<std::string> accepted;
    const std::vector<AppendSpec> specs = mod_rules::ReadAppends(
        mod_rules::ScanMods(mods.Dir()), refs, report,
        [&](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
            accepted.push_back(folder.name + ":" + modKey + ":" + std::to_string(count));
            return true;
        }
    );
    CHECK(accepted.size() == 1 && accepted[0] == "Alpha:Alpha:6");
    CHECK(specs.size() == 6);

    CHECK(Said(report, 7, "[Alpha] Offering NoPools skipped, it needs pools = all, or offeringitem ids or enum names."));
    CHECK(Said(report, 15, "[Alpha] Catch Bass: traps is ignored, it goes with item or creature."));
    CHECK(Said(report, 19, "[Alpha] Catch BadTime skipped, times must be windows such as 7:00-12:00, or all, not "
                           "25:00-1:00."));
    CHECK(Said(report, 29, "[Alpha] Catch Both skipped, it has both fish and item, and takes one."));
    CHECK(Said(report, 30, "[Alpha] Catch None skipped, it needs fish, item or creature."));
    CHECK(Said(report, 38, "[Alpha] Forage Shroom: colour is not a forage entry key, so it is ignored."));
    CHECK(Said(report, 48, "[Alpha] Forage BadDaily skipped, daily must be min-max from 0 to 100, such as 1-3."));
    CHECK(Said(report, 51, "[Alpha] Offering ModId skipped, item 1500000001 is a mod item's id; name a mod item by "
                           "its key."));
    CHECK(Said(report, 53, "[Alpha] Offering Row skipped, pools CANOPY_OFFERINGITEM_X is a mod row's enum name, "
                           "which pools does not take."));
    CHECK(Said(report, 55, "[Alpha] Offering Gift skipped, an earlier Offering section of the mod has that key."));
    CHECK(Said(report, 61, "[Alpha] Catch Rate skipped, rate must be a number over 0 and up to 1000, such as 0.5 or "
                           "3."));
    CHECK(Said(report, 2, "Offerings in folder Beta skipped, another folder already has offerings under the key "
                          "Alpha."));
    CHECK(report.Count(Severity::Error) == 10 && report.Count(Severity::Warning) == 2);

    const AppendSpec* gift = Spec(specs, "Gift");
    CHECK(gift && gift->allRows && gift->list == "itemInfo" && gift->label == "Mod offering Alpha.Gift" &&
          gift->line == 3 && gift->elements.size() == 1);
    CHECK(gift && gift->refs.size() == 1 && gift->refs[0].id == 1 && !gift->refs[0].vanilla &&
          ElementAt(*gift, 0, "itemID") == "ref 0" && ElementAt(*gift, 0, "itemName") == "Alpha.Gift" &&
          ElementAt(*gift, 0, "dropRate") == "2" && ElementAt(*gift, 0, "count") == "1");

    const AppendSpec* bass = Spec(specs, "Bass");
    CHECK(bass && bass->schema == ts::FindPatchable("fishing") && bass->rows.size() == 2 && !bass->allRows);
    CHECK(bass && bass->refs[0].id == 300 && bass->refs[0].vanilla && bass->refs[0].alsoIn == std::string("fish.dat"));
    // Two seasons times two windows, the game's way of listing a fish.
    CHECK(bass && bass->elements.size() == 4 && ElementAt(*bass, 0, "condSeason.0") == "0" &&
          ElementAt(*bass, 0, "condTime") == "7:00-12:00" && ElementAt(*bass, 1, "condTime") == "18:00-0:00" &&
          ElementAt(*bass, 3, "condSeason.0") == "3" && ElementAt(*bass, 0, "rate") == "0.5" &&
          ElementAt(*bass, 0, "condSeason.count") == "1");

    const AppendSpec* trap = Spec(specs, "Trap");
    CHECK(trap && trap->allRows && trap->elements.size() == 1 && ElementAt(*trap, 0, "type") == "1" &&
          ElementAt(*trap, 0, "checkStatusName") == "bait" && ElementAt(*trap, 0, "checkStatusValue") == "1");
    const AppendSpec* bug = Spec(specs, "Bug");
    CHECK(bug && bug->rows.size() == 2 && bug->rows[1].enumName == "CAPTURE_ID_X" && bug->elements.size() == 2 &&
          ElementAt(*bug, 0, "type") == "0" && ElementAt(*bug, 0, "checkStatusValue") == "0" &&
          bug->refs[0].table == std::string("creature.dat"));

    const AppendSpec* shroom = Spec(specs, "Shroom");
    CHECK(shroom && shroom->outer == "tableInfo" && shroom->match == "tableID" && !shroom->allOuter &&
          shroom->matches.size() == 1 && shroom->matches[0].id == 500000000 && shroom->refs.size() == 3);
    CHECK(shroom && ElementAt(*shroom, 0, "popWeather.count") == "2" && ElementAt(*shroom, 0, "popWeather.1") == "ref 2" &&
          ElementAt(*shroom, 0, "condOnFlag") == "1" && ElementAt(*shroom, 0, "condOffFlag") == "100" &&
          ElementAt(*shroom, 0, "popMax") == "-1" && ElementAt(*shroom, 0, "weight") == "10");

    // Every season is one element of season 4, as vanilla crops list it.
    const AppendSpec* berry = Spec(specs, "Berry");
    CHECK(berry && berry->refs[0].enumName == "CROPS_ID_X" && berry->elements.size() == 1 &&
          ElementAt(*berry, 0, "popSeason") == "4" && ElementAt(*berry, 0, "dayPopMin") == "2" &&
          ElementAt(*berry, 0, "dayPopMax") == "4" && ElementAt(*berry, 0, "objectMaxCount") == "7" &&
          ElementAt(*berry, 0, "cropsName") == "Alpha.Berry");

    std::vector<const AppendSpec*> all;
    for (const AppendSpec& spec : specs) {
        all.push_back(&spec);
    }
    CHECK(mod_rules::CountAppends(all) == "1 offering, 3 catches and 2 forage entries");
}

// ---- Applying ------------------------------------------------------------

void TestApplying() {
    std::printf("List additions applied\n");
    TempMods mods("apply");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                              // 1
            "key = Alpha",                        // 2
            "[offering.Gift]",                    // 3
            "pools = all",                        // 4
            "item = Gem",                         // 5
            "rate = 2",                           // 6
            "[offering.Lost]",                    // 7
            "pools = 1, 9, OFFERING_ID_X",        // 8
            "item = 300",                         // 9
            "[offering.Bad]",                     // 10
            "pools = 1",                          // 11
            "item = 13",                          // 12
            "[forage.Shroom]",                    // 13
            "spots = 500000001, 500000009",       // 14
            "item = 300",                         // 15
            "[catch.Bass]",                       // 16
            "spots = all",                        // 17
            "fish = 300",                         // 18
            "seasons = summer",                   // 19
            "times = 07:00-12:00",                // 20
        })
    );
    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Item, "Alpha.Gem");
    mod_rules::Report report;
    const std::vector<AppendSpec> specs =
        mod_rules::ReadAppends(mod_rules::ScanMods(mods.Dir()), refs, report, mod_rules::AcceptAll);
    CHECK(specs.size() == 5 && report.Items().empty());
    const StubTables tables;

    // Offering pools: one empty, one full.
    const ts::TableSchema& offering = *ts::FindPatchable("offeringitem");
    std::vector<uint8_t> fullRow = EmptyRow(offering, 2);
    blob::Pool scratch(1);
    std::string error;
    CHECK(ts::ResizeVector(offering, fullRow, "itemInfo", 16, scratch, error));
    const auto pools = BuildTable({EmptyRow(offering, 1), fullRow});
    std::vector<const AppendSpec*> mine = {Spec(specs, "Gift"), Spec(specs, "Lost"), Spec(specs, "Bad")};
    std::vector<uint8_t> out;
    AppendResult result;
    CHECK(mod_rules::ApplyAppends(pools->table, mine, tables, out, result));
    CHECK(result.elements == 2 && result.rows == 1 && result.outcomes.size() == 3);
    const auto grown = ParseTable(out);
    CHECK(grown->table.rows.size() == 2);
    if (grown->table.rows.size() == 2) {
        CHECK(CountAt(offering, grown->table.rows[0].data, grown->table.rows[0].size, "itemInfo") == 2);
        CHECK(ReadAt(offering, grown->table, 0, "itemInfo.0.itemID") == "1");
        CHECK(ReadAt(offering, grown->table, 0, "itemInfo.0.itemName") == "Alpha.Gift");
        CHECK(ReadAt(offering, grown->table, 0, "itemInfo.0.dropRate") == "2");
        CHECK(ReadAt(offering, grown->table, 0, "itemInfo.1.itemID") == "300");
        CHECK(ReadAt(offering, grown->table, 0, "itemInfo.1.itemName") == "Alpha.Lost");
        // The full pool is kept byte for byte.
        CHECK(std::equal(fullRow.begin(), fullRow.end(), grown->table.rows[1].data) &&
              grown->table.rows[1].size == fullRow.size());
    }
    const AppendOutcome& giftOut = result.outcomes[0];
    CHECK(giftOut.rows.size() == 1 && giftOut.rows[0].first == 1 && giftOut.failed.size() == 1);
    const std::vector<std::string> giftLines = mod_rules::FailureMessages(giftOut);
    CHECK(giftLines.size() == 1 &&
          giftLines[0] == "Mod offering Alpha.Gift skipped on offeringitem 2, itemInfo can have 16 elements at most.");
    const AppendOutcome& lostOut = result.outcomes[1];
    CHECK(lostOut.foundRows == std::vector<size_t>{0} && !mod_rules::Unresolved(lostOut));
    const std::vector<std::string> lostLines = mod_rules::MissingMessages(*lostOut.spec, lostOut.foundRows, {});
    CHECK(lostLines.size() == 1 && lostLines[0] == "Mod offering Alpha.Lost skipped on pools 9 and OFFERING_ID_X, "
                                                   "which offeringitem.dat does not have.");
    const AppendOutcome& badOut = result.outcomes[2];
    CHECK(mod_rules::Unresolved(badOut) && badOut.rows.empty());
    const std::vector<std::string> badLines = mod_rules::FailureMessages(badOut);
    CHECK(badLines.size() == 1 && badLines[0] == "Mod offering Alpha.Bad skipped, item: 13 is not in item.dat.");

    // Nothing added leaves out empty.
    CHECK(!mod_rules::ApplyAppends(pools->table, {Spec(specs, "Bad")}, tables, out, result) && out.empty());

    // Forage spots: only the table whose tableID is named grows.
    const ts::TableSchema& placement = *ts::FindPatchable("placementitem");
    std::vector<uint8_t> area = EmptyRow(placement, 1);
    CHECK(ts::ResizeVector(placement, area, "tableInfo", 2, scratch, error));
    CHECK(Write(placement, area, "tableInfo.0.tableID", "500000000", scratch) &&
          Write(placement, area, "tableInfo.1.tableID", "500000001", scratch));
    const auto areas = BuildTable({area});
    CHECK(mod_rules::ApplyAppends(areas->table, {Spec(specs, "Shroom")}, tables, out, result));
    const auto placed = ParseTable(out);
    if (CHECK(placed->table.rows.size() == 1)) {
        const blob::Row& row = placed->table.rows[0];
        CHECK(CountAt(placement, row.data, row.size, "tableInfo.0.itemInfo") == 0);
        CHECK(CountAt(placement, row.data, row.size, "tableInfo.1.itemInfo") == 1);
        CHECK(ReadAt(placement, placed->table, 0, "tableInfo.1.itemInfo.0.itemID") == "300");
        CHECK(ReadAt(placement, placed->table, 0, "tableInfo.1.itemInfo.0.popMax") == "-1");
        CHECK(ReadAt(placement, placed->table, 0, "tableInfo.1.itemInfo.0.itemName") == "Alpha.Shroom");
        CHECK(ts::RowFits(placement, row.data, row.size));
    }
    const AppendOutcome& shroomOut = result.outcomes[0];
    CHECK(shroomOut.foundMatches == std::vector<size_t>{0});
    const std::vector<std::string> shroomLines =
        mod_rules::MissingMessages(*shroomOut.spec, shroomOut.foundRows, shroomOut.foundMatches);
    CHECK(shroomLines.size() == 1 && shroomLines[0] == "Mod forage entry Alpha.Shroom skipped on spots 500000009, "
                                                       "which no placementitem.dat row lists.");

    // Fishing spots: a window written with a leading zero is the game's.
    const ts::TableSchema& fishing = *ts::FindPatchable("fishing");
    const auto spots = BuildTable({EmptyRow(fishing, 1), EmptyRow(fishing, 2)});
    CHECK(mod_rules::ApplyAppends(spots->table, {Spec(specs, "Bass")}, tables, out, result));
    CHECK(result.elements == 2 && result.rows == 2);
    const auto fished = ParseTable(out);
    if (CHECK(fished->table.rows.size() == 2)) {
        CHECK(ReadAt(fishing, fished->table, 1, "captureInfo.0.id") == "300");
        CHECK(ReadAt(fishing, fished->table, 1, "captureInfo.0.condSeason.0") == "1");
        CHECK(ReadAt(fishing, fished->table, 1, "captureInfo.0.condTime") == "7:00-12:00");
        CHECK(ReadAt(fishing, fished->table, 1, "captureInfo.0.rate") == "1");
    }
}

// ---- The real game, read only --------------------------------------------

uint64_t FirstId(const mod_rules::VanillaData& vanilla, const char* file) {
    const blob::Table* table = vanilla.Table(file);
    return table && !table->rows.empty() ? blob::RowId(table->rows[0]) : 0;
}

void TestRealGame() {
    std::printf("List additions on the game's tables\n");
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return;
    }
    mod_rules::VanillaData vanilla;
    std::string error;
    if (!CHECK(vanilla.Open(folder.wstring(), error))) {
        return;
    }
    const uint64_t fishId = FirstId(vanilla, "fish.dat");
    const uint64_t creatureId = FirstId(vanilla, "creature.dat");
    const uint64_t cropId = FirstId(vanilla, "crops.dat");
    // An item that is not a fish.
    uint64_t itemId = 0;
    const blob::Table* items = vanilla.Table("item.dat");
    for (size_t index = 0; items && index < items->rows.size() && itemId == 0; ++index) {
        const uint64_t id = blob::RowId(items->rows[index]);
        itemId = vanilla.HasRow("fish.dat", id) ? 0 : id;
    }
    // A forage spot some placementitem row lists.
    const ts::TableSchema& placement = *ts::FindPatchable("placementitem");
    const blob::Table* placements = vanilla.Table("placementitem.dat");
    uint64_t spotId = 0;
    if (placements && !placements->rows.empty()) {
        std::vector<ts::Location> places;
        const blob::Row& row = placements->rows[0];
        if (ts::ResolveRow(placement, "tableInfo.0.tableID", row.data, row.size, places, error) && places.size() == 1) {
            spotId = ts::ReadNumber(row.data, places[0]);
        }
    }
    if (!CHECK(fishId && creatureId && cropId && itemId && spotId)) {
        return;
    }
    std::printf("  ids: fish %llu, item %llu, creature %llu, crop %llu, spot %llu\n",
                static_cast<unsigned long long>(fishId), static_cast<unsigned long long>(itemId),
                static_cast<unsigned long long>(creatureId), static_cast<unsigned long long>(cropId),
                static_cast<unsigned long long>(spotId));

    TempMods mods("game");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                                     // 1
            "key = Alpha",                               // 2
            "[offering.Gift]",                           // 3
            "pools = all",                               // 4
            "item = " + std::to_string(itemId),          // 5
            "[catch.Fish]",                              // 6
            "spots = all",                               // 7
            "fish = " + std::to_string(fishId),          // 8
            "[catch.NotFish]",                           // 9
            "spots = all",                               // 10
            "fish = " + std::to_string(itemId),          // 11
            "[catch.Trap]",                              // 12
            "traps = all",                               // 13
            "creature = " + std::to_string(creatureId),  // 14
            "[forage.Spot]",                             // 15
            "spots = " + std::to_string(spotId),         // 16
            "item = " + std::to_string(itemId),          // 17
            "[forage.Wild]",                             // 18
            "areas = all",                               // 19
            "crop = " + std::to_string(cropId),          // 20
            "[offering.Lost]",                           // 21
            "pools = 999999999",                         // 22
            "item = " + std::to_string(itemId),          // 23
        })
    );
    mod_rules::Context context;
    context.modsDir = mods.Dir();
    context.folders = mod_rules::ScanMods(mods.Dir());
    context.vanilla = &vanilla;
    mod_rules::CheckAppends(context);
    const mod_rules::Report& report = context.report;
    CHECK(Said(report, 9, "Mod catch Alpha.NotFish skipped, fish: it names fish " + std::to_string(itemId) +
                              ", which is not in fish.dat."));
    CHECK(Said(report, 21, "Mod offering Alpha.Lost skipped on pools 999999999, which offeringitem.dat does not "
                           "have."));
    CHECK(report.Count(Severity::Error) == 2 && report.Count(Severity::Warning) == 0);
    for (const Diagnostic& item : report.Items()) {
        if (item.line != 9 && item.line != 21) {
            std::printf("  unexpected at line %d\n", item.line);
        }
    }

    // Whole tables: every row keeps its place and fits, and each list grows
    // by what the addition gives it.
    mod_rules::Report readReport;
    const std::vector<AppendSpec> specs =
        mod_rules::ReadAppends(context.folders, context.refs, readReport, mod_rules::AcceptAll);
    const mod_rules::VanillaRowTables rowTables(vanilla);
    struct Case {
        const char* key;
        const char* file;
        const char* list;
        uint32_t added;
    };
    for (const Case& test : {Case{"Gift", "offeringitem.dat", "itemInfo", 1},
                             Case{"Fish", "fishing.dat", "captureInfo", 16},
                             Case{"Trap", "capture.dat", "captureInfo", 2},
                             Case{"Wild", "naturaloccurrence.dat", "cropsInfo", 1}}) {
        const AppendSpec* spec = Spec(specs, test.key);
        const blob::Table* table = vanilla.Table(test.file);
        if (!CHECK(spec && table)) {
            continue;
        }
        std::vector<uint8_t> out;
        AppendResult result;
        const bool any = mod_rules::ApplyAppends(*table, {spec}, rowTables, out, result);
        const auto grown = ParseTable(out);
        size_t fits = 0;
        size_t grew = 0;
        for (size_t row = 0; any && row < table->rows.size() && row < grown->table.rows.size(); ++row) {
            const blob::Row& before = table->rows[row];
            const blob::Row& after = grown->table.rows[row];
            fits += ts::RowFits(*spec->schema, after.data, after.size) ? 1 : 0;
            grew += blob::RowId(before) == blob::RowId(after) &&
                            CountAt(*spec->schema, after.data, after.size, test.list) ==
                                CountAt(*spec->schema, before.data, before.size, test.list) + test.added
                        ? 1
                        : 0;
        }
        std::printf("  %s: %zu rows, %zu elements added, %zu rows refused\n", test.file, table->rows.size(),
                    result.elements, result.outcomes.empty() ? 0 : result.outcomes[0].failed.size());
        CHECK(any && grown->table.rows.size() == table->rows.size() && fits == table->rows.size() &&
              grew == table->rows.size() && result.rows == table->rows.size());
    }
}

}  // namespace

void TestAppends(int& checks, int& failures) {
    TestAppendElement();
    TestReading();
    TestApplying();
    TestRealGame();
    checks += g_checks;
    failures += g_failures;
}
