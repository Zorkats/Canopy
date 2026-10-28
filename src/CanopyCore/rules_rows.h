#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "table_schema.h"

// The rules of [row.<table>.<Key>] on the tables whose layout is generated
// from the game's own definitions: a copy of a vanilla row with fields set
// by path and lists resized. texture, icon and animereplacetexture rows are
// art (rules_art). See docs/ROWS.md.
namespace mod_rules {

class RowTables;
struct RowSpec;

// Runs on a built row after its values are written: what a typed section
// works out from the base row itself. False, with why, when the row cannot
// be built.
using RowFinish = bool (*)(
    const RowSpec& mod, std::vector<uint8_t>& row, const RowTables& tables, blob::Pool& pool, std::string& why
);

// One key = value of a row: a field path, or <list path>.count.
struct RowValue {
    // As the section writes it, which may name several places (name for
    // every language); matched without case: drawinfo.0.w, form.2.growinfo.
    std::string path;
    std::string value;
    int line = 0;
    // <list path>.count: path is the list, elements its new count.
    bool count = false;
    uint32_t elements = 0;

    // A reference: the table it names a row of, and, once resolved, the id
    // or the enum name the table load looks up. vanilla is true for an id
    // written as a number, which is checked against the table.
    const char* refTable = nullptr;
    bool flag = false;
    uint64_t id = 0;
    std::string enumName;
    bool vanilla = false;
    // The key a typed section wrote, for messages, when it is not path:
    // item for itemID. Empty for a [row.] value.
    std::string name;
};

struct RowSpec {
    const table_schema::TableSchema* schema = nullptr;
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as written in messages.
    std::string fullKey;
    int line = 0;
    // "[Mod] Row blacksmith.Key" while reading, the label after.
    std::string where;
    // "Mod blacksmith row Mod.Key".
    std::string label;
    // The typed section the row comes from, "sale", "upgrade", "crop" or
    // "crop harvest", null for a [row.] section (rules_shops, rules_crops).
    const char* kind = nullptr;
    // The registry key, empty for RowRegistryKey's own. Rows with one key
    // share one id, and when one of them is dropped so are the others: a
    // [crop.] section's crops and cropsharvest rows.
    std::string registryKey;
    // The vanilla row copied first. 0 with basePath set: the first vanilla
    // row whose field basePath holds baseValue, a value resolved like any
    // other, and baseMissing says why when no row does.
    uint64_t baseId = 0;
    std::string basePath;
    RowValue baseValue;
    std::string baseMissing;
    // Top-level fields set to zero bytes after the copy: fields no path
    // can set, such as storesales randomInfo.
    std::vector<std::string> zero;
    // Counts outer lists first, then values in file order.
    std::vector<RowValue> counts;
    std::vector<RowValue> values;
    // Values no path writes, resolved like the others for finish to read:
    // a [crop.] section's seed, harvest and swaps. Each is told apart by
    // its name.
    std::vector<RowValue> inputs;
    RowFinish finish = nullptr;
    // Given by ResolveRows, 0 for a row that has none.
    uint64_t id = 0;
};

// The most mod rows Canopy takes on generated tables, in all.
constexpr size_t kMaxRows = 10000;

// Why schema's table takes no [row.] section, empty when it takes them: the
// game's save keeps the table's ids, or a typed section owns the table. A
// typed section to use instead is named when there is one.
std::string RowsRefused(const table_schema::TableSchema& schema);

// Reads every [row.<table>.<Key>] section of folders on a table without a
// curated schema, every [sale.<Key>] and [upgrade.<Key>] (rules_shops) and
// every [crop.<Key>] (rules_crops), in folder and file order: per mod key
// the first folder owns it, keys are unique per mod and table, a seed
// plants one crop, and at most kMaxRows load. accept is called once per
// folder with usable rows.
std::vector<RowSpec> ReadRows(const std::vector<ModFolder>& folders, Report& report, const FolderAccept& accept);

// The registry key of a row: its registryKey, else <table stem>.<Mod>.<Key>.
std::string RowRegistryKey(const RowSpec& row);
// Gives every key its number in the row band: numbers[i] is 0 for a key that
// gets none. False when the numbers cannot be kept, so no row gets one.
using RowNumbers = std::function<bool(const std::vector<std::string>& keys, std::vector<uint64_t>& numbers)>;
// Gives the rows their ids, then resolves every reference: a row of this
// mod's or another mod's [row.] by Key or Mod.Key, a part of an installed
// mod through refs (items, flags, icons, textures, skins, objects), a
// vanilla id, or an enum name. A row whose reference does not resolve, or
// names a row that was dropped, loses its id, and so does every row that
// shares its registry key. An empty number numbers the keys 1, 2, and so on.
void ResolveRows(std::vector<RowSpec>& rows, const RowNumbers& number, const Refs& refs, Report& report);
// The row of schema's table with modKey and key that has an id, or null.
const RowSpec* FindRow(
    const std::vector<RowSpec>& rows, const table_schema::TableSchema& schema, const std::string& modKey,
    const std::string& key
);
// The key a message names value by: its name, else its path.
const std::string& ValueName(const RowValue& value);
// Reads key = value of a [row.] section into row, as a path or a
// <list>.count. False, reported, when the row must be skipped.
bool ReadRowValue(const std::string& key, const std::string& value, int line, Report& report, RowSpec& row);
// Drops row and every row that shares its registry key: they lose their
// ids.
void DropRow(std::vector<RowSpec>& rows, const RowSpec& row);
// "2 blacksmith rows, 1 skilltree row and 3 sales".
std::string CountRows(const std::vector<const RowSpec*>& rows);
// The enum name a row gets before it is made unique against its table:
// CANOPY_<TABLE>_<MOD>_<KEY>.
std::string RowEnumName(const RowSpec& row);

// What building a row asks of the game's tables.
class RowTables {
public:
    virtual ~RowTables() = default;
    // The id of the row named name in file. False, with why, when there is
    // none.
    virtual bool EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const = 0;
    // False, with why, when a vanilla id written in a row is known not to
    // be in file. True when it is, or cannot be known yet.
    virtual bool VanillaFine(const char* file, uint64_t id, std::string& why) const = 0;
};

// The game's tables as the vanilla archives have them, for desktop checks.
// A mod row's enum name is made as its table loads, so only the game can
// look one up: EnumId gives such a name a stand-in id in the row band.
class VanillaRowTables : public RowTables {
public:
    explicit VanillaRowTables(const VanillaData& vanilla);

    bool EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const override;
    bool VanillaFine(const char* file, uint64_t id, std::string& why) const override;

private:
    const VanillaData& vanilla_;
};

// The text a resolved value writes: a reference as its id. False, with why,
// when an enum name is not in its table or a vanilla id is known not to be.
bool RowValueText(const RowValue& value, const RowTables& tables, std::string& text, std::string& why);

// Builds mod's row: a copy of its base from table, whose ids byId maps to
// rows, with the lists resized, the values written and finish run, texts
// through pool.
// The id and the enum name are left as the base has them. False, with why,
// when it cannot.
bool BuildRow(
    const RowSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId,
    const RowTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
);

// The desktop check: reads and resolves the rows and, with vanilla data,
// builds each one against the game's table.
void CheckRows(Context& context);

}  // namespace mod_rules
