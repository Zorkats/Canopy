#define NOMINMAX
#include "mod_recipes.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "mod_items.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_recipes.h"
#include "save_overlay.h"

// =============================================================
// CUSTOM RECIPES DESIGN
// =============================================================
// A mod defines recipes in its canopy.ini: one [craft.Key] section per
// craft table recipe and one [cooking.Key] section per cooking recipe. Each
// names its output and up to five ingredients, by mod item key or vanilla
// item id, and an optional unlock flag. A folder with recipes and no Lua or
// DLL is still registered as a mod. With no recipes anywhere, nothing below
// is hooked.
//
// Ids. Each recipe gets 1700000000 + n, where n comes from
// mods\CanopyData\recipes.ini, given out once and never reused, like the
// item ids. Craft and cooking recipes share the counter.
//
// Tables. CDatabase<CraftData>::load 0x59C30 and CDatabase<CookingData>::load
// 0x58E20 take (db, name, blob) like the item table's load, once each, on
// the boot worker. Each detour builds one blob with the vanilla rows, then
// the mod rows, and calls the original once with it. A mod row is a copy of
// its base recipe's row with a new id, enum name, output, ingredients and
// unlock flag. A craft row keeps the base's station and preview; a cooking
// row keeps the base's dish gimmick and texture, which the cooking menu
// shows as its preview. The output's name and icon come from the output
// item's own row.
//
// Ingredients. The craft and cooking checks match ingredients by item id,
// and create the output from its id, so mod items work on both sides. A
// cooking ingredient can also be a group, such as any egg: the row then
// carries the group number and the vanilla item the game pairs with it.
//
// Unlock. A recipe with no unlock flag is listed from the start, as the
// vanilla rows with 0 are. Otherwise it is listed once the flag is set; a
// craft recipe can name an item whose use sets the flag, as the vanilla
// recipe scrolls do.
//
// Title. The recipe tables load before the item table, so the tick checks
// each recipe at the title, after the item self-check: it looks the id up
// through the game's own find, compares the output, count and unlock flag,
// and looks up every item it names. A recipe that fails is taken out of the
// record list the menus walk, so it is never listed.
//
// Save. The save keeps the favorite craft and cooking recipes in two maps
// keyed by recipe id. Before the save data is written the mod ids are
// unlinked from them, and after the write they are linked back; the
// favorites are kept in mods\CanopyData\slot<N>.recipes.ini by key, and put
// back in the maps after a load. A vanilla save never holds a mod recipe
// id.
//
// Rules. What a [craft.Key] or [cooking.Key] section accepts is CanopyCore's
// rules_recipes, shared with the desktop tools; this file installs what
// they read.
//
// Threads. The table detours run on the boot worker and only touch the
// blobs, the recipe definitions and the log. Everything else runs on the
// main thread.

namespace mod_recipes {
namespace {

using namespace core;
using mod_tables::Plural;

using DatabaseLoadFn = int64_t(__fastcall*)(uint8_t* db, const char* name, const uint8_t* blob);
using MapNodeFreeFn = void(__fastcall*)(void* unused, void* node);
// unordered_map<u64, bool>::try_emplace: writes the node and whether it was
// inserted to result.
using FavoriteInsertFn = void*(__fastcall*)(void* map, void* result, const uint64_t* key);

using Kind = mod_rules::RecipeKind;
using mod_rules::kCraft;
using mod_rules::kCooking;
using mod_rules::kCookingNeeds;
using mod_rules::kCookingNeedStride;
using mod_rules::kMaxNeeds;

constexpr size_t kKinds = 2;

// ---- craft.dat and cooking.dat row formats -------------------------------
// Both: id, enum name, comment. Craft, 148 bytes: station gimmick, five
// (item, count) ingredients, output and count, preview and its value,
// unlock flag, recipe item. Cooking, 176 bytes: output and count, dish
// gimmick, dish texture, five (item, group, count) ingredients, unlock flag.
constexpr size_t kRowId = 0;
constexpr size_t kRowEnum = 8;
constexpr size_t kRowComment = 16;
constexpr size_t kCraftNeeds = 32;
constexpr size_t kCraftNeedStride = 12;
constexpr size_t kCraftOutput = 92;
constexpr size_t kCraftCount = 100;
constexpr size_t kCraftUnlock = 116;
constexpr size_t kCraftRecipeItem = 124;
constexpr size_t kCookingOutput = 24;
constexpr size_t kCookingCount = 32;
constexpr size_t kCookingUnlock = 152;

constexpr const char* kTableFiles[kKinds] = {"craft.dat", "cooking.dat"};
constexpr size_t kRowSizes[kKinds] = {mod_rules::kCraftRowSize, mod_rules::kCookingRowSize};
constexpr const char* kKindNames[kKinds] = {"craft", "cooking"};
constexpr const char* kEnumStems[kKinds] = {"CRAFT_TABLE_CANOPY_", "COOKING_ID_CANOPY_"};
constexpr size_t kFavoriteMaps[kKinds] = {retail::kSaveDataFavoriteCraft, retail::kSaveDataFavoriteCook};

// The favorite menus hold at most 100 recipes.
constexpr size_t kMaxFavorites = 100;
constexpr size_t kMaxSidecarLines = 4096;
constexpr int kOverlayOrder = 80;
constexpr const char* kRegistryName = "recipes.ini";
constexpr const char* kRegistryHeader =
    "; Canopy custom recipe ids. Each mod recipe keeps its number for good, and a number is never given\r\n"
    "; out twice. Do not edit or delete this file.\r\n";

using Need = mod_rules::RecipeNeed;

// A recipe as canopy.ini says it, plus what the loader adds to it.
struct RecipeDef : mod_rules::RecipeSpec {
    std::string enumName;
    // Set by the table detour before its done flag.
    bool appended = false;
    // Set at the title once the game's own lookup finds it.
    bool enabled = false;
};

// One table's load, boot worker.
struct TableState {
    DatabaseLoadFn original = nullptr;
    std::atomic<bool> seen{false};
    std::atomic<bool> done{false};
    std::atomic<bool> inLoad{false};
    std::atomic<uint8_t*> db{nullptr};
};

// A favorite map node unlinked for the write.
struct Parked {
    uint8_t* saveData;
    Kind kind;
    uint8_t* node;
};

// One favorite line of a sidecar.
struct Favorite {
    Kind kind = kCraft;
    std::string fullKey;
    std::string raw;
};

std::vector<RecipeDef> g_Recipes;
bool g_Active = false;
TableState g_Tables[kKinds];
bool g_Checked = false;
// Records the title check took out of the record lists.
std::set<const void*> g_Broken;

// Save splice, main thread.
std::vector<Parked> g_Parked;
// The favorites as of the last strip, for continue mode.
std::vector<std::pair<Kind, uint64_t>> g_Saved;
std::vector<Favorite> g_Carried;
std::vector<Favorite> g_Loaded;
int32_t g_LoadedSlot = 0;

bool IsBand(uint64_t id) {
    return mod_rules::InRecipeBand(id);
}

const RecipeDef* FindById(uint64_t id) {
    for (const RecipeDef& recipe : g_Recipes) {
        if (recipe.id == id) {
            return &recipe;
        }
    }
    return nullptr;
}

const RecipeDef* FindByKey(Kind kind, const std::string& fullKey) {
    for (const RecipeDef& recipe : g_Recipes) {
        if (recipe.kind == kind && _stricmp(recipe.fullKey.c_str(), fullKey.c_str()) == 0) {
            return &recipe;
        }
    }
    return nullptr;
}

// ---- Table append (boot worker) ------------------------------------------

std::vector<uint8_t> BuildRow(
    const RecipeDef& recipe, const mod_tables::Row& base, const std::map<uint64_t, uint64_t>& groups,
    mod_tables::TextPool& pool
) {
    std::vector<uint8_t> row(base.data, base.data + base.size);
    WriteAt<uint64_t>(row.data(), kRowId, recipe.id);
    pool.Write(row, kRowEnum, recipe.enumName);
    pool.Write(row, kRowComment, recipe.fullKey);
    if (recipe.kind == kCraft) {
        std::fill(row.begin() + kCraftNeeds, row.begin() + kCraftNeeds + kCraftNeedStride * kMaxNeeds, uint8_t{0});
        for (size_t index = 0; index < recipe.needs.size(); ++index) {
            WriteAt<uint64_t>(row.data(), kCraftNeeds + kCraftNeedStride * index, recipe.needs[index].item);
            WriteAt<int32_t>(row.data(), kCraftNeeds + kCraftNeedStride * index + 8, recipe.needs[index].count);
        }
        WriteAt<uint64_t>(row.data(), kCraftOutput, recipe.output);
        WriteAt<int32_t>(row.data(), kCraftCount, recipe.count);
        WriteAt<uint64_t>(row.data(), kCraftUnlock, static_cast<uint64_t>(recipe.unlock));
        WriteAt<uint64_t>(row.data(), kCraftRecipeItem, recipe.recipeItem);
        return row;
    }
    std::fill(row.begin() + kCookingNeeds, row.begin() + kCookingNeeds + kCookingNeedStride * kMaxNeeds, uint8_t{0});
    for (size_t index = 0; index < recipe.needs.size(); ++index) {
        const Need& need = recipe.needs[index];
        const size_t at = kCookingNeeds + kCookingNeedStride * index;
        WriteAt<uint64_t>(row.data(), at, need.group ? groups.at(need.group) : need.item);
        WriteAt<uint64_t>(row.data(), at + 8, need.group);
        WriteAt<int32_t>(row.data(), at + 16, need.count);
    }
    WriteAt<uint64_t>(row.data(), kCookingOutput, recipe.output);
    WriteAt<int32_t>(row.data(), kCookingCount, recipe.count);
    WriteAt<uint64_t>(row.data(), kCookingUnlock, static_cast<uint64_t>(recipe.unlock));
    return row;
}

// The vanilla item each cooking group is paired with.
std::map<uint64_t, uint64_t> CookingGroups(const mod_tables::Table& table) {
    std::map<uint64_t, uint64_t> groups;
    for (const auto& entry : table.rows) {
        mod_rules::AddCookingGroups(entry.second.data, entry.second.size, groups);
    }
    return groups;
}

size_t BuildBlob(Kind kind, uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged) {
    mod_tables::Table table;
    if (!mod_tables::ParseTable(blob, kRowSizes[kind], kRowEnum, table)) {
        Log(std::string("Mod recipes: the ") + kKindNames[kind] + " table rows do not add up, so no " + kKindNames[kind] +
            " recipes were appended.");
        return 0;
    }
    const std::map<uint64_t, uint64_t> groups = kind == kCooking ? CookingGroups(table) : std::map<uint64_t, uint64_t>{};
    mod_tables::Appender appender(table);
    mod_rules::Report report = mod_tables::LogReport();
    for (RecipeDef& recipe : g_Recipes) {
        if (recipe.kind != kind) {
            continue;
        }
        const auto base = table.rows.find(recipe.baseId);
        const bool found = base != table.rows.end();
        const blob::Row baseRow = found ? blob::Row{base->second.data, base->second.size} : blob::Row{};
        const bool idTaken = table.rows.count(recipe.id) || mod_tables::DatabaseHas(db, recipe.id);
        if (!mod_rules::CheckAgainstTable(recipe, found ? &baseRow : nullptr, idTaken, groups, report)) {
            continue;
        }
        recipe.enumName = appender.UniqueEnum(
            kEnumStems[kind] + mod_tables::EnumPart(recipe.modKey) + "_" + mod_tables::EnumPart(recipe.key)
        );
        appender.Add(BuildRow(recipe, base->second, groups, appender.Pool()));
        recipe.appended = true;
    }
    if (appender.Count() == 0) {
        return 0;
    }
    appender.Merge(blob, merged);
    return appender.Count();
}

__declspec(noinline) void TryBuild(
    Kind kind, uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged, size_t& appended
) {
    try {
        appended = BuildBlob(kind, db, blob, merged);
    } catch (...) {
        merged.clear();
        appended = 0;
        for (RecipeDef& recipe : g_Recipes) {
            if (recipe.kind == kind) {
                recipe.appended = false;
            }
        }
        Log(std::string("Mod recipes: building the ") + kKindNames[kind] + " rows failed, so none were appended.");
    }
}

template <Kind kind>
int64_t __fastcall Detour_TableLoad(uint8_t* db, const char* name, const uint8_t* blob) {
    TableState& state = g_Tables[kind];
    if (!blob || state.inLoad.load() || !mod_tables::IsTable(name, kTableFiles[kind]) || state.seen.exchange(true)) {
        return state.original(db, name, blob);
    }
    std::vector<uint8_t> merged;
    size_t appended = 0;
    TryBuild(kind, db, blob, merged, appended);
    state.inLoad = true;
    const int64_t result = state.original(db, name, merged.empty() ? blob : merged.data());
    state.inLoad = false;
    if (!merged.empty()) {
        Log("Mod recipes: " + Plural(appended, "recipe", "recipes") + " appended to the " + kKindNames[kind] + " table.");
    }
    state.db.store(db, std::memory_order_release);
    state.done.store(true, std::memory_order_release);
    return result;
}

// ---- Title self-check ----------------------------------------------------

bool MatchBroken(const void* element) {
    return element && g_Broken.count(ReadAt<const void*>(element, 0)) > 0;
}

// The item a recipe names is in the item table and, for a mod item, on.
bool ItemUsable(uint8_t* items, uint64_t id) {
    return id != 0 && mod_tables::FindRecord(items, id) && mod_items::Usable(id);
}

// Why a recipe's record or items are wrong, empty when they are right.
std::string CheckRecipe(const RecipeDef& recipe, const uint8_t* record, uint8_t* items) {
    const bool craft = recipe.kind == kCraft;
    if (ReadAt<uint64_t>(record, craft ? retail::kCraftRecordOutput : retail::kCookingRecordOutput) != recipe.output ||
        ReadAt<int32_t>(record, craft ? retail::kCraftRecordCount : retail::kCookingRecordCount) != recipe.count ||
        ReadAt<uint64_t>(record, craft ? retail::kCraftRecordUnlock : retail::kCookingRecordUnlock) !=
            static_cast<uint64_t>(recipe.unlock)) {
        return "the " + std::string(kKindNames[recipe.kind]) + " table does not have it as written";
    }
    if (!items) {
        return "the item table is not there";
    }
    if (!ItemUsable(items, recipe.output)) {
        return "its output " + recipe.outputRef + " is not an item that is on";
    }
    for (const Need& need : recipe.needs) {
        if (need.group == 0 && !ItemUsable(items, need.item)) {
            return "its ingredient " + need.ref + " is not an item that is on";
        }
    }
    if (recipe.recipeItem != 0 && !ItemUsable(items, recipe.recipeItem)) {
        return "its recipe item " + recipe.recipeRef + " is not an item that is on";
    }
    return {};
}

void SelfCheck() {
    g_Checked = true;
    uint8_t* items = mod_tables::ItemDatabase();
    for (const Kind kind : {kCraft, kCooking}) {
        if (std::none_of(g_Recipes.begin(), g_Recipes.end(), [&](const RecipeDef& recipe) {
                return recipe.kind == kind;
            })) {
            continue;
        }
        if (!g_Tables[kind].done.load(std::memory_order_acquire)) {
            Log(std::string("Mod recipes: the ") + kKindNames[kind] + " table was never seen loading, so custom " +
                kKindNames[kind] + " recipes are off.");
            continue;
        }
        uint8_t* db = g_Tables[kind].db.load(std::memory_order_acquire);
        g_Broken.clear();
        for (RecipeDef& recipe : g_Recipes) {
            if (recipe.kind != kind || !recipe.appended) {
                continue;
            }
            const std::string what =
                std::string("Mod ") + kKindNames[kind] + " recipe " + recipe.fullKey + " is id " + std::to_string(recipe.id);
            const uint8_t* record = mod_tables::FindRecord(db, recipe.id);
            const std::string wrong = record ? CheckRecipe(recipe, record, items) : "the game cannot find it";
            if (!wrong.empty()) {
                if (record) {
                    g_Broken.insert(record);
                }
                Log(what + ", but " + wrong + ". It stays off.");
                continue;
            }
            recipe.enabled = true;
            Log(what + ", making " + std::to_string(recipe.count) + " of " + recipe.outputRef + " from " +
                Plural(recipe.needs.size(), "ingredient", "ingredients") +
                (recipe.unlock ? ", unlocked by flag " + std::to_string(recipe.unlock) + "." : ", listed from the start."));
        }
        if (!g_Broken.empty() && db) {
            std::vector<save_overlay::VectorEntry> removed;
            save_overlay::VectorRemove(db + retail::kDatabaseRecords, &MatchBroken, removed);
            Log("Mod recipes: took " + Plural(removed.size(), "recipe", "recipes") + " out of the " + kKindNames[kind] +
                " menu.");
        }
        g_Broken.clear();
    }
}

// ---- Save splice -----------------------------------------------------------

void FreeNode(uint8_t* node) {
    GameFunction<MapNodeFreeFn>(retail::kMapNodeFree)(nullptr, node);
}

// Unlinks every mod recipe id from the save data's favorite maps and holds
// the nodes.
void Strip(uint8_t* live) {
    const size_t before = g_Parked.size();
    g_Saved.clear();
    for (const Kind kind : {kCraft, kCooking}) {
        uint8_t* map = live + kFavoriteMaps[kind];
        uint8_t* end = ReadAt<uint8_t*>(map, retail::kMapListHead);
        if (!end) {
            continue;
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
            if (save_overlay::MapPark(map, node)) {
                g_Parked.push_back({live, kind, node});
            }
        }
    }
    for (const Parked& parked : g_Parked) {
        if (ReadAt<uint8_t>(parked.node, retail::kMapNodeValue) != 0) {
            g_Saved.emplace_back(parked.kind, ReadAt<uint64_t>(parked.node, retail::kMapNodeKey));
        }
    }
    const size_t moved = g_Parked.size() - before;
    if (moved > 0) {
        Log("Mod recipes: moved " + Plural(moved, "favorite", "favorites") + " out of the save while it is written.");
    }
}

// The file body as of the strip: every held favorite, then the carried
// lines of recipes that are not installed.
std::string Snapshot(size_t& count) {
    std::vector<std::string> lines;
    std::set<std::string> held;
    for (const auto& [kind, id] : g_Saved) {
        if (const RecipeDef* recipe = FindById(id)) {
            const std::string line = std::string(kKindNames[kind]) + ", " + recipe->fullKey;
            if (held.insert(canopy_ini::Lower(line)).second) {
                lines.push_back(line);
            }
        }
    }
    for (const Favorite& favorite : g_Carried) {
        const std::string line = std::string(kKindNames[favorite.kind]) + ", " + favorite.fullKey;
        if (held.insert(canopy_ini::Lower(line)).second) {
            lines.push_back(favorite.raw);
        }
    }
    count = lines.size();
    if (lines.empty()) {
        return {};
    }
    std::string text = "[recipes]\r\n";
    for (const std::string& line : lines) {
        text += "favorite = " + line + "\r\n";
    }
    return text;
}

void Restore(uint8_t* live) {
    size_t restored = 0;
    for (const Parked& parked : g_Parked) {
        if (live && parked.saveData == live && save_overlay::MapUnpark(live + kFavoriteMaps[parked.kind], parked.node)) {
            ++restored;
            continue;
        }
        FreeNode(parked.node);
    }
    if (restored > 0) {
        Log("Mod recipes: " + Plural(restored, "favorite is", "favorites are") + " back after the save.");
    }
    g_Parked.clear();
}

bool ParseFavorite(const std::string& value, Favorite& favorite) {
    const std::vector<std::string> parts = mod_tables::SplitList(value);
    favorite.raw = value;
    if (parts.size() != 2 || parts[1].empty()) {
        return false;
    }
    for (const Kind kind : {kCraft, kCooking}) {
        if (_stricmp(parts[0].c_str(), kKindNames[kind]) == 0) {
            favorite.kind = kind;
            favorite.fullKey = parts[1];
            return true;
        }
    }
    return false;
}

void Loaded(int32_t slot, save_overlay::FileState state, const std::string& text) {
    g_Carried.clear();
    g_Loaded.clear();
    g_LoadedSlot = slot;
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
            canopy_ini::Lower(canopy_ini::Trim(line.substr(0, equals))) != "favorite") {
            continue;
        }
        Favorite favorite;
        if (ParseFavorite(canopy_ini::Trim(line.substr(equals + 1)), favorite)) {
            g_Loaded.push_back(favorite);
        }
    }
}

// Marks id a favorite in the save data. False when the menu is full.
bool AddFavorite(uint8_t* live, Kind kind, uint64_t id) {
    uint8_t* map = live + kFavoriteMaps[kind];
    if (save_overlay::MapFind(map, id)) {
        return true;
    }
    if (ReadAt<size_t>(map, retail::kMapSize) >= kMaxFavorites) {
        return false;
    }
    struct {
        uint8_t* node;
        uint64_t inserted;
    } result = {};
    GameFunction<FavoriteInsertFn>(retail::kFavoriteInsert)(map, &result, &id);
    if (!result.node) {
        return false;
    }
    WriteAt<uint8_t>(result.node, retail::kMapNodeValue, 1);
    return true;
}

void AfterLoad(uint8_t* live) {
    size_t applied = 0;
    size_t kept = 0;
    for (const Favorite& favorite : g_Loaded) {
        const RecipeDef* recipe = FindByKey(favorite.kind, favorite.fullKey);
        if (!recipe || !recipe->enabled || !AddFavorite(live, favorite.kind, recipe->id)) {
            g_Carried.push_back(favorite);
            ++kept;
            continue;
        }
        ++applied;
    }
    if (applied > 0 || kept > 0) {
        Log("Mod recipes: restored " + Plural(applied, "favorite", "favorites") + " from slot " +
            std::to_string(g_LoadedSlot) +
            (kept > 0 ? ", and kept " + Plural(kept, "line", "lines") + " of recipes that are not installed or are off."
                      : "."));
    }
    g_Loaded.clear();
}

// Continue mode read the stripped save back, so the favorites of the last
// strip go back in.
void AfterInPlace(uint8_t* live) {
    size_t applied = 0;
    for (const auto& [kind, id] : g_Saved) {
        const RecipeDef* recipe = FindById(id);
        if (recipe && recipe->enabled && !save_overlay::MapFind(live + kFavoriteMaps[kind], id) &&
            AddFavorite(live, kind, id)) {
            ++applied;
        }
    }
    if (applied > 0) {
        Log("Mod recipes: put back " + Plural(applied, "favorite", "favorites") + " after continue.");
    }
}

// The title screen: everything held for the old game goes.
void Clear() {
    for (const Parked& parked : g_Parked) {
        FreeNode(parked.node);
    }
    g_Parked.clear();
    g_Saved.clear();
    g_Carried.clear();
    g_Loaded.clear();
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData, bool beforeGameInit) {
    std::vector<mod_tables::Folder> folders =
        mod_tables::FindFolders({mod_rules::kCraftSection.prefix, mod_rules::kCookingSection.prefix});
    if (folders.empty()) {
        Log("No mod recipes, so custom recipes are off.");
        return false;
    }
    if (!ticking || !saveData) {
        Log("Custom recipes need the frame tick and the per-save data, so they are off.");
        return false;
    }
    if (!beforeGameInit) {
        Log("Custom recipes need Canopy to start before game init, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModRecipesRequirements)) {
        Log("Custom recipes are off.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    const auto accept = [](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Recipes in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
            Plural(count, "recipe", "recipes") + (joined ? "." : ", registered for its recipes."));
        return true;
    };
    for (mod_rules::RecipeSpec& spec :
         mod_rules::ReadRecipes(folders, ModFilePath("mods"), mod_tables::LoaderRefs(), report, accept)) {
        RecipeDef recipe;
        static_cast<mod_rules::RecipeSpec&>(recipe) = std::move(spec);
        g_Recipes.push_back(std::move(recipe));
    }
    std::vector<std::string> keys;
    for (const RecipeDef& recipe : g_Recipes) {
        keys.push_back(std::string(kKindNames[recipe.kind]) + "." + recipe.fullKey);
    }
    std::vector<uint64_t> numbers;
    if (g_Recipes.empty() ||
        !mod_tables::AssignNumbers(
            kRegistryName, kRegistryHeader, "Custom recipes", mod_rules::kRecipeBandSize, keys, numbers
        )) {
        g_Recipes.clear();
        Log("No usable mod recipe, so custom recipes are off.");
        return false;
    }
    for (size_t index = 0; index < g_Recipes.size(); ++index) {
        g_Recipes[index].id = numbers[index] ? mod_rules::kRecipeBandBase + numbers[index] : 0;
    }
    g_Recipes.erase(
        std::remove_if(g_Recipes.begin(), g_Recipes.end(), [](const RecipeDef& recipe) { return recipe.id == 0; }),
        g_Recipes.end()
    );
    if (g_Recipes.empty()) {
        Log("No usable mod recipe, so custom recipes are off.");
        return false;
    }

    DetourSet hooks("Custom recipes");
    if (!hooks.Add(
            retail::kCraftTableLoad, reinterpret_cast<void*>(&Detour_TableLoad<kCraft>),
            reinterpret_cast<void**>(&g_Tables[kCraft].original), "craftTableLoad"
        ) ||
        !hooks.Add(
            retail::kCookingTableLoad, reinterpret_cast<void*>(&Detour_TableLoad<kCooking>),
            reinterpret_cast<void**>(&g_Tables[kCooking].original), "cookingTableLoad"
        )) {
        hooks.RemoveAll();
        g_Recipes.clear();
        Log("Custom recipes are off, their hooks could not be created.");
        return false;
    }
    const save_overlay::Adapter adapter = {
        "recipes", "Mod recipes", "favorite", "favorites", "Each line: craft or cooking, mod.recipe.", kOverlayOrder,
        nullptr, &Strip, &Snapshot, &Restore, &Loaded, &AfterLoad, &AfterInPlace, &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        hooks.RemoveAll();
        g_Recipes.clear();
        Log("Custom recipes are off, the overlay save could not start.");
        return false;
    }
    g_Active = true;
    std::set<std::string> mods;
    size_t crafts = 0;
    for (const RecipeDef& recipe : g_Recipes) {
        mods.insert(recipe.modKey);
        crafts += recipe.kind == kCraft ? 1 : 0;
    }
    Log("Custom recipes installed: " + Plural(crafts, "craft recipe", "craft recipes") + " and " +
        Plural(g_Recipes.size() - crafts, "cooking recipe", "cooking recipes") + " from " +
        Plural(mods.size(), "mod", "mods") + ".");
    return true;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

}  // namespace mod_recipes
