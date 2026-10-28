#pragma once
#include <string>

#include "canopy_ini.h"
#include "mod_rules.h"
#include "rules_rows.h"

// The rules of [sale.<Key>], an item a shop sells, and [upgrade.<Key>], a
// tool the blacksmith upgrades. Each is one row of a generated table,
// storesales.dat or blacksmith.dat, read by ReadRows with the [row.]
// sections: they share the row band and registry, references, the build and
// the title check. See docs/SHOPS.md.
namespace mod_rules {

constexpr const char* kSalePrefix = "sale.";
constexpr const char* kUpgradePrefix = "upgrade.";
// RowSpec::kind of the rows they give.
constexpr const char* kSaleKind = "sale";
constexpr const char* kUpgradeKind = "upgrade";

extern const SectionInfo kSaleSection;
extern const SectionInfo kUpgradeSection;

// True for a [sale.] or [upgrade.] section, a bare one included.
bool IsShopSection(const canopy_ini::IniSection& section);
// Reads a [sale.] or [upgrade.] section of folder into row. False, reported,
// when it is skipped.
bool ReadShop(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, Report& report,
    RowSpec& row
);

}  // namespace mod_rules
