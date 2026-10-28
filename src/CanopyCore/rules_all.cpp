#include "mod_rules.h"
#include "rules_append.h"
#include "rules_art.h"
#include "rules_crops.h"
#include "rules_flags.h"
#include "rules_items.h"
#include "rules_mod.h"
#include "rules_objects.h"
#include "rules_patch.h"
#include "rules_recipes.h"
#include "rules_rows.h"
#include "rules_shops.h"
#include "rules_villagers.h"
#include "rules_whistles.h"

// Every section kind, and the desktop check that runs them all in the
// loader's install order: the Lua host's [mod] and settings, art, objects,
// items, flags, villagers, whistles, recipes, generic rows, which name any of
// those, then patches and list appends, which apply as the tables load but
// read Mod.Key references to what the others installed.

namespace mod_rules {

const std::vector<const SectionInfo*>& Sections() {
    static const std::vector<const SectionInfo*> sections = {
        &kModSection,      &kSettingSection, &kObjectSection,  &kItemSection,  &kFlagSection,    &kVillagerSection,
        &kWhistleSection,  &kCraftSection,   &kCookingSection, &kTextureSection, &kIconSection,
        &kSkinSection,     &kRowSection,     &kPatchSection,   &kOfferingSection, &kCatchSection,
        &kForageSection,   &kSaleSection,    &kUpgradeSection, &kCropSection,
    };
    return sections;
}

void CheckAll(Context& context) {
    CheckMods(context);
    CheckArt(context);
    CheckObjects(context);
    CheckItems(context);
    CheckFlags(context);
    CheckVillagers(context);
    CheckWhistles(context);
    CheckRecipes(context);
    CheckRows(context);
    CheckPatches(context);
    CheckAppends(context);
}

}  // namespace mod_rules
