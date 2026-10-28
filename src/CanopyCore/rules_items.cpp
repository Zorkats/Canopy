#include "rules_items.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "platform_files.h"
#include "rules_crops.h"
#include "rules_flags.h"
#include "rules_mod.h"

// =============================================================
// ITEM RULES
// =============================================================
// What the loader's custom items accept from canopy.ini, moved here from
// mod_items.cpp so the desktop tools check a mod the same way. Every message
// is the loader's log line.

namespace mod_rules {
namespace {

constexpr const char* kPrefix = "item.";
constexpr size_t kPrefixLength = 5;
constexpr size_t kMaxNameLength = 200;
constexpr size_t kMaxHelpLength = 1000;

// The item.dat row layout. Texts are 8-byte refs: 7 names, 7 brand names and
// 3 blocks of 7 helps. A row's tail is its last kTailSize bytes: the icon, the
// outline, the dyeing and table gimmick ids, then the version, disable
// version, region, product and platform, which gate DLC rows.
constexpr size_t kRowId = 0;
constexpr size_t kRowEnum = 8;
constexpr size_t kRowRefId = 16;
constexpr size_t kRowNames = 32;
constexpr size_t kRowBrandNames = 88;
constexpr size_t kRowHelps = 144;
constexpr size_t kHelpBlocks = 3;
constexpr size_t kTextRef = 8;
constexpr size_t kRowType = 312;
constexpr size_t kRowCategory = 320;
// buyingPrice; sellingPrice follows it.
constexpr size_t kRowBuyingPrice = 332;
constexpr size_t kRowPremium = 340;
constexpr size_t kRowEnergy = 360;
constexpr size_t kRowLifespan = 372;
constexpr size_t kRowLostLog = 376;
constexpr size_t kRowFeatureCount = 384;
constexpr size_t kRowFeatures = 388;
constexpr size_t kMaxFeatures = 64;
// From the action id: its value, 4 texts (gimmick script arg name, script
// module, script entry, script arg), then the effects, each an id, a count and
// that many 8-byte values.
constexpr size_t kActionValue = 8;
constexpr size_t kActionTexts = 16;
constexpr size_t kActionTextCount = 4;
constexpr size_t kActionHead = 48;
constexpr size_t kEffectCount = 4;
constexpr size_t kEffectHead = 12;
constexpr size_t kEffectValues = 8;
constexpr size_t kTailSize = 52;
constexpr size_t kTailIcon = 0;
constexpr size_t kTailOutline = 8;
constexpr size_t kTailDyeing = 16;
constexpr size_t kTailGimmick = 24;
constexpr size_t kMinRowSize = kRowFeatures + kActionHead + kEffectCount * kEffectHead + kTailSize;
// A consumable raw good: type 0, categories 2 to 15. A placeable is type 5.
constexpr uint64_t kTypeConsume = 0;
constexpr uint64_t kTypeGimmick = 5;
constexpr uint64_t kFirstRawCategory = 2;
constexpr uint64_t kLastRawCategory = 15;
// MATERIAL.
constexpr uint64_t kDefaultCategory = 2;
// action.dat: UseItem, Placement and Script.
constexpr uint64_t kActionUse = 1200;
constexpr uint64_t kActionPlace = 1400;
constexpr uint64_t kActionScript = 9000;
// itemeffect.dat: HEAL_HP, HEAL_ST and ITEM_CONSUME, in the order food has
// them.
constexpr uint64_t kEffectHealHp = 2;
constexpr uint64_t kEffectHealStamina = 3;
constexpr uint64_t kEffectConsume = 8;
// The most stamina or hp one item restores. Vanilla food stays below 420.
constexpr int32_t kMaxRestore = 999;
constexpr int32_t kMaxPrice = 999999;
// Most vanilla rows have premium 1; what it does is unknown.
constexpr int32_t kPremium = 1;

const FieldInfo kFields[] = {
    {"name", ValueKind::LangText, true,
     "The name the game shows, one line of up to 200 bytes. name.<lang> sets it for one language."},
    {"help", ValueKind::LangText, false,
     "The description, one line of up to 1000 bytes. help.<lang> sets it for one language."},
    {"base", ValueKind::VanillaId, false,
     "A vanilla item to copy: a consumable raw good (categories 2 to 15). Leave it out to build the item from "
     "defaults.",
     nullptr, "item.dat"},
    {"icon", ValueKind::IconRef, false,
     "The icon: a vanilla icon id, or a mod icon by key or Mod.Key. An item without a base needs one, unless it "
     "places an object."},
    {"use", ValueKind::Choice, false,
     "What using it from the bag does: nothing, eat it, run the mod's canopy.items.on_use handler, or place an "
     "object.",
     "none", nullptr, 0, 0, "none|consume|script|place"},
    {"stamina", ValueKind::Integer, false, "The stamina eating one restores, for use = consume.", nullptr, nullptr,
     1, kMaxRestore},
    {"hp", ValueKind::Integer, false, "The health eating one restores, for use = consume.", nullptr, nullptr, 1,
     kMaxRestore},
    {"consume", ValueKind::Switch, false, "For use = script: on takes one away each time it is used.", "off"},
    {"object", ValueKind::ObjectRef, false, "For use = place: the mod object it places, by key or Mod.Key."},
    {"category", ValueKind::VanillaId, false,
     "The item category. Defaults to the base's, to MATERIAL, or for use = place to its object's furniture "
     "category.",
     nullptr, "itemcategory.dat"},
    {"buy", ValueKind::Integer, false,
     "What a shop asks for it, 0 to 999999, before a sale's discount. Defaults to the base's price, or 0 without a "
     "base. A [sale.] section puts it in a shop.",
     nullptr, nullptr, 0, kMaxPrice},
    {"price", ValueKind::Other, false, "Ignored. Use buy for what a shop asks."},
    {"sell", ValueKind::Other, false, "Ignored. A mod item cannot be sold."},
    {"energy", ValueKind::Other, false, "Ignored. A mod item has no energy or lifespan."},
    {"count", ValueKind::Other, false, "Ignored. The game decides how many an item stacks."},
    {"type", ValueKind::Other, false, "Ignored. The type follows use."},
};

// In ItemUse order.
const char* const kUseNames[] = {"none", "consume", "script", "place"};

bool IsItemSection(const canopy_ini::IniSection& section) {
    return platform::CompareNoCase(section.name.c_str(), kPrefix, kPrefixLength) == 0;
}

bool HasItems(const ModFolder& folder) {
    return std::any_of(folder.sections.begin(), folder.sections.end(), IsItemSection);
}

// A restore amount: 1 to kMaxRestore, or 0 when left out. False, reported,
// when it is written wrong.
bool ReadRestore(
    const ModFolder& folder, const canopy_ini::IniSection& section, const char* key, const std::string& where,
    Report& report, int32_t& value
) {
    const std::string text = canopy_ini::Value(&section, key, "");
    value = 0;
    if (text.empty()) {
        return true;
    }
    if (!ParseI32(text, value) || value < 1 || value > kMaxRestore) {
        value = 0;
        report.Error(
            folder.name, canopy_ini::ValueLine(&section, key),
            where + " skipped, " + key + " must be a whole number from 1 to 999."
        );
        return false;
    }
    return true;
}

// Reads use and the keys that go with it. False, reported, when the item is
// skipped.
bool ReadUse(
    const ModFolder& folder, const std::string& modsDir, const std::string& modKey,
    const canopy_ini::IniSection& section, const Refs& refs, Report& report, const std::string& where,
    ItemSpec& item
) {
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    const std::string use = canopy_ini::Lower(canopy_ini::Trim(canopy_ini::Value(&section, "use", "")));
    if (!use.empty()) {
        const auto found = std::find(std::begin(kUseNames), std::end(kUseNames), use);
        if (found == std::end(kUseNames)) {
            report.Error(folder.name, at("use"), where + " skipped, use must be none, consume, script or place.");
            return false;
        }
        item.use = static_cast<ItemUse>(found - std::begin(kUseNames));
        item.useSet = true;
    }
    if (!ReadRestore(folder, section, "stamina", where, report, item.stamina) ||
        !ReadRestore(folder, section, "hp", where, report, item.hp)) {
        return false;
    }
    if (item.use == ItemUse::Consume && item.stamina == 0 && item.hp == 0) {
        report.Error(folder.name, at("use"), where + " skipped, use = consume needs stamina or hp.");
        return false;
    }
    if (item.use != ItemUse::Consume && (item.stamina != 0 || item.hp != 0)) {
        report.Warning(
            folder.name, at(item.stamina != 0 ? "stamina" : "hp"),
            where + ": stamina and hp are ignored, they only go with use = consume."
        );
        item.stamina = 0;
        item.hp = 0;
    }
    const std::string consume = canopy_ini::Value(&section, "consume", "");
    if (!consume.empty()) {
        if (item.use != ItemUse::Script) {
            report.Warning(folder.name, at("consume"), where + ": consume is ignored, it only goes with use = script.");
        } else if (!ParseSwitch(consume, item.consume)) {
            report.Warning(folder.name, at("consume"), where + ": consume must be on or off, so it is off.");
        }
    }
    if (item.use == ItemUse::Script && WithLuaScripts({folder}, modsDir).empty()) {
        report.Error(
            folder.name, at("use"), where + " skipped, use = script needs the mod's lua\\main.lua to handle it."
        );
        return false;
    }
    const std::string object = canopy_ini::Trim(canopy_ini::Value(&section, "object", ""));
    if (item.use != ItemUse::Place) {
        if (!object.empty()) {
            report.Warning(folder.name, at("object"), where + ": object is ignored, it only goes with use = place.");
        }
        return true;
    }
    if (object.empty()) {
        report.Error(folder.name, at("use"), where + " skipped, use = place needs object = <mod object>.");
        return false;
    }
    item.objectKey = FullKey(modKey, object);
    item.object = KeyRef(refs, RefKind::Object, modKey, object);
    if (item.object == 0) {
        report.Error(folder.name, at("object"), where + " skipped, no mod object is named " + object + ".");
        return false;
    }
    return true;
}

// Reads one [item.X] section. Reports and returns false when it is unusable.
bool ReadItem(
    const ModFolder& folder, const std::string& modsDir, const std::string& modKey,
    const canopy_ini::IniSection& section, const Refs& refs, Report& report, ItemSpec& item
) {
    item.modKey = modKey;
    item.folder = folder.name;
    item.key = section.name.substr(kPrefixLength);
    item.fullKey = modKey + "." + item.key;
    item.line = section.line;
    const std::string where = "[" + modKey + "] Item " + item.key;
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    if (!KeyValid(item.key)) {
        report.Error(
            folder.name, section.line, where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    const std::string base = canopy_ini::Value(&section, "base", "");
    if (!base.empty() && (!ParseU64(base, item.baseId) || item.baseId == 0)) {
        item.baseId = 0;
        report.Error(folder.name, at("base"), where + " skipped, base must be a vanilla item id.");
        return false;
    }
    if (!ReadUse(folder, modsDir, modKey, section, refs, report, where, item)) {
        return false;
    }
    // A vanilla icon id, else a mod icon by key or Mod.Key.
    const std::string icon = canopy_ini::Value(&section, "icon", "");
    if (!icon.empty() && (!ParseU64(icon, item.icon) || item.icon == 0)) {
        item.icon = KeyRef(refs, RefKind::Icon, modKey, icon);
        item.modIcon = item.icon != 0;
        if (!item.modIcon && item.baseId != 0) {
            report.Warning(
                folder.name, at("icon"),
                where + " keeps its base item's icon, icon must be a vanilla icon id or a mod icon."
            );
        }
    }
    if (item.baseId == 0 && item.icon == 0 && item.use != ItemUse::Place) {
        report.Error(
            folder.name, icon.empty() ? section.line : at("icon"),
            where + " skipped, an item without a base needs icon = <vanilla icon id or mod icon>."
        );
        return false;
    }
    ReadTexts(section, "name", item.names);
    ReadTexts(section, "help", item.helps);
    for (int language = 0; language < kLanguageCount; ++language) {
        const bool nameValid = TextValid(item.names[language], kMaxNameLength);
        if (item.names[language].empty()) {
            report.Error(folder.name, at("name"), where + " skipped, it needs name = <text>.");
            return false;
        }
        if (!nameValid || !TextValid(item.helps[language], kMaxHelpLength)) {
            report.Error(
                folder.name, at(nameValid ? "help" : "name"),
                where + " skipped, a name must be one line of up to 200 bytes and a help one line of up to 1000."
            );
            return false;
        }
    }
    const std::string category = canopy_ini::Value(&section, "category", "");
    if (!category.empty() &&
        (!ParseU64(category, item.category) || item.category == 0 || item.category >= kFirstModId)) {
        item.category = 0;
        report.Warning(
            folder.name, at("category"), where + ": category must be a vanilla item category id, so the default is used."
        );
    }
    const std::string buy = canopy_ini::Value(&section, "buy", "");
    if (!buy.empty() && (!ParseI32(buy, item.buy) || item.buy < 0 || item.buy > kMaxPrice)) {
        item.buy = -1;
        report.Warning(
            folder.name, at("buy"),
            where + ": buy must be a whole number from 0 to 999999, so " +
                (item.baseId != 0 ? "the base's price is kept." : "the item is free.")
        );
    }
    if (!canopy_ini::Value(&section, "price", "").empty()) {
        report.Warning(folder.name, at("price"), where + ": price is ignored, buy sets what a shop asks.");
    }
    for (const char* ignored : {"sell", "energy", "count", "type"}) {
        if (!canopy_ini::Value(&section, ignored, "").empty()) {
            report.Warning(
                folder.name, at(ignored),
                where + ": " + ignored + " is ignored, mod items cannot be sold and have no energy."
            );
        }
    }
    return true;
}

size_t ActionOffset(const blob::Row& row) {
    return kRowFeatures + 8 * static_cast<size_t>(blob::Get<uint32_t>(row.data, kRowFeatureCount));
}

// True when the base row is a consumable raw good, which an item copies, with
// no use action unless the item sets its own. A seed copies a vanilla seed
// too, whose Plant action it replaces. The row must have a tail, so every
// read below is inside it.
bool CopyableBase(const blob::Row& base, bool ownUse, bool seed) {
    if (!TailOffset(base)) {
        return false;
    }
    const uint64_t action = blob::Get<uint64_t>(base.data, ActionOffset(base));
    const uint64_t category = blob::Get<uint64_t>(base.data, kRowCategory);
    if (blob::Get<uint64_t>(base.data, kRowType) != kTypeConsume) {
        return false;
    }
    if (seed && category == kSeedCategory && action == kActionPlant) {
        return true;
    }
    return category >= kFirstRawCategory && category <= kLastRawCategory && (ownUse || seed || action == 0);
}

// Appends an effect: its id, then one value, or none when value is null.
void PutEffect(std::vector<uint8_t>& row, uint64_t id, const int64_t* value) {
    const size_t at = row.size();
    row.resize(at + kEffectHead + (value ? kEffectValues : 0), 0);
    blob::Put<uint64_t>(row.data(), at, id);
    blob::Put<uint32_t>(row.data(), at + 8, value ? 1 : 0);
    if (value) {
        blob::Put<int64_t>(row.data(), at + kEffectHead, *value);
    }
}

// Appends the action of item's use: the action id, its value, its 4 texts
// and its 4 effects.
void PutAction(const ItemSpec& item, const ItemRowParts& parts, const TextWriter& write, std::vector<uint8_t>& row) {
    const size_t action = row.size();
    row.resize(action + kActionHead, 0);
    const bool script = item.use == ItemUse::Script;
    uint64_t id = 0;
    if (item.use == ItemUse::Consume) {
        id = kActionUse;
    } else if (script) {
        id = kActionScript;
    } else if (item.use == ItemUse::Place) {
        id = kActionPlace;
        blob::Put<uint64_t>(row.data(), action + kActionValue, parts.gimmick);
    } else if (parts.crop != 0) {
        id = kActionPlant;
        blob::Put<uint64_t>(row.data(), action + kActionValue, parts.crop);
    }
    blob::Put<uint64_t>(row.data(), action, id);
    write(row, action + kActionTexts, "");
    write(row, action + kActionTexts + kTextRef, script ? parts.module : "");
    write(row, action + kActionTexts + 2 * kTextRef, script ? kItemScriptEntry : "");
    write(row, action + kActionTexts + 3 * kTextRef, "");
    size_t effects = 0;
    if (item.use == ItemUse::Consume) {
        const int64_t hp = item.hp;
        const int64_t stamina = item.stamina;
        if (hp != 0) {
            PutEffect(row, kEffectHealHp, &hp);
            ++effects;
        }
        if (stamina != 0) {
            PutEffect(row, kEffectHealStamina, &stamina);
            ++effects;
        }
        PutEffect(row, kEffectConsume, nullptr);
        ++effects;
    }
    for (; effects < kEffectCount; ++effects) {
        PutEffect(row, 0, nullptr);
    }
}

}  // namespace

const SectionInfo kItemSection = {kPrefix, "Item", "docs/ITEMS.md", kFields, std::size(kFields)};

const uint64_t kModItemFeatures[6] = {5, 6, 7, 8, 69, 71};

const char* ItemUseName(ItemUse use) {
    const size_t index = static_cast<size_t>(use);
    return index < std::size(kUseNames) ? kUseNames[index] : "?";
}

std::vector<ModFolder> WithItems(const std::vector<ModFolder>& folders) {
    std::vector<ModFolder> wanted;
    for (const ModFolder& folder : folders) {
        if (HasItems(folder)) {
            wanted.push_back(folder);
        }
    }
    return wanted;
}

std::vector<ItemSpec> ReadItems(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
) {
    std::vector<ItemSpec> all;
    std::vector<std::string> owners;
    // The objects an item already places, by Mod.Key.
    std::vector<std::string> placed;
    const auto isPlaced = [&](const std::string& objectKey) {
        return std::any_of(placed.begin(), placed.end(), [&](const std::string& other) {
            return platform::CompareNoCase(other.c_str(), objectKey.c_str()) == 0;
        });
    };
    for (const ModFolder& folder : folders) {
        if (!HasItems(folder)) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Items in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Items in folder " + folder.name + " skipped, another folder already has items under the key " +
                    modKey + "."
            );
            continue;
        }
        std::vector<ItemSpec> items;
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (!IsItemSection(section)) {
                continue;
            }
            ItemSpec item;
            if (!ReadItem(folder, modsDir, modKey, section, refs, report, item)) {
                continue;
            }
            const auto same = [&](const ItemSpec& other) {
                return platform::CompareNoCase(other.key.c_str(), item.key.c_str()) == 0;
            };
            if (std::any_of(items.begin(), items.end(), same)) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Item " + item.key + " skipped, the mod already has an item with that key."
                );
                continue;
            }
            const bool placedHere = std::any_of(items.begin(), items.end(), [&](const ItemSpec& other) {
                return other.use == ItemUse::Place &&
                       platform::CompareNoCase(other.objectKey.c_str(), item.objectKey.c_str()) == 0;
            });
            if (item.use == ItemUse::Place && (placedHere || isPlaced(item.objectKey))) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Item " + item.key + " skipped, another item already places the object " +
                        item.objectKey + "."
                );
                continue;
            }
            if (all.size() + items.size() >= kMaxItems) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Item " + item.key + " skipped, Canopy takes at most 1000 mod items."
                );
                break;
            }
            items.push_back(std::move(item));
        }
        if (items.empty() || !accept(folder, modKey, items.size())) {
            continue;
        }
        owners.push_back(modKey);
        for (ItemSpec& item : items) {
            if (item.use == ItemUse::Place) {
                placed.push_back(item.objectKey);
            }
            all.push_back(std::move(item));
        }
    }
    return all;
}

size_t TailOffset(const blob::Row& row) {
    if (row.size < kMinRowSize) {
        return 0;
    }
    const uint64_t features = blob::Get<uint32_t>(row.data, kRowFeatureCount);
    const size_t tail = row.size - kTailSize;
    size_t offset = kRowFeatures + 8 * features + kActionHead;
    if (features > kMaxFeatures || offset > tail) {
        return 0;
    }
    for (size_t effect = 0; effect < kEffectCount; ++effect) {
        if (offset + kEffectHead > tail) {
            return 0;
        }
        offset += kEffectHead + kEffectValues * blob::Get<uint32_t>(row.data, offset + 8);
    }
    return offset == tail ? tail : 0;
}

void AddIconOutline(const blob::Row& row, IconOutlines& outlines) {
    if (row.size < kMinRowSize) {
        return;
    }
    const size_t tail = row.size - kTailSize;
    outlines.emplace(
        blob::Get<uint64_t>(row.data, tail + kTailIcon), blob::Get<uint64_t>(row.data, tail + kTailOutline)
    );
}

bool CheckAgainstTable(const ItemSpec& item, const blob::Row* base, Report& report, bool seed) {
    if (item.baseId == 0) {
        return true;
    }
    const std::string where = "Mod item " + item.fullKey;
    if (!base) {
        report.Error(
            item.folder, item.line,
            where + " skipped, its base " + std::to_string(item.baseId) + " is not in the item table."
        );
        return false;
    }
    if (!CopyableBase(*base, item.useSet, seed)) {
        report.Error(
            item.folder, item.line,
            where + " skipped, its base must be a consumable raw good (categories 2 to 15)" +
                (seed ? " or a seed." : item.useSet ? "." : " with no use, unless the item sets use.")
        );
        return false;
    }
    return true;
}

bool PlacesGimmick(const blob::Row& item, uint64_t gimmick) {
    if (!TailOffset(item) || blob::Get<uint64_t>(item.data, kRowType) != kTypeGimmick) {
        return false;
    }
    const size_t action = ActionOffset(item);
    return blob::Get<uint64_t>(item.data, action) == kActionPlace &&
           blob::Get<uint64_t>(item.data, action + kActionValue) == gimmick;
}

const blob::Row* FurnitureItem(const blob::Table& items, uint64_t gimmick) {
    const auto found = std::find_if(items.rows.begin(), items.rows.end(), [&](const blob::Row& row) {
        return PlacesGimmick(row, gimmick);
    });
    return found == items.rows.end() ? nullptr : &*found;
}

uint64_t RowCategory(const blob::Row& row) {
    return row.size >= kRowCategory + 8 ? blob::Get<uint64_t>(row.data, kRowCategory) : 0;
}

std::vector<uint8_t> BuildItemRow(const ItemSpec& item, const ItemRowParts& parts, const TextWriter& write) {
    const blob::Row* base = parts.base;
    // A base keeps its own action when the item sets no use and is no seed.
    const bool baseAction = base && !item.useSet && parts.crop == 0;
    std::vector<uint8_t> row;
    if (base) {
        row.assign(base->data, base->data + kRowFeatureCount);
    } else {
        row.assign(kRowFeatureCount, 0);
        blob::Put<int32_t>(row.data(), kRowPremium, kPremium);
    }
    blob::Put<uint64_t>(row.data(), kRowId, parts.id);
    write(row, kRowEnum, parts.enumName);
    blob::Put<uint64_t>(row.data(), kRowRefId, 0);
    constexpr size_t kLanguages = kLanguageCount;
    for (size_t language = 0; language < kLanguages; ++language) {
        write(row, kRowNames + kTextRef * language, item.names[language]);
        write(row, kRowBrandNames + kTextRef * language, "");
        for (size_t block = 0; block < kHelpBlocks; ++block) {
            const size_t offset = kRowHelps + kTextRef * (kLanguages * block + language);
            write(row, offset, block == 0 ? item.helps[language] : "");
        }
    }
    // energy, maxEnergy, lostWhenEmpty and lifespan.
    for (size_t offset = kRowEnergy; offset <= kRowLifespan; offset += 4) {
        blob::Put<int32_t>(row.data(), offset, 0);
    }
    blob::Put<uint64_t>(row.data(), kRowLostLog, 0);
    if (item.buy >= 0) {
        blob::Put<int32_t>(row.data(), kRowBuyingPrice, item.buy);
    }
    if (!baseAction) {
        blob::Put<uint64_t>(row.data(), kRowType, item.use == ItemUse::Place ? kTypeGimmick : kTypeConsume);
    }
    uint64_t category = item.category;
    if (category == 0 && item.use == ItemUse::Place) {
        category = parts.placeCategory;
    }
    if (category == 0 && parts.crop != 0) {
        category = kSeedCategory;
    }
    if (category == 0) {
        category = base ? RowCategory(*base) : kDefaultCategory;
    }
    blob::Put<uint64_t>(row.data(), kRowCategory, category);
    const uint32_t featureCount = static_cast<uint32_t>(std::size(kModItemFeatures));
    row.resize(kRowFeatures + 8 * featureCount);
    blob::Put<uint32_t>(row.data(), kRowFeatureCount, featureCount);
    for (uint32_t index = 0; index < featureCount; ++index) {
        blob::Put<uint64_t>(row.data(), kRowFeatures + 8 * index, kModItemFeatures[index]);
    }
    const size_t baseTail = base ? TailOffset(*base) : 0;
    if (baseAction) {
        const size_t action = row.size();
        row.insert(row.end(), base->data + ActionOffset(*base), base->data + baseTail);
        for (size_t text = 0; text < kActionTextCount; ++text) {
            write(row, action + kActionTexts + kTextRef * text, "");
        }
    } else {
        PutAction(item, parts, write, row);
    }
    const size_t tail = row.size();
    if (base) {
        row.insert(row.end(), base->data + baseTail, base->data + base->size);
    } else {
        row.resize(tail + kTailSize, 0);
    }
    blob::Put<uint64_t>(row.data(), tail + kTailIcon, parts.icon);
    blob::Put<uint64_t>(row.data(), tail + kTailOutline, parts.outline);
    blob::Put<uint64_t>(row.data(), tail + kTailDyeing, 0);
    blob::Put<uint64_t>(row.data(), tail + kTailGimmick, item.use == ItemUse::Place ? parts.gimmick : 0);
    return row;
}

bool CheckVanillaIcon(const ItemSpec& item, const IconOutlines& outlines, Report& report) {
    if (outlines.count(item.icon)) {
        return true;
    }
    report.Warning(
        item.folder, item.line,
        "Mod item " + item.fullKey + (item.baseId != 0 ? " keeps its base item's icon" : " has no icon") +
            ", no vanilla item uses icon " + std::to_string(item.icon) + "."
    );
    return false;
}

void CheckItems(Context& context) {
    const std::vector<ItemSpec> items =
        ReadItems(context.folders, context.modsDir, context.refs, context.report, AcceptAll);
    for (const ItemSpec& item : items) {
        context.refs.Add(RefKind::Item, item.fullKey);
    }
    const blob::Table* table = context.vanilla ? context.vanilla->Table("item.dat") : nullptr;
    if (!table) {
        return;
    }
    IconOutlines outlines;
    for (const blob::Row& row : table->rows) {
        AddIconOutline(row, outlines);
    }
    const bool haveCategories = context.vanilla->Table("itemcategory.dat") != nullptr;
    const std::set<std::string> seeds = SeedKeys(context.folders);
    for (const ItemSpec& item : items) {
        const auto base = std::find_if(table->rows.begin(), table->rows.end(), [&](const blob::Row& row) {
            return blob::RowId(row) == item.baseId;
        });
        const bool found = base != table->rows.end();
        const bool seed = seeds.count(canopy_ini::Lower(item.fullKey)) != 0;
        if (!CheckAgainstTable(item, found ? &*base : nullptr, context.report, seed)) {
            continue;
        }
        if (item.category != 0 && haveCategories && !context.vanilla->HasRow("itemcategory.dat", item.category)) {
            context.report.Warning(
                item.folder, item.line,
                "Mod item " + item.fullKey + " uses the default category, no item category has the id " +
                    std::to_string(item.category) + "."
            );
        }
        if (!item.modIcon && item.icon != 0) {
            CheckVanillaIcon(item, outlines, context.report);
        }
    }
}

}  // namespace mod_rules
