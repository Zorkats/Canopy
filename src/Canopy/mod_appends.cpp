#define NOMINMAX
#include "mod_appends.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "blob.h"
#include "core.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_append.h"
#include "table_engine.h"
#include "table_schema.h"

// =============================================================
// LIST ADDITIONS DESIGN
// =============================================================
// [offering.], [catch.] and [forage.] sections add elements to lists inside
// rows the game already has: an item to offering pools, a fish to fishing
// spots, a catch to traps, an item to forage spots, a wild crop to areas.
// The rules (reading, resolving, applying) are CanopyCore's rules_append, so
// the desktop check says what the loader does. No id is given out and the
// save keeps none of these lists, so a mod that goes away leaves nothing
// behind.
//
// Install. At startup, after items and flags, whose Mod.Key a section may
// name, and after patches: every folder's sections are read, each folder
// with one is registered as a mod, and each table with an addition is handed
// to the table engine with table_engine::Rewrite. The engine runs the
// rewrites of a file in the order they registered, so a table's vanilla rows
// are patched first and its lists grown after, and a patch's element index
// always names a vanilla element.
//
// Load. The engine calls RewriteRows on the worker that loads
// framework_X.dat, then X.dat. An addition goes into whichever holds the
// rows it names, or into every row for all. A vanilla enum name it names is
// looked up then, waiting up to five seconds for its table, and a vanilla id
// is checked if its table loaded in an earlier batch. Once X.dat has loaded,
// what an addition named that neither file had is logged once.
//
// Title. Each addition is reported with the rows it went into, and any
// vanilla id it names that its table turned out not to have. The lists are
// k_vectors inside the game's records, which have no fixed place to read
// back, so the check stops at the rows.

namespace mod_appends {
namespace {

using namespace core;
namespace ts = table_schema;
using mod_rules::AppendSpec;

struct TableState {
    const ts::TableSchema* schema = nullptr;
    // Fixed once installed.
    std::vector<const AppendSpec*> specs;
    // Under g_Mutex.
    std::map<const AppendSpec*, std::set<size_t>> foundRows;
    std::map<const AppendSpec*, std::set<size_t>> foundMatches;
    std::set<const AppendSpec*> unresolved;
    bool reported = false;
};

// What one addition did, by row id: the elements each row got. Under
// g_Mutex.
struct SpecState {
    std::map<uint64_t, size_t> rows;
    bool failed = false;
};

// Fixed once installed.
std::vector<AppendSpec> g_Specs;
std::vector<std::unique_ptr<TableState>> g_Tables;
bool g_Active = false;
bool g_Checked = false;
// Guards every TableState's found sets and reported, g_Done and g_Said.
std::mutex g_Mutex;
std::map<const AppendSpec*, SpecState> g_Done;
// The failure lines already logged, so a table that loads twice says each
// once.
std::set<std::string> g_Said;

TableState* FindTable(const char* file) {
    for (const std::unique_ptr<TableState>& table : g_Tables) {
        if (mod_tables::IsTable(file, table->schema->file)) {
            return table.get();
        }
    }
    return nullptr;
}

std::string Stem(const char* file) {
    const std::string name = file;
    return name.size() > 4 ? name.substr(0, name.size() - 4) : name;
}

// Logs once what the additions of table named that no file of it had.
void ReportMissing(TableState& table) {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (table.reported) {
            return;
        }
        table.reported = true;
        for (const AppendSpec* spec : table.specs) {
            if (table.unresolved.count(spec)) {
                continue;
            }
            const std::set<size_t>& rows = table.foundRows[spec];
            const std::set<size_t>& matches = table.foundMatches[spec];
            const std::vector<std::string> missing = mod_rules::MissingMessages(
                *spec, std::vector<size_t>(rows.begin(), rows.end()), std::vector<size_t>(matches.begin(), matches.end())
            );
            lines.insert(lines.end(), missing.begin(), missing.end());
        }
    }
    for (const std::string& line : lines) {
        Log(line);
    }
}

// Registers the folder as a mod, as the other kinds register theirs.
bool Register(const mod_tables::Folder& folder, const std::string& modKey, size_t count) {
    bool joined = false;
    const std::string name = mod_rules::ModName(folder, modKey);
    if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
        CANOPY_NO_MOD) {
        Log("List additions in folder " + folder.name + " skipped, the mod could not be registered.");
        return false;
    }
    Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
        mod_tables::Plural(count, "list addition", "list additions") +
        (joined ? "." : ", registered for its list additions."));
    return true;
}

// ---- Table loads (game workers) ------------------------------------------

void RewriteRows(const char* file, bool framework, const blob::Table& table, std::vector<uint8_t>& rewritten) {
    TableState* state = FindTable(file);
    if (!state) {
        return;
    }
    const mod_tables::LoadingTables tables(state->schema->file);
    mod_rules::AppendResult result;
    mod_rules::ApplyAppends(table, state->specs, tables, rewritten, result);
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const mod_rules::AppendOutcome& outcome : result.outcomes) {
            const AppendSpec* spec = outcome.spec;
            state->foundRows[spec].insert(outcome.foundRows.begin(), outcome.foundRows.end());
            state->foundMatches[spec].insert(outcome.foundMatches.begin(), outcome.foundMatches.end());
            if (mod_rules::Unresolved(outcome)) {
                state->unresolved.insert(spec);
            }
            SpecState& done = g_Done[spec];
            for (const auto& row : outcome.rows) {
                done.rows[row.first] = row.second;
            }
            done.failed = done.failed || !outcome.failed.empty();
            for (const std::string& line : mod_rules::FailureMessages(outcome)) {
                if (g_Said.insert(line).second) {
                    lines.push_back(line);
                }
            }
        }
    }
    for (const std::string& line : lines) {
        Log(line);
    }
    if (!rewritten.empty()) {
        Log("List additions: " + std::string(framework ? "framework_" : "") + state->schema->file + " gets " +
            mod_tables::Plural(result.elements, "element", "elements") + " on " +
            mod_tables::Plural(result.rows, "row", "rows") + ".");
    }
    if (!framework) {
        ReportMissing(*state);
    }
}

// ---- Title self-check ----------------------------------------------------

// The vanilla ids spec names that their tables do not have, as "fish 123".
std::string MissingIds(const AppendSpec& spec) {
    std::string missing;
    std::vector<const mod_rules::AppendRef*> refs;
    for (const mod_rules::AppendRef& ref : spec.refs) {
        refs.push_back(&ref);
    }
    for (const mod_rules::AppendRef& ref : spec.matches) {
        refs.push_back(&ref);
    }
    for (const mod_rules::AppendRef* ref : refs) {
        if (!ref->vanilla || !ref->enumName.empty()) {
            continue;
        }
        for (const char* file : {ref->table, ref->alsoIn}) {
            if (file && table_engine::Known(file) && !table_engine::HasId(file, ref->id)) {
                missing += (missing.empty() ? "" : ", ") + ref->key + " " + ref->text + " (not in " + file + ")";
                break;
            }
        }
    }
    return missing;
}

void SelfCheck() {
    g_Checked = true;
    if (!table_engine::Complete()) {
        Log("List additions: the game tables were never seen loading, so none was added.");
        return;
    }
    for (const std::unique_ptr<TableState>& table : g_Tables) {
        ReportMissing(*table);
    }
    std::map<const AppendSpec*, SpecState> done;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        done = g_Done;
    }
    for (const AppendSpec& spec : g_Specs) {
        const SpecState& state = done[&spec];
        size_t elements = 0;
        for (const auto& row : state.rows) {
            elements += row.second;
        }
        const std::string missing = MissingIds(spec);
        std::string line = spec.label;
        if (state.rows.empty()) {
            line += state.failed ? " went into no row, as said above" : " went into no row";
        } else {
            line += " is in " + mod_tables::Plural(state.rows.size(), (Stem(spec.schema->file) + " row").c_str(),
                                                   (Stem(spec.schema->file) + " rows").c_str()) +
                    ", " + mod_tables::Plural(elements, "element", "elements");
        }
        Log(line + (missing.empty() ? "." : ", but it names " + missing + "."));
    }
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool beforeGameInit) {
    const std::vector<mod_tables::Folder> folders = mod_tables::FindFolders({"offering.", "catch.", "forage."});
    if (folders.empty()) {
        return false;
    }
    if (!ticking) {
        Log("List additions need the frame tick, so they are off.");
        return false;
    }
    if (!table_engine::Install(beforeGameInit)) {
        Log("List additions need the table engine, so they are off.");
        return false;
    }
    mod_rules::Report report = mod_tables::LogReport();
    g_Specs = mod_rules::ReadAppends(folders, mod_tables::LoaderRefs(), report, &Register);
    if (g_Specs.empty()) {
        Log("No usable list additions, so list additions are off.");
        return false;
    }
    std::set<std::string> mods;
    std::vector<const AppendSpec*> all;
    for (AppendSpec& spec : g_Specs) {
        spec.where = spec.label;
        mods.insert(spec.modKey);
        all.push_back(&spec);
        // The tables an enum name or a vanilla id is checked against.
        for (const std::vector<mod_rules::AppendRef>* refs : {&spec.refs, &spec.matches}) {
            for (const mod_rules::AppendRef& ref : *refs) {
                if (ref.vanilla) {
                    table_engine::Watch(ref.table);
                    if (ref.alsoIn) {
                        table_engine::Watch(ref.alsoIn);
                    }
                }
            }
        }
    }
    for (const ts::TableSchema* schema : ts::Patchable()) {
        auto table = std::make_unique<TableState>();
        table->schema = schema;
        for (const AppendSpec* spec : all) {
            if (spec->schema == schema) {
                table->specs.push_back(spec);
            }
        }
        if (!table->specs.empty()) {
            table_engine::Rewrite(schema->file, &RewriteRows);
            g_Tables.push_back(std::move(table));
        }
    }
    g_Active = true;
    Log("List additions installed: " + mod_rules::CountAppends(all) + " from " +
        mod_tables::Plural(mods.size(), "mod", "mods") + ".");
    return true;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

}  // namespace mod_appends
