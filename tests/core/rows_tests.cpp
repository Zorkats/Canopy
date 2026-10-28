#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "mod_rules.h"
#include "rules_rows.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the [row.<table>.<Key>] rules on generated tables: which tables
// refuse rows, reading sections and every error, numbering and references
// between rows, building a row, and the desktop check against the game when
// it is installed. Mods are written to a temporary folder; the game is read,
// never written. Only ids, counts and sizes are printed, never game text.
namespace {

namespace fs = std::filesystem;
namespace ts = table_schema;
using mod_rules::Diagnostic;
using mod_rules::RefKind;
using mod_rules::RowSpec;
using mod_rules::RowValue;
using mod_rules::Severity;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED rows_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

class TempMods {
public:
    explicit TempMods(const char* name) {
        root_ = fs::temp_directory_path() /
                ("canopy_rows_tests_" + std::to_string(test_support::ProcessId()) + "_" + name);
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

const RowSpec* Row(const std::vector<RowSpec>& rows, const char* table, const char* modKey, const char* key) {
    const ts::TableSchema* schema = ts::FindPatchable(table);
    return schema ? mod_rules::FindRow(rows, *schema, modKey, key) : nullptr;
}

// Without case: a value keeps its key as the section spells it.
bool SamePath(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

const RowValue* ValueOf(const RowSpec& row, const std::string& path) {
    for (const RowValue& value : row.values) {
        if (SamePath(value.path, path)) {
            return &value;
        }
    }
    return nullptr;
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

// The value at path in row, read as the game's grid shows it.
std::string ReadAt(const ts::TableSchema& schema, const std::vector<uint8_t>& row, const blob::Table& table,
                   const blob::Pool& pool, const std::string& path) {
    std::vector<ts::Location> layout;
    std::string error;
    ts::Location location;
    std::string value;
    if (!ts::Layout(schema, row.data(), row.size(), layout, error) || !ts::Locate(layout, path, location, error) ||
        !ts::Read(row.data(), row.size(), location, table, &pool, value, error)) {
        return "<" + error + ">";
    }
    return value;
}

// ---- Tables --------------------------------------------------------------

void TestRefused() {
    std::printf("Row tables\n");
    const auto refused = [](const char* table) {
        const ts::TableSchema* schema = ts::FindPatchable(table);
        return schema ? mod_rules::RowsRefused(*schema) : std::string("<no schema>");
    };
    CHECK(refused("blacksmith").empty());
    CHECK(refused("skilltree").empty());
    CHECK(refused("font").empty());
    CHECK(refused("gimmick").find("save keeps gimmick ids") != std::string::npos &&
          refused("gimmick").find("[object.<Key>]") != std::string::npos);
    CHECK(refused("craft") == "craft rows are added through their own sections. Use [recipe.<Key>] sections instead "
                              "(docs/RECIPES.md)");
    CHECK(refused("storesales").find("[sale.<Key>]") != std::string::npos);
    CHECK(refused("quest").find("save keeps quest ids") != std::string::npos &&
          refused("quest").find("[") == std::string::npos);
    CHECK(refused("talk").find("save keeps talk ids") != std::string::npos);
    size_t outfits = 0;
    size_t open = 0;
    for (const ts::TableSchema* schema : ts::Patchable()) {
        const std::string why = mod_rules::RowsRefused(*schema);
        outfits += why.find("[outfit.<Key>]") != std::string::npos ? 1 : 0;
        open += why.empty() && !ts::Curated(*schema) ? 1 : 0;
    }
    CHECK(outfits == 6);
    std::printf("  %zu generated tables take rows\n", open);
    CHECK(open > 80);
    CHECK(ts::BandStart(*ts::FindPatchable("blacksmith")) == ts::kRowBandStart);
    CHECK(ts::BandStart(ts::kIcon) == ts::kIcon.idBandStart && ts::BandSize(ts::kIcon) == ts::kIcon.idBandSize);
}

// ---- Reading -------------------------------------------------------------

void TestReading() {
    std::printf("Row sections\n");
    TempMods mods("read");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                                  // 1
            "key = Alpha",                            // 2
            "[row.blacksmith.Sword]",                 // 3
            "base = 7",                               // 4
            "srcItemID = Gem",                        // 5  this mod's item
            "dstItemID = 300",                        // 6  vanilla item
            "NeedItem.1.num = 3",                     // 7  any case
            "comment = A sword",                      // 8
            "[row.blacksmith.Sword]",                 // 9  key taken
            "base = 7",                               // 10
            "[row.nosuch.X]",                         // 11
            "base = 1",                               // 12
            "[row.tips.NoBase]",                      // 13
            "name = x",                               // 14
            "[row.blacksmith.BadKey]",                // 15
            "base = 7",                               // 16
            "id = 5",                                 // 17
            "[row.blacksmith.Count]",                 // 18
            "base = 7",                               // 19
            "needItem.count = 3",                     // 20
            "[row.blacksmith.Twice]",                 // 21
            "base = 7",                               // 22
            "needItem.0.num = 1",                     // 23
            "needItem.0.num = 2",                     // 24
            "[row.tips.Hint]",                        // 25
            "base = 1",                               // 26
            "beginFlag = 96000",                      // 27 mod flag band
            "[row.gimmick.Box]",                      // 28 refused
            "base = 1",                               // 29
            "[row.font.Mono]",                        // 30
            "base = 1",                               // 31
            "language.1.shadowColor.count = 3",       // 32 inner first in the file
            "language.count = 2",                     // 33
            "language.1.shadowColor.2 = 7",           // 34
            "language.0.fontName = Mono",             // 35
            "language.1.boldFontID = Bold",           // 36 a row of this mod
            "[row.font.Bold]",                        // 37
            "base = 1",                               // 38
            "language.count = 1",                     // 39
            "language.0.boldFontID = Chain",          // 40 a row skipped later
            "[row.font.Chain]",                       // 41
            "base = 1",                               // 42
            "language.count = 1",                     // 43
            "language.0.boldFontID = Beta.Missing",   // 44 no such row
            "[row.fishing.Rod]",                      // 45
            "base = 2",                               // 46
            "countGameValueID = Beta.Value",          // 47 another mod's row
            "[row.skilltree.Node]",                   // 48
            "base = 5",                               // 49
            "releaseFlag = Flagged",                  // 50 this mod's flag
            "iconID = ICON_ID_SOMETHING",             // 51 enum name
            "gameEffectID = 2100000005",              // 52 a mod id
            "[row.blacksmith.Base]",                  // 53
            "base = 2100000001",                      // 54 base in the band
            "[row.blacksmith]",                       // 55 rules_art's to report
            "base = 1",                               // 56
            "[row.texture.Art]",                      // 57 rules_art's
            "file = a.png",                           // 58
        })
    );
    mods.Write("Beta/canopy.ini", Lines({"[mod]", "key = Beta", "[row.gamevalue.Value]", "base = 1", "minValue = -5"}));

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    mod_rules::Report report;
    std::vector<std::string> accepted;
    std::vector<RowSpec> rows =
        mod_rules::ReadRows(folders, report, [&](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
            accepted.push_back(folder.name + ":" + modKey + ":" + std::to_string(count));
            return true;
        });
    CHECK(accepted.size() == 2 && accepted[0] == "Alpha:Alpha:7" && accepted[1] == "Beta:Beta:1");
    CHECK(rows.size() == 8);

    CHECK(Said(report, 9, "[Alpha] Row blacksmith.Sword skipped, the mod already has a blacksmith row with that key."));
    CHECK(Said(report, 11, "[Alpha] Row nosuch.X skipped, Canopy has no schema for the table nosuch, so it takes no "
                           "rows. ROWS.md lists the tables that take rows."));
    CHECK(Said(report, 13, "[Alpha] Row tips.NoBase skipped, it needs base = <the id of a vanilla tips row to copy>."));
    CHECK(Said(report, 17, "[Alpha] Row blacksmith.BadKey skipped, id"));
    CHECK(Said(report, 20, "[Alpha] Row blacksmith.Count skipped, needitem.count: needItem is not a list."));
    CHECK(Said(report, 24, "[Alpha] Row blacksmith.Twice skipped, needitem.0.num sets needItem.0.num, which line 23 "
                           "already sets."));
    CHECK(Said(report, 28, "[Alpha] Row gimmick.Box skipped, the game's save keeps gimmick ids"));
    CHECK(Said(report, 54, "[Alpha] Row blacksmith.Base skipped, base must be one vanilla id of blacksmith.dat."));
    // The art rules report these.
    CHECK(!Said(report, 55, "") && !Said(report, 57, ""));
    CHECK(report.Count(Severity::Error) == 8 && report.Count(Severity::Warning) == 0);

    // Counts go outer first, whatever the file's order.
    const RowSpec& mono = rows[2];
    CHECK(mono.key == "Mono" && mono.counts.size() == 2 && mono.counts[0].path == "language" &&
          mono.counts[0].elements == 2 && mono.counts[1].path == "language.1.shadowcolor" &&
          mono.counts[1].elements == 3);
    CHECK(mono.values.size() == 3 && mono.values[2].refTable == std::string("font.dat"));

    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Item, "Alpha.Gem");
    refs.Add(RefKind::Flag, "Alpha.Flagged");
    mod_rules::Report resolved;
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), refs, resolved);

    const RowSpec* sword = Row(rows, "blacksmith", "alpha", "SWORD");
    CHECK(sword && sword->id == 2100000001 && sword->baseId == 7 && sword->label == "Mod blacksmith row Alpha.Sword");
    CHECK(sword && mod_rules::RowRegistryKey(*sword) == "blacksmith.Alpha.Sword" &&
          mod_rules::RowEnumName(*sword) == "CANOPY_BLACKSMITH_ALPHA_SWORD");
    const RowValue* gem = sword ? ValueOf(*sword, "srcItemID") : nullptr;
    CHECK(gem && gem->id == 1 && !gem->vanilla);
    const RowValue* vanillaItem = sword ? ValueOf(*sword, "dstItemID") : nullptr;
    CHECK(vanillaItem && vanillaItem->id == 300 && vanillaItem->vanilla);
    CHECK(sword && ValueOf(*sword, "NeedItem.1.num") && !ValueOf(*sword, "NeedItem.1.num")->refTable);

    const RowSpec* bold = Row(rows, "font", "Alpha", "Bold");
    const RowSpec* chain = Row(rows, "font", "Alpha", "Chain");
    const RowSpec* monoRow = Row(rows, "font", "Alpha", "Mono");
    // Chain names a row no mod has, Bold names Chain, Mono names Bold: each
    // is skipped in turn.
    CHECK(!chain && !bold && !monoRow);
    CHECK(Said(resolved, 44, "[Alpha] Row font.Chain skipped, language.0.boldfontid Beta.Missing is not a font row of "
                             "an installed mod."));
    CHECK(Said(resolved, 40, "[Alpha] Row font.Bold skipped, language.0.boldfontid names the font row Alpha.Chain, "
                             "which was skipped."));
    CHECK(Said(resolved, 36, "[Alpha] Row font.Mono skipped, language.1.boldfontid names the font row Alpha.Bold, "
                             "which was skipped."));

    const RowSpec* rod = Row(rows, "fishing", "Alpha", "Rod");
    const RowSpec* value = Row(rows, "gamevalue", "Beta", "Value");
    CHECK(value && value->id == 2100000008);
    CHECK(rod && value && ValueOf(*rod, "countGameValueID")->id == value->id);

    CHECK(!Row(rows, "tips", "Alpha", "Hint"));
    CHECK(Said(resolved, 27, "[Alpha] Row tips.Hint skipped, beginflag 96000 is not a vanilla flag id; name a mod "
                             "flag by its key."));
    CHECK(!Row(rows, "skilltree", "Alpha", "Node"));
    CHECK(Said(resolved, 52, "[Alpha] Row skilltree.Node skipped, gameeffectid 2100000005 is a mod id"));
    CHECK(resolved.Count(Severity::Error) == 5);

    std::vector<const RowSpec*> kept;
    for (const RowSpec& row : rows) {
        if (row.id != 0) {
            kept.push_back(&row);
        }
    }
    CHECK(mod_rules::CountRows(kept) == "1 blacksmith row, 1 fishing row and 1 gamevalue row");
}

void TestEnumRef() {
    std::printf("Row references by enum name\n");
    TempMods mods("enum");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({"[mod]", "key = Alpha", "[row.skilltree.Node]", "base = 5", "releaseFlag = Flagged",
               "iconID = ICON_ID_SOMETHING", "[row.blacksmith.Nameless]", "base = 1", "gimmickID = NOT_AN_ENUM_x"})
    );
    mod_rules::Report report;
    std::vector<RowSpec> rows = mod_rules::ReadRows(mod_rules::ScanMods(mods.Dir()), report, mod_rules::AcceptAll);
    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Flag, "Alpha.Flagged");
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), refs, report);
    const RowSpec* node = Row(rows, "skilltree", "Alpha", "Node");
    CHECK(node && ValueOf(*node, "iconID")->enumName == "ICON_ID_SOMETHING" && ValueOf(*node, "releaseFlag")->id == 1 &&
          ValueOf(*node, "releaseFlag")->flag);
    CHECK(!Row(rows, "blacksmith", "Alpha", "Nameless"));
    CHECK(Said(report, 9, "gimmickid NOT_AN_ENUM_x is not a gimmick row or object of this mod, a Mod.Key, a vanilla "
                          "id or an enum name"));
}

// ---- Building ------------------------------------------------------------

void TestBuilding() {
    std::printf("Row building\n");
    const ts::TableSchema& font = *ts::FindPatchable("font");
    // A vanilla font row with no languages, id 1, and a pool with its NUL.
    std::vector<uint8_t> base(font.rowSize, 0);
    blob::Put<uint64_t>(base.data(), 0, 1);
    std::vector<uint8_t> bytes;
    blob::Build({base}, std::string(1, '\0'), bytes);
    blob::Table table;
    std::string error;
    CHECK(blob::Parse(bytes.data(), bytes.size(), table, error));
    const std::unordered_map<uint64_t, size_t> byId = {{1, 0}};

    RowSpec mod;
    mod.schema = &font;
    mod.baseId = 1;
    mod.counts = {RowValue{"language", "2", 1, true, 2}, RowValue{"language.1.shadowColor", "3", 2, true, 3}};
    RowValue shadow{"language.1.shadowColor.2", "7", 3};
    RowValue name{"language.0.fontName", "Mono", 4};
    RowValue other{"language.1.boldFontID", "Bold", 5};
    other.refTable = "font.dat";
    other.id = 2100000002;
    RowValue named{"language.0.boldFontID", "FONT_ID_X", 6};
    named.refTable = "font.dat";
    named.enumName = "FONT_ID_X";
    mod.values = {shadow, name, other, named};

    const StubTables tables;
    blob::Pool pool(table.poolBytes);
    std::vector<uint8_t> row;
    std::string why;
    CHECK(mod_rules::BuildRow(mod, table, byId, tables, pool, row, why));
    CHECK(row.size() == font.rowSize + 2 * 116 + 3 * 4);
    CHECK(ts::RowFits(font, row.data(), row.size()));
    CHECK(ReadAt(font, row, table, pool, "language.1.shadowColor.2") == "7");
    CHECK(ReadAt(font, row, table, pool, "language.1.shadowColor.0") == "0");
    CHECK(ReadAt(font, row, table, pool, "language.0.fontName") == "Mono");
    CHECK(ReadAt(font, row, table, pool, "language.1.boldFontID") == "2100000002");
    CHECK(ReadAt(font, row, table, pool, "language.0.boldFontID") == "42");
    CHECK(blob::Get<uint64_t>(row.data(), 0) == 1);

    // A vanilla id the table lacks, a base it lacks, an element the row lacks.
    RowSpec missing = mod;
    RowValue vanilla{"language.0.boldFontID", "13", 6};
    vanilla.refTable = "font.dat";
    vanilla.id = 13;
    vanilla.vanilla = true;
    missing.values = {vanilla};
    CHECK(!mod_rules::BuildRow(missing, table, byId, tables, pool, row, why) &&
          why == "language.0.boldFontID: 13 is not in font.dat");
    RowSpec noBase = mod;
    noBase.baseId = 9;
    CHECK(!mod_rules::BuildRow(noBase, table, byId, tables, pool, row, why) && why == "its base 9 is not in font.dat");
    RowSpec past = mod;
    past.counts.clear();
    past.values = {shadow};
    CHECK(!mod_rules::BuildRow(past, table, byId, tables, pool, row, why) &&
          why.rfind("language.1.shadowColor.2: ", 0) == 0);
}

// ---- List capacity -------------------------------------------------------

void TestCapacity() {
    std::printf("List capacity\n");
    const ts::TableSchema& offering = *ts::FindPatchable("offeringitem");
    const ts::FieldSpec* items = ts::VectorField(offering);
    // k_vector<ItemInfo, 16> in the record; fishing's captureInfo is a
    // std::vector; the skin entries are curated.
    CHECK(items && items->capacity == 16 && ts::MaxElements(*items) == 16);
    const ts::FieldSpec* catches = ts::VectorField(*ts::FindPatchable("fishing"));
    CHECK(catches && catches->capacity == 0 && ts::MaxElements(*catches) == ts::kMaxPathIndex + 1);
    CHECK(ts::MaxElements(*ts::VectorField(ts::kAnimeReplaceTexture)) == 32);

    std::string error;
    const ts::FieldSpec* found = nullptr;
    CHECK(ts::VectorPath(offering, "ITEMINFO", error, &found) && found == items);
    std::vector<ts::Location> places;
    CHECK(ts::ResolvePath(offering, "itemInfo.15.count", places, error));
    CHECK(!ts::ResolvePath(offering, "itemInfo.16.count", places, error) &&
          error == "itemInfo has no element 16, it holds 16 at most");
    CHECK(!ts::VectorPath(*ts::FindPatchable("font"), "language.7.shadowColor", error) &&
          error == "language has no element 7");

    std::vector<uint8_t> row(offering.rowSize, 0);
    blob::Put<uint64_t>(row.data(), 0, 1);
    blob::Pool pool(1);
    CHECK(ts::ResizeVector(offering, row, "itemInfo", 16, pool, error));
    CHECK(row.size() == offering.rowSize + 16 * items->stride);
    CHECK(!ts::ResizeVector(offering, row, "itemInfo", 17, pool, error) &&
          error == "itemInfo can have 16 elements at most");
    CHECK(row.size() == offering.rowSize + 16 * items->stride);

    TempMods mods("capacity");
    mods.Write("Alpha/canopy.ini", Lines({"[mod]", "key = Alpha", "[row.offeringitem.Big]", "base = 1",
                                          "itemInfo.count = 17", "[row.offeringitem.Full]", "base = 1",
                                          "itemInfo.count = 16", "itemInfo.15.count = 2"}));
    mod_rules::Report report;
    const std::vector<RowSpec> rows =
        mod_rules::ReadRows(mod_rules::ScanMods(mods.Dir()), report, mod_rules::AcceptAll);
    CHECK(rows.size() == 1 && rows[0].key == "Full");
    // itemInfo.15.count is the element's field named count, not a list.
    CHECK(rows.size() == 1 && rows[0].counts.size() == 1 && rows[0].counts[0].elements == 16 &&
          rows[0].values.size() == 1 && SamePath(rows[0].values[0].path, "itemInfo.15.count"));
    CHECK(Said(report, 5, "[Alpha] Row offeringitem.Big skipped, iteminfo.count must be a count from 0 to 16."));
    CHECK(report.Count(Severity::Error) == 1);
}

// ---- The real game, read only --------------------------------------------

void TestRealGame() {
    std::printf("Rows on the game's tables\n");
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
    // No vanilla row of a generated table, or of its twin, is in the band.
    size_t tables = 0;
    size_t inBand = 0;
    for (const ts::TableSchema* schema : ts::Patchable()) {
        if (ts::Curated(*schema)) {
            continue;
        }
        for (const std::string& file : {std::string(schema->file), "framework_" + std::string(schema->file)}) {
            const blob::Table* table = vanilla.Table(file);
            tables += table ? 1 : 0;
            for (size_t index = 0; table && index < table->rows.size(); ++index) {
                const uint64_t id = blob::RowId(table->rows[index]);
                inBand += id >= ts::kRowBandStart && id < ts::kRowBandStart + ts::kRowBandSize ? 1 : 0;
            }
        }
    }
    std::printf("  %zu tables, %zu rows in the row band\n", tables, inBand);
    CHECK(tables > 100 && inBand == 0);

    // Every list of every vanilla row, of every table, fits its record.
    size_t lists = 0;
    size_t over = 0;
    for (const ts::TableSchema* schema : ts::Patchable()) {
        for (const std::string& file : {std::string(schema->file), "framework_" + std::string(schema->file)}) {
            const blob::Table* table = vanilla.Table(file);
            for (size_t index = 0; table && index < table->rows.size(); ++index) {
                const blob::Row& row = table->rows[index];
                std::vector<ts::Location> layout;
                if (!ts::Layout(*schema, row.data, row.size, layout, error)) {
                    continue;
                }
                for (const ts::Location& place : layout) {
                    if (place.field && place.field->type == ts::FieldType::Vector) {
                        ++lists;
                        if (ts::ReadNumber(row.data, place) > ts::MaxElements(*place.field)) {
                            ++over;
                            std::printf("  %s row %zu: %s is over its capacity\n", file.c_str(), index,
                                        place.path.c_str());
                        }
                    }
                }
            }
        }
    }
    std::printf("  %zu lists in vanilla rows, %zu over capacity\n", lists, over);
    CHECK(lists > 1000 && over == 0);

    const blob::Table* smithy = vanilla.Table("blacksmith.dat");
    const blob::Table* items = vanilla.Table("item.dat");
    const blob::Table* icons = vanilla.Table("icon.dat");
    const blob::Table* skills = vanilla.Table("skilltree.dat");
    const blob::Table* treasure = vanilla.Table("treasurebox.dat");
    if (!CHECK(smithy && items && icons && skills && treasure && !smithy->rows.empty() && !skills->rows.empty())) {
        return;
    }
    // A plain treasure row: one with unlock (+32) 0, which the common pool draws by weight (+24).
    std::string treasureId;
    for (const blob::Row& row : treasure->rows) {
        int32_t unlock = 0;
        if (row.size >= 36) {
            std::memcpy(&unlock, row.data + 32, sizeof(unlock));
        }
        if (unlock == 0 && treasureId.empty()) {
            treasureId = std::to_string(blob::RowId(row));
        }
    }
    CHECK(!treasureId.empty());
    const std::string smithId = std::to_string(blob::RowId(smithy->rows[0]));
    const std::string itemId = std::to_string(blob::RowId(items->rows[0]));
    const std::string skillId = std::to_string(blob::RowId(skills->rows[0]));
    std::string iconEnum;
    CHECK(blob::ReadText(*icons, icons->rows[0], 8, iconEnum) && !iconEnum.empty());

    TempMods mods("game");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                          // 1
            "key = Alpha",                    // 2
            "[row.blacksmith.Sword]",         // 3
            "base = " + smithId,              // 4
            "dstItemID = " + itemId,          // 5
            "needItem.0.num = 2",             // 6
            "[row.blacksmith.Lost]",          // 7
            "base = 999999999",               // 8
            "[row.blacksmith.Ghost]",         // 9
            "base = " + smithId,              // 10
            "srcItemID = 1499999999",         // 11
            "[row.skilltree.Node]",           // 12
            "base = " + skillId,              // 13
            "iconID = " + iconEnum,           // 14
            "showFlag.count = 2",             // 15
            "showFlag.1 = 100",               // 16
            "name.en = Mod node",             // 17
            "[row.skilltree.BadIcon]",        // 18
            "base = " + skillId,              // 19
            "iconID = ICON_ID_CANOPY_NOT_A",  // 20 a mod icon's name stands in
            "[row.skilltree.NoIcon]",         // 21
            "base = " + skillId,              // 22
            "iconID = ICON_ID_NO_SUCH_ICON",  // 23
            "[row.treasurebox.Gem]",          // 24
            "base = " + treasureId,           // 25
            "itemID = " + itemId,             // 26
            "weight = 3",                     // 27
            "unlock = 0",                     // 28
        })
    );
    mod_rules::Context context;
    context.modsDir = mods.Dir();
    context.folders = mod_rules::ScanMods(mods.Dir());
    context.vanilla = &vanilla;
    mod_rules::CheckRows(context);
    const mod_rules::Report& report = context.report;
    CHECK(Said(report, 7, "Mod blacksmith row Alpha.Lost skipped, its base 999999999 is not in blacksmith.dat."));
    CHECK(Said(report, 9, "Mod blacksmith row Alpha.Ghost skipped, srcitemid: it names item 1499999999, which is not "
                          "in item.dat."));
    CHECK(Said(report, 21, "Mod skilltree row Alpha.NoIcon skipped, iconid: icon.dat has no ICON_ID_NO_SUCH_ICON."));
    CHECK(report.Count(Severity::Error) == 3);
    for (const Diagnostic& item : report.Items()) {
        if (item.line != 7 && item.line != 9 && item.line != 21) {
            std::printf("  unexpected at line %d\n", item.line);
        }
    }
}

}  // namespace

void TestRows(int& checks, int& failures) {
    TestRefused();
    TestReading();
    TestEnumRef();
    TestBuilding();
    TestCapacity();
    TestRealGame();
    checks += g_checks;
    failures += g_failures;
}
