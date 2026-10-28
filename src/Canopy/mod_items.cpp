#define NOMINMAX
#include "mod_items.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "core.h"
#include "game_events.h"
#include "hook_chain.h"
#include "lua_host.h"
#include "mod_art.h"
#include "mod_loader.h"
#include "mod_objects.h"
#include "mod_tables.h"
#include "rules_crops.h"
#include "rules_items.h"
#include "save_overlay.h"
#include "table_engine.h"

// =============================================================
// CUSTOM ITEMS DESIGN
// =============================================================
// A mod defines items in its canopy.ini, one [item.Key] section each, as a
// copy of a vanilla item or built from defaults, with its own name, help,
// icon and use. A folder with items and no Lua or DLL is still registered as
// a mod. With no items anywhere, nothing below is hooked.
//
// Rules. What an [item.Key] section accepts is CanopyCore's rules_items,
// shared with the desktop tools; this file installs what they read.
//
// Ids. Each item gets 1500000000 + n, where n comes from
// mods\CanopyData\items.ini: the first time Canopy sees Mod.Key it gives it
// the next number and writes the file. A number is never given out twice,
// even after its mod is gone, so a sidecar line never points at another
// mod's item.
//
// Table. CDatabase<ItemData>::load 0x83180(db, name, blob) parses a table
// file and appends its rows. The game calls it once for the item table, on
// the boot worker. Calling it a second time with only the mod rows would
// grow and move the record vector and rebuild the id and name maps around
// it, so instead the detour builds one blob that holds the vanilla rows,
// then the mod rows, and calls the original once with it. CanopyCore's
// BuildItemRow writes each mod row: the base item's row or a row of
// defaults, with a new id, name, help and enum name, the features NOT_SALE,
// NOT_DISCARD, NOT_STORED, NOT_PUBLISH_ENCYCLOPEDIA, 69 (no encyclopedia
// popup) and 71 (no encyclopedia completion count), no energy or lifespan,
// no brand name, and the item's use. A base must be a consumable raw good
// (categories 2 to 15). The icon is the item's own, else its base's, else,
// for a place item, the icon of the vanilla item that places its object's
// clone, whose category it takes too. The blob is the game's own format, so
// the game parses the mod rows exactly like its own. At the title the tick
// looks each id up through the game's own find and checks its category,
// icons and, for a place item, its object's gimmick row; an item that fails
// stays off.
//
// Use. The hotbar runs the row's action. consume (1200) eats it with the
// game's own effects, and place (1400) puts down the mod object. script
// (9000) has the game require canopyitem_<mod>_<key> and call its main.
// The script loader 0x73BA90 is detoured, chained after the Lua host's,
// the villagers' and the whistles' detours, and serves every such module
// from one chunk whose main hands the item's Mod.Key to Canopy. Canopy then
// takes one from the bag through the game's own subtract 0x1560E0 when the
// item says consume, under the acquisition guard, and queues "use:<Key>"
// for the mod's Lua, whose canopy.items.on_use handler runs on its next
// update.
//
// Acquisition. The only way in is canopy.items.give, which builds a
// CItemStatus like GameAddInventoryItem does and calls the game's own
// silent add 0x155730. While that add runs for a mod item, a guard on the
// main thread makes the letter and quest counters (0x10E970), the
// encyclopedia (0x1574D0) and every story flag write (0x147980, which
// includes the add's own flag 700) do nothing. The counter and
// encyclopedia detours also refuse a mod item id outside the guard. Vanilla
// items never see the guard.
//
// The mail order catalog list (save data +14488) is the only way items reach
// a vanilla letter: at the day change the game mails its items as letter 212
// and clears it. Its one writer, the catalog menu, builds each entry from a
// catalog row's item id, and no catalog row names a mod item, so a mod item
// never enters it. The save strip covers the list anyway.
//
// Save. Items are one adapter of the overlay save core (save_overlay.cpp),
// with the highest strip order, so they are stripped last and restored
// first. The strip moves every mod item status out of the inventory, tool
// slots and the other item containers into a list Canopy holds, and erases
// mod ids from the id-keyed maps, so the written save never holds one. A
// status taken out of a vector keeps its index there and goes back to it.
// Once the write is done the core puts the items back in their slots, and
// when the save utility reports the save done and good, the list as it was
// at the strip goes to mods\CanopyData\slot<N>.items.ini. A failed save
// writes nothing. A finished load hands that file over, and once the save
// is loaded the items are rebuilt in their slots without any side effect.
// Lines for items that are not installed are kept and written back.
// Deleting the slot deletes the file.
//
// Threads. The table detour runs on the boot worker and only touches the
// blob, the item definitions and the log. Everything else runs on the main
// thread; the overlay save core brings the save's write and the save
// utility's reports there.

namespace mod_items {
namespace {

using namespace core;

using mod_tables::EnumPart;
using mod_tables::FindRecord;
using mod_tables::ParseI32;
using mod_tables::Plural;

using DatabaseLoadFn = int64_t(__fastcall*)(uint8_t* db, const char* name, const uint8_t* blob);
using AddInventoryItemFn = int32_t(__fastcall*)(uint8_t* saveData, uint8_t** status, uint32_t* slot, uint8_t dryRun);
using LetterConditionFn = int64_t(__fastcall*)(void* letters, int32_t kind, int32_t target, int64_t value, int32_t count);
using EncyclopediaReleaseFn = uint8_t(__fastcall*)(uint8_t* saveData, uint64_t id, uint32_t rank, uint8_t notify);
using SetSaveFlagFn = uint64_t(__fastcall*)(uint8_t* saveData, int64_t flag);
using ItemStatusCtorFn = uint8_t*(__fastcall*)(uint8_t* status, uint8_t* saveData, uint64_t id);
using DatabaseFn = uint8_t*(__fastcall*)(void* registry);
using PushLogFn = void*(__fastcall*)(
    uint8_t* manager, void* source, int32_t tag, uint32_t count, int64_t logId, int64_t* parent
);
using MapNodeFreeFn = void(__fastcall*)(void* unused, void* node);
using SubInventoryItemFn = uint8_t(__fastcall*)(uint8_t* saveData, int32_t slot, int32_t count);

struct lua_State;
using LuaCFunction = int (*)(lua_State* state);
using LuaSetTopFn = void(__fastcall*)(lua_State* state, int index);
using LuaTypeFn = int(__fastcall*)(lua_State* state, int index);
using LuaToLStringFn = const char*(__fastcall*)(lua_State* state, int index, size_t* length);
using LuaPushStringFn = const char*(__fastcall*)(lua_State* state, const char* text);
using LuaPushCClosureFn = void(__fastcall*)(lua_State* state, LuaCFunction function, int upvalues);
using LuaPCallKFn = int(__fastcall*)(
    lua_State* state, int argumentCount, int resultCount, int handler, intptr_t context, void* continuation
);
using LuaLoadBufferXFn = int(__fastcall*)(
    lua_State* state, const char* buffer, size_t size, const char* name, const char* mode
);

// ---- item.dat row format -------------------------------------------------
// Blob: u32 row count, u32 row bytes, u32 pool bytes, then each row as a
// u32 length and its bytes, then the string pool. Row bytes leave out the
// length prefixes. A text is a u32 pool offset and a u32 length; every
// pool string ends with a NUL.

constexpr size_t kBlobHeader = 12;
constexpr size_t kRowEnum = 8;
constexpr size_t kRowFeatures = 388;
// The tail, the row's last 52 bytes, starts with the icon and the outline.
// CanopyCore's rules_items has the rest of the row.
constexpr size_t kTailIcon = 0;
constexpr size_t kTailOutline = 8;

constexpr const char* kItemTableName = "data/database/item.dat";
constexpr uint64_t kBandBase = 1500000000;
constexpr uint64_t kBandSize = 100000000;
constexpr int32_t kMaxCount = 999;
constexpr int32_t kMaxRank = 100;
constexpr size_t kMaxSidecarLines = 4096;
// The highest overlay save order, so items strip last and restore first.
constexpr int kOverlayOrder = 100;
// A script item's module: canopyitem_<mod>_<key>.
constexpr const char* kModulePrefix = "canopyitem_";
// The loader's chain owner, after the Lua host (-1), villagers (-2) and
// whistles (-3).
constexpr int64_t kLoaderChainOwner = -4;
constexpr int kTypeString = 4;
constexpr int kTypeTable = 5;
// Every script item's module: main hands the item's Mod.Key to Canopy.
constexpr const char* kModuleSource = "local use, name = ... return { main = function() use(name) end }";
constexpr const char* kRegistryName = "items.ini";
constexpr const char* kRegistryHeader =
    "; Canopy custom item ids. Each mod item keeps its number for good, and a number is never given out\r\n"
    "; twice. Do not edit or delete this file: saved mod items refer to these numbers.\r\n";

// What the rules read, and what installing it adds.
struct ItemDef : mod_rules::ItemSpec {
    uint64_t id = 0;
    std::string enumName;
    // Script: the module the row names, and its chunk name.
    std::string module;
    std::string chunkName;
    // The crop it plants, from mod crops' Install: its action becomes Plant.
    uint64_t crop = 0;
    // Set by the table detour before g_TableDone.
    bool appended = false;
    // Set at the title once the game's own lookup finds it.
    bool enabled = false;
};

// Where a stripped status came from.
enum class Place { Inventory, Tool, Vector, Drop };

struct Held {
    uint8_t* status;
    Place place;
    // Slot index for Inventory and Tool, the vector's offset for Vector.
    size_t where;
    // The list node for Drop.
    uint8_t* node;
    // The index in its vector before the strip, for Vector.
    size_t index = 0;
};

// One line of a sidecar.
struct Entry {
    std::string fullKey;
    std::string place;
    int32_t index = -1;
    int32_t count = 1;
    int32_t rank = 0;
    std::string raw;
};

std::vector<ItemDef> g_Items;
std::unordered_map<uint64_t, size_t> g_ById;
size_t g_ModCount = 0;
bool g_Active = false;

DatabaseLoadFn g_OriginalLoad = nullptr;
AddInventoryItemFn g_OriginalAdd = nullptr;
LetterConditionFn g_OriginalLetter = nullptr;
EncyclopediaReleaseFn g_OriginalEncyclopedia = nullptr;
SetSaveFlagFn g_OriginalSetFlag = nullptr;
LuaCFunction g_OriginalLoader = nullptr;
bool g_LoaderHooked = false;

struct LuaApi {
    LuaSetTopFn setTop;
    LuaTypeFn type;
    LuaToLStringFn toLString;
    LuaPushStringFn pushString;
    LuaPushCClosureFn pushCClosure;
    LuaPCallKFn pcallK;
    LuaLoadBufferXFn loadBufferX;
};
LuaApi g_Lua = {};

// Table load, boot worker.
std::atomic<bool> g_TableSeen{false};
std::atomic<bool> g_TableDone{false};
std::atomic<bool> g_InLoad{false};
std::atomic<bool> g_LoggedReload{false};
bool g_Checked = false;

// Acquisition guard: depth and the thread that holds it.
std::atomic<int32_t> g_GuardDepth{0};
std::atomic<DWORD> g_GuardThread{0};

// Save splice, main thread.
std::vector<Held> g_Stripped;
std::vector<uint8_t*> g_Pending;
std::vector<std::string> g_Carried;
std::vector<Entry> g_Loaded;
bool g_ApplyPending = false;
int32_t g_LoadedSlot = 0;
uint64_t g_LastRetry = 0;

// ---- Small helpers -------------------------------------------------------

bool IsBand(uint64_t id) {
    return id >= kBandBase && id < kBandBase + kBandSize;
}

const ItemDef* FindById(uint64_t id) {
    const auto found = g_ById.find(id);
    return found == g_ById.end() ? nullptr : &g_Items[found->second];
}

const ItemDef* FindByKey(const std::string& fullKey) {
    for (const ItemDef& item : g_Items) {
        if (_stricmp(item.fullKey.c_str(), fullKey.c_str()) == 0) {
            return &item;
        }
    }
    return nullptr;
}

uint8_t* LiveSaveData() {
    uint8_t* manager = ReadGlobal<uint8_t*>(retail::kGameManager);
    return manager ? ReadAt<uint8_t*>(manager, retail::kManagerSaveData) : nullptr;
}

// The item id of a CItemStatus, false for anything else or a status whose
// item is unknown.
bool StatusId(const uint8_t* status, uint64_t& id) {
    if (!status || ReadAt<uintptr_t>(status, 0) != g_ModuleBase + retail::kItemStatusVtable) {
        return false;
    }
    const uint8_t* data = ReadAt<const uint8_t*>(status, retail::kItemStatusData);
    const uint8_t* record = data ? ReadAt<const uint8_t*>(data, 0) : nullptr;
    if (!record) {
        return false;
    }
    id = ReadAt<uint64_t>(record, retail::kItemRecordId);
    return true;
}

bool IsModStatus(const uint8_t* status) {
    uint64_t id = 0;
    return StatusId(status, id) && IsBand(id);
}

bool MatchModStatus(const void* element) {
    return IsModStatus(static_cast<const uint8_t*>(element));
}

// MSVC unordered_map<u64, T> lookups and unlinks are the overlay save
// core's.
using save_overlay::MapFind;

// Unlinks node and frees it the way the game's own erase does.
void MapErase(uint8_t* map, uint8_t* node) {
    if (save_overlay::MapPark(map, node)) {
        GameFunction<MapNodeFreeFn>(retail::kMapNodeFree)(nullptr, node);
    }
}

// Erases every mod id key from the map. Returns how many.
size_t EraseModKeys(uint8_t* map) {
    uint8_t* end = ReadAt<uint8_t*>(map, retail::kMapListHead);
    if (!end) {
        return 0;
    }
    std::vector<uint8_t*> found;
    size_t steps = ReadAt<size_t>(map, retail::kMapSize);
    for (uint8_t* node = ReadAt<uint8_t*>(end, retail::kMapNodeNext); node && node != end && steps-- > 0;
         node = ReadAt<uint8_t*>(node, retail::kMapNodeNext)) {
        if (IsBand(ReadAt<uint64_t>(node, retail::kMapNodeKey))) {
            found.push_back(node);
        }
    }
    for (uint8_t* node : found) {
        MapErase(map, node);
    }
    return found.size();
}

// Drops Canopy's reference to a status. A status of an older save data, or
// one the live uid map no longer points at, forgets its save data first,
// so its destructor cannot unregister another status with the same uid.
void SafeRelease(uint8_t* status) {
    uint8_t* owner = ReadAt<uint8_t*>(status, retail::kItemStatusSaveData);
    uint8_t* live = LiveSaveData();
    if (owner) {
        uint8_t* node =
            owner == live ? MapFind(live + retail::kSaveDataUidMap, ReadAt<uint64_t>(status, retail::kItemStatusUid))
                          : nullptr;
        if (!node || ReadAt<uint8_t*>(node, retail::kMapNodeValue) != status) {
            WriteAt<uint8_t*>(status, retail::kItemStatusSaveData, nullptr);
        }
    }
    Release(status, retail::kItemStatusRefCount);
}

int32_t InventoryCapacity(const uint8_t* saveData) {
    const uint64_t bits = ReadAt<uint64_t>(saveData, retail::kSaveDataCapacity);
    if (bits & retail::kCapacityLarge) {
        return 30;
    }
    return (bits & retail::kCapacityMedium) ? 20 : 10;
}

uint8_t** InventorySlot(uint8_t* saveData, int32_t index) {
    return reinterpret_cast<uint8_t**>(saveData + retail::kSaveDataInventory + sizeof(void*) * index);
}

uint8_t** ToolSlot(uint8_t* saveData, int32_t index) {
    return reinterpret_cast<uint8_t**>(saveData + retail::kSaveDataTools + sizeof(void*) * index);
}

// Puts a status Canopy holds a reference to into the first free bag slot,
// handing the reference over. False when the bag is full.
bool PutInBag(uint8_t* saveData, uint8_t* status) {
    const int32_t capacity = InventoryCapacity(saveData);
    for (int32_t index = 0; index < capacity; ++index) {
        uint8_t** slot = InventorySlot(saveData, index);
        if (!*slot) {
            WriteAt<uint16_t>(status, retail::kItemStatusState, 0);
            *slot = status;
            return true;
        }
    }
    return false;
}

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

// ---- Table append (boot worker) ------------------------------------------

// The first vanilla row that places gimmick, empty when none does. A placed
// mod item takes its category and, with no icon of its own, its icon.
blob::Row FurnitureRow(const mod_tables::Table& table, uint64_t gimmick) {
    for (const uint64_t id : table.order) {
        const auto found = table.rows.find(id);
        if (found == table.rows.end() || IsBand(id)) {
            continue;
        }
        const blob::Row row{found->second.data, found->second.size};
        if (gimmick != 0 && mod_rules::PlacesGimmick(row, gimmick)) {
            return row;
        }
    }
    return blob::Row{};
}

// Builds the vanilla table plus every usable mod row into merged. Returns
// how many mod rows it holds; 0 leaves merged empty.
size_t BuildMergedBlob(uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged) {
    const uint32_t count = ReadAt<uint32_t>(blob, 0);
    const uint32_t rowBytes = ReadAt<uint32_t>(blob, 4);
    const uint32_t poolBytes = ReadAt<uint32_t>(blob, 8);
    if (count == 0 || count > blob::kMaxRows || rowBytes > blob::kMaxPart || poolBytes > blob::kMaxPart) {
        Log("Mod items: the item table has an unexpected header, so no items were appended.");
        return 0;
    }
    mod_tables::Table table;
    if (!mod_tables::ParseTable(blob, kRowFeatures, kRowEnum, table)) {
        Log("Mod items: the item table rows do not add up, so no items were appended.");
        return 0;
    }
    // The icons of every row count, a repeated id's too, and table.rows keeps
    // only the first row of each id, so the rows are walked once more.
    mod_rules::IconOutlines iconOutline;
    size_t offset = kBlobHeader;
    for (uint32_t index = 0; index < count; ++index) {
        const uint32_t size = ReadAt<uint32_t>(blob, offset);
        mod_rules::AddIconOutline(blob::Row{blob + offset + 4, size}, iconOutline);
        offset += 4 + size;
    }

    mod_rules::Report report = mod_tables::LogReport();
    mod_tables::Appender appender(table);
    std::set<std::string> mods;
    const mod_rules::TextWriter write = [&](std::vector<uint8_t>& row, size_t at, const std::string& text) {
        appender.Pool().Write(row, at, text);
    };
    for (ItemDef& item : g_Items) {
        const std::string where = "Mod item " + item.fullKey;
        const auto base = item.baseId != 0 ? table.rows.find(item.baseId) : table.rows.end();
        const bool hasBase = base != table.rows.end();
        const blob::Row baseRow = hasBase ? blob::Row{base->second.data, base->second.size} : blob::Row{};
        if (!mod_rules::CheckAgainstTable(item, hasBase ? &baseRow : nullptr, report, item.crop != 0)) {
            continue;
        }
        if (table.rows.count(item.id) || mod_tables::DatabaseHas(db, item.id)) {
            Log(where + " skipped, its id " + std::to_string(item.id) + " is already in the item table.");
            continue;
        }
        if (item.use == mod_rules::ItemUse::Script && !g_LoaderHooked) {
            Log(where + " skipped, script items need the script loader hook, see above.");
            continue;
        }
        mod_rules::ItemRowParts parts;
        parts.id = item.id;
        parts.base = hasBase ? &baseRow : nullptr;
        parts.module = item.module;
        parts.crop = item.crop;
        uint64_t icon = 0;
        uint64_t outline = 0;
        if (hasBase) {
            const size_t tail = mod_rules::TailOffset(baseRow);
            icon = ReadAt<uint64_t>(baseRow.data, tail + kTailIcon);
            outline = ReadAt<uint64_t>(baseRow.data, tail + kTailOutline);
        }
        if (item.use == mod_rules::ItemUse::Place) {
            // The vanilla item that places the object's clone lends its
            // category, and its icon to an item with neither base nor icon.
            const blob::Row furniture = FurnitureRow(table, mod_objects::CloneOf(item.object));
            if (!furniture.data) {
                report.Error(
                    item.folder, item.line,
                    where + " skipped, no vanilla item places the furniture its object " + item.objectKey + " copies."
                );
                continue;
            }
            parts.gimmick = item.object;
            parts.placeCategory = mod_rules::RowCategory(furniture);
            if (!hasBase) {
                const size_t tail = mod_rules::TailOffset(furniture);
                icon = ReadAt<uint64_t>(furniture.data, tail + kTailIcon);
                outline = ReadAt<uint64_t>(furniture.data, tail + kTailOutline);
            }
        }
        const char* keeps = hasBase ? " keeps its base item's icon" : (icon != 0 ? " keeps its furniture's icon" : nullptr);
        if (item.modIcon) {
            if (mod_art::IconAdded(item.icon)) {
                // A mod icon has no outline of its own, so it is its own.
                icon = item.icon;
                outline = item.icon;
            } else {
                report.Warning(
                    item.folder, item.line,
                    where + (keeps ? keeps : " has no icon") + ", its mod icon " + std::to_string(item.icon) +
                        " is not in the icon table."
                );
            }
        } else if (item.icon != 0 && mod_rules::CheckVanillaIcon(item, iconOutline, report)) {
            icon = item.icon;
            outline = iconOutline.at(item.icon);
        }
        if (icon == 0) {
            report.Error(item.folder, item.line, where + " skipped, it has no icon to show.");
            continue;
        }
        parts.icon = icon;
        parts.outline = outline;
        item.enumName = appender.UniqueEnum("CANOPY_" + EnumPart(item.modKey) + "_" + EnumPart(item.key));
        parts.enumName = item.enumName;
        appender.Add(mod_rules::BuildItemRow(item, parts, write));
        item.appended = true;
        mods.insert(item.modKey);
    }
    if (appender.Count() == 0) {
        return 0;
    }
    appender.Merge(blob, merged);
    g_ModCount = mods.size();
    return appender.Count();
}

bool IsItemTable(const char* name) {
    if (!name) {
        return false;
    }
    std::string path = name;
    std::replace(path.begin(), path.end(), '\\', '/');
    return _stricmp(path.c_str(), kItemTableName) == 0;
}

__declspec(noinline) bool TryBuild(uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged, size_t& appended) {
    try {
        appended = BuildMergedBlob(db, blob, merged);
        return true;
    } catch (...) {
        merged.clear();
        for (ItemDef& item : g_Items) {
            item.appended = false;
        }
        Log("Mod items: building the item rows failed, so no items were appended.");
        return false;
    }
}

int64_t __fastcall Detour_DatabaseLoad(uint8_t* db, const char* name, const uint8_t* blob) {
    if (!blob || !IsItemTable(name) || g_InLoad.load()) {
        return g_OriginalLoad(db, name, blob);
    }
    if (g_TableSeen.exchange(true)) {
        if (!g_LoggedReload.exchange(true)) {
            Log("Mod items: the item table loaded again, mod items were only appended the first time.");
        }
        return g_OriginalLoad(db, name, blob);
    }
    std::vector<uint8_t> merged;
    size_t appended = 0;
    TryBuild(db, blob, merged, appended);
    g_InLoad = true;
    const int64_t result = g_OriginalLoad(db, name, merged.empty() ? blob : merged.data());
    g_InLoad = false;
    if (!merged.empty()) {
        Log(
            "Mod items: " + Plural(appended, "item", "items") + " from " + Plural(g_ModCount, "mod", "mods") +
            " appended to the item table."
        );
    }
    g_TableDone.store(true, std::memory_order_release);
    return result;
}

// ---- Title self-check ----------------------------------------------------

void SelfCheck() {
    g_Checked = true;
    if (!g_TableDone.load(std::memory_order_acquire)) {
        Log("Mod items: the item table was never seen loading, so custom items are off.");
        return;
    }
    void* registry = ReadGlobal<void*>(retail::kDatabaseRegistry);
    uint8_t* items = registry ? GameFunction<DatabaseFn>(retail::kItemDatabase)(registry) : nullptr;
    uint8_t* categories = registry ? GameFunction<DatabaseFn>(retail::kCategoryDatabase)(registry) : nullptr;
    uint8_t* icons = registry ? GameFunction<DatabaseFn>(retail::kIconDatabase)(registry) : nullptr;
    for (ItemDef& item : g_Items) {
        if (!item.appended) {
            continue;
        }
        const std::string what = "Mod item " + item.fullKey + " is id " + std::to_string(item.id);
        uint8_t* record = FindRecord(items, item.id);
        if (!record || ReadAt<uint64_t>(record, retail::kItemRecordId) != item.id) {
            Log(what + ", but the item table does not have it. It stays off.");
            continue;
        }
        if (!FindRecord(categories, ReadAt<uint64_t>(record, retail::kItemRecordCategory))) {
            Log(what + ", but its category is missing. It stays off.");
            continue;
        }
        if (!FindRecord(icons, ReadAt<uint64_t>(record, retail::kItemRecordIcon)) ||
            !FindRecord(icons, ReadAt<uint64_t>(record, retail::kItemRecordOutline))) {
            Log(what + ", but its icon is missing. It stays off.");
            continue;
        }
        if (item.use == mod_rules::ItemUse::Place && !FindRecord(table_engine::Database("gimmick.dat"), item.object)) {
            Log(what + ", but the gimmick table does not have its object " + item.objectKey + ". It stays off.");
            continue;
        }
        item.enabled = true;
        Log(what + ", found in the item table.");
    }
}

// ---- Acquisition guard ---------------------------------------------------

bool Guarded() {
    return g_GuardDepth.load() > 0 && g_GuardThread.load() == GetCurrentThreadId();
}

int32_t __fastcall Detour_AddInventoryItem(uint8_t* saveData, uint8_t** status, uint32_t* slot, uint8_t dryRun) {
    if (!status || !IsModStatus(*status)) {
        return g_OriginalAdd(saveData, status, slot, dryRun);
    }
    const DWORD thread = GetCurrentThreadId();
    const bool outer = g_GuardDepth.load() == 0;
    if (outer) {
        g_GuardThread = thread;
    } else if (g_GuardThread.load() != thread) {
        // Another thread holds the guard; mod items are only added on the
        // main thread, so this one is refused rather than left unguarded.
        return ReadAt<int32_t>(*status, retail::kItemStatusCount);
    }
    ++g_GuardDepth;
    const int32_t result = g_OriginalAdd(saveData, status, slot, dryRun);
    --g_GuardDepth;
    return result;
}

int64_t __fastcall Detour_LetterCondition(void* letters, int32_t kind, int32_t target, int64_t value, int32_t count) {
    if (Guarded() || (kind == 0 && target == 0 && IsBand(static_cast<uint64_t>(value)))) {
        return 0;
    }
    return g_OriginalLetter(letters, kind, target, value, count);
}

uint8_t __fastcall Detour_EncyclopediaRelease(uint8_t* saveData, uint64_t id, uint32_t rank, uint8_t notify) {
    if (Guarded() || IsBand(id)) {
        return 0;
    }
    return g_OriginalEncyclopedia(saveData, id, rank, notify);
}

uint64_t __fastcall Detour_SetSaveFlag(uint8_t* saveData, int64_t flag) {
    if (Guarded()) {
        return 0;
    }
    return g_OriginalSetFlag(saveData, flag);
}

// ---- Script items --------------------------------------------------------

ItemDef* FindModule(const char* name) {
    if (!name || _strnicmp(name, kModulePrefix, std::strlen(kModulePrefix)) != 0) {
        return nullptr;
    }
    for (ItemDef& item : g_Items) {
        if (!item.module.empty() && _stricmp(item.module.c_str(), name) == 0) {
            return &item;
        }
    }
    return nullptr;
}

// One script item was used: one is gone when the item says so, then the
// mod's Lua hears "use:<Key>". The first bag stack of the item is the one
// taken from, which is the used one unless the bag holds two.
void UseItem(const char* fullKey) {
    const ItemDef* item = fullKey ? FindByKey(fullKey) : nullptr;
    if (!item || !item->enabled || item->use != mod_rules::ItemUse::Script) {
        return;
    }
    uint8_t* live = LiveSaveData();
    if (item->consume && live && !save_overlay::Stripped()) {
        uint64_t id = 0;
        const int32_t capacity = InventoryCapacity(live);
        for (int32_t index = 0; index < capacity; ++index) {
            if (StatusId(*InventorySlot(live, index), id) && id == item->id) {
                // The subtract sets flag 700, which the guard turns off.
                const DWORD thread = GetCurrentThreadId();
                if (g_GuardDepth.load() == 0) {
                    g_GuardThread = thread;
                } else if (g_GuardThread.load() != thread) {
                    break;
                }
                ++g_GuardDepth;
                GameFunction<SubInventoryItemFn>(retail::kSubInventoryItem)(live, index, 1);
                --g_GuardDepth;
                break;
            }
        }
    }
    if (!lua_host::PostEvent(item->modKey, "use:" + item->key)) {
        Log("[" + item->modKey + "] Item " + item->key + " was used, but the mod's Lua is not running.");
    }
}

int Native_Use(lua_State* state) {
    try {
        UseItem(g_Lua.type(state, 1) == kTypeString ? g_Lua.toLString(state, 1, nullptr) : nullptr);
    } catch (...) {
    }
    return 0;
}

// Compiles the item's module and leaves its table on the stack, or nothing
// when it fails, which the game reports as a script error.
int ServeModule(lua_State* state, const ItemDef& item) {
    if (g_Lua.loadBufferX(state, kModuleSource, std::strlen(kModuleSource), item.chunkName.c_str(), "t") == 0) {
        g_Lua.pushCClosure(state, &Native_Use, 0);
        g_Lua.pushString(state, item.fullKey.c_str());
        if (g_Lua.pcallK(state, 2, 1, 0, 0, nullptr) == 0 && g_Lua.type(state, -1) == kTypeTable) {
            return 1;
        }
    }
    Log("Mod item " + item.fullKey + ": its script module did not build, so using it does nothing.");
    g_Lua.setTop(state, 0);
    return 0;
}

int Detour_Loader(lua_State* state) {
    if (g_Lua.type(state, 1) == kTypeString) {
        if (const ItemDef* item = FindModule(g_Lua.toLString(state, 1, nullptr))) {
            return ServeModule(state, *item);
        }
    }
    return g_OriginalLoader(state);
}

bool HasScripts() {
    return std::any_of(g_Items.begin(), g_Items.end(), [](const ItemDef& item) {
        return item.use == mod_rules::ItemUse::Script;
    });
}

void HookLoader() {
    if (!HasScripts()) {
        return;
    }
    if (!RequirementsMet(retail::kLuaHostRequirements)) {
        Log("Mod items: the script loader does not match, so script items are off.");
        return;
    }
    g_Lua.setTop = GameFunction<LuaSetTopFn>(retail::kLuaSetTop);
    g_Lua.type = GameFunction<LuaTypeFn>(retail::kLuaType);
    g_Lua.toLString = GameFunction<LuaToLStringFn>(retail::kLuaToLString);
    g_Lua.pushString = GameFunction<LuaPushStringFn>(retail::kLuaPushString);
    g_Lua.pushCClosure = GameFunction<LuaPushCClosureFn>(retail::kLuaPushCClosure);
    g_Lua.pcallK = GameFunction<LuaPCallKFn>(retail::kLuaPCallK);
    g_Lua.loadBufferX = GameFunction<LuaLoadBufferXFn>(retail::kLuaLoadBufferX);
    void* loader = reinterpret_cast<void*>(g_ModuleBase + retail::kScriptModuleLoader);
    if (const char* reason = hook_chain::Add(
            loader, kLoaderChainOwner, reinterpret_cast<void*>(&Detour_Loader),
            reinterpret_cast<void**>(&g_OriginalLoader)
        )) {
        Log(std::string("Mod items: the script loader hook was not created, ") + reason +
            ", so script items are off.");
        return;
    }
    g_LoaderHooked = true;
}

// ---- Giving and counting -------------------------------------------------

ItemDef* FindItem(const std::string& modKey, const char* key) {
    for (ItemDef& item : g_Items) {
        if (_stricmp(item.modKey.c_str(), modKey.c_str()) == 0 && _stricmp(item.key.c_str(), key) == 0) {
            return &item;
        }
    }
    return nullptr;
}

int32_t CountItem(const ItemDef& item) {
    int64_t total = 0;
    uint64_t id = 0;
    const auto add = [&](const uint8_t* status) {
        if (StatusId(status, id) && id == item.id) {
            total += ReadAt<int32_t>(status, retail::kItemStatusCount);
        }
    };
    if (uint8_t* live = LiveSaveData()) {
        for (int32_t index = 0; index < retail::kInventorySlots; ++index) {
            add(*InventorySlot(live, index));
        }
        for (int32_t index = 0; index < retail::kToolSlots; ++index) {
            add(*ToolSlot(live, index));
        }
    }
    for (const Held& held : g_Stripped) {
        add(held.status);
    }
    for (const uint8_t* status : g_Pending) {
        add(status);
    }
    return static_cast<int32_t>(std::min<int64_t>(total, INT32_MAX));
}

// Adds count of item to the bag the way the game adds a found item, with
// the guard on. Returns how many went in, or why none could.
std::string Give(const ItemDef& item, int32_t count) {
    if (!item.enabled) {
        return "rthis item is off, see Canopy.log";
    }
    if (!game_events::InGame() || !PlayerInField()) {
        return "ronly while a save is loaded and the player is on the map";
    }
    uint8_t* live = LiveSaveData();
    if (save_overlay::Stripped() || !live) {
        return "rthe game is saving";
    }
    uint8_t* status = AllocateZeroed(retail::kItemStatusSize);
    if (!status) {
        return "rout of memory";
    }
    GameFunction<ItemStatusCtorFn>(retail::kItemStatusCtor)(status, live, item.id);
    AddRef(status, retail::kItemStatusRefCount);
    WriteAt<int32_t>(status, retail::kItemStatusCount, count);
    uint32_t slot = 0;
    const int32_t remainder = GameFunction<AddInventoryItemFn>(retail::kAddInventoryItem)(live, &status, &slot, 0);
    const int32_t added = std::clamp(count - remainder, 0, count);
    if (added > 0) {
        int64_t parent = 0;
        GameFunction<PushLogFn>(retail::kPushLog)(
            ReadGlobal<uint8_t*>(retail::kGameManager), ReadAt<void*>(status, retail::kItemStatusData), 0,
            static_cast<uint32_t>(added), retail::kItemGetLogId, &parent
        );
    }
    Release(status, retail::kItemStatusRefCount);
    Log(
        "[" + item.modKey + "] Gave " + std::to_string(added) + " of " + std::to_string(count) + " " + item.fullKey +
        (added < count ? ", the bag is full." : ".")
    );
    if (added == 0) {
        return "rthe bag is full";
    }
    return "i" + std::to_string(added);
}

// ---- Save splice: strip ----------------------------------------------------

// The sidecar line of one status.
std::string EntryLine(const uint8_t* status, const char* place, int32_t index) {
    uint64_t id = 0;
    StatusId(status, id);
    const ItemDef* item = FindById(id);
    const std::string key = item ? item->fullKey : "?" + std::to_string(id);
    return key + ", " + place + ", " + std::to_string(index) + ", " +
           std::to_string(ReadAt<int32_t>(status, retail::kItemStatusCount)) + ", " +
           std::to_string(ReadAt<int32_t>(status, retail::kItemStatusRank));
}

// The file body as of the strip: every held, waiting and carried item.
std::string Snapshot(size_t& count) {
    std::vector<std::string> lines;
    for (const Held& held : g_Stripped) {
        if (held.place == Place::Inventory) {
            lines.push_back(EntryLine(held.status, "inventory", static_cast<int32_t>(held.where)));
        } else if (held.place == Place::Tool) {
            lines.push_back(EntryLine(held.status, "tool", static_cast<int32_t>(held.where)));
        } else {
            lines.push_back(EntryLine(held.status, "other", -1));
        }
    }
    for (const uint8_t* status : g_Pending) {
        lines.push_back(EntryLine(status, "other", -1));
    }
    lines.insert(lines.end(), g_Carried.begin(), g_Carried.end());
    count = lines.size();
    if (lines.empty()) {
        return {};
    }
    std::string text = "[items]\r\n";
    for (const std::string& line : lines) {
        text += "item = " + line + "\r\n";
    }
    return text;
}

void Strip(uint8_t* live) {
    const size_t before = g_Stripped.size();
    for (int32_t index = 0; index < retail::kInventorySlots; ++index) {
        uint8_t** slot = InventorySlot(live, index);
        if (IsModStatus(*slot)) {
            g_Stripped.push_back({*slot, Place::Inventory, static_cast<size_t>(index), nullptr});
            *slot = nullptr;
        }
    }
    for (int32_t index = 0; index < retail::kToolSlots; ++index) {
        uint8_t** slot = ToolSlot(live, index);
        if (IsModStatus(*slot)) {
            g_Stripped.push_back({*slot, Place::Tool, static_cast<size_t>(index), nullptr});
            *slot = nullptr;
        }
    }
    for (const size_t offset : retail::kSaveDataItemVectors) {
        std::vector<save_overlay::VectorEntry> removed;
        save_overlay::VectorRemove(live + offset, &MatchModStatus, removed);
        for (const save_overlay::VectorEntry& entry : removed) {
            g_Stripped.push_back({static_cast<uint8_t*>(entry.value), Place::Vector, offset, nullptr, entry.index});
        }
    }
    uint8_t* list = live + retail::kSaveDataDropList;
    uint8_t* head = ReadAt<uint8_t*>(list, retail::kListHead);
    size_t steps = ReadAt<size_t>(list, retail::kListSize);
    for (uint8_t* node = head ? ReadAt<uint8_t*>(head, retail::kListNodeNext) : nullptr;
         node && node != head && steps-- > 0;) {
        uint8_t* next = ReadAt<uint8_t*>(node, retail::kListNodeNext);
        uint8_t* status = ReadAt<uint8_t*>(node, retail::kListNodeValue);
        if (IsModStatus(status)) {
            uint8_t* prev = ReadAt<uint8_t*>(node, retail::kListNodePrev);
            WriteAt(prev, retail::kListNodeNext, next);
            WriteAt(next, retail::kListNodePrev, prev);
            WriteAt(list, retail::kListSize, ReadAt<size_t>(list, retail::kListSize) - 1);
            g_Stripped.push_back({status, Place::Drop, 0, node});
        }
        node = next;
    }
    size_t erased = 0;
    for (const size_t offset :
         {retail::kSaveDataEncyclopedia, retail::kSaveDataFavoriteCraft, retail::kSaveDataFavoriteCook}) {
        erased += EraseModKeys(live + offset);
    }
    const size_t moved = g_Stripped.size() - before;
    if (moved > 0 || erased > 0) {
        Log(
            "Mod items: moved " + Plural(moved, "item", "items") + " out of the save while it is written" +
            (erased > 0 ? ", and erased " + Plural(erased, "mod id", "mod ids") + " from its id lists." : ".")
        );
    }
}

// ---- Save splice: restore ------------------------------------------------

void PlaceOrPend(uint8_t* live, uint8_t* status, const std::string& why) {
    if (PutInBag(live, status)) {
        return;
    }
    g_Pending.push_back(status);
    uint64_t id = 0;
    StatusId(status, id);
    const ItemDef* item = FindById(id);
    Log(
        "Mod item " + (item ? item->fullKey : std::to_string(id)) + " " + why +
        ", and the bag is full. It goes in once a slot frees up."
    );
}

void Restore(uint8_t* live) {
    size_t restored = 0;
    // Vector statuses go back at their old indices, one vector at a time.
    std::vector<save_overlay::VectorEntry> vectors[std::size(retail::kSaveDataItemVectors)];
    for (const Held& held : g_Stripped) {
        if (!live || ReadAt<uint8_t*>(held.status, retail::kItemStatusSaveData) != live) {
            // A drop node of an older save data is left alone.
            SafeRelease(held.status);
            continue;
        }
        ++restored;
        if (held.place == Place::Inventory || held.place == Place::Tool) {
            uint8_t** slot = held.place == Place::Inventory ? InventorySlot(live, static_cast<int32_t>(held.where))
                                                            : ToolSlot(live, static_cast<int32_t>(held.where));
            if (!*slot) {
                *slot = held.status;
            } else {
                PlaceOrPend(live, held.status, "could not go back to its slot");
            }
        } else if (held.place == Place::Vector) {
            const size_t* offsets = std::begin(retail::kSaveDataItemVectors);
            const size_t vector =
                static_cast<size_t>(std::find(offsets, std::end(retail::kSaveDataItemVectors), held.where) - offsets);
            if (vector < std::size(vectors)) {
                vectors[vector].push_back({held.index, held.status});
            } else {
                PlaceOrPend(live, held.status, "could not go back to its list");
            }
        } else {
            uint8_t* list = live + retail::kSaveDataDropList;
            uint8_t* head = ReadAt<uint8_t*>(list, retail::kListHead);
            if (!head) {
                PlaceOrPend(live, held.status, "could not go back to its list");
                continue;
            }
            uint8_t* tail = ReadAt<uint8_t*>(head, retail::kListNodePrev);
            WriteAt(held.node, retail::kListNodeNext, head);
            WriteAt(held.node, retail::kListNodePrev, tail);
            WriteAt(tail, retail::kListNodeNext, held.node);
            WriteAt(head, retail::kListNodePrev, held.node);
            WriteAt(list, retail::kListSize, ReadAt<size_t>(list, retail::kListSize) + 1);
        }
    }
    for (size_t vector = 0; vector < std::size(vectors); ++vector) {
        if (!vectors[vector].empty() &&
            !save_overlay::VectorReinsert(live + retail::kSaveDataItemVectors[vector], vectors[vector])) {
            for (const save_overlay::VectorEntry& entry : vectors[vector]) {
                PlaceOrPend(live, static_cast<uint8_t*>(entry.value), "could not go back to its list");
            }
        }
    }
    if (restored > 0) {
        Log("Mod items: " + Plural(restored, "item is", "items are") + " back after the save.");
    }
    g_Stripped.clear();
}

void RetryPending() {
    uint8_t* live = LiveSaveData();
    if (!live || g_Pending.empty() || save_overlay::Stripped()) {
        return;
    }
    std::vector<uint8_t*> waiting;
    waiting.swap(g_Pending);
    size_t placed = 0;
    for (uint8_t* status : waiting) {
        if (ReadAt<uint8_t*>(status, retail::kItemStatusSaveData) != live) {
            SafeRelease(status);
        } else if (PutInBag(live, status)) {
            ++placed;
        } else {
            g_Pending.push_back(status);
        }
    }
    if (placed > 0) {
        Log("Mod items: " + Plural(placed, "waiting item went", "waiting items went") + " into the bag.");
    }
}

void ReleaseAll() {
    for (const Held& held : g_Stripped) {
        SafeRelease(held.status);
    }
    for (uint8_t* status : g_Pending) {
        SafeRelease(status);
    }
    g_Stripped.clear();
    g_Pending.clear();
}

// ---- Save splice: sidecars -------------------------------------------------

bool ParseEntry(const std::string& value, Entry& entry) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(',', start);
        if (end == std::string::npos) {
            end = value.size();
        }
        parts.push_back(canopy_ini::Trim(value.substr(start, end - start)));
        start = end + 1;
    }
    entry.raw = value;
    if (parts.size() != 5 || parts[0].empty()) {
        return false;
    }
    entry.fullKey = parts[0];
    entry.place = canopy_ini::Lower(parts[1]);
    return ParseI32(parts[2], entry.index) && ParseI32(parts[3], entry.count) && ParseI32(parts[4], entry.rank);
}

// A good load hands over its file; its items wait for the save loaded
// event. Items still waiting from the old game are dropped.
void Loaded(int32_t slot, save_overlay::FileState state, const std::string& text) {
    for (uint8_t* status : g_Pending) {
        SafeRelease(status);
    }
    g_Pending.clear();
    g_Carried.clear();
    g_Loaded.clear();
    g_LoadedSlot = slot;
    g_ApplyPending = false;
    if (state != save_overlay::FileState::Read) {
        return;
    }
    size_t start = 0;
    while (start < text.size() && g_Loaded.size() < kMaxSidecarLines) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = canopy_ini::Trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || equals == std::string::npos ||
            canopy_ini::Lower(canopy_ini::Trim(line.substr(0, equals))) != "item") {
            continue;
        }
        Entry entry;
        ParseEntry(canopy_ini::Trim(line.substr(equals + 1)), entry);
        g_Loaded.push_back(entry);
    }
    g_ApplyPending = true;
}

// Rebuilds the loaded sidecar's items in the live save data, with no side
// effect: no counter, flag, encyclopedia or log.
void ApplyLoaded() {
    uint8_t* live = LiveSaveData();
    if (!g_ApplyPending || !live) {
        return;
    }
    g_ApplyPending = false;
    size_t placed = 0;
    size_t kept = 0;
    for (const Entry& entry : g_Loaded) {
        const ItemDef* item = entry.fullKey.empty() ? nullptr : FindByKey(entry.fullKey);
        if (!item || !item->enabled || entry.count < 1) {
            g_Carried.push_back(entry.raw);
            ++kept;
            continue;
        }
        uint8_t* status = AllocateZeroed(retail::kItemStatusSize);
        if (!status) {
            g_Carried.push_back(entry.raw);
            ++kept;
            continue;
        }
        GameFunction<ItemStatusCtorFn>(retail::kItemStatusCtor)(status, live, item->id);
        AddRef(status, retail::kItemStatusRefCount);
        WriteAt<int32_t>(status, retail::kItemStatusCount, std::min(entry.count, kMaxCount));
        WriteAt<int32_t>(status, retail::kItemStatusRank, std::clamp(entry.rank, 0, kMaxRank));
        uint8_t** slot = nullptr;
        if (entry.place == "inventory" && entry.index >= 0 && entry.index < InventoryCapacity(live)) {
            slot = InventorySlot(live, entry.index);
        } else if (entry.place == "tool" && entry.index >= 0 && entry.index < retail::kToolSlots) {
            slot = ToolSlot(live, entry.index);
        }
        if (slot && !*slot) {
            *slot = status;
        } else {
            PlaceOrPend(live, status, "came back from the save");
        }
        ++placed;
    }
    if (placed > 0 || kept > 0) {
        Log(
            "Mod items: restored " + Plural(placed, "item", "items") + " from " + SlotName(g_LoadedSlot) +
            (kept > 0 ? ", and kept " + Plural(kept, "line", "lines") + " of items that are not installed or are off." : ".")
        );
    }
    g_Loaded.clear();
}

// The title screen: everything held for the old game goes.
void Clear() {
    ReleaseAll();
    g_Carried.clear();
    g_Loaded.clear();
    g_ApplyPending = false;
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData, bool beforeGameInit) {
    std::vector<mod_rules::ModFolder> folders = mod_rules::WithItems(mod_rules::ScanMods(ModFilePath("mods")));
    if (folders.empty()) {
        Log("No mod items, so custom items are off.");
        return false;
    }
    if (!ticking || !saveData) {
        Log("Custom items need the frame tick and per-save data, so they are off.");
        return false;
    }
    if (!beforeGameInit) {
        Log("Custom items need Canopy to start before game init, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModItemsRequirements)) {
        Log("Custom items are off.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    const auto accept = [](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Items in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
            Plural(count, "item", "items") + (joined ? "." : ", registered for its items."));
        return true;
    };
    for (mod_rules::ItemSpec& spec :
         mod_rules::ReadItems(folders, ModFilePath("mods"), mod_tables::LoaderRefs(), report, accept)) {
        ItemDef item;
        static_cast<mod_rules::ItemSpec&>(item) = std::move(spec);
        g_Items.push_back(std::move(item));
    }
    std::vector<std::string> keys;
    for (const ItemDef& item : g_Items) {
        keys.push_back(item.fullKey);
    }
    std::vector<uint64_t> numbers;
    if (g_Items.empty() ||
        !mod_tables::AssignNumbers(kRegistryName, kRegistryHeader, "Custom items", kBandSize, keys, numbers)) {
        g_Items.clear();
        Log("No usable mod item, so custom items are off.");
        return false;
    }
    for (size_t index = 0; index < g_Items.size(); ++index) {
        g_Items[index].id = numbers[index] ? kBandBase + numbers[index] : 0;
    }
    g_Items.erase(
        std::remove_if(g_Items.begin(), g_Items.end(), [](const ItemDef& item) { return item.id == 0; }),
        g_Items.end()
    );
    if (g_Items.empty()) {
        Log("No usable mod item, so custom items are off.");
        return false;
    }
    for (size_t index = 0; index < g_Items.size(); ++index) {
        ItemDef& item = g_Items[index];
        g_ById.emplace(item.id, index);
        if (item.use == mod_rules::ItemUse::Script) {
            item.module = kModulePrefix + mod_tables::ModulePart(item.modKey) + "_" + mod_tables::ModulePart(item.key);
            item.chunkName = "=canopy item " + item.fullKey;
        }
    }

    DetourSet hooks("Custom items");
    const auto add = [&](uintptr_t rva, auto detour, auto original, const char* name) {
        return hooks.Add(rva, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original), name);
    };
    if (!add(retail::kDatabaseLoad, &Detour_DatabaseLoad, &g_OriginalLoad, "itemTableLoad") ||
        !add(retail::kAddInventoryItem, &Detour_AddInventoryItem, &g_OriginalAdd, "addInventoryItem") ||
        !add(retail::kLetterCondition, &Detour_LetterCondition, &g_OriginalLetter, "letterCondition") ||
        !add(retail::kEncyclopediaRelease, &Detour_EncyclopediaRelease, &g_OriginalEncyclopedia, "encyclopediaRelease") ||
        !add(retail::kSetSaveFlag, &Detour_SetSaveFlag, &g_OriginalSetFlag, "setFlag")) {
        hooks.RemoveAll();
        g_Items.clear();
        g_ById.clear();
        Log("Custom items are off, their hooks could not be created.");
        return false;
    }
    const save_overlay::Adapter adapter = {
        "items", "Mod items", "item", "items", "Each line: mod.item, place, index, count, rank.", kOverlayOrder,
        nullptr, &Strip, &Snapshot, &Restore, &Loaded, nullptr, nullptr, &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        hooks.RemoveAll();
        g_Items.clear();
        g_ById.clear();
        Log("Custom items are off, the overlay save could not start.");
        return false;
    }
    // Before the table loads, which leaves script items out without it.
    HookLoader();
    g_Active = true;
    std::set<std::string> mods;
    for (const ItemDef& item : g_Items) {
        mods.insert(item.modKey);
    }
    Log(
        "Custom items installed: " + Plural(g_Items.size(), "item", "items") + " from " +
        Plural(mods.size(), "mod", "mods") + "."
    );
    return true;
}

void Tick() {
    if (!g_Active) {
        return;
    }
    const uint64_t now = GetTickCount64();
    if (!g_Pending.empty() && !save_overlay::Stripped() && PlayerInField() && now - g_LastRetry > 2000) {
        g_LastRetry = now;
        RetryPending();
    }
}

void OnEvent(CanopyEvent event) {
    if (!g_Active) {
        return;
    }
    if (event == CANOPY_EVENT_TITLE_SCREEN) {
        // The overlay save core already cleared the held items.
        if (!g_Checked) {
            SelfCheck();
        }
    } else if (event == CANOPY_EVENT_SAVE_LOADED || event == CANOPY_EVENT_FIELD_ENTER) {
        ApplyLoaded();
        if (event == CANOPY_EVENT_FIELD_ENTER) {
            RetryPending();
        }
    }
}

std::string LuaRequest(const std::string& modKey, const char* action, const char* item, const char* count) {
    if (!action || !item) {
        return "ecanopy.items needs an item key";
    }
    ItemDef* found = FindItem(modKey, item);
    if (!found) {
        return std::string("ethis mod defines no item named ") + item;
    }
    if (std::strcmp(action, "id") == 0) {
        return "i" + std::to_string(found->id);
    }
    if (std::strcmp(action, "on_use") == 0) {
        if (found->use != mod_rules::ItemUse::Script) {
            return std::string("ecanopy.items.on_use needs an item with use = script, ") + item + " has another use";
        }
        return "i" + std::to_string(found->id);
    }
    if (std::strcmp(action, "count") == 0) {
        return "i" + std::to_string(g_Active ? CountItem(*found) : 0);
    }
    if (std::strcmp(action, "give") == 0) {
        int32_t number = 1;
        if (count && (!ParseI32(count, number) || number < 1 || number > kMaxCount)) {
            return "ecanopy.items.give needs a count from 1 to 999";
        }
        if (!g_Active) {
            return "rcustom items are off, see Canopy.log";
        }
        return Give(*found, number);
    }
    return std::string("ecanopy.items does not know ") + action;
}

uint64_t IdOf(const std::string& fullKey) {
    const ItemDef* item = FindByKey(fullKey);
    return item ? item->id : 0;
}

uint64_t PlaceItemFor(const std::string& objectKey) {
    for (const ItemDef& item : g_Items) {
        if (item.use == mod_rules::ItemUse::Place && _stricmp(item.objectKey.c_str(), objectKey.c_str()) == 0) {
            return item.id;
        }
    }
    return 0;
}

bool MakeSeed(uint64_t id, uint64_t crop, std::string& why) {
    const auto found = g_ById.find(id);
    if (found == g_ById.end()) {
        why = "its seed is not a mod item that loaded";
        return false;
    }
    ItemDef& item = g_Items[found->second];
    if (item.use != mod_rules::ItemUse::None) {
        why = mod_rules::SeedRefused(item.fullKey, mod_rules::ItemUseName(item.use));
        return false;
    }
    if (item.crop != 0 && item.crop != crop) {
        why = "its seed " + item.fullKey + " already plants another crop";
        return false;
    }
    item.crop = crop;
    return true;
}

bool OwnsModule(const char* name) {
    return g_LoaderHooked && FindModule(name) != nullptr;
}

bool Usable(uint64_t id) {
    if (!IsBand(id)) {
        return true;
    }
    const ItemDef* item = FindById(id);
    return item && item->enabled;
}

bool IsModItem(uint64_t id) {
    return IsBand(id);
}

}  // namespace mod_items
