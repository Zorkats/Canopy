#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "rules_rows.h"
#include "table_schema.h"

// The rules of the sections that add elements to lists inside vanilla rows:
// [offering.<Key>] (an item in offering pools, offeringitem.dat), and
// [catch.<Key>] and [forage.<Key>] (a fish at fishing spots, a catch in
// traps, an item at forage spots, a wild crop in areas: fishing.dat,
// capture.dat, placementitem.dat and naturaloccurrence.dat). See
// docs/SHOPS.md and docs/CATCHES.md.
//
// Each section becomes one AppendSpec: the rows it goes to and the elements
// it adds to a list of each, every element's fields set by the paths
// [row.] sections use. ApplyAppends adds them as the table loads, the same
// way for the loader and the desktop check.
namespace mod_rules {

extern const SectionInfo kOfferingSection;
extern const SectionInfo kCatchSection;
extern const SectionInfo kForageSection;

// A reference a section makes: an id known at startup, or a vanilla enum
// name looked up as the tables load.
struct AppendRef {
    // The table it names a row of: "item.dat".
    const char* table = nullptr;
    // The key it was written under, for messages: fish, pools.
    std::string key;
    std::string text;
    uint64_t id = 0;
    std::string enumName;
    // An id written as a number, or found by enum name, which the table is
    // checked for.
    bool vanilla = false;
    // A second table a vanilla id must be in too: fish.dat for a fish,
    // whose id is its item's.
    const char* alsoIn = nullptr;
};

// One field of a new element: a path inside the element, and its value, or
// the index of the reference in AppendSpec::refs that gives it.
struct ElementValue {
    std::string path;
    std::string value;
    int ref = -1;
};

struct AppendSpec {
    const SectionInfo* section = nullptr;
    const table_schema::TableSchema* schema = nullptr;
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key.
    std::string fullKey;
    int line = 0;
    // "[Mod] Catch Key" while reading, the label after.
    std::string where;
    // "Mod catch Mod.Key".
    std::string label;
    // The rows of schema's table that get the elements: every row, or the
    // ones rows names.
    bool allRows = false;
    std::vector<AppendRef> rows;
    // The list the elements go to, a path from the row (itemInfo). With
    // outer set, it is a list inside the elements of the outer list
    // (tableInfo.<i>.itemInfo), of every element when allOuter is set, or
    // of those whose field match names one of matches.
    std::string outer;
    std::string match;
    bool allOuter = false;
    std::vector<AppendRef> matches;
    std::string list;
    // The elements added to each list, in order.
    std::vector<std::vector<ElementValue>> elements;
    std::vector<AppendRef> refs;
};

// The most append sections Canopy takes, in all.
constexpr size_t kMaxAppends = 2000;

// Reads every [offering.], [catch.] and [forage.] section of folders, in
// folder and file order: per mod key the first folder owns it, keys are
// unique per mod and kind, and at most kMaxAppends load. References to mod
// parts resolve through refs. accept is called once per folder with usable
// sections.
std::vector<AppendSpec> ReadAppends(
    const std::vector<ModFolder>& folders, const Refs& refs, Report& report, const FolderAccept& accept
);

// "2 catches and 1 offering".
std::string CountAppends(const std::vector<const AppendSpec*>& specs);

// What ApplyAppends did with one spec.
struct AppendOutcome {
    const AppendSpec* spec = nullptr;
    // The ids of the rows it went into, with the elements each got.
    std::vector<std::pair<uint64_t, size_t>> rows;
    // The rows it did not go into, with why: a full list, an element that
    // does not fit. A reference that does not resolve leaves rows empty and
    // says why once, with row id 0.
    std::vector<std::pair<uint64_t, std::string>> failed;
    // The indexes in spec->rows of the rows this table has, and in
    // spec->matches of the ones some row has an outer element for.
    std::vector<size_t> foundRows;
    std::vector<size_t> foundMatches;
};

struct AppendResult {
    std::vector<AppendOutcome> outcomes;
    size_t elements = 0;
    size_t rows = 0;
};

// Adds the elements of specs, all of one table, to the table's rows as out:
// every row in its place, those with elements grown, the header's counts
// right and the new texts in a pool after the table's own. A spec goes into
// each row it names that the table has, all its elements or none per row.
// References resolve through tables. False, with out empty, when no element
// was added.
bool ApplyAppends(
    const blob::Table& table, const std::vector<const AppendSpec*>& specs, const RowTables& tables,
    std::vector<uint8_t>& out, AppendResult& result
);

// True when a reference of the outcome's spec did not resolve, so it went
// nowhere and what it named was not looked for.
bool Unresolved(const AppendOutcome& outcome);
// The outcome's failures as lines, one per reason, each naming the rows it
// kept out of: "Mod catch A.B skipped on capture 3 and 7, captureInfo can
// have 32 elements at most."
std::vector<std::string> FailureMessages(const AppendOutcome& outcome);
// What spec named that no file of its table had, once all have loaded, as
// lines: rows are the indexes in spec.rows that were found, matches those in
// spec.matches.
std::vector<std::string> MissingMessages(
    const AppendSpec& spec, const std::vector<size_t>& rows, const std::vector<size_t>& matches
);

// The desktop check: reads the sections and, with vanilla data, applies
// them to the game's tables as the loader would.
void CheckAppends(Context& context);

}  // namespace mod_rules
