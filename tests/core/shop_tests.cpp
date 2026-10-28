#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "rules_items.h"
#include "rules_rows.h"
#include "rules_shops.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the [sale.<Key>] and [upgrade.<Key>] rules: reading sections and
// every error, the keys they share with [row.] sections, the item buy price,
// and building rows against the game's own tables when it is installed. Mods
// are written to a temporary folder; the game is read, never written. Only
// ids, counts and sizes are printed, never game text.
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
        std::printf("  FAILED shop_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

class TempMods {
public:
    explicit TempMods(const char* name) {
        root_ = fs::temp_directory_path() /
                ("canopy_shop_tests_" + std::to_string(test_support::ProcessId()) + "_" + name);
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

const RowSpec* Find(const std::vector<RowSpec>& rows, const char* key) {
    for (const RowSpec& row : rows) {
        if (row.key == key) {
            return &row;
        }
    }
    return nullptr;
}

// The text of the value at path, "<none>" when the row has none.
std::string Text(const RowSpec& row, const std::string& path) {
    for (const RowValue& value : row.values) {
        if (value.path == path) {
            return value.value;
        }
    }
    return "<none>";
}

const RowValue* Value(const RowSpec& row, const std::string& path) {
    for (const RowValue& value : row.values) {
        if (value.path == path) {
            return &value;
        }
    }
    return nullptr;
}

// The new count of the list at path, -1 when the row sets none.
int64_t CountOf(const RowSpec& row, const std::string& path) {
    for (const RowValue& count : row.counts) {
        if (count.path == path) {
            return count.elements;
        }
    }
    return -1;
}

// Builds rows of the game as the desktop check does, with no enum names.
class NoEnums : public mod_rules::RowTables {
public:
    bool EnumId(const char*, const std::string& name, uint64_t&, std::string& why) const override {
        why = "no enum " + name;
        return false;
    }
    bool VanillaFine(const char*, uint64_t, std::string&) const override {
        return true;
    }
};

// The number at path of a built row, -999999 when it has no such place.
int64_t Number(const ts::TableSchema& schema, const uint8_t* data, size_t size, const std::string& path) {
    std::vector<ts::Location> layout;
    ts::Location location;
    std::string error;
    if (!ts::Layout(schema, data, size, layout, error) || !ts::Locate(layout, path, location, error)) {
        return -999999;
    }
    const uint64_t raw = ts::ReadNumber(data, location);
    return location.size == 4 ? static_cast<int64_t>(static_cast<int32_t>(raw)) : static_cast<int64_t>(raw);
}

// The element count of the list at path of a built row, -1 for none.
int64_t Elements(const ts::TableSchema& schema, const uint8_t* data, size_t size, const std::string& path) {
    uint32_t count = 0;
    std::string error;
    return ts::ElementCount(schema, data, size, path, count, error) ? static_cast<int64_t>(count) : -1;
}

// The sum of every byte under randomInfo, which a sale clears.
uint64_t RandomBytes(const ts::TableSchema& schema, const uint8_t* data, size_t size) {
    std::vector<ts::Location> layout;
    std::string error;
    uint64_t sum = 0;
    if (!ts::Layout(schema, data, size, layout, error)) {
        return 0;
    }
    for (const ts::Location& place : layout) {
        if (place.path.rfind("randomInfo", 0) == 0) {
            for (size_t index = 0; index < place.size && place.offset + index < size; ++index) {
                sum += data[place.offset + index];
            }
        }
    }
    return sum;
}

// ---- Reading -------------------------------------------------------------

void TestReading() {
    std::printf("Sale and upgrade sections\n");
    TempMods mods("read");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                                     // 1
            "key = Alpha",                               // 2
            "[sale.Stew]",                               // 3
            "item = Stew",                               // 4  this mod's item
            "shop = 1",                                  // 5
            "stock = 5",                                 // 6
            "restock = never",                           // 7
            "seasons = autumn, spring, autumn",          // 8  kept in order, once each
            "flag = Ready, 100",                         // 9
            "unless = 200",                              // 10
            "sets = Bought",                             // 11 ignored
            "discount = 20",                             // 12
            "priority = 90",                             // 13
            "silhouette = on",                           // 14
            "color = red",                               // 15 not a sale key
            "[sale.Plain]",                              // 16
            "item = 300",                                // 17 vanilla item
            "shop = 0",                                  // 18
            "restock = never",                           // 19 unlimited stock
            "seasons = spring, summer, autumn, winter",  // 20 every season
            "[sale.NoItem]",                             // 21
            "shop = 0",                                  // 22
            "[sale.NoShop]",                             // 23
            "item = 300",                                // 24
            "[sale.BadStock]",                           // 25
            "item = 300",                                // 26
            "shop = 0",                                  // 27
            "stock = 0",                                 // 28
            "[sale.BadSeason]",                          // 29
            "item = 300",                                // 30
            "shop = 0",                                  // 31
            "seasons = monsoon",                         // 32
            "[sale.ManyFlags]",                          // 33
            "item = 300",                                // 34
            "shop = 0",                                  // 35
            "unless = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17",  // 36
            "[upgrade.Axe]",                             // 37
            "from = 1000",                               // 38
            "to = ITEM_ID_AXE_2",                        // 39 enum name
            "needs = Gem * 3, 220100",                   // 40
            "time = 20",                                 // 41
            "[upgrade.Same]",                            // 42
            "from = 1000",                               // 43
            "to = 1000",                                 // 44
            "[upgrade.ModTool]",                         // 45
            "from = Gem",                                // 46
            "to = 1001",                                 // 47
            "needs = 1, 2",                              // 48
            "[upgrade.OneNeed]",                         // 49
            "from = 1000",                               // 50
            "to = 1001",                                 // 51
            "needs = 220100",                            // 52
            "[upgrade.BadCount]",                        // 53
            "from = 1000",                               // 54
            "to = 1001",                                 // 55
            "needs = 1 * 0, 2",                          // 56
            "[row.blacksmith.Axe]",                      // 57 shares the upgrade's key
            "base = 7",                                  // 58
            "[sale.Bad Key]",                            // 59
            "item = 1",                                  // 60
            "[sale.Stew]",                               // 61 key taken
            "item = 1",                                  // 62
            "shop = 0",                                  // 63
            "[sale.]",                                   // 64 no key
        })
    );

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    mod_rules::Report report;
    std::vector<RowSpec> rows = mod_rules::ReadRows(folders, report, mod_rules::AcceptAll);
    CHECK(rows.size() == 3);

    const auto error = [&](int line, const std::string& text) {
        const Diagnostic* said = Said(report, line, text);
        return said && said->severity == Severity::Error;
    };
    CHECK(error(21, "[Alpha] Sale NoItem skipped, it needs item = <a mod item's key, or a vanilla item id or enum "
                    "name>."));
    CHECK(error(23, "[Alpha] Sale NoShop skipped, it needs shop = <the storeID of a vanilla shop, such as 0>."));
    CHECK(error(28, "[Alpha] Sale BadStock skipped, stock must be a whole number from 1 to 99, or unlimited."));
    CHECK(error(32, "[Alpha] Sale BadSeason skipped, seasons must be spring, summer, autumn and winter, or all, not "
                    "monsoon."));
    CHECK(error(36, "[Alpha] Sale ManyFlags skipped, unless names 17 flags, and a sale takes 16 at most."));
    CHECK(error(44, "[Alpha] Upgrade Same skipped, to names the tool from names; an upgrade makes another tool."));
    CHECK(error(46, "[Alpha] Upgrade ModTool skipped, from Gem is not a vanilla item id or enum name; from takes no "
                    "mod item."));
    CHECK(error(52, "[Alpha] Upgrade OneNeed skipped, it needs needs = <two materials"));
    CHECK(error(56, "[Alpha] Upgrade BadCount skipped, needs 1 * 0 must be item or item * count, with a count of 1 "
                    "to 99."));
    CHECK(error(57, "[Alpha] Row blacksmith.Axe skipped, the mod already has an upgrade Axe, and both are blacksmith "
                    "rows, which share their keys."));
    CHECK(error(59, "[Alpha] Sale Bad Key skipped, its key must be 1 to 32 letters"));
    CHECK(error(61, "[Alpha] Sale Stew skipped, the mod already has a sale with that key."));
    CHECK(error(64, "skipped, its key must be 1 to 32 letters"));
    const Diagnostic* color = Said(report, 15, "[Alpha] Sale Stew: color is not a sale key, so it is ignored.");
    CHECK(color && color->severity == Severity::Warning);
    const Diagnostic* restock = Said(report, 19, "[Alpha] Sale Plain: restock is ignored, the stock is unlimited.");
    CHECK(restock && restock->severity == Severity::Warning);
    const Diagnostic* sets = Said(report, 11, "[Alpha] Sale Stew: sets is ignored, buying a mod sale turns no flag on. "
                                              "Use unless to stop listing it once a flag is on.");
    CHECK(sets && sets->severity == Severity::Warning);
    CHECK(report.Count(Severity::Error) == 13 && report.Count(Severity::Warning) == 3);
    for (const Diagnostic& item : report.Items()) {
        if (item.severity == Severity::Error && !Said(report, item.line, "skipped")) {
            std::printf("  unexpected at line %d\n", item.line);
        }
    }

    const RowSpec* stew = Find(rows, "Stew");
    if (CHECK(stew)) {
        CHECK(stew->kind == std::string(mod_rules::kSaleKind) && stew->schema == ts::FindPatchable("storesales"));
        CHECK(stew->where == "[Alpha] Sale Stew" && stew->label == "Mod sale Alpha.Stew");
        CHECK(stew->basePath == "storeID" && stew->baseValue.value == "1" && stew->baseId == 0);
        CHECK(stew->zero.size() == 1 && stew->zero[0] == "randomInfo");
        CHECK(Text(*stew, "itemID") == "Stew" && Text(*stew, "storeID") == "1");
        CHECK(Text(*stew, "stockInfo.stock") == "5" && Text(*stew, "stockInfo.restock") == "0");
        CHECK(CountOf(*stew, "condSeason") == 2 && Text(*stew, "condSeason.0") == "2" &&
              Text(*stew, "condSeason.1") == "0");
        CHECK(CountOf(*stew, "condOnFlag") == 2 && Text(*stew, "condOnFlag.0") == "Ready" &&
              Text(*stew, "condOnFlag.1") == "100");
        CHECK(CountOf(*stew, "condOffFlag") == 1 && CountOf(*stew, "releaseValue") == 0);
        CHECK(Text(*stew, "discountRate") == "20" && Text(*stew, "discountPrice") == "0");
        CHECK(Text(*stew, "priority") == "90" && Text(*stew, "showSilhouette") == "1" && Text(*stew, "size") == "0");
        CHECK(!Value(*stew, "releaseValue.0"));
    }
    const RowSpec* plain = Find(rows, "Plain");
    if (CHECK(plain)) {
        // Unlimited never restocks; every season is no condition at all.
        CHECK(Text(*plain, "stockInfo.stock") == "0" && Text(*plain, "stockInfo.restock") == "0");
        CHECK(CountOf(*plain, "condSeason") == 0 && CountOf(*plain, "condOnFlag") == 0);
        CHECK(Text(*plain, "priority") == "50" && Text(*plain, "showSilhouette") == "0");
    }
    const RowSpec* axe = Find(rows, "Axe");
    if (CHECK(axe)) {
        CHECK(axe->kind == std::string(mod_rules::kUpgradeKind) && axe->schema == ts::FindPatchable("blacksmith"));
        CHECK(axe->basePath == "dstItemID" && axe->baseValue.value == "ITEM_ID_AXE_2" &&
              axe->baseMissing == "no vanilla upgrade makes ITEM_ID_AXE_2, so the blacksmith has no box to hand it "
                                  "over in");
        CHECK(Text(*axe, "srcItemID") == "1000" && Text(*axe, "dstItemID") == "ITEM_ID_AXE_2");
        CHECK(Text(*axe, "needItem.0.itemID") == "Gem" && Text(*axe, "needItem.0.num") == "3");
        CHECK(Text(*axe, "needItem.1.itemID") == "220100" && Text(*axe, "needItem.1.num") == "1");
        CHECK(Text(*axe, "createTime") == "20");
    }

    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Item, "Alpha.Stew");
    refs.Add(RefKind::Item, "Alpha.Gem");
    refs.Add(RefKind::Flag, "Alpha.Ready");
    mod_rules::Report resolved;
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), refs, resolved);
    CHECK(resolved.Count(Severity::Error) == 0);
    for (const Diagnostic& item : resolved.Items()) {
        std::printf("  resolve note at line %d\n", item.line);
    }
    std::vector<const RowSpec*> kept;
    for (const RowSpec& row : rows) {
        if (row.id != 0) {
            kept.push_back(&row);
        }
    }
    CHECK(kept.size() == 3);
    CHECK(mod_rules::CountRows(kept) == "2 sales and 1 upgrade");
    if (stew) {
        CHECK(mod_rules::RowRegistryKey(*stew) == "storesales.Alpha.Stew");
        const RowValue* item = Value(*stew, "itemID");
        CHECK(item && item->id == 1 && !item->vanilla);
        const RowValue* vanillaFlag = Value(*stew, "condOnFlag.1");
        CHECK(vanillaFlag && vanillaFlag->id == 100 && vanillaFlag->vanilla);
    }
    if (axe) {
        CHECK(axe->baseValue.enumName == "ITEM_ID_AXE_2");
        const RowValue* gem = Value(*axe, "needItem.0.itemID");
        CHECK(gem && gem->id == 2 && !gem->vanilla);
    }
}

// A sale naming an item no mod has is skipped at resolve, by its key.
void TestMissingItem() {
    std::printf("Sale references\n");
    TempMods mods("refs");
    mods.Write("Alpha/canopy.ini",
               Lines({"[mod]", "key = Alpha", "[sale.Lost]", "item = Nothing", "shop = 0", "flag = NoFlag"}));
    mod_rules::Report report;
    std::vector<RowSpec> rows = mod_rules::ReadRows(mod_rules::ScanMods(mods.Dir()), report, mod_rules::AcceptAll);
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), mod_rules::KeyRefs(), report);
    CHECK(rows.size() == 1 && rows[0].id == 0);
    const Diagnostic* said = Said(report, 4, "[Alpha] Sale Lost skipped, item Nothing");
    CHECK(said && said->severity == Severity::Error);
}

// ---- Item buy price ------------------------------------------------------

void TestBuyPrice() {
    std::printf("Item buy price\n");
    TempMods mods("buy");
    mods.Write("Alpha/canopy.ini", Lines({
                                       "[mod]",           // 1
                                       "key = Alpha",     // 2
                                       "[item.Cheap]",    // 3
                                       "name = Cheap",    // 4
                                       "icon = 77",       // 5
                                       "buy = 250",       // 6
                                       "[item.Odd]",      // 7
                                       "name = Odd",      // 8
                                       "icon = 77",       // 9
                                       "buy = -4",        // 10
                                       "sell = 9",        // 11
                                       "[item.Free]",     // 12
                                       "name = Free",     // 13
                                       "icon = 77",       // 14
                                   }));
    mod_rules::Report report;
    const std::vector<mod_rules::ItemSpec> items = mod_rules::ReadItems(
        mod_rules::ScanMods(mods.Dir()), mods.Dir(), mod_rules::KeyRefs(), report, mod_rules::AcceptAll
    );
    CHECK(items.size() == 3);
    if (items.size() == 3) {
        CHECK(items[0].buy == 250 && items[1].buy == -1 && items[2].buy == -1);
    }
    const Diagnostic* bad = Said(report, 10, "[Alpha] Item Odd: buy must be a whole number from 0 to 999999, so the "
                                             "item is free.");
    CHECK(bad && bad->severity == Severity::Warning);
    CHECK(Said(report, 11, "[Alpha] Item Odd: sell is ignored, mod items cannot be sold"));
    CHECK(report.Count(Severity::Error) == 0 && report.Count(Severity::Warning) == 2);

    if (items.size() == 3) {
        mod_rules::ItemRowParts parts;
        parts.id = 1500000001;
        parts.icon = 77;
        const mod_rules::TextWriter write = [](std::vector<uint8_t>&, size_t, const std::string&) {};
        const std::vector<uint8_t> cheap = mod_rules::BuildItemRow(items[0], parts, write);
        const std::vector<uint8_t> free = mod_rules::BuildItemRow(items[2], parts, write);
        CHECK(cheap.size() > 340 && blob::Get<int32_t>(cheap.data(), 332) == 250);
        CHECK(free.size() > 340 && blob::Get<int32_t>(free.data(), 332) == 0);
    }
}

// ---- The game's tables ---------------------------------------------------

void TestRealGame() {
    std::printf("Sales and upgrades on the game's tables\n");
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
    const ts::TableSchema& sales = *ts::FindPatchable("storesales");
    const ts::TableSchema& smith = *ts::FindPatchable("blacksmith");
    const blob::Table* salesTable = vanilla.Table("storesales.dat");
    const blob::Table* smithTable = vanilla.Table("blacksmith.dat");
    const blob::Table* items = vanilla.Table("item.dat");
    if (!CHECK(salesTable && smithTable && items && !smithTable->rows.empty() && !items->rows.empty())) {
        return;
    }
    // Every vanilla upgrade fills both of its material slots, which [upgrade.]
    // requires.
    size_t twoNeeds = 0;
    std::set<uint64_t> made;
    for (const blob::Row& row : smithTable->rows) {
        twoNeeds += (Number(smith, row.data, row.size, "needItem.0.itemID") > 0 &&
                             Number(smith, row.data, row.size, "needItem.1.itemID") > 0) ? 1 : 0;
        made.insert(static_cast<uint64_t>(Number(smith, row.data, row.size, "dstItemID")));
    }
    std::printf("  %zu upgrades, %zu with two materials\n", smithTable->rows.size(), twoNeeds);
    CHECK(twoNeeds == smithTable->rows.size());

    // A shop whose first row is a random one, so clearing randomInfo shows.
    int64_t randomShop = -1;
    std::set<int64_t> shops;
    for (const blob::Row& row : salesTable->rows) {
        const int64_t shop = Number(sales, row.data, row.size, "storeID");
        if (shops.insert(shop).second && RandomBytes(sales, row.data, row.size) != 0 && randomShop < 0) {
            randomShop = shop;
        }
    }
    std::printf("  %zu shops, first row random in shop %lld\n", shops.size(), static_cast<long long>(randomShop));
    const int64_t shop = randomShop >= 0 ? randomShop : *shops.begin();
    // And one whose first row has release flags, so clearing them shows.
    int64_t releaseShop = -1;
    shops.clear();
    for (const blob::Row& row : salesTable->rows) {
        const int64_t other = Number(sales, row.data, row.size, "storeID");
        if (shops.insert(other).second && Elements(sales, row.data, row.size, "releaseValue") > 0 && releaseShop < 0) {
            releaseShop = other;
        }
    }
    std::printf("  first row has release flags in shop %lld\n", static_cast<long long>(releaseShop));
    CHECK(releaseShop >= 0);

    const blob::Row& firstSmith = smithTable->rows[0];
    const std::string from = std::to_string(Number(smith, firstSmith.data, firstSmith.size, "srcItemID"));
    const std::string to = std::to_string(Number(smith, firstSmith.data, firstSmith.size, "dstItemID"));
    const std::string item = std::to_string(blob::RowId(items->rows[0]));
    std::string unmade;
    for (const blob::Row& row : items->rows) {
        if (!made.count(blob::RowId(row))) {
            unmade = std::to_string(blob::RowId(row));
            break;
        }
    }

    TempMods mods("game");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                                // 1
            "key = Alpha",                          // 2
            "[sale.Good]",                          // 3
            "item = " + item,                       // 4
            "shop = " + std::to_string(shop),       // 5
            "stock = 3",                            // 6
            "seasons = winter",                     // 7
            "priority = 77",                        // 8
            "[sale.NoShop]",                        // 9
            "item = " + item,                       // 10
            "shop = 50",                            // 11
            "[upgrade.Better]",                     // 12
            "from = " + from,                       // 13
            "to = " + to,                           // 14
            "needs = " + item + " * 2, " + item,    // 15
            "[upgrade.NoBox]",                      // 16
            "from = " + from,                       // 17
            "to = " + unmade,                       // 18
            "needs = " + item + ", " + item,        // 19
            "[sale.Owned]",                         // 20
            "item = " + item,                       // 21
            "shop = " + std::to_string(releaseShop),  // 22
        })
    );
    mod_rules::Context context;
    context.modsDir = mods.Dir();
    context.folders = mod_rules::ScanMods(mods.Dir());
    context.vanilla = &vanilla;
    mod_rules::CheckRows(context);
    const mod_rules::Report& report = context.report;
    CHECK(Said(report, 9, "Mod sale Alpha.NoShop skipped, no vanilla sale is in shop 50, so the game has no such "
                          "shop."));
    CHECK(Said(report, 16, "Mod upgrade Alpha.NoBox skipped, no vanilla upgrade makes " + unmade +
                               ", so the blacksmith has no box to hand it over in."));
    CHECK(report.Count(Severity::Error) == 2);
    for (const Diagnostic& diagnostic : report.Items()) {
        if (diagnostic.line != 9 && diagnostic.line != 16) {
            std::printf("  unexpected at line %d\n", diagnostic.line);
        }
    }

    // Build the two good rows and read them back.
    mod_rules::Report ignored;
    std::vector<RowSpec> rows = mod_rules::ReadRows(context.folders, ignored, mod_rules::AcceptAll);
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), mod_rules::KeyRefs(), ignored);
    const NoEnums tables;
    const RowSpec* good = Find(rows, "Good");
    const RowSpec* better = Find(rows, "Better");
    const RowSpec* owned = Find(rows, "Owned");
    if (!CHECK(good && better && owned && good->id != 0 && better->id != 0 && owned->id != 0)) {
        return;
    }
    std::unordered_map<uint64_t, size_t> byId;
    for (size_t index = 0; index < salesTable->rows.size(); ++index) {
        byId.emplace(blob::RowId(salesTable->rows[index]), index);
    }
    blob::Pool pool(salesTable->poolBytes);
    std::vector<uint8_t> row;
    std::string why;
    if (CHECK(mod_rules::BuildRow(*good, *salesTable, byId, tables, pool, row, why))) {
        const uint8_t* data = row.data();
        CHECK(Number(sales, data, row.size(), "storeID") == shop);
        CHECK(std::to_string(Number(sales, data, row.size(), "itemID")) == item);
        CHECK(Number(sales, data, row.size(), "stockInfo.stock") == 3);
        CHECK(Number(sales, data, row.size(), "stockInfo.restock") == 1);
        CHECK(Elements(sales, data, row.size(), "condSeason") == 1);
        CHECK(Number(sales, data, row.size(), "condSeason.0") == 3);
        CHECK(Elements(sales, data, row.size(), "condOnFlag") == 0);
        CHECK(Number(sales, data, row.size(), "priority") == 77);
        CHECK(Number(sales, data, row.size(), "discountRate") == 0);
        CHECK(Number(sales, data, row.size(), "discountPrice") == 0);
        CHECK(RandomBytes(sales, data, row.size()) == 0);
        CHECK(Elements(sales, data, row.size(), "releaseValue") == 0);
        CHECK(ts::RowFits(sales, data, row.size()));
    } else {
        std::printf("  why: %s\n", why.c_str());
    }
    // The base row's release flags are not copied.
    if (CHECK(mod_rules::BuildRow(*owned, *salesTable, byId, tables, pool, row, why))) {
        CHECK(Number(sales, row.data(), row.size(), "storeID") == releaseShop);
        CHECK(Elements(sales, row.data(), row.size(), "releaseValue") == 0);
        CHECK(ts::RowFits(sales, row.data(), row.size()));
    } else {
        std::printf("  why: %s\n", why.c_str());
    }

    byId.clear();
    for (size_t index = 0; index < smithTable->rows.size(); ++index) {
        byId.emplace(blob::RowId(smithTable->rows[index]), index);
    }
    blob::Pool smithPool(smithTable->poolBytes);
    if (CHECK(mod_rules::BuildRow(*better, *smithTable, byId, tables, smithPool, row, why))) {
        const uint8_t* data = row.data();
        CHECK(std::to_string(Number(smith, data, row.size(), "srcItemID")) == from);
        CHECK(std::to_string(Number(smith, data, row.size(), "dstItemID")) == to);
        // The box comes with the vanilla row that makes the same tool.
        CHECK(Number(smith, data, row.size(), "gimmickID") ==
              Number(smith, firstSmith.data, firstSmith.size, "gimmickID"));
        CHECK(Number(smith, data, row.size(), "needItem.0.num") == 2);
        CHECK(Number(smith, data, row.size(), "needItem.1.num") == 1);
        CHECK(Number(smith, data, row.size(), "createTime") == 10);
    } else {
        std::printf("  why: %s\n", why.c_str());
    }
}

}  // namespace

void TestShops(int& checks, int& failures) {
    g_checks = 0;
    g_failures = 0;
    TestReading();
    TestMissingItem();
    TestBuyPrice();
    TestRealGame();
    checks += g_checks;
    failures += g_failures;
}
