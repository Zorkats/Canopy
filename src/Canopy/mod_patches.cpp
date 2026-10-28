#define NOMINMAX
#include "mod_patches.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "blob.h"
#include "core.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "platform_files.h"
#include "rules_patch.h"
#include "table_engine.h"
#include "table_schema.h"

// =============================================================
// MOD PATCHES DESIGN
// =============================================================
// A [patch.<table>.<id>] section changes named fields of a row the game
// already has. The rules (reading, merging, applying) are CanopyCore's
// rules_patch, so the desktop check says what the loader does.
//
// Install. At startup, after art, items and flags, so a patch may name their
// Mod.Key: every folder's sections are read, each folder with one is
// registered as a mod (so a mod that only patches is in the mod list), and
// the sections are merged, one patch per row, the last folder in load order
// winning a field two set. When a section names its row by enum name, the
// game's own tables are read from its archives for the merge, as canopy
// validate reads them, so that row and the same row named by id are one;
// the archives are opened with the share mode the game uses and closed
// before Install returns. Each table with a patch is handed to the table
// engine with table_engine::Rewrite. Nothing is hooked when there is no
// usable patch.
//
// Load. The engine calls RewriteRows on the worker that loads
// framework_X.dat, then X.dat, with the vanilla rows, before any module
// appends rows. A patch applies to whichever of the two holds its row, and
// the engine passes the rewritten bytes on: to the game, to the modules that
// append rows (whose copies of a base row so start from the patched one) and
// to the per-table detours of items, villagers, whistles and recipes, which
// see the patched rows as vanilla. Once X.dat has loaded, a patch whose row
// neither file had is logged once as skipped.
//
// Title. Each applied field the game's record keeps at a known place, a
// number, a Bool or a reference, is compared with the patched value; texts,
// vector elements and fields with no record place cannot be checked. When
// the engine never saw the tables load, Canopy started late and nothing was
// patched, which is logged once.

namespace mod_patches {
namespace {

using namespace core;
namespace ts = table_schema;
using FT = ts::FieldType;
using mod_rules::PatchField;
using mod_rules::RowPatch;

struct TableState {
    const ts::TableSchema* schema = nullptr;
    // Fixed once installed.
    std::vector<const RowPatch*> patches;
    // Under g_Mutex.
    std::set<const RowPatch*> found;
    bool reported = false;
};

struct AppliedField {
    const ts::TableSchema* schema;
    uint64_t id;
    const PatchField* field;
};

// Fixed once installed.
std::vector<RowPatch> g_Patches;
std::vector<std::unique_ptr<TableState>> g_Tables;
bool g_Active = false;
bool g_Checked = false;
// Guards every TableState's found and reported, and g_Applied.
std::mutex g_Mutex;
std::vector<AppliedField> g_Applied;

TableState* FindTable(const char* file) {
    for (const std::unique_ptr<TableState>& table : g_Tables) {
        if (mod_tables::IsTable(file, table->schema->file)) {
            return table.get();
        }
    }
    return nullptr;
}

// Logs once each patch of table whose row no file had.
void ReportMissing(TableState& table) {
    std::vector<const RowPatch*> missing;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (table.reported) {
            return;
        }
        table.reported = true;
        for (const RowPatch* patch : table.patches) {
            if (!table.found.count(patch)) {
                missing.push_back(patch);
            }
        }
    }
    for (const RowPatch* patch : missing) {
        Log("Patch " + mod_rules::PatchLabel(*patch) + " skipped, the game's table has no such row.");
    }
}

// Registers each folder with a section as a mod, as the other kinds register
// theirs, and returns the sections of the folders that were registered, in
// load order.
std::vector<mod_rules::PatchSection> Register(
    const std::vector<mod_tables::Folder>& folders, std::vector<mod_rules::PatchSection>& sections
) {
    std::vector<mod_rules::PatchSection> kept;
    for (const mod_tables::Folder& folder : folders) {
        size_t rows = 0;
        size_t fields = 0;
        std::string modKey;
        for (const mod_rules::PatchSection& section : sections) {
            if (section.folder == folder.name) {
                ++rows;
                fields += section.fields.size();
                modKey = section.modKey;
            }
        }
        if (rows == 0) {
            continue;
        }
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Patches in folder " + folder.name + " skipped, the mod could not be registered.");
            continue;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") patches " +
            mod_tables::Plural(fields, "field", "fields") + " on " + mod_tables::Plural(rows, "row", "rows") +
            (joined ? "." : ", registered for its patches."));
        for (mod_rules::PatchSection& section : sections) {
            if (section.folder == folder.name) {
                kept.push_back(std::move(section));
            }
        }
    }
    return kept;
}

// ---- Table loads (game workers) ------------------------------------------

void RewriteRows(const char* file, bool framework, const blob::Table& table, std::vector<uint8_t>& rewritten) {
    TableState* state = FindTable(file);
    if (!state) {
        return;
    }
    mod_rules::PatchResult result;
    mod_rules::ApplyPatches(table, state->patches, rewritten, result);
    size_t rows = 0;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const mod_rules::PatchedRow& row : result.rows) {
            state->found.insert(row.patch);
            rows += row.applied.empty() ? 0 : 1;
            for (const PatchField* field : row.applied) {
                g_Applied.push_back({state->schema, row.id, field});
            }
        }
    }
    for (const mod_rules::PatchedRow& row : result.rows) {
        for (const auto& [field, why] : row.failed) {
            Log("[" + field->modKey + "] Patch " + mod_rules::PatchLabel(*row.patch) + ", " + field->path +
                " skipped, " + why + ".");
        }
    }
    if (!rewritten.empty()) {
        Log("Patches: " + std::string(framework ? "framework_" : "") + state->schema->file + " gets " +
            mod_tables::Plural(result.fields, "field", "fields") + " on " + mod_tables::Plural(rows, "row", "rows") +
            ".");
    }
    if (!framework) {
        ReportMissing(*state);
    }
}

// ---- Title self-check ----------------------------------------------------

// The value at data as the location's type writes it.
std::string Show(const ts::Location& location, const uint8_t* data) {
    switch (location.type) {
    case FT::I32:
    case FT::FlagRef:
        return std::to_string(blob::Get<int32_t>(data, 0));
    case FT::U32:
    case FT::Ref32:
        return std::to_string(blob::Get<uint32_t>(data, 0));
    case FT::F32: {
        char text[32];
        std::snprintf(text, sizeof(text), "%g", static_cast<double>(blob::Get<float>(data, 0)));
        return text;
    }
    case FT::Bool:
        return data[0] != 0 ? "1" : "0";
    case FT::I64:
        return std::to_string(blob::Get<int64_t>(data, 0));
    default:
        return std::to_string(blob::Get<uint64_t>(data, 0));
    }
}

void SelfCheck() {
    g_Checked = true;
    if (!table_engine::Complete()) {
        Log("Patches: the game tables were never seen loading, so no patch applied.");
        return;
    }
    for (const std::unique_ptr<TableState>& table : g_Tables) {
        ReportMissing(*table);
    }
    std::vector<AppliedField> applied;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        applied = g_Applied;
    }
    size_t match = 0;
    size_t differ = 0;
    size_t unchecked = 0;
    for (const AppliedField& entry : applied) {
        ts::Location location;
        const int64_t offset = mod_rules::PatchRecordOffset(*entry.schema, entry.field->path, location);
        // The patched value as the row holds it, at the start of a scratch row.
        std::vector<uint8_t> expected(location.size);
        ts::Location scratch = location;
        scratch.offset = 0;
        blob::Pool pool(0);
        std::string why;
        if (offset < 0 || location.size == 0 || !ts::Write(expected, scratch, entry.field->value, pool, why)) {
            ++unchecked;
            continue;
        }
        const std::string where = mod_rules::PatchLabel(*entry.schema, entry.id, std::string()) + " " +
                                  entry.field->path;
        const uint8_t* record = mod_tables::FindRecord(table_engine::Database(entry.schema->file), entry.id);
        if (!record) {
            ++differ;
            Log("Patch self-check: " + where + " cannot be found, the game has no record " +
                std::to_string(entry.id) + ".");
            continue;
        }
        const uint8_t* actual = record + offset;
        const bool same = location.type == FT::Bool ? (actual[0] != 0) == (expected[0] != 0)
                                                     : std::memcmp(actual, expected.data(), expected.size()) == 0;
        if (same) {
            ++match;
            continue;
        }
        ++differ;
        Log("Patch self-check: " + where + " is " + Show(location, actual) + " in the game, not " +
            Show(location, expected.data()) + ".");
    }
    Log("Patch self-check: " + mod_tables::Plural(match, "field matches", "fields match") + ", " +
        std::to_string(differ) + (differ == 1 ? " differs, " : " differ, ") + std::to_string(unchecked) +
        " cannot be checked.");
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool beforeGameInit) {
    const std::vector<mod_tables::Folder> folders = mod_tables::FindFolders({"patch."});
    if (folders.empty()) {
        Log("No patches, so patches are off.");
        return false;
    }
    if (!ticking) {
        Log("Patches need the frame tick, so they are off.");
        return false;
    }
    if (!table_engine::Install(beforeGameInit)) {
        Log("Patches need the table engine, so they are off.");
        return false;
    }
    mod_rules::Report report = mod_tables::LogReport();
    std::vector<mod_rules::PatchSection> read = mod_rules::ReadPatches(folders, mod_tables::LoaderRefs(), report);
    const std::vector<mod_rules::PatchSection> sections = Register(folders, read);
    // The tables have not loaded yet, so a row named by its enum name is
    // looked up in the archives; read only when a section needs it.
    mod_rules::VanillaData vanilla;
    mod_rules::PatchEnumLookup lookup;
    if (std::any_of(sections.begin(), sections.end(), [](const mod_rules::PatchSection& section) {
            return !section.enumName.empty();
        })) {
        std::string folder = ModFilePath("");
        folder.pop_back();
        std::string error;
        if (vanilla.Open(platform::AnsiToWide(folder), error)) {
            lookup = mod_rules::VanillaEnumLookup(vanilla);
        } else {
            Log("Patches: the game's archives could not be read (" + error +
                "), so a row named by enum name is found as its table loads.");
        }
    }
    g_Patches = mod_rules::MergePatches(sections, lookup, report);
    if (g_Patches.empty()) {
        Log("No usable patches, so patches are off.");
        return false;
    }
    for (const ts::TableSchema* schema : ts::Patchable()) {
        auto table = std::make_unique<TableState>();
        table->schema = schema;
        for (const RowPatch& patch : g_Patches) {
            if (patch.schema == schema) {
                table->patches.push_back(&patch);
            }
        }
        if (!table->patches.empty()) {
            table_engine::Rewrite(schema->file, &RewriteRows);
            g_Tables.push_back(std::move(table));
        }
    }
    g_Active = true;
    Log("Patches: " + mod_rules::CountPatches(g_Patches) + ".");
    return true;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

}  // namespace mod_patches
