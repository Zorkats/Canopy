#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "mod_rules.h"

// The rules of [item.<Key>] sections. See docs/ITEMS.md.
namespace mod_rules {

// What a mod item does when the player uses it from the bag.
enum class ItemUse : uint8_t {
    // Nothing: a material, an ingredient or a gift.
    None,
    // Eaten: restores stamina and hp, then one is gone.
    Consume,
    // Runs the mod's canopy.items.on_use handler.
    Script,
    // Places the mod object it names, which gives it back when picked up.
    Place,
};

// What an [item.<Key>] section says. The loader adds the id the item is given.
struct ItemSpec {
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as the registry and the sidecars write it.
    std::string fullKey;
    int line = 0;
    // The vanilla item the item copies, 0 for an item built from defaults.
    uint64_t baseId = 0;
    ItemUse use = ItemUse::None;
    // True when use was written. An item with a base and no use keeps the
    // base's use, which is none.
    bool useSet = false;
    // Consume: what eating one restores, 0 for nothing.
    int32_t stamina = 0;
    int32_t hp = 0;
    // Script: one is gone each time it is used.
    bool consume = false;
    // The itemcategory.dat id, 0 for the default: the base's, MATERIAL, or
    // for a placed item its object's furniture category.
    uint64_t category = 0;
    // Place: the object's Mod.Key and id.
    std::string objectKey;
    uint64_t object = 0;
    std::string names[kLanguageCount];
    std::string helps[kLanguageCount];
    // The icon the item shows instead of its base item's, 0 for the base's.
    // A mod icon when modIcon is set, else a vanilla icon id.
    uint64_t icon = 0;
    bool modIcon = false;
    // What a shop asks for it before any discount, -1 for the base's price,
    // or 0 without a base. A [sale.] section puts it in a shop.
    int32_t buy = -1;
};

// The name use = takes for use: none, consume, script or place.
const char* ItemUseName(ItemUse use);

// item.dat: the action of a seed, whose value is the crops.dat id of the crop
// it plants, and the SEED category.
constexpr uint64_t kActionPlant = 1060;
constexpr uint64_t kSeedCategory = 1;

// The most mod items Canopy takes in all.
constexpr size_t kMaxItems = 1000;

extern const SectionInfo kItemSection;

// Reads every [item.X] section of folders, in folder and file order, as the
// loader installs them: per mod key the first folder owns it, keys are unique
// per mod, and at most kMaxItems load. modsDir is only read to tell whether
// a folder has Lua, which use = script needs. Objects are read first, so
// refs knows them. accept sees each folder with usable items.
std::vector<ItemSpec> ReadItems(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
);
// The folders with an [item.X] section, a bare [item.] included. The loader
// scans with this, since FindFolders would drop a folder whose only item
// section is bare.
std::vector<ModFolder> WithItems(const std::vector<ModFolder>& folders);

// ---- The item table ------------------------------------------------------

// Icon id to the outline id of the first row that has the icon.
using IconOutlines = std::unordered_map<uint64_t, uint64_t>;

// Where the tail of an item.dat row starts, 0 when the row's lengths do not
// add up. The tail is the row's last 52 bytes: the icon, then the outline.
size_t TailOffset(const blob::Row& row);
// Adds a row's icon and outline unless the icon is there. A row too short for
// a tail adds nothing.
void AddIconOutline(const blob::Row& row, IconOutlines& outlines);
// The checks against the vanilla item table the loader runs when it appends
// rows, for an item with a base: the base row is there and is a consumable
// raw good, with no use action unless the item sets its own. A seed, an item
// a [crop.] plants with, may copy a vanilla seed too, since its action is
// replaced. base is null when the table has no row baseId. False, reported,
// when the item is skipped.
bool CheckAgainstTable(const ItemSpec& item, const blob::Row* base, Report& report, bool seed = false);
// True when the item row places gimmick: a placeable item (type 5, action
// 1400) whose action value is gimmick.
bool PlacesGimmick(const blob::Row& item, uint64_t gimmick);
// The first vanilla item that places gimmick, null when there is none. A
// placed mod item takes its category.
const blob::Row* FurnitureItem(const blob::Table& items, uint64_t gimmick);
// An item row's category.
uint64_t RowCategory(const blob::Row& row);

// ---- Building a row ------------------------------------------------------

// Writes a text ref at offset in row, adding the text to the table's pool.
using TextWriter = std::function<void(std::vector<uint8_t>& row, size_t offset, const std::string& text)>;

// What a row needs besides the spec.
struct ItemRowParts {
    uint64_t id = 0;
    std::string enumName;
    // The base row, which must have a tail, or null to build from defaults.
    const blob::Row* base = nullptr;
    uint64_t icon = 0;
    uint64_t outline = 0;
    // Place: the gimmick id, and the category used when the item sets none.
    uint64_t gimmick = 0;
    uint64_t placeCategory = 0;
    // Script: the module the game requires, whose main runs the handler.
    std::string module;
    // The crop the item plants, 0 for an item that is no seed. A seed is a
    // type 0 item of the SEED category, unless it sets its own, whose action
    // is Plant with the crop's id: the item's own use is none.
    uint64_t crop = 0;
};

// The features every mod item has: NOT_SALE, NOT_DISCARD, NOT_STORED,
// NOT_PUBLISH_ENCYCLOPEDIA, 69 (no encyclopedia popup) and 71 (out of the
// encyclopedia completion count).
extern const uint64_t kModItemFeatures[6];
// The entry a script item's module runs.
constexpr const char* kItemScriptEntry = "main";

// The item.dat row of item: a copy of the base, or a row of defaults, with
// the item's id, texts, features, icon and use.
std::vector<uint8_t> BuildItemRow(const ItemSpec& item, const ItemRowParts& parts, const TextWriter& write);
// For an item with a vanilla icon: some vanilla item uses it. False, reported
// as a Warning, when none does, and the item keeps its base item's icon.
bool CheckVanillaIcon(const ItemSpec& item, const IconOutlines& outlines, Report& report);

// The desktop check: reads the items, registers their keys, then checks them
// against the vanilla table when there is one.
void CheckItems(Context& context);

}  // namespace mod_rules
