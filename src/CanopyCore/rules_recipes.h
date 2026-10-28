#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mod_rules.h"

// The rules of [craft.<Key>] and [cooking.<Key>] sections. See docs/RECIPES.md.
namespace mod_rules {

extern const SectionInfo kCraftSection;
extern const SectionInfo kCookingSection;

// The two kinds of recipe, in the order of their tables.
enum RecipeKind : size_t {
    kCraft = 0,
    kCooking = 1,
};

// craft.dat rows are 148 bytes and cooking.dat rows 176. A cooking row keeps
// its five ingredients as (item, group, count), kCookingNeedStride bytes each,
// from kCookingNeeds.
constexpr size_t kCraftRowSize = 148;
constexpr size_t kCookingRowSize = 176;
constexpr size_t kCookingNeeds = 52;
constexpr size_t kCookingNeedStride = 20;
// The most ingredients a recipe has.
constexpr size_t kMaxNeeds = 5;

// Mod recipe ids are kRecipeBandBase + n for n below kRecipeBandSize. Vanilla
// ids are below the band.
constexpr uint64_t kRecipeBandBase = 1700000000;
constexpr uint64_t kRecipeBandSize = 100000000;
// True for an id inside the band.
bool InRecipeBand(uint64_t id);

// One ingredient as canopy.ini says it.
struct RecipeNeed {
    // The ingredient as written: an item, or group:<number> for cooking.
    std::string ref;
    uint64_t item = 0;
    // A cooking group, 0 for a plain item.
    uint64_t group = 0;
    int32_t count = 1;
};

// A craft or cooking recipe as canopy.ini says it.
struct RecipeSpec {
    RecipeKind kind = kCraft;
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as the sidecar writes it.
    std::string fullKey;
    int line = 0;
    // The row id the loader gives the recipe, 0 until it does.
    uint64_t id = 0;
    // The vanilla recipe the mod row copies.
    uint64_t baseId = 1;
    std::string outputRef;
    uint64_t output = 0;
    int32_t count = 1;
    std::vector<RecipeNeed> needs;
    int32_t unlock = 0;
    std::string recipeRef;
    uint64_t recipeItem = 0;
    // The lines of base and of needs, where the table checks point.
    int baseLine = 0;
    int needsLine = 0;
};

// Reads every [craft.X] and [cooking.X] section of folders, in folder and
// file order, as the loader installs them: per mod key the first folder owns
// it; per mod, the keys of each kind are unique; across all mods, at most
// 2000 recipes load. modsDir is not read, since recipes have no files. accept
// sees each folder with usable recipes and drops them when it returns false.
std::vector<RecipeSpec> ReadRecipes(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
);

// Adds each cooking group of a cooking.dat row to groups, with the vanilla
// item it is paired with. A row shorter than kCookingRowSize has none.
void AddCookingGroups(const uint8_t* row, uint32_t size, std::map<uint64_t, uint64_t>& groups);
// The checks against the vanilla table that the loader runs when it appends
// rows: the base row is there with the expected layout, the id is free
// (idTaken says the table or the game has it), and every cooking group is one
// a vanilla recipe uses. base is null when the table has no row baseId.
// False, reported, when the recipe is skipped.
bool CheckAgainstTable(
    const RecipeSpec& recipe, const blob::Row* base, bool idTaken, const std::map<uint64_t, uint64_t>& groups,
    Report& report
);

// The desktop check: reads the recipes, registers them under RefKind::Recipe,
// then checks them against the vanilla craft and cooking tables.
void CheckRecipes(Context& context);

}  // namespace mod_rules
