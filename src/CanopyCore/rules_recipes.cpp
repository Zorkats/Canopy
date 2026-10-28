#include "rules_recipes.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <utility>

#include "platform_files.h"

// =============================================================
// RECIPE RULES
// =============================================================
// What the loader's custom craft and cooking recipes accept from canopy.ini,
// moved here from mod_recipes.cpp so the desktop tools check a mod the same
// way. Every message is the loader's log line.

namespace mod_rules {
namespace {

constexpr size_t kMaxRecipes = 2000;
constexpr int32_t kMaxCount = 99;
constexpr uint64_t kMaxGroup = 1000;
constexpr const char* kGroupPrefix = "group:";
constexpr const char* kCraftTable = "craft.dat";
constexpr const char* kCookingTable = "cooking.dat";

const FieldInfo kCraftFields[] = {
    {"base", ValueKind::VanillaId, false, "The vanilla craft recipe to copy, for its station and preview.", "1",
     "craft.dat"},
    {"output", ValueKind::ItemRef, true,
     "The item the recipe makes: a mod item by key or Mod.Key, or a vanilla item id."},
    {"count", ValueKind::Integer, false, "How many of the output the recipe makes, 1 to 99.", "1", nullptr, 1, 99},
    {"needs", ValueKind::List, true,
     "1 to 5 ingredients, separated by commas: an item, or item * count with a count of 1 to 99."},
    {"unlock", ValueKind::FlagRef, false,
     "The flag that lists the recipe once it is set. Without one, it is listed from the start."},
    {"recipe", ValueKind::ItemRef, false,
     "The item whose use sets the unlock flag, as the vanilla recipe scrolls do. Needs unlock."},
};

const FieldInfo kCookingFields[] = {
    {"base", ValueKind::VanillaId, false, "The vanilla cooking recipe to copy, for its dish and preview.", "1",
     "cooking.dat"},
    {"output", ValueKind::ItemRef, true,
     "The item the dish makes: a mod item by key or Mod.Key, or a vanilla item id."},
    {"count", ValueKind::Integer, false, "How many of the output the recipe makes, 1 to 99.", "1", nullptr, 1, 99},
    {"needs", ValueKind::List, true,
     "1 to 5 ingredients, separated by commas: an item, or group:<number> for a group a vanilla cooking recipe "
     "uses, each with an optional * count of 1 to 99."},
    {"unlock", ValueKind::FlagRef, false,
     "The flag that lists the recipe once it is set. Without one, it is listed from the start."},
    {"recipe", ValueKind::ItemRef, false, "Craft only. A cooking recipe ignores it and warns."},
};

const char* KindName(RecipeKind kind) {
    return kind == kCraft ? "craft" : "cooking";
}

const SectionInfo& InfoOf(RecipeKind kind) {
    return kind == kCraft ? kCraftSection : kCookingSection;
}

size_t RowSize(RecipeKind kind) {
    return kind == kCraft ? kCraftRowSize : kCookingRowSize;
}

bool ParseCount(const std::string& text, int32_t& count) {
    return ParseI32(text, count) && count >= 1 && count <= kMaxCount;
}

// One ingredient: ref or ref * count, where ref is an item or, for cooking,
// group:<number>.
bool ParseNeed(
    RecipeKind kind, const std::string& modKey, const Refs& refs, const std::string& text, RecipeNeed& need
) {
    const size_t star = text.find('*');
    need.ref = canopy_ini::Trim(text.substr(0, star));
    if (star != std::string::npos && !ParseCount(canopy_ini::Trim(text.substr(star + 1)), need.count)) {
        return false;
    }
    const size_t prefix = std::strlen(kGroupPrefix);
    if (kind == kCooking && platform::CompareNoCase(need.ref.c_str(), kGroupPrefix, prefix) == 0) {
        return ParseU64(canopy_ini::Trim(need.ref.substr(prefix)), need.group) && need.group >= 1 &&
               need.group <= kMaxGroup;
    }
    need.item = ItemRef(refs, modKey, need.ref);
    return need.item != 0;
}

// Reads one [craft.X] or [cooking.X] section. Reports and returns false when
// it is unusable.
bool ReadRecipe(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, RecipeKind kind,
    const Refs& refs, Report& report, RecipeSpec& recipe
) {
    recipe.kind = kind;
    recipe.modKey = modKey;
    recipe.folder = folder.name;
    recipe.key = SectionKey(section, InfoOf(kind).prefix);
    recipe.fullKey = modKey + "." + recipe.key;
    recipe.line = section.line;
    const std::string where = "[" + modKey + "] " + InfoOf(kind).title + " " + recipe.key;
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    if (!KeyValid(recipe.key)) {
        report.Error(
            folder.name, section.line, where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    const std::string base = canopy_ini::Value(&section, "base", "");
    recipe.baseLine = at("base");
    if (!base.empty() && (!ParseU64(base, recipe.baseId) || recipe.baseId == 0 || InRecipeBand(recipe.baseId))) {
        report.Error(
            folder.name, recipe.baseLine,
            where + " skipped, base must be a vanilla " + KindName(kind) + " recipe id."
        );
        return false;
    }
    recipe.outputRef = canopy_ini::Value(&section, "output", "");
    recipe.output = ItemRef(refs, modKey, recipe.outputRef);
    if (recipe.output == 0) {
        report.Error(
            folder.name, at("output"),
            where + " skipped, output must name an item of an installed mod or a vanilla item id" +
                (recipe.outputRef.empty() ? "." : ", and " + recipe.outputRef + " is neither.")
        );
        return false;
    }
    const std::string count = canopy_ini::Value(&section, "count", "");
    if (!count.empty() && !ParseCount(count, recipe.count)) {
        report.Error(folder.name, at("count"), where + " skipped, count must be 1 to 99.");
        return false;
    }
    const std::string needs = canopy_ini::Value(&section, "needs", "");
    recipe.needsLine = at("needs");
    for (const std::string& part : needs.empty() ? std::vector<std::string>{} : SplitList(needs)) {
        RecipeNeed need;
        if (!ParseNeed(kind, modKey, refs, part, need)) {
            report.Error(
                folder.name, recipe.needsLine,
                where + " skipped, its ingredient " + (part.empty() ? std::string("(empty)") : part) +
                    (kind == kCooking
                         ? " is not item, item * count, group:<number> or group:<number> * count, with an item"
                         : " is not item or item * count, with an item") +
                    " of an installed mod or a vanilla item id and a count of 1 to 99."
            );
            return false;
        }
        recipe.needs.push_back(need);
    }
    if (recipe.needs.empty() || recipe.needs.size() > kMaxNeeds) {
        report.Error(folder.name, recipe.needsLine, where + " skipped, needs must list 1 to 5 ingredients.");
        return false;
    }
    const std::string unlock = canopy_ini::Value(&section, "unlock", "");
    if (!unlock.empty()) {
        recipe.unlock = FlagRef(refs, modKey, unlock);
        if (recipe.unlock == 0) {
            report.Error(
                folder.name, at("unlock"),
                where + " skipped, unlock must name a flag of an installed mod or a vanilla flag id, and " + unlock +
                    " is neither."
            );
            return false;
        }
    }
    recipe.recipeRef = canopy_ini::Value(&section, "recipe", "");
    if (!recipe.recipeRef.empty()) {
        recipe.recipeItem = kind == kCraft ? ItemRef(refs, modKey, recipe.recipeRef) : 0;
        if (recipe.recipeItem == 0 || recipe.unlock == 0) {
            report.Warning(
                folder.name, at("recipe"),
                where + " has no recipe item, recipe is for craft recipes with an unlock flag and must name an item."
            );
            recipe.recipeItem = 0;
        }
    }
    return true;
}

}  // namespace

const SectionInfo kCraftSection = {
    "craft.", "Craft recipe", "docs/RECIPES.md", kCraftFields, std::size(kCraftFields)};
const SectionInfo kCookingSection = {
    "cooking.", "Cooking recipe", "docs/RECIPES.md", kCookingFields, std::size(kCookingFields)};

bool InRecipeBand(uint64_t id) {
    return id >= kRecipeBandBase && id < kRecipeBandBase + kRecipeBandSize;
}

std::vector<RecipeSpec> ReadRecipes(
    const std::vector<ModFolder>& folders, const std::string&, const Refs& refs, Report& report,
    const FolderAccept& accept
) {
    std::vector<RecipeSpec> all;
    std::vector<std::string> owners;
    for (const ModFolder& folder : folders) {
        if (!HasSection(folder, {kCraftSection.prefix, kCookingSection.prefix})) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Recipes in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Recipes in folder " + folder.name + " skipped, another folder already has recipes under the key " +
                    modKey + "."
            );
            continue;
        }
        std::vector<RecipeSpec> recipes;
        for (const canopy_ini::IniSection& section : folder.sections) {
            for (const RecipeKind kind : {kCraft, kCooking}) {
                if (SectionKey(section, InfoOf(kind).prefix).empty()) {
                    continue;
                }
                RecipeSpec recipe;
                if (!ReadRecipe(folder, modKey, section, kind, refs, report, recipe)) {
                    continue;
                }
                const auto same = [&](const RecipeSpec& other) {
                    return other.kind == kind && platform::CompareNoCase(other.key.c_str(), recipe.key.c_str()) == 0;
                };
                if (std::any_of(recipes.begin(), recipes.end(), same)) {
                    report.Error(
                        folder.name, section.line,
                        "[" + modKey + "] " + InfoOf(kind).title + " " + recipe.key +
                            " skipped, the mod already has one with that key."
                    );
                    continue;
                }
                if (all.size() + recipes.size() >= kMaxRecipes) {
                    report.Error(
                        folder.name, section.line,
                        "[" + modKey + "] " + InfoOf(kind).title + " " + recipe.key +
                            " skipped, Canopy takes at most " + std::to_string(kMaxRecipes) + " mod recipes."
                    );
                    continue;
                }
                recipes.push_back(std::move(recipe));
            }
        }
        if (recipes.empty() || !accept(folder, modKey, recipes.size())) {
            continue;
        }
        owners.push_back(modKey);
        for (RecipeSpec& recipe : recipes) {
            all.push_back(std::move(recipe));
        }
    }
    return all;
}

void AddCookingGroups(const uint8_t* row, uint32_t size, std::map<uint64_t, uint64_t>& groups) {
    if (size < kCookingRowSize) {
        return;
    }
    for (size_t index = 0; index < kMaxNeeds; ++index) {
        const size_t at = kCookingNeeds + kCookingNeedStride * index;
        const uint64_t group = blob::Get<uint64_t>(row, at + 8);
        if (group != 0) {
            groups.emplace(group, blob::Get<uint64_t>(row, at));
        }
    }
}

bool CheckAgainstTable(
    const RecipeSpec& recipe, const blob::Row* base, bool idTaken, const std::map<uint64_t, uint64_t>& groups,
    Report& report
) {
    const std::string where = std::string("Mod ") + KindName(recipe.kind) + " recipe " + recipe.fullKey;
    if (!base) {
        report.Error(
            recipe.folder, recipe.baseLine,
            where + " skipped, its base " + std::to_string(recipe.baseId) + " is not in the " +
                KindName(recipe.kind) + " table."
        );
        return false;
    }
    if (base->size != RowSize(recipe.kind)) {
        report.Error(recipe.folder, recipe.baseLine, where + " skipped, its base row has an unexpected layout.");
        return false;
    }
    if (idTaken) {
        report.Error(
            recipe.folder, recipe.line,
            where + " skipped, its id " + std::to_string(recipe.id) + " is already in the " + KindName(recipe.kind) +
                " table."
        );
        return false;
    }
    const auto unknown = std::find_if(recipe.needs.begin(), recipe.needs.end(), [&](const RecipeNeed& need) {
        return need.group != 0 && !groups.count(need.group);
    });
    if (unknown != recipe.needs.end()) {
        report.Error(
            recipe.folder, recipe.needsLine, where + " skipped, no vanilla cooking recipe uses " + unknown->ref + "."
        );
        return false;
    }
    return true;
}

void CheckRecipes(Context& context) {
    const std::vector<RecipeSpec> recipes =
        ReadRecipes(context.folders, context.modsDir, context.refs, context.report, AcceptAll);
    for (const RecipeSpec& recipe : recipes) {
        context.refs.Add(RefKind::Recipe, recipe.fullKey);
    }
    const blob::Table* craft = context.vanilla ? context.vanilla->Table(kCraftTable) : nullptr;
    const blob::Table* cooking = context.vanilla ? context.vanilla->Table(kCookingTable) : nullptr;
    std::map<uint64_t, uint64_t> groups;
    if (cooking) {
        for (const blob::Row& row : cooking->rows) {
            AddCookingGroups(row.data, row.size, groups);
        }
    }
    for (const RecipeSpec& recipe : recipes) {
        const blob::Table* table = recipe.kind == kCraft ? craft : cooking;
        if (!table) {
            continue;
        }
        const auto base = std::find_if(table->rows.begin(), table->rows.end(), [&](const blob::Row& row) {
            return blob::RowId(row) == recipe.baseId;
        });
        CheckAgainstTable(recipe, base == table->rows.end() ? nullptr : &*base, false, groups, context.report);
    }
}

}  // namespace mod_rules
