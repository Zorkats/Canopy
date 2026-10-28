#include "rules_shops.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

#include "platform_files.h"

// =============================================================
// SHOP RULES
// =============================================================
// [sale.<Key>] is a storesales.dat row: an item a shop lists. Every day the
// game rebuilds each shop's list from the rows of its storeID whose seasons
// and flags hold, the stock of an entry it already had carried over, so a
// sale is no more than its row. The row is a copy of the first vanilla row of
// the same shop, which proves the shop exists, with every field it uses
// written: item, shop, stock and restock, seasons, the flags that must be on
// and off, the discount, the list order and the silhouette. randomInfo, which
// only the traveling stall's rotation uses, is cleared, so the row is always
// listed. releaseValue is cleared too: the game turns those flags on only for
// a fixed list of vanilla items it knows by id, and the hunting lodge refuses
// a sale once all of them are on, so on a mod sale they could only block it. The price is the item's own
// buyingPrice, less the discount: a mod item's comes from its buy key.
//
// [upgrade.<Key>] is a blacksmith.dat row: the blacksmith takes a tool and
// two materials, and after a wait hands over a better tool in a box. The box
// is a gimmick made for its tool, which only vanilla upgrades have, so an
// upgrade makes a tool a vanilla upgrade makes and copies that row, box
// included. The tool brought must be vanilla too.
//
// The save keeps the shop lists, CDataPtr to the sales row and the item.
// The loader takes mod entries out of them while the save is written
// (mod_shops). The blacksmith's rows are not saved.

namespace mod_rules {
namespace {

namespace ts = table_schema;

const char* const kSeasons[] = {"spring", "summer", "autumn", "winter"};
constexpr int32_t kMaxStock = 99;
constexpr int32_t kMaxPriority = 100;
constexpr int32_t kDefaultPriority = 50;
constexpr int32_t kMaxShop = 99;
constexpr int32_t kMaxDiscount = 99;
constexpr int32_t kMaxTime = 1000;
constexpr int32_t kDefaultTime = 10;
constexpr int32_t kMaxNeedCount = 99;
// stockInfo.restock: 0 keeps what is left, 1 fills the stock every day.
constexpr int kRestockNever = 0;
constexpr int kRestockDaily = 1;
// The k_vector sizes of the record (storesales condOnFlag, condOffFlag) and
// blacksmith needItem.
constexpr size_t kMaxConditionFlags = 16;
constexpr size_t kNeeds = 2;

const FieldInfo kSaleFields[] = {
    {"item", ValueKind::ItemRef, true, "The item sold: a mod item by key or Mod.Key, or a vanilla item id or enum name."},
    {"shop", ValueKind::Integer, true,
     "The shop, by the storeID its vanilla sales have: 0, 1, 2, 3, 4 or 99. Studio's game browser lists each shop's "
     "storesales rows.",
     nullptr, nullptr, 0, kMaxShop},
    {"stock", ValueKind::Other, false, "How many the shop has, 1 to 99, or unlimited.", "unlimited"},
    {"restock", ValueKind::Choice, false,
     "With a stock: daily fills it again every day, never leaves what was bought gone for good.", "daily", nullptr, 0, 0,
     "daily|never"},
    {"seasons", ValueKind::List, false, "The seasons it is listed in: spring, summer, autumn and winter, or all.",
     "all"},
    {"flag", ValueKind::List, false,
     "Flags that must all be on for it to be listed, up to 16: mod flags by key or Mod.Key, or vanilla flag ids."},
    {"unless", ValueKind::List, false, "Flags that must all be off for it to be listed, up to 16."},
    {"discount", ValueKind::Integer, false, "Percent off the item's price, 0 to 99.", "0", nullptr, 0, kMaxDiscount},
    {"priority", ValueKind::Integer, false, "Its place in the list, 0 to 100: higher comes first.", "50", nullptr, 0,
     kMaxPriority},
    {"silhouette", ValueKind::Switch, false,
     "on: while its seasons or flags do not hold, the shop shows it as a dark shape.", "off"},
};

const FieldInfo kUpgradeFields[] = {
    {"from", ValueKind::VanillaId, true, "The tool the player brings: a vanilla item id or enum name.", nullptr,
     "item.dat"},
    {"to", ValueKind::VanillaId, true,
     "The tool the blacksmith hands back: a vanilla item that a vanilla upgrade makes.", nullptr, "item.dat"},
    {"needs", ValueKind::List, true,
     "The two materials it takes, each item or item * count with a count of 1 to 99: mod items by key or Mod.Key, or "
     "vanilla item ids or enum names."},
    {"time", ValueKind::Integer, false, "How long the blacksmith works on it, 1 to 1000. Every vanilla upgrade has 10.",
     "10", nullptr, 1, kMaxTime},
};

bool SameText(const std::string& left, const char* right) {
    return platform::CompareNoCase(left.c_str(), right) == 0;
}

bool Digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// An enum name as the game writes them: capitals, digits and '_', with one
// '_' at least.
bool LooksLikeEnum(const std::string& text) {
    return text.size() <= 128 && text.find('_') != std::string::npos &&
           std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}

// What one section is being read into, with the helpers its keys share.
struct Reader {
    const ModFolder& folder;
    const canopy_ini::IniSection& section;
    Report& report;
    RowSpec& row;

    std::string Value(const char* key) const {
        return canopy_ini::Value(&section, key, "");
    }
    int Line(const char* key) const {
        return canopy_ini::ValueLine(&section, key);
    }
    bool Fail(const char* key, const std::string& why) const {
        report.Error(folder.name, key ? Line(key) : section.line, row.where + " skipped, " + why + ".");
        return false;
    }

    // path = text, as key wrote it.
    void Set(const std::string& path, const std::string& text, const char* key) const {
        RowValue value;
        value.path = path;
        value.value = text;
        value.line = Line(key);
        value.name = key;
        row.values.push_back(std::move(value));
    }
    // path names a row of table: text is resolved by ResolveRows, which
    // refuses a mod item's or flag's id.
    bool Ref(const std::string& path, const char* table, const std::string& text, const char* key) const {
        RowValue value;
        value.path = path;
        value.value = text;
        value.line = Line(key);
        value.name = key;
        value.refTable = table;
        value.flag = std::strcmp(table, "gameflag.dat") == 0;
        row.values.push_back(std::move(value));
        return true;
    }
    // A vanilla item: an id or an enum name, never a mod item's key.
    bool VanillaItem(const char* key, std::string& text) const {
        text = Value(key);
        if (text.empty()) {
            return Fail(nullptr, std::string("it needs ") + key + " = <a vanilla item id or enum name>");
        }
        if (!Digits(text) && !LooksLikeEnum(text)) {
            return Fail(key, std::string(key) + " " + text + " is not a vanilla item id or enum name; " + key +
                                 " takes no mod item");
        }
        return true;
    }
    void Count(const std::string& path, size_t elements, const char* key) const {
        RowValue count;
        count.path = path;
        count.name = std::string(key);
        count.line = Line(key);
        count.count = true;
        count.elements = static_cast<uint32_t>(elements);
        row.counts.push_back(std::move(count));
    }
    // An integer from low to high, fallback when the key is not given.
    bool Integer(const char* key, int32_t low, int32_t high, int32_t fallback, int32_t& out) const {
        const std::string text = Value(key);
        out = fallback;
        if (text.empty()) {
            return true;
        }
        if (!ParseI32(text, out) || out < low || out > high) {
            return Fail(key, std::string(key) + " must be a whole number from " + std::to_string(low) + " to " +
                                 std::to_string(high));
        }
        return true;
    }
    // A list of flags into the vector list, at most most of them.
    bool Flags(const char* key, const char* list, size_t most) const {
        const std::string text = Value(key);
        std::vector<std::string> flags;
        for (const std::string& part : text.empty() ? std::vector<std::string>() : SplitList(text)) {
            if (part.empty()) {
                return Fail(key, std::string(key) + " has an empty entry");
            }
            if (std::none_of(flags.begin(), flags.end(), [&](const std::string& other) { return other == part; })) {
                flags.push_back(part);
            }
        }
        if (flags.size() > most) {
            return Fail(key, std::string(key) + " names " + std::to_string(flags.size()) + " flags, and a " + row.kind +
                                 " takes " + std::to_string(most) + " at most");
        }
        Count(list, flags.size(), key);
        for (size_t index = 0; index < flags.size(); ++index) {
            if (!Ref(std::string(list) + "." + std::to_string(index), "gameflag.dat", flags[index], key)) {
                return false;
            }
        }
        return true;
    }
    // Warns about every key the section's kind does not have, but skip,
    // which the reader warns about itself.
    void Unknown(const FieldInfo* fields, size_t count, const char* skip = nullptr) const {
        for (size_t index = 0; index < section.values.size(); ++index) {
            const std::string& key = section.values[index].first;
            const bool known = std::any_of(fields, fields + count, [&](const FieldInfo& field) {
                return SameText(key, field.key);
            }) || (skip && SameText(key, skip));
            if (!known) {
                report.Warning(
                    folder.name, index < section.valueLines.size() ? section.valueLines[index] : section.line,
                    row.where + ": " + key + " is not a " + row.kind + " key, so it is ignored."
                );
            }
        }
    }
};

bool ReadSale(const Reader& in) {
    RowSpec& row = in.row;
    in.Unknown(kSaleFields, std::size(kSaleFields), "sets");
    if (!in.Value("sets").empty()) {
        in.report.Warning(
            in.folder.name, in.Line("sets"),
            row.where + ": sets is ignored, buying a mod sale turns no flag on. Use unless to stop listing it once a "
                        "flag is on."
        );
    }
    const std::string item = in.Value("item");
    if (item.empty()) {
        return in.Fail(nullptr, "it needs item = <a mod item's key, or a vanilla item id or enum name>");
    }
    if (!in.Ref("itemID", "item.dat", item, "item")) {
        return false;
    }
    if (in.Value("shop").empty()) {
        return in.Fail(nullptr, "it needs shop = <the storeID of a vanilla shop, such as 0>");
    }
    int32_t shop = 0;
    if (!in.Integer("shop", 0, kMaxShop, 0, shop)) {
        return false;
    }
    in.Set("storeID", std::to_string(shop), "shop");
    // The first vanilla row of the shop is the base: the shop must exist.
    row.basePath = "storeID";
    row.baseValue.path = "storeID";
    row.baseValue.name = "shop";
    row.baseValue.value = std::to_string(shop);
    row.baseValue.line = in.Line("shop");
    row.baseMissing = "no vanilla sale is in shop " + std::to_string(shop) + ", so the game has no such shop";

    const std::string stockText = in.Value("stock");
    int32_t stock = 0;
    if (!stockText.empty() && !SameText(stockText, "unlimited") &&
        (!ParseI32(stockText, stock) || stock < 1 || stock > kMaxStock)) {
        return in.Fail("stock", "stock must be a whole number from 1 to 99, or unlimited");
    }
    const std::string restockText = in.Value("restock");
    int restock = kRestockDaily;
    if (SameText(restockText, "never")) {
        restock = kRestockNever;
    } else if (!restockText.empty() && !SameText(restockText, "daily")) {
        return in.Fail("restock", "restock must be daily or never, not " + restockText);
    }
    if (stock == 0 && !restockText.empty()) {
        in.report.Warning(
            in.folder.name, in.Line("restock"), row.where + ": restock is ignored, the stock is unlimited."
        );
    }
    // The game reads a stock of 0 or less as unlimited, which never restocks.
    in.Set("stockInfo.stock", std::to_string(stock), "stock");
    in.Set("stockInfo.restock", std::to_string(stock == 0 ? kRestockNever : restock), "restock");

    std::vector<int> seasons;
    const std::string seasonsText = in.Value("seasons");
    if (!seasonsText.empty() && !SameText(seasonsText, "all")) {
        for (const std::string& part : SplitList(seasonsText)) {
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
    }
    // Every season is no condition at all, the way vanilla rows list one.
    if (seasons.size() == std::size(kSeasons)) {
        seasons.clear();
    }
    in.Count("condSeason", seasons.size(), "seasons");
    for (size_t index = 0; index < seasons.size(); ++index) {
        in.Set("condSeason." + std::to_string(index), std::to_string(seasons[index]), "seasons");
    }
    if (!in.Flags("flag", "condOnFlag", kMaxConditionFlags) || !in.Flags("unless", "condOffFlag", kMaxConditionFlags)) {
        return false;
    }
    // No release flags, whatever the base row has: see the notes at the top.
    in.Count("releaseValue", 0, "item");
    int32_t discount = 0;
    int32_t priority = kDefaultPriority;
    if (!in.Integer("discount", 0, kMaxDiscount, 0, discount) ||
        !in.Integer("priority", 0, kMaxPriority, kDefaultPriority, priority)) {
        return false;
    }
    in.Set("discountRate", std::to_string(discount), "discount");
    in.Set("discountPrice", "0", "discount");
    in.Set("priority", std::to_string(priority), "priority");
    const std::string silhouette = in.Value("silhouette");
    in.Set("showSilhouette", canopy_ini::Truthy(silhouette) ? "1" : "0", "silhouette");
    in.Set("size", "0", "item");
    row.zero = {"randomInfo"};
    return true;
}

// One material: item or item * count.
bool ReadNeed(const Reader& in, const std::string& text, size_t index) {
    std::string item = text;
    int32_t count = 1;
    const size_t star = text.find('*');
    if (star != std::string::npos) {
        item = canopy_ini::Trim(text.substr(0, star));
        if (!ParseI32(canopy_ini::Trim(text.substr(star + 1)), count) || count < 1 || count > kMaxNeedCount) {
            return in.Fail("needs", "needs " + text + " must be item or item * count, with a count of 1 to 99");
        }
    }
    if (item.empty()) {
        return in.Fail("needs", "needs has an entry without an item");
    }
    const std::string at = "needItem." + std::to_string(index);
    if (!in.Ref(at + ".itemID", "item.dat", item, "needs")) {
        return false;
    }
    in.Set(at + ".num", std::to_string(count), "needs");
    return true;
}

bool ReadUpgrade(const Reader& in) {
    RowSpec& row = in.row;
    in.Unknown(kUpgradeFields, std::size(kUpgradeFields));
    std::string from;
    std::string to;
    if (!in.VanillaItem("from", from) || !in.VanillaItem("to", to)) {
        return false;
    }
    if (from == to) {
        return in.Fail("to", "to names the tool from names; an upgrade makes another tool");
    }
    if (!in.Ref("srcItemID", "item.dat", from, "from") || !in.Ref("dstItemID", "item.dat", to, "to")) {
        return false;
    }
    // The vanilla upgrade that makes to is the base, for its box.
    row.basePath = "dstItemID";
    row.baseValue = row.values.back();
    row.baseMissing = "no vanilla upgrade makes " + to + ", so the blacksmith has no box to hand it over in";

    const std::string needs = in.Value("needs");
    const std::vector<std::string> parts = needs.empty() ? std::vector<std::string>() : SplitList(needs);
    if (parts.size() != kNeeds) {
        return in.Fail(
            needs.empty() ? nullptr : "needs",
            "it needs needs = <two materials, such as 220100, 210010 * 5>, as every vanilla upgrade has"
        );
    }
    for (size_t index = 0; index < parts.size(); ++index) {
        if (!ReadNeed(in, parts[index], index)) {
            return false;
        }
    }
    int32_t time = kDefaultTime;
    if (!in.Integer("time", 1, kMaxTime, kDefaultTime, time)) {
        return false;
    }
    in.Set("createTime", std::to_string(time), "time");
    return true;
}

}  // namespace

const SectionInfo kSaleSection = {kSalePrefix, "Sale", "docs/SHOPS.md", kSaleFields, std::size(kSaleFields)};
const SectionInfo kUpgradeSection = {
    kUpgradePrefix, "Upgrade", "docs/SHOPS.md", kUpgradeFields, std::size(kUpgradeFields)
};

bool IsShopSection(const canopy_ini::IniSection& section) {
    for (const char* prefix : {kSalePrefix, kUpgradePrefix}) {
        const size_t length = std::strlen(prefix);
        if (section.name.size() >= length && platform::CompareNoCase(section.name.c_str(), prefix, length) == 0) {
            return true;
        }
    }
    return false;
}

bool ReadShop(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    RowSpec& row
) {
    const bool sale = !SectionKey(section, kSalePrefix).empty() ||
                      platform::CompareNoCase(section.name.c_str(), kSalePrefix, std::strlen(kSalePrefix)) == 0;
    row.kind = sale ? kSaleKind : kUpgradeKind;
    row.key = SectionKey(section, sale ? kSalePrefix : kUpgradePrefix);
    row.modKey = modKey;
    row.folder = folder.name;
    row.line = section.line;
    row.schema = ts::FindPatchable(sale ? "storesales" : "blacksmith");
    row.where = "[" + modKey + "] " + (sale ? "Sale " : "Upgrade ") + row.key;
    row.fullKey = modKey + "." + row.key;
    row.label = std::string("Mod ") + row.kind + " " + row.fullKey;
    if (!row.schema) {
        report.Error(folder.name, section.line, row.where + " skipped, Canopy has no schema for its table.");
        return false;
    }
    if (!KeyValid(row.key)) {
        report.Error(
            folder.name, section.line, row.where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    const Reader in{folder, section, report, row};
    return sale ? ReadSale(in) : ReadUpgrade(in);
}

}  // namespace mod_rules
