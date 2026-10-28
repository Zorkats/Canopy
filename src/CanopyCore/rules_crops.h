#pragma once
#include <set>
#include <string>
#include <vector>

#include "canopy_ini.h"
#include "mod_rules.h"
#include "rules_items.h"
#include "rules_rows.h"

// The rules of [crop.<Key>], a crop a mod item plants. One section is two
// rows of generated tables that share one id, as every vanilla crop's do: a
// crops.dat row, how it grows and looks, and a cropsharvest.dat row, what it
// drops. Both copy the same vanilla crop and are read by ReadRows with the
// [row.] sections. See docs/CROPS.md.
namespace mod_rules {

constexpr const char* kCropPrefix = "crop.";
// RowSpec::kind of the two rows: the crops row, then its harvest row.
constexpr const char* kCropKind = "crop";
constexpr const char* kCropHarvestKind = "crop harvest";

extern const SectionInfo kCropSection;

// True for a [crop.] section, a bare one included.
bool IsCropSection(const canopy_ini::IniSection& section);
// Reads a [crop.] section of folder into its crops row and its harvest row.
// False, reported, when it is skipped.
bool ReadCrop(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    RowSpec& crop, RowSpec& harvest
);
// True for a crop's crops row.
bool IsCrop(const RowSpec& row);
// The seed of a crops row as Mod.Key, empty for a crop without one.
std::string CropSeedKey(const RowSpec& row);
// The seed of a crops row, resolved by ResolveRows, or null.
const RowValue* CropSeed(const RowSpec& row);
// Why a mod item with use cannot be a seed, empty when it can: a seed has
// no use of its own.
std::string SeedRefused(const std::string& seedKey, const char* useName);

// The items the [crop.] sections of folders name as seeds, as lowercase
// Mod.Key. The desktop item check lets them copy a vanilla seed.
std::set<std::string> SeedKeys(const std::vector<ModFolder>& folders);

// The desktop check of the seeds of rows, after ResolveRows: each must be a
// mod item without a use of its own. A crop whose seed is not loses its
// rows.
void CheckCropSeeds(Context& context, std::vector<RowSpec>& rows);

}  // namespace mod_rules
