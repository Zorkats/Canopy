#include "rules_crops.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "platform_files.h"
#include "rules_items.h"

// =============================================================
// CROP RULES
// =============================================================
// A crop is two rows with one id: crops.dat says how it grows (its forms,
// each a list of growth stages), what it looks like and when it grows, and
// cropsharvest.dat, whose rows have the id of their crop and mirror its forms
// and stages, says what each stage drops. A seed is an item whose action is
// Plant (1060) with the crop's id as its value; the game looks that id up in
// the crops table when the seed is used. Nothing else links the three.
//
// A [crop.] section copies one vanilla crop into both rows, so the forms and
// stages line up, and sets what a mod usually changes: the name, the seasons,
// the skins of the growth stages, the produce and other drops. The name is
// written to every stage's harvest name too, which every vanilla crop has
// equal to its name. crops.<path> and cropsharvest.<path> keys reach any
// other field, the way [row.] keys do, except the forms and stages, which
// must stay as the base has them for the two rows to agree.
//
// The seed is a mod item the section names. The loader turns that item into
// a seed as its row is built, so the item itself needs no use. A crop whose
// growth stage changes into another crop names that crop's id in the save,
// so changeCrops takes a vanilla crop only.
//
// The save keeps a planted crop's row id. The loader saves a planted mod crop
// as its base crop and puts it back after (mod_crops).

namespace mod_rules {
namespace {

namespace ts = table_schema;

const char* const kSeasons[] = {"spring", "summer", "autumn", "winter"};
constexpr size_t kMaxNameLength = 200;
constexpr size_t kSkinSlots = 5;
constexpr size_t kMaxSwaps = 16;
constexpr int32_t kMaxDropCount = 99;
// A crop of this type is not planted from a seed: the game refuses it.
constexpr int64_t kTypeNoSeed = 2;
const char* const kCropsKeys = "crops.";
const char* const kHarvestKeys = "cropsharvest.";
// The season lists of a crops row: when it grows, when it may be sown and
// when it may be harvested. An empty one of the last two sets no limit.
const char* const kSeasonLists[] = {"suitableSeason", "sowingSeason", "harvestSeason"};

// The names of RowSpec::inputs.
const char* const kSeedInput = "seed";
const char* const kSeasonsInput = "seasons";
const char* const kHarvestInput = "harvest";
const char* const kCountInput = "harvest count";
const char* const kSwapFromInput = "swap from";
const char* const kSwapToInput = "swap to";

const FieldInfo kCropFields[] = {
    {"base", ValueKind::VanillaId, true,
     "The vanilla crop to copy: how it grows and looks, its seasons and what it drops.", nullptr, "crops.dat"},
    {"name", ValueKind::LangText, true,
     "The name the game shows for the crop and its harvest, one line of up to 200 bytes. name.<lang> sets it for one "
     "language."},
    {"seed", ValueKind::ItemRef, false,
     "The mod item that plants it, by key or Mod.Key: an item with no use of its own. An item plants one crop."},
    {"seasons", ValueKind::List, false,
     "The seasons it grows in: spring, summer, autumn and winter, or all. Where the base limits when it is sown or "
     "harvested, those limits follow."},
    {"skins", ValueKind::List, false,
     "Up to 5 skins for its growth stages, in the base's order: mod skins by key or Mod.Key, or vanilla "
     "AnimeReplaceTexture ids. A slot left out keeps the base's."},
    {"harvest", ValueKind::Other, false,
     "What it gives instead of the base's produce, the item its first drop gives: an item, item * count or item * "
     "min-max, with counts of 1 to 99."},
    {"swap", ValueKind::List, false,
     "Other drops to change, up to 16, each <vanilla item> -> <item>: every drop of the first item gives the second "
     "instead."},
};

bool SameText(const std::string& left, const char* right) {
    return platform::CompareNoCase(left.c_str(), right) == 0;
}

bool StartsWith(const std::string& text, const char* prefix) {
    const size_t length = std::strlen(prefix);
    return text.size() >= length && platform::CompareNoCase(text.c_str(), prefix, length) == 0;
}

bool EndsWith(const std::string& text, const char* suffix) {
    const size_t length = std::strlen(suffix);
    return text.size() >= length && SameText(text.substr(text.size() - length), suffix);
}

bool Digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

const RowValue* Input(const RowSpec& row, const char* name) {
    const auto found = std::find_if(row.inputs.begin(), row.inputs.end(), [&](const RowValue& value) {
        return value.name == name;
    });
    return found == row.inputs.end() ? nullptr : &*found;
}

// ---- Building ------------------------------------------------------------

// Writes text at path, every place it names.
bool WriteAt(
    const ts::TableSchema& schema, std::vector<uint8_t>& row, const std::string& path, const std::string& text,
    blob::Pool& pool, std::string& why
) {
    std::vector<ts::Location> places;
    if (!ts::ResolveRow(schema, path, row.data(), row.size(), places, why)) {
        why = path + ": " + why;
        return false;
    }
    for (const ts::Location& place : places) {
        if (!ts::Write(row, place, text, pool, why)) {
            why = path + ": " + why;
            return false;
        }
    }
    return true;
}

// The number at path, false with why when the row has no single place there.
bool NumberAt(
    const ts::TableSchema& schema, const std::vector<uint8_t>& row, const std::string& path, uint64_t& value,
    std::string& why
) {
    std::vector<ts::Location> places;
    if (!ts::ResolveRow(schema, path, row.data(), row.size(), places, why) || places.size() != 1) {
        why = path + ": " + (why.empty() ? "not one place" : why);
        return false;
    }
    value = ts::ReadNumber(row.data(), places[0]);
    return true;
}

// Calls visit(path) for each growth stage of a crops or cropsharvest row:
// form.<f>.growInfo.<g>.
template <typename Visit>
bool ForEachStage(const ts::TableSchema& schema, const std::vector<uint8_t>& row, std::string& why, Visit visit) {
    uint32_t forms = 0;
    if (!ts::ElementCount(schema, row.data(), row.size(), "form", forms, why)) {
        return false;
    }
    for (uint32_t form = 0; form < forms; ++form) {
        const std::string list = "form." + std::to_string(form) + ".growInfo";
        uint32_t stages = 0;
        if (!ts::ElementCount(schema, row.data(), row.size(), list, stages, why)) {
            return false;
        }
        for (uint32_t stage = 0; stage < stages; ++stage) {
            if (!visit(list + "." + std::to_string(stage))) {
                return false;
            }
        }
    }
    return true;
}

// The crops row: every stage's harvest name is the crop's name, and the
// season lists follow seasons.
bool FinishCrop(
    const RowSpec& mod, std::vector<uint8_t>& row, const RowTables&, blob::Pool& pool, std::string& why
) {
    const ts::TableSchema& schema = *mod.schema;
    if (Input(mod, kSeedInput)) {
        uint64_t type = 0;
        if (!NumberAt(schema, row, "type", type, why)) {
            return false;
        }
        if (static_cast<int64_t>(static_cast<int32_t>(type)) == kTypeNoSeed) {
            why = "its base is a crop the game never plants from a seed (type 2), so its seed would plant nothing";
            return false;
        }
    }
    const bool named = ForEachStage(schema, row, why, [&](const std::string& stage) {
        for (const RowValue& value : mod.values) {
            if (StartsWith(value.path, "name.") &&
                !WriteAt(schema, row, stage + ".harvestCropsName" + value.path.substr(4), value.value, pool, why)) {
                return false;
            }
        }
        return true;
    });
    if (!named) {
        return false;
    }
    const RowValue* seasons = Input(mod, kSeasonsInput);
    if (!seasons) {
        return true;
    }
    const std::vector<std::string> wanted = SplitList(seasons->value);
    for (const char* list : kSeasonLists) {
        uint32_t had = 0;
        if (!ts::ElementCount(schema, row.data(), row.size(), list, had, why)) {
            return false;
        }
        if (had == 0 && list != kSeasonLists[0]) {
            continue;
        }
        if (!ts::ResizeVector(schema, row, list, static_cast<uint32_t>(wanted.size()), pool, why)) {
            return false;
        }
        for (size_t index = 0; index < wanted.size(); ++index) {
            if (!WriteAt(schema, row, std::string(list) + "." + std::to_string(index), wanted[index], pool, why)) {
                return false;
            }
        }
    }
    return true;
}

// The id a resolved item input writes.
bool ItemId(const RowValue& value, const RowTables& tables, std::string& text, uint64_t& id, std::string& why) {
    return RowValueText(value, tables, text, why) && ParseU64(text, id);
}

// The harvest row: the produce, the item of the base's first drop, becomes
// the harvest item, and each swap changes the drops of its first item. Both
// match the drops as the base has them.
bool FinishHarvest(
    const RowSpec& mod, std::vector<uint8_t>& row, const RowTables& tables, blob::Pool& pool, std::string& why
) {
    const ts::TableSchema& schema = *mod.schema;
    const RowValue* harvest = Input(mod, kHarvestInput);
    const bool swaps = Input(mod, kSwapFromInput) != nullptr;
    if (!harvest && !swaps) {
        return true;
    }
    // Each drop as form.<f>.growInfo.<g>.harvest.<d>, with its item.
    std::vector<std::pair<std::string, uint64_t>> drops;
    const bool walked = ForEachStage(schema, row, why, [&](const std::string& stage) {
        uint32_t count = 0;
        if (!ts::ElementCount(schema, row.data(), row.size(), stage + ".harvest", count, why)) {
            return false;
        }
        for (uint32_t drop = 0; drop < count; ++drop) {
            const std::string at = stage + ".harvest." + std::to_string(drop);
            uint64_t item = 0;
            if (!NumberAt(schema, row, at + ".itemID", item, why)) {
                return false;
            }
            drops.emplace_back(at, item);
        }
        return true;
    });
    if (!walked) {
        return false;
    }
    if (drops.empty()) {
        why = "its base crop drops nothing, so there is no drop to change";
        return false;
    }
    const uint64_t produce = drops[0].second;
    std::vector<std::pair<std::string, std::string>> writes;
    if (harvest) {
        std::string text;
        uint64_t id = 0;
        if (!ItemId(*harvest, tables, text, id, why)) {
            why = "harvest: " + why;
            return false;
        }
        const RowValue* count = Input(mod, kCountInput);
        const size_t dash = count ? count->value.find('-') : std::string::npos;
        for (const auto& [at, item] : drops) {
            if (item != produce) {
                continue;
            }
            writes.emplace_back(at + ".itemID", text);
            if (count) {
                writes.emplace_back(at + ".minDropCount", count->value.substr(0, dash));
                writes.emplace_back(at + ".maxDropCount", count->value.substr(dash + 1));
            }
        }
    }
    for (size_t index = 0; index + 1 < mod.inputs.size(); ++index) {
        const RowValue& from = mod.inputs[index];
        if (from.name != kSwapFromInput) {
            continue;
        }
        const RowValue& to = mod.inputs[index + 1];
        std::string fromText;
        std::string toText;
        uint64_t fromId = 0;
        uint64_t toId = 0;
        if (!from.vanilla && from.enumName.empty()) {
            why = "swap " + from.value + " names a mod item, and a base crop drops vanilla items only";
            return false;
        }
        if (!ItemId(from, tables, fromText, fromId, why) || !ItemId(to, tables, toText, toId, why)) {
            why = "swap: " + why;
            return false;
        }
        if (harvest && fromId == produce) {
            why = "swap " + from.value + " changes the produce, which harvest already changes";
            return false;
        }
        size_t matched = 0;
        for (const auto& [at, item] : drops) {
            if (item == fromId) {
                writes.emplace_back(at + ".itemID", toText);
                ++matched;
            }
        }
        if (matched == 0) {
            why = "swap " + from.value + ": its base crop never drops item " + fromText;
            return false;
        }
    }
    for (const auto& [path, text] : writes) {
        if (!WriteAt(schema, row, path, text, pool, why)) {
            return false;
        }
    }
    return true;
}

// ---- Reading -------------------------------------------------------------

// What one section is being read into.
struct Reader {
    const ModFolder& folder;
    const canopy_ini::IniSection& section;
    Report& report;
    RowSpec& crop;
    RowSpec& harvest;

    std::string Value(const char* key) const {
        return canopy_ini::Trim(canopy_ini::Value(&section, key, ""));
    }
    int Line(const char* key) const {
        return canopy_ini::ValueLine(&section, key);
    }
    bool Fail(const char* key, const std::string& why) const {
        report.Error(folder.name, key ? Line(key) : section.line, crop.where + " skipped, " + why + ".");
        return false;
    }
    void Set(const std::string& path, const std::string& text, const char* key) const {
        RowValue value;
        value.path = path;
        value.value = text;
        value.line = Line(key);
        value.name = key;
        crop.values.push_back(std::move(value));
    }
    // An input of row: refTable for a reference ResolveRows resolves.
    void Add(RowSpec& row, const char* name, const std::string& text, const char* key, const char* refTable) const {
        RowValue value;
        value.name = name;
        value.value = text;
        value.line = Line(key);
        value.refTable = refTable;
        row.inputs.push_back(std::move(value));
    }
};

bool ReadName(const Reader& in) {
    std::string names[kLanguageCount];
    ReadTexts(in.section, "name", names);
    for (int language = 0; language < kLanguageCount; ++language) {
        if (names[language].empty()) {
            return in.Fail(nullptr, "it needs name = <text>");
        }
        if (!TextValid(names[language], kMaxNameLength)) {
            return in.Fail("name", "a name must be one line of up to 200 bytes");
        }
        in.Set("name." + std::to_string(language), names[language], "name");
    }
    return true;
}

bool ReadSeasons(const Reader& in) {
    const std::string text = in.Value("seasons");
    if (text.empty()) {
        return true;
    }
    std::vector<int> seasons;
    if (SameText(text, "all")) {
        seasons = {0, 1, 2, 3};
    }
    for (const std::string& part : seasons.empty() ? SplitList(text) : std::vector<std::string>()) {
        const auto found = std::find_if(std::begin(kSeasons), std::end(kSeasons), [&](const char* name) {
            return SameText(part, name);
        });
        if (found == std::end(kSeasons)) {
            return in.Fail("seasons", "seasons must be spring, summer, autumn and winter, or all, not " + part);
        }
        const int season = static_cast<int>(found - std::begin(kSeasons));
        if (std::find(seasons.begin(), seasons.end(), season) == seasons.end()) {
            seasons.push_back(season);
        }
    }
    std::sort(seasons.begin(), seasons.end());
    std::string list;
    for (const int season : seasons) {
        list += (list.empty() ? "" : ", ") + std::to_string(season);
    }
    in.Add(in.crop, kSeasonsInput, list, "seasons", nullptr);
    return true;
}

bool ReadSkins(const Reader& in) {
    const std::string text = in.Value("skins");
    if (text.empty()) {
        return true;
    }
    const std::vector<std::string> skins = SplitList(text);
    if (skins.size() > kSkinSlots) {
        return in.Fail("skins", "skins names " + std::to_string(skins.size()) + " skins, and a crop has 5 at most");
    }
    for (size_t index = 0; index < skins.size(); ++index) {
        if (skins[index].empty()) {
            return in.Fail("skins", "skins has an empty entry");
        }
        RowValue value;
        value.path = "animeReplaceTextureID." + std::to_string(index);
        value.value = skins[index];
        value.line = in.Line("skins");
        value.name = "skins";
        value.refTable = "animereplacetexture.dat";
        in.crop.values.push_back(std::move(value));
    }
    return true;
}

bool ReadSeed(const Reader& in) {
    const std::string seed = in.Value("seed");
    if (seed.empty()) {
        return true;
    }
    if (Digits(seed)) {
        return in.Fail(
            "seed", "seed takes a mod item by key or Mod.Key, not " + seed + "; a vanilla seed already plants its crop"
        );
    }
    in.Add(in.crop, kSeedInput, seed, "seed", "item.dat");
    return true;
}

// harvest = item, item * count or item * min-max.
bool ReadHarvest(const Reader& in) {
    const std::string text = in.Value("harvest");
    if (text.empty()) {
        return true;
    }
    std::string item = text;
    const size_t star = text.find('*');
    if (star != std::string::npos) {
        item = canopy_ini::Trim(text.substr(0, star));
        const std::string count = canopy_ini::Trim(text.substr(star + 1));
        const size_t dash = count.find('-');
        int32_t low = 0;
        int32_t high = 0;
        const bool parsed =
            dash == std::string::npos
                ? ParseI32(count, low) && (high = low, true)
                : ParseI32(canopy_ini::Trim(count.substr(0, dash)), low) &&
                      ParseI32(canopy_ini::Trim(count.substr(dash + 1)), high);
        if (!parsed || low < 1 || high > kMaxDropCount || low > high) {
            return in.Fail(
                "harvest", "harvest " + text + " must be item, item * count or item * min-max, with counts of 1 to 99"
            );
        }
        in.Add(in.harvest, kCountInput, std::to_string(low) + "-" + std::to_string(high), "harvest", nullptr);
    }
    if (item.empty()) {
        return in.Fail("harvest", "harvest has no item");
    }
    // The item goes first, which finish does not rely on.
    in.harvest.inputs.insert(in.harvest.inputs.begin(), RowValue());
    RowValue& value = in.harvest.inputs.front();
    value.name = kHarvestInput;
    value.value = item;
    value.line = in.Line("harvest");
    value.refTable = "item.dat";
    return true;
}

// swap = <vanilla item> -> <item>, ...
bool ReadSwaps(const Reader& in) {
    const std::string text = in.Value("swap");
    if (text.empty()) {
        return true;
    }
    const std::vector<std::string> swaps = SplitList(text);
    if (swaps.size() > kMaxSwaps) {
        return in.Fail("swap", "swap has " + std::to_string(swaps.size()) + " entries, and a crop takes 16 at most");
    }
    std::vector<std::string> froms;
    for (const std::string& swap : swaps) {
        const size_t arrow = swap.find("->");
        const std::string from = arrow == std::string::npos ? std::string() : canopy_ini::Trim(swap.substr(0, arrow));
        const std::string to = arrow == std::string::npos ? std::string() : canopy_ini::Trim(swap.substr(arrow + 2));
        if (from.empty() || to.empty()) {
            return in.Fail("swap", "swap " + swap + " must be <vanilla item> -> <item>");
        }
        if (from.find('.') != std::string::npos ||
            std::any_of(froms.begin(), froms.end(), [&](const std::string& other) { return SameText(other, from.c_str()); })) {
            return in.Fail(
                "swap", "swap " + swap + " must change a vanilla item, by id or enum name, and each item once"
            );
        }
        froms.push_back(from);
        in.Add(in.harvest, kSwapFromInput, from, "swap", "item.dat");
        in.Add(in.harvest, kSwapToInput, to, "swap", "item.dat");
    }
    return true;
}

// A crops.<path> or cropsharvest.<path> key. False, reported, when the crop
// is skipped.
bool ReadPath(const Reader& in, const std::string& key, const std::string& value, int line) {
    const bool cropKey = StartsWith(key, kCropsKeys);
    RowSpec& row = cropKey ? in.crop : in.harvest;
    const std::string path = key.substr(std::strlen(cropKey ? kCropsKeys : kHarvestKeys));
    const auto refuse = [&](const std::string& why) {
        in.report.Error(in.folder.name, line, in.crop.where + " skipped, " + key + ": " + why + ".");
        return false;
    };
    // Keys are lowercase, as canopy_ini stores them.
    const std::string lower = canopy_ini::Lower(path);
    if (lower == "form.count" || EndsWith(lower, ".growinfo.count")) {
        return refuse("the forms and growth stages stay as the base has them, so the crop and its harvest agree");
    }
    if (cropKey && (lower == "name" || StartsWith(lower, "name.") ||
                    lower.find("harvestcropsname") != std::string::npos)) {
        return refuse("the name key sets the crop's name and its harvest names");
    }
    if (cropKey && !in.Value("seasons").empty() &&
        std::any_of(std::begin(kSeasonLists), std::end(kSeasonLists), [&](const char* list) {
            return StartsWith(path, list);
        })) {
        return refuse("the seasons key already sets the season lists");
    }
    if (cropKey && EndsWith(path, ".changeCrops") && !Digits(value)) {
        return refuse(
            "changeCrops takes a vanilla crop id, since the save keeps the crop a stage changes into as a number"
        );
    }
    // Messages name the row by the crop.
    const std::string where = row.where;
    row.where = in.crop.where;
    const bool read = ReadRowValue(path, value, line, in.report, row);
    row.where = where;
    return read;
}

}  // namespace

const SectionInfo kCropSection = {kCropPrefix, "Crop", "docs/CROPS.md", kCropFields, std::size(kCropFields)};

bool IsCropSection(const canopy_ini::IniSection& section) {
    const size_t length = std::strlen(kCropPrefix);
    return section.name.size() >= length && platform::CompareNoCase(section.name.c_str(), kCropPrefix, length) == 0;
}

bool ReadCrop(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    RowSpec& crop, RowSpec& harvest
) {
    crop.kind = kCropKind;
    harvest.kind = kCropHarvestKind;
    crop.key = harvest.key = SectionKey(section, kCropPrefix);
    crop.modKey = harvest.modKey = modKey;
    crop.folder = harvest.folder = folder.name;
    crop.line = harvest.line = section.line;
    crop.schema = ts::FindPatchable("crops");
    harvest.schema = ts::FindPatchable("cropsharvest");
    crop.where = harvest.where = "[" + modKey + "] Crop " + crop.key;
    crop.fullKey = harvest.fullKey = modKey + "." + crop.key;
    crop.label = "Mod crop " + crop.fullKey;
    harvest.label = "Mod crop " + crop.fullKey + "'s harvest";
    crop.registryKey = harvest.registryKey = "crops." + crop.fullKey;
    crop.finish = &FinishCrop;
    harvest.finish = &FinishHarvest;
    if (!crop.schema || !harvest.schema) {
        report.Error(folder.name, section.line, crop.where + " skipped, Canopy has no schema for its tables.");
        return false;
    }
    if (!KeyValid(crop.key)) {
        report.Error(
            folder.name, section.line, crop.where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    const Reader in{folder, section, report, crop, harvest};
    const std::string base = in.Value("base");
    if (base.empty()) {
        return in.Fail(nullptr, "it needs base = <the id of a vanilla crop to copy>");
    }
    if (!ParseU64(base, crop.baseId) || crop.baseId == 0 ||
        (crop.baseId >= ts::kRowBandStart && crop.baseId < ts::kRowBandStart + ts::kRowBandSize)) {
        return in.Fail("base", "base must be the id of a vanilla crop");
    }
    harvest.baseId = crop.baseId;
    if (!ReadName(in) || !ReadSeasons(in) || !ReadSkins(in) || !ReadSeed(in) || !ReadHarvest(in) || !ReadSwaps(in)) {
        return false;
    }
    for (size_t index = 0; index < section.values.size(); ++index) {
        const auto& [key, value] = section.values[index];
        const int line = index < section.valueLines.size() ? section.valueLines[index] : section.line;
        if (StartsWith(key, kCropsKeys) || StartsWith(key, kHarvestKeys)) {
            if (!ReadPath(in, key, value, line)) {
                return false;
            }
            continue;
        }
        const bool known = StartsWith(key, "name.") ||
                           std::any_of(std::begin(kCropFields), std::end(kCropFields), [&](const FieldInfo& field) {
                               return SameText(key, field.key);
                           });
        if (!known) {
            report.Warning(
                folder.name, line,
                crop.where + ": " + key + " is not a crop key, so it is ignored. crops.<path> and cropsharvest.<path> "
                                          "set any other field."
            );
        }
    }
    if (!Input(crop, kSeedInput)) {
        report.Note(folder.name, section.line, crop.where + " has no seed, so only a script can plant it.");
    }
    return true;
}

bool IsCrop(const RowSpec& row) {
    return row.kind && std::strcmp(row.kind, kCropKind) == 0;
}

std::string CropSeedKey(const RowSpec& row) {
    const RowValue* seed = IsCrop(row) ? Input(row, kSeedInput) : nullptr;
    return seed ? FullKey(row.modKey, seed->value) : std::string();
}

const RowValue* CropSeed(const RowSpec& row) {
    return IsCrop(row) ? Input(row, kSeedInput) : nullptr;
}

std::string SeedRefused(const std::string& seedKey, const char* useName) {
    return "its seed " + seedKey + " has use = " + useName + ", and a seed item has no use of its own";
}

std::set<std::string> SeedKeys(const std::vector<ModFolder>& folders) {
    std::set<std::string> keys;
    for (const ModFolder& folder : folders) {
        const std::string modKey = ModKey(folder);
        for (const canopy_ini::IniSection& section : folder.sections) {
            const std::string seed = IsCropSection(section)
                                         ? canopy_ini::Trim(canopy_ini::Value(&section, "seed", ""))
                                         : std::string();
            if (!seed.empty() && !Digits(seed)) {
                keys.insert(canopy_ini::Lower(FullKey(modKey, seed)));
            }
        }
    }
    return keys;
}

void CheckCropSeeds(Context& context, std::vector<RowSpec>& rows) {
    Report quiet;
    const std::vector<ItemSpec> items =
        ReadItems(context.folders, context.modsDir, context.refs, quiet, AcceptAll);
    for (RowSpec& row : rows) {
        const RowValue* seed = CropSeed(row);
        if (row.id == 0 || !seed) {
            continue;
        }
        const std::string key = CropSeedKey(row);
        const auto item = std::find_if(items.begin(), items.end(), [&](const ItemSpec& other) {
            return SameText(other.fullKey, key.c_str());
        });
        std::string why;
        if (item == items.end()) {
            why = "its seed " + seed->value + " is not a mod item";
        } else if (item->use != ItemUse::None) {
            why = SeedRefused(key, ItemUseName(item->use));
        }
        if (!why.empty()) {
            context.report.Error(row.folder, seed->line, row.where + " skipped, " + why + ".");
            DropRow(rows, row);
        }
    }
}

}  // namespace mod_rules
