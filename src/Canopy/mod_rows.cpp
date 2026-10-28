#define NOMINMAX
#include "mod_rows.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "core.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_crops.h"
#include "rules_rows.h"
#include "rules_shops.h"
#include "table_engine.h"
#include "table_schema.h"

// =============================================================
// MOD ROWS DESIGN
// =============================================================
// A mod adds a row to a game table with [row.<table>.<Key>]: base names the
// vanilla row copied, <list>.count resizes a list of it, and every other key
// sets a field by the path [patch.] sections use. Only the tables whose
// schema is generated take these; texture, icon and animereplacetexture rows
// are mod art's. A table whose ids the save keeps, or that a typed section
// owns, is refused by the rules with the section to use instead. A folder
// with rows and no Lua or DLL is still registered as a mod. With no rows,
// nothing below is hooked.
//
// [sale.<Key>] and [upgrade.<Key>] (rules_shops) are storesales and
// blacksmith rows read with the rest, so they share all of the below. The
// save keeps sales in the shop lists, which mod_shops handles. A [crop.<Key>]
// (rules_crops) is a crops row and a cropsharvest row with one id; the save
// keeps planted crops, which mod_crops handles, and it turns the seed item
// into a seed.
//
// Ids. Every row gets 2100000000 + n, n from mods\CanopyData\rows.ini under
// <table>.<Mod>.<Key>, one registry for every table, given out here before
// any table loads. No vanilla row of a generated table has an id in the band,
// and it stays below 2^31 for the tables that keep ids in 32 bits. A table
// with enum names gives the row CANOPY_<TABLE>_<MOD>_<KEY>, made unique
// against the table as it loads.
//
// References. A reference field takes a row of this mod or another's
// [row.] by Key or Mod.Key, a part another module installed (an item, a
// flag, an icon and so on) by Key or Mod.Key, a vanilla id, or an enum name.
// Mod references are checked here, at startup. An enum name is looked up as
// the row is built, waiting up to five seconds for its table on the worker.
// A vanilla id is checked then if its table loaded in an earlier batch, and
// at the title otherwise, where a missing one is reported: the row stays, as
// a vanilla row naming a missing id would.
//
// Loads. The table engine calls the append on the game's job workers, after
// any mod art or object rows of the same file. It only reads the rows, which
// never change after Install, and sets each row's appended flag.
//
// Title. The tick looks each row up through the game's own find and reports
// it, with any vanilla id it names that its table does not have.

namespace mod_rows {
namespace {

using namespace core;
using mod_tables::Plural;
namespace ts = table_schema;

std::vector<mod_rules::RowSpec> g_Rows;
// One per row of g_Rows, set by the table load on a worker.
std::unique_ptr<std::atomic<bool>[]> g_Appended;
bool g_Active = false;
bool g_Checked = false;
// Set by DisableSales: sale rows are not appended.
bool g_SalesOff = false;
// One per row of g_Rows: set by DropCrop and DisableCrops, the row is not
// appended.
std::vector<char> g_Dropped;

const char* const kRegistryHeader =
    "; Canopy mod row ids, for the [row.] sections of generated tables. Each mod row keeps its number for\r\n"
    "; good, and a number is never given out twice. Do not edit or delete this file.\r\n";

bool Number(const std::vector<std::string>& keys, std::vector<uint64_t>& numbers) {
    return mod_tables::AssignNumbers(ts::kRowRegistry, kRegistryHeader, "Mod rows", ts::kRowBandSize, keys, numbers);
}

std::vector<const mod_rules::RowSpec*> Counted(const std::vector<mod_rules::RowSpec>& rows) {
    std::vector<const mod_rules::RowSpec*> counted;
    for (const mod_rules::RowSpec& row : rows) {
        counted.push_back(&row);
    }
    return counted;
}

bool IsSale(const mod_rules::RowSpec& row) {
    return row.kind && std::strcmp(row.kind, mod_rules::kSaleKind) == 0;
}

std::string Stem(const char* file) {
    const std::string name = file;
    return name.size() > 4 ? name.substr(0, name.size() - 4) : name;
}

bool IsCropRow(const mod_rules::RowSpec& row) {
    return row.kind && (std::strcmp(row.kind, mod_rules::kCropKind) == 0 ||
                        std::strcmp(row.kind, mod_rules::kCropHarvestKind) == 0);
}

// True when folder has a [row.<table>.<Key>] section on a table that is not
// mod art's, or a [sale.], [upgrade.] or [crop.] section.
bool HasRows(const mod_tables::Folder& folder) {
    for (const canopy_ini::IniSection& section : folder.sections) {
        if (mod_rules::IsShopSection(section) || mod_rules::IsCropSection(section)) {
            return true;
        }
        const std::string rest = mod_tables::SectionKey(section, "row.");
        const size_t dot = rest.find('.');
        if (dot != std::string::npos && !ts::Find(rest.substr(0, dot))) {
            return true;
        }
    }
    return false;
}

// ---- Rows (game workers) -------------------------------------------------

void AppendRows(const char* file, uint8_t*, const blob::Table& table, blob::Appender& appender) {
    const ts::TableSchema* schema = ts::FindPatchable(file);
    if (!schema) {
        return;
    }
    std::unordered_map<uint64_t, size_t> byId;
    for (size_t index = 0; index < table.rows.size(); ++index) {
        byId.emplace(blob::RowId(table.rows[index]), index);
    }
    const bool named = schema->enumOffset >= 0;
    blob::EnumNames names = named ? blob::EnumNames(table, static_cast<size_t>(schema->enumOffset)) : blob::EnumNames();
    const mod_tables::LoadingTables tables(schema->file);
    for (size_t index = 0; index < g_Rows.size(); ++index) {
        const mod_rules::RowSpec& mod = g_Rows[index];
        if (mod.schema != schema || g_Dropped[index] || (g_SalesOff && IsSale(mod))) {
            continue;
        }
        if (byId.count(mod.id) || table_engine::HasId(schema->file, mod.id)) {
            Log(mod.label + " skipped, its id " + std::to_string(mod.id) + " is already in " + schema->file + ".");
            continue;
        }
        std::vector<uint8_t> row;
        std::string why;
        if (!mod_rules::BuildRow(mod, table, byId, tables, appender.Texts(), row, why)) {
            Log(mod.label + " skipped, " + why + ".");
            continue;
        }
        blob::Put<uint64_t>(row.data(), 0, mod.id);
        if (named) {
            const std::string stem = mod_rules::RowEnumName(mod);
            std::string name = names.Unique(stem);
            while (table_engine::HasEnum(schema->file, name)) {
                name = names.Unique(stem);
            }
            appender.Texts().Write(row, static_cast<size_t>(schema->enumOffset), name);
        }
        appender.Add(row);
        g_Appended[index].store(true, std::memory_order_release);
    }
}

// ---- Title self-check ----------------------------------------------------

void CheckRow(const mod_rules::RowSpec& mod, size_t index) {
    if (!g_Appended[index].load(std::memory_order_acquire)) {
        return;
    }
    const std::string what = mod.label + " is id " + std::to_string(mod.id);
    if (!mod_tables::FindRecord(table_engine::Database(mod.schema->file), mod.id)) {
        Log(what + ", but the " + mod.schema->section + " table does not have it.");
        return;
    }
    std::string missing;
    for (const mod_rules::RowValue& value : mod.values) {
        if (!value.vanilla || _stricmp(value.refTable, "gameflag.dat") == 0 || !table_engine::Known(value.refTable) ||
            table_engine::HasId(value.refTable, value.id)) {
            continue;
        }
        missing += (missing.empty() ? "" : ", ") + mod_rules::ValueName(value) + " names " + Stem(value.refTable) + " " +
                   std::to_string(value.id);
    }
    if (!missing.empty()) {
        Log(what + ", but " + missing + ", which its table does not have.");
        return;
    }
    Log(what + ".");
}

void SelfCheck() {
    g_Checked = true;
    if (!table_engine::Complete()) {
        Log("Mod rows: the game tables were never seen loading, so mod rows are off.");
        return;
    }
    for (size_t index = 0; index < g_Rows.size(); ++index) {
        CheckRow(g_Rows[index], index);
    }
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool beforeGameInit) {
    std::vector<mod_tables::Folder> folders = mod_tables::FindFolders(
        {"row.", mod_rules::kSalePrefix, mod_rules::kUpgradePrefix, mod_rules::kCropPrefix}
    );
    folders.erase(std::remove_if(folders.begin(), folders.end(), [](const mod_tables::Folder& folder) {
                      return !HasRows(folder);
                  }), folders.end());
    if (folders.empty()) {
        return false;
    }
    if (!ticking) {
        Log("Mod rows need the frame tick, so they are off.");
        return false;
    }
    if (!table_engine::Install(beforeGameInit)) {
        Log("Mod rows need the table engine, so they are off.");
        return false;
    }
    const mod_rules::FolderAccept accept = [](const mod_tables::Folder& folder, const std::string& modKey,
                                              size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Rows in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " + Plural(count, "row", "rows") +
            (joined ? "." : ", registered for its rows."));
        return true;
    };
    mod_rules::Report report = mod_tables::LogReport();
    std::vector<mod_rules::RowSpec> rows = mod_rules::ReadRows(folders, report, accept);
    mod_rules::ResolveRows(rows, &Number, mod_tables::LoaderRefs(), report);
    rows.erase(
        std::remove_if(rows.begin(), rows.end(), [](const mod_rules::RowSpec& row) { return row.id == 0; }), rows.end()
    );
    if (rows.empty()) {
        Log("No usable mod rows, so mod rows are off.");
        return false;
    }
    g_Rows = std::move(rows);
    g_Appended = std::make_unique<std::atomic<bool>[]>(g_Rows.size());
    g_Dropped.assign(g_Rows.size(), 0);
    std::set<const ts::TableSchema*> tables;
    std::set<std::string> mods;
    for (mod_rules::RowSpec& row : g_Rows) {
        row.where = row.label;
        mods.insert(row.modKey);
        if (tables.insert(row.schema).second) {
            table_engine::Contribute(row.schema->file, &AppendRows);
        }
        // The tables an enum name or a vanilla id is checked against.
        for (const mod_rules::RowValue& value : row.values) {
            if (value.refTable && (value.vanilla || !value.enumName.empty())) {
                table_engine::Watch(value.refTable);
            }
        }
    }
    g_Active = true;
    Log("Mod rows installed: " + mod_rules::CountRows(Counted(g_Rows)) + " from " + Plural(mods.size(), "mod", "mods") +
        ".");
    return true;
}

bool HasSales() {
    return std::any_of(g_Rows.begin(), g_Rows.end(), IsSale);
}

void DisableSales() {
    const size_t count = static_cast<size_t>(std::count_if(g_Rows.begin(), g_Rows.end(), IsSale));
    if (count > 0 && !g_SalesOff) {
        g_SalesOff = true;
        Log("Mod rows: " + Plural(count, "sale is", "sales are") + " off, mod shops could not start.");
    }
}

std::vector<Crop> Crops() {
    std::vector<Crop> crops;
    for (const mod_rules::RowSpec& row : g_Rows) {
        if (!mod_rules::IsCrop(row)) {
            continue;
        }
        const mod_rules::RowValue* seed = mod_rules::CropSeed(row);
        crops.push_back({row.fullKey, row.id, row.baseId, seed ? seed->id : 0});
    }
    return crops;
}

bool HasCrops() {
    return std::any_of(g_Rows.begin(), g_Rows.end(), [](const mod_rules::RowSpec& row) {
        return mod_rules::IsCrop(row);
    });
}

void DropCrop(const std::string& fullKey, const std::string& why) {
    for (size_t index = 0; index < g_Rows.size(); ++index) {
        const mod_rules::RowSpec& row = g_Rows[index];
        if (IsCropRow(row) && _stricmp(row.fullKey.c_str(), fullKey.c_str()) == 0) {
            if (mod_rules::IsCrop(row) && !g_Dropped[index]) {
                Log(row.label + " skipped, " + why + ".");
            }
            g_Dropped[index] = 1;
        }
    }
}

void DisableCrops() {
    size_t count = 0;
    for (size_t index = 0; index < g_Rows.size(); ++index) {
        if (IsCropRow(g_Rows[index]) && !g_Dropped[index]) {
            count += mod_rules::IsCrop(g_Rows[index]) ? 1 : 0;
            g_Dropped[index] = 1;
        }
    }
    if (count > 0) {
        Log("Mod rows: " + Plural(count, "crop is", "crops are") + " off, mod crops could not start.");
    }
}

bool CropAppended(uint64_t id) {
    size_t appended = 0;
    for (size_t index = 0; index < g_Rows.size(); ++index) {
        if (IsCropRow(g_Rows[index]) && g_Rows[index].id == id && g_Appended[index].load(std::memory_order_acquire)) {
            ++appended;
        }
    }
    return appended == 2;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

}  // namespace mod_rows
