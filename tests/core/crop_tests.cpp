#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "rules_crops.h"
#include "rules_items.h"
#include "rules_rows.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the [crop.<Key>] rules: reading sections and every refusal, the
// two rows sharing one id and dropping together, seeds, the seed item row,
// and building both rows against the game's own tables when it is installed.
// Mods are written to a temporary folder; the game is read, never written.
// Only ids, counts and sizes are printed, never game text.
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
        std::printf("  FAILED crop_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

class TempMods {
public:
    explicit TempMods(const char* name) {
        root_ = fs::temp_directory_path() /
                ("canopy_crop_tests_" + std::to_string(test_support::ProcessId()) + "_" + name);
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

const RowSpec* Find(const std::vector<RowSpec>& rows, const char* key, const char* kind) {
    for (const RowSpec& row : rows) {
        if (row.key == key && row.kind && std::string(row.kind) == kind) {
            return &row;
        }
    }
    return nullptr;
}

const RowValue* Value(const RowSpec& row, const std::string& path) {
    for (const RowValue& value : row.values) {
        if (value.path == path) {
            return &value;
        }
    }
    return nullptr;
}

const RowValue* Input(const RowSpec& row, const std::string& name, size_t skip = 0) {
    for (const RowValue& value : row.inputs) {
        if (value.name == name && skip-- == 0) {
            return &value;
        }
    }
    return nullptr;
}

std::string InputText(const RowSpec& row, const std::string& name, size_t skip = 0) {
    const RowValue* value = Input(row, name, skip);
    return value ? value->value : "<none>";
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

// The number at path of a row, -999999 when it has no such place.
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

// The element count of the list at path of a row, -1 for none.
int64_t Elements(const ts::TableSchema& schema, const uint8_t* data, size_t size, const std::string& path) {
    uint32_t count = 0;
    std::string error;
    return ts::ElementCount(schema, data, size, path, count, error) ? static_cast<int64_t>(count) : -1;
}

// ---- Reading -------------------------------------------------------------

void TestReading() {
    std::printf("Crop sections\n");
    TempMods mods("read");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                                                    // 1
            "key = Alpha",                                              // 2
            "[crop.Berry]",                                             // 3
            "base = 5",                                                 // 4
            "name = Moon",                                              // 5
            "seed = BerrySeed",                                         // 6
            "seasons = winter, spring, winter",                         // 7
            "skins = Leaf, 1234",                                       // 8
            "harvest = Berry * 2-4",                                    // 9
            "swap = 300 -> Pip, ITEM_ID_X -> 400",                      // 10
            "crops.form.0.growInfo.0.needGrowth = 3",                   // 11
            "cropsharvest.form.0.growInfo.0.harvest.0.dropRate = 50",   // 12
            "mystery = 1",                                              // 13 warned
            "[crop.NoBase]",                                            // 14
            "name = X",                                                 // 15
            "[crop.BandBase]",                                          // 16
            "base = 2100000005",                                        // 17
            "name = X",                                                 // 18
            "[crop.Twin]",                                              // 19
            "base = 5",                                                 // 20
            "name = Y",                                                 // 21
            "seed = Alpha.BerrySeed",                                   // 22 the same seed
            "[crop.Grow]",                                              // 23
            "base = 5",                                                 // 24
            "name = Z",                                                 // 25
            "crops.form.0.growInfo.count = 2",                          // 26
            "[crop.Change]",                                            // 27
            "base = 5",                                                 // 28
            "name = W",                                                 // 29
            "crops.form.0.growInfo.0.changeCrops = Berry",              // 30
            "[crop.BadSeason]",                                         // 31
            "base = 5",                                                 // 32
            "name = V",                                                 // 33
            "seasons = monsoon",                                        // 34
            "[crop.BadHarvest]",                                        // 35
            "base = 5",                                                 // 36
            "name = U",                                                 // 37
            "harvest = Berry * 0",                                      // 38
            "[crop.DigitSeed]",                                         // 39
            "base = 5",                                                 // 40
            "name = T",                                                 // 41
            "seed = 1234",                                              // 42
            "[crop.Seasons]",                                           // 43
            "base = 5",                                                 // 44
            "name = S",                                                 // 45
            "seasons = all",                                            // 46
            "crops.suitableSeason.0 = 1",                               // 47
            "[crop.Plain]",                                             // 48
            "base = 5",                                                 // 49
            "name = R",                                                 // 50
            "[crop.Lost]",                                              // 51
            "base = 5",                                                 // 52
            "name = Q",                                                 // 53
            "harvest = Missing",                                        // 54
            "[crop.Names]",                                             // 55
            "base = 5",                                                 // 56
            "name = P",                                                 // 57
            "crops.form.0.growInfo.0.harvestCropsName = O",             // 58
            "[crop.Swaps]",                                             // 59
            "base = 5",                                                 // 60
            "name = N",                                                 // 61
            "swap = 300 -> Pip, 300 -> Berry",                          // 62
        })
    );
    mod_rules::Report report;
    std::vector<RowSpec> rows = mod_rules::ReadRows(mod_rules::ScanMods(mods.Dir()), report, mod_rules::AcceptAll);
    CHECK(Said(report, 13, "[Alpha] Crop Berry: mystery is not a crop key, so it is ignored."));
    CHECK(Said(report, 14, "[Alpha] Crop NoBase skipped, it needs base = <the id of a vanilla crop to copy>."));
    CHECK(Said(report, 17, "[Alpha] Crop BandBase skipped, base must be the id of a vanilla crop."));
    CHECK(Said(report, 22, "another crop already grows from the seed Alpha.BerrySeed"));
    CHECK(Said(report, 26, "[Alpha] Crop Grow skipped, crops.form.0.growinfo.count: the forms and growth stages"));
    CHECK(Said(report, 30, "changeCrops takes a vanilla crop id"));
    CHECK(Said(report, 34, "seasons must be spring, summer, autumn and winter, or all, not monsoon"));
    CHECK(Said(report, 38, "harvest Berry * 0 must be item, item * count or item * min-max"));
    CHECK(Said(report, 42, "seed takes a mod item by key or Mod.Key, not 1234"));
    CHECK(Said(report, 47, "the seasons key already sets the season lists"));
    CHECK(Said(report, 48, "[Alpha] Crop Plain has no seed, so only a script can plant it."));
    CHECK(Said(report, 58, "the name key sets the crop's name and its harvest names"));
    CHECK(Said(report, 62, "must change a vanilla item, by id or enum name, and each item once"));
    CHECK(report.Count(Severity::Error) == 11);
    CHECK(report.Count(Severity::Warning) == 1);

    const RowSpec* crop = Find(rows, "Berry", mod_rules::kCropKind);
    const RowSpec* harvest = Find(rows, "Berry", mod_rules::kCropHarvestKind);
    if (!CHECK(crop && harvest)) {
        return;
    }
    CHECK(crop->schema == ts::FindPatchable("crops") && harvest->schema == ts::FindPatchable("cropsharvest"));
    CHECK(crop->baseId == 5 && harvest->baseId == 5);
    CHECK(mod_rules::IsCrop(*crop) && !mod_rules::IsCrop(*harvest));
    CHECK(mod_rules::CropSeedKey(*crop) == "Alpha.BerrySeed" && mod_rules::CropSeedKey(*harvest).empty());
    CHECK(InputText(*crop, "seasons") == "0, 3");
    CHECK(Value(*crop, "name.0") && Value(*crop, "name.6") && Value(*crop, "name.6")->value == "Moon");
    const RowValue* leaf = Value(*crop, "animeReplaceTextureID.0");
    CHECK(leaf && leaf->value == "Leaf" && std::string(leaf->refTable) == "animereplacetexture.dat");
    CHECK(Value(*crop, "animeReplaceTextureID.1") && !Value(*crop, "animeReplaceTextureID.2"));
    CHECK(Value(*crop, "form.0.growinfo.0.needgrowth") && Value(*crop, "form.0.growinfo.0.needgrowth")->value == "3");
    CHECK(Value(*harvest, "form.0.growinfo.0.harvest.0.droprate"));
    CHECK(!Value(*crop, "form.0.growinfo.0.harvest.0.droprate"));
    CHECK(harvest->inputs.size() == 6 && harvest->inputs[0].name == "harvest" && harvest->inputs[0].value == "Berry");
    CHECK(InputText(*harvest, "harvest count") == "2-4");
    CHECK(InputText(*harvest, "swap from") == "300" && InputText(*harvest, "swap to") == "Pip");
    CHECK(InputText(*harvest, "swap from", 1) == "ITEM_ID_X" && InputText(*harvest, "swap to", 1) == "400");

    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Item, "Alpha.BerrySeed");
    refs.Add(RefKind::Item, "Alpha.Berry");
    refs.Add(RefKind::Item, "Alpha.Pip");
    refs.Add(RefKind::Skin, "Alpha.Leaf");
    mod_rules::Report resolved;
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), refs, resolved);
    CHECK(Said(resolved, 54, "[Alpha] Crop Lost skipped, harvest Missing"));
    CHECK(resolved.Count(Severity::Error) == 1);
    crop = Find(rows, "Berry", mod_rules::kCropKind);
    harvest = Find(rows, "Berry", mod_rules::kCropHarvestKind);
    const RowSpec* lostCrop = Find(rows, "Lost", mod_rules::kCropKind);
    const RowSpec* plainCrop = Find(rows, "Plain", mod_rules::kCropKind);
    const RowSpec* plainHarvest = Find(rows, "Plain", mod_rules::kCropHarvestKind);
    if (!CHECK(crop && harvest && lostCrop && plainCrop && plainHarvest)) {
        return;
    }
    // The two rows of a crop share one id, and a crop whose harvest row
    // fails loses its crops row too.
    CHECK(crop->id != 0 && crop->id == harvest->id);
    CHECK(plainCrop->id != 0 && plainCrop->id == plainHarvest->id && plainCrop->id != crop->id);
    CHECK(lostCrop->id == 0 && Find(rows, "Lost", mod_rules::kCropHarvestKind)->id == 0);
    CHECK(mod_rules::RowRegistryKey(*crop) == "crops.Alpha.Berry" &&
          mod_rules::RowRegistryKey(*harvest) == "crops.Alpha.Berry");
    const RowValue* seed = mod_rules::CropSeed(*crop);
    CHECK(seed && seed->id == 1 && !seed->vanilla);
    CHECK(Value(*crop, "animeReplaceTextureID.0")->id == 1);
    CHECK(Input(*harvest, "swap from")->vanilla && Input(*harvest, "swap from")->id == 300);
    CHECK(Input(*harvest, "swap from", 1)->enumName == "ITEM_ID_X");
    std::vector<const RowSpec*> kept;
    for (const RowSpec& row : rows) {
        if (row.id != 0) {
            kept.push_back(&row);
        }
    }
    CHECK(kept.size() == 4);
    CHECK(mod_rules::CountRows(kept) == "2 crops");
}

// The desktop check: a seed must be a mod item with no use of its own.
void TestSeeds() {
    std::printf("Crop seeds\n");
    TempMods mods("seeds");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                 // 1
            "key = Alpha",           // 2
            "[item.Seed]",           // 3
            "name = Seed",           // 4
            "icon = 77",             // 5
            "[item.Snack]",          // 6
            "name = Snack",          // 7
            "icon = 77",             // 8
            "use = consume",         // 9
            "stamina = 5",           // 10
            "[crop.Good]",           // 11
            "base = 5",              // 12
            "name = Good",           // 13
            "seed = Seed",           // 14
            "[crop.Eaten]",          // 15
            "base = 5",              // 16
            "name = Eaten",          // 17
            "seed = Snack",          // 18
            "[crop.Ghost]",          // 19
            "base = 5",              // 20
            "name = Ghost",          // 21
            "seed = Nothing",        // 22
        })
    );
    mod_rules::Context context;
    context.modsDir = mods.Dir();
    context.folders = mod_rules::ScanMods(mods.Dir());
    mod_rules::CheckAll(context);
    const mod_rules::Report& report = context.report;
    CHECK(Said(report, 18, "[Alpha] Crop Eaten skipped, its seed Alpha.Snack has use = consume, and a seed item has "
                           "no use of its own."));
    CHECK(Said(report, 22, "[Alpha] Crop Ghost skipped, seed Nothing"));
    CHECK(report.Count(Severity::Error) == 2);
    for (const Diagnostic& item : report.Items()) {
        if (item.severity == Severity::Error && item.line != 18 && item.line != 22) {
            std::printf("  unexpected error at line %d\n", item.line);
        }
    }
}

// A seed item's row: type 0, the SEED category, and the Plant action with
// the crop's id. A seed may copy a vanilla seed, which other items may not.
void TestSeedRow() {
    std::printf("Seed item rows\n");
    constexpr size_t kAction = 436;
    mod_rules::ItemSpec seed;
    seed.fullKey = "Alpha.Seed";
    for (std::string& name : seed.names) {
        name = "Seed";
    }
    mod_rules::ItemRowParts parts;
    parts.id = 1500000001;
    parts.icon = 77;
    parts.outline = 78;
    parts.crop = 2100000004;
    const mod_rules::TextWriter write = [](std::vector<uint8_t>&, size_t, const std::string&) {};
    const std::vector<uint8_t> row = mod_rules::BuildItemRow(seed, parts, write);
    CHECK(blob::Get<uint64_t>(row.data(), 312) == 0 && blob::Get<uint64_t>(row.data(), 320) == 1);
    CHECK(blob::Get<uint64_t>(row.data(), kAction) == mod_rules::kActionPlant);
    CHECK(blob::Get<uint64_t>(row.data(), kAction + 8) == 2100000004);
    seed.category = 3;
    CHECK(blob::Get<uint64_t>(mod_rules::BuildItemRow(seed, parts, write).data(), 320) == 3);

    const blob::Row vanillaSeed{row.data(), static_cast<uint32_t>(row.size())};
    mod_rules::ItemSpec copy = seed;
    copy.baseId = 100;
    copy.category = 0;
    mod_rules::Report report;
    CHECK(!mod_rules::CheckAgainstTable(copy, &vanillaSeed, report));
    CHECK(mod_rules::CheckAgainstTable(copy, &vanillaSeed, report, true));
    // A copy of a seed that is no seed keeps the base's action; a seed's is
    // its own crop's.
    parts.base = &vanillaSeed;
    parts.crop = 2100000009;
    const std::vector<uint8_t> second = mod_rules::BuildItemRow(copy, parts, write);
    CHECK(blob::Get<uint64_t>(second.data(), 320) == 1);
    CHECK(blob::Get<uint64_t>(second.data(), kAction + 8) == 2100000009);
    CHECK(std::string(mod_rules::ItemUseName(mod_rules::ItemUse::Place)) == "place");
}

// ---- The game's tables ---------------------------------------------------

void TestRealGame() {
    std::printf("Crops on the game's tables\n");
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
    const ts::TableSchema& crops = *ts::FindPatchable("crops");
    const ts::TableSchema& harvests = *ts::FindPatchable("cropsharvest");
    const blob::Table* cropTable = vanilla.Table("crops.dat");
    const blob::Table* harvestTable = vanilla.Table("cropsharvest.dat");
    if (!CHECK(cropTable && harvestTable && !cropTable->rows.empty())) {
        return;
    }
    std::unordered_map<uint64_t, size_t> harvestById;
    for (size_t index = 0; index < harvestTable->rows.size(); ++index) {
        harvestById.emplace(blob::RowId(harvestTable->rows[index]), index);
    }
    // A plantable base whose drops have a produce and another item, with a
    // sowing season list, and a crop no seed plants.
    uint64_t base = 0;
    uint64_t unseeded = 0;
    uint64_t produce = 0;
    uint64_t other = 0;
    std::set<int64_t> baseItems;
    for (const blob::Row& row : cropTable->rows) {
        const uint64_t id = blob::RowId(row);
        const int64_t type = Number(crops, row.data, row.size, "type");
        if (type == 2 && unseeded == 0) {
            unseeded = id;
        }
        const auto found = harvestById.find(id);
        if (base != 0 || type == 2 || found == harvestById.end() ||
            Elements(crops, row.data, row.size, "sowingSeason") <= 0) {
            continue;
        }
        const blob::Row& drops = harvestTable->rows[found->second];
        std::set<int64_t> items;
        int64_t first = -1;
        for (int64_t form = 0; form < Elements(harvests, drops.data, drops.size, "form"); ++form) {
            const std::string list = "form." + std::to_string(form) + ".growInfo";
            for (int64_t stage = 0; stage < Elements(harvests, drops.data, drops.size, list); ++stage) {
                const std::string at = list + "." + std::to_string(stage) + ".harvest";
                for (int64_t drop = 0; drop < Elements(harvests, drops.data, drops.size, at); ++drop) {
                    const int64_t item = Number(harvests, drops.data, drops.size, at + "." + std::to_string(drop) + ".itemID");
                    first = first < 0 ? item : first;
                    items.insert(item);
                }
            }
        }
        for (const int64_t item : items) {
            if (item != first && first > 0) {
                base = id;
                produce = static_cast<uint64_t>(first);
                other = static_cast<uint64_t>(item);
                baseItems = items;
                break;
            }
        }
    }
    std::printf("  %zu crops, base %llu, unseeded %llu\n", cropTable->rows.size(), static_cast<unsigned long long>(base),
                static_cast<unsigned long long>(unseeded));
    if (!CHECK(base != 0 && unseeded != 0)) {
        return;
    }
    // Some vanilla item the base never drops.
    uint64_t neverDropped = 0;
    const blob::Table* items = vanilla.Table("item.dat");
    for (const blob::Row& row : items->rows) {
        const uint64_t id = blob::RowId(row);
        if (!baseItems.count(static_cast<int64_t>(id))) {
            neverDropped = id;
        }
    }
    const blob::Row& baseHarvest = harvestTable->rows[harvestById.at(base)];
    const std::string b = std::to_string(base);
    TempMods mods("game");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",
            "key = Alpha",
            "[item.Seed]",
            "name = Seed",
            "icon = 77",
            "[item.Fruit]",
            "name = Fruit",
            "icon = 77",
            "[crop.Good]",
            "base = " + b,
            "name = Good",
            "seed = Seed",
            "seasons = summer",
            "harvest = Fruit * 3-5",
            "swap = " + std::to_string(other) + " -> " + std::to_string(produce),
            "[crop.Wild]",
            "base = " + std::to_string(unseeded),
            "name = Wild",
            "seed = Alpha.Fruit",
            "[crop.Odd]",
            "base = " + b,
            "name = Odd",
            "swap = " + std::to_string(neverDropped) + " -> 1",
            "[crop.Twice]",
            "base = " + b,
            "name = Twice",
            "harvest = Fruit",
            "swap = " + std::to_string(produce) + " -> 1",
        })
    );
    mod_rules::Report ignored;
    std::vector<RowSpec> rows = mod_rules::ReadRows(mod_rules::ScanMods(mods.Dir()), ignored, mod_rules::AcceptAll);
    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Item, "Alpha.Seed");
    refs.Add(RefKind::Item, "Alpha.Fruit");
    mod_rules::ResolveRows(rows, mod_rules::RowNumbers(), refs, ignored);
    CHECK(ignored.Count(Severity::Error) == 0);
    const NoEnums tables;
    std::unordered_map<uint64_t, size_t> cropById;
    for (size_t index = 0; index < cropTable->rows.size(); ++index) {
        cropById.emplace(blob::RowId(cropTable->rows[index]), index);
    }
    const blob::Row& baseCrop = cropTable->rows[cropById.at(base)];
    const RowSpec* good = Find(rows, "Good", mod_rules::kCropKind);
    const RowSpec* goodHarvest = Find(rows, "Good", mod_rules::kCropHarvestKind);
    if (!CHECK(good && goodHarvest && good->id != 0)) {
        return;
    }
    blob::Pool cropPool(cropTable->poolBytes);
    std::vector<uint8_t> row;
    std::string why;
    if (CHECK(mod_rules::BuildRow(*good, *cropTable, cropById, tables, cropPool, row, why))) {
        const uint8_t* data = row.data();
        CHECK(ts::RowFits(crops, data, row.size()));
        CHECK(Number(crops, data, row.size(), "type") == Number(crops, baseCrop.data, baseCrop.size, "type"));
        CHECK(Elements(crops, data, row.size(), "suitableSeason") == 1);
        CHECK(Number(crops, data, row.size(), "suitableSeason.0") == 1);
        CHECK(Elements(crops, data, row.size(), "sowingSeason") == 1);
        CHECK(Number(crops, data, row.size(), "sowingSeason.0") == 1);
        CHECK(Elements(crops, data, row.size(), "harvestSeason") ==
              std::min<int64_t>(1, Elements(crops, baseCrop.data, baseCrop.size, "harvestSeason")));
        CHECK(Elements(crops, data, row.size(), "form") == Elements(crops, baseCrop.data, baseCrop.size, "form"));
    } else {
        std::printf("  why: %s\n", why.c_str());
    }

    blob::Pool harvestPool(harvestTable->poolBytes);
    if (CHECK(mod_rules::BuildRow(*goodHarvest, *harvestTable, harvestById, tables, harvestPool, row, why))) {
        const uint8_t* data = row.data();
        CHECK(ts::RowFits(harvests, data, row.size()));
        size_t produced = 0;
        size_t swapped = 0;
        size_t same = 0;
        size_t wrong = 0;
        for (int64_t form = 0; form < Elements(harvests, data, row.size(), "form"); ++form) {
            const std::string list = "form." + std::to_string(form) + ".growInfo";
            for (int64_t stage = 0; stage < Elements(harvests, data, row.size(), list); ++stage) {
                const std::string at = list + "." + std::to_string(stage) + ".harvest";
                for (int64_t drop = 0; drop < Elements(harvests, data, row.size(), at); ++drop) {
                    const std::string d = at + "." + std::to_string(drop);
                    const int64_t was = Number(harvests, baseHarvest.data, baseHarvest.size, d + ".itemID");
                    const int64_t now = Number(harvests, data, row.size(), d + ".itemID");
                    if (was == static_cast<int64_t>(produce)) {
                        produced += now == 2 && Number(harvests, data, row.size(), d + ".minDropCount") == 3 &&
                                            Number(harvests, data, row.size(), d + ".maxDropCount") == 5
                                        ? 1
                                        : 0;
                        wrong += now == 2 ? 0 : 1;
                    } else if (was == static_cast<int64_t>(other)) {
                        swapped += now == static_cast<int64_t>(produce) ? 1 : 0;
                        wrong += now == static_cast<int64_t>(produce) ? 0 : 1;
                    } else {
                        same += now == was ? 1 : 0;
                        wrong += now == was ? 0 : 1;
                    }
                }
            }
        }
        std::printf("  %zu produce, %zu swapped, %zu kept drops\n", produced, swapped, same);
        CHECK(produced > 0 && swapped > 0 && wrong == 0);
    } else {
        std::printf("  why: %s\n", why.c_str());
    }

    const RowSpec* wild = Find(rows, "Wild", mod_rules::kCropKind);
    if (CHECK(wild && wild->id != 0)) {
        CHECK(!mod_rules::BuildRow(*wild, *cropTable, cropById, tables, cropPool, row, why));
        CHECK(why.find("type 2") != std::string::npos);
    }
    const RowSpec* odd = Find(rows, "Odd", mod_rules::kCropHarvestKind);
    if (CHECK(odd && odd->id != 0)) {
        CHECK(!mod_rules::BuildRow(*odd, *harvestTable, harvestById, tables, harvestPool, row, why));
        CHECK(why.find("its base crop never drops item " + std::to_string(neverDropped)) != std::string::npos);
    }
    const RowSpec* twice = Find(rows, "Twice", mod_rules::kCropHarvestKind);
    if (CHECK(twice && twice->id != 0)) {
        CHECK(!mod_rules::BuildRow(*twice, *harvestTable, harvestById, tables, harvestPool, row, why));
        CHECK(why.find("changes the produce, which harvest already changes") != std::string::npos);
    }
}

}  // namespace

void TestCrops(int& checks, int& failures) {
    g_checks = 0;
    g_failures = 0;
    TestReading();
    TestSeeds();
    TestSeedRow();
    TestRealGame();
    checks += g_checks;
    failures += g_failures;
}
