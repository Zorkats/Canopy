#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "table_schema.h"

// The rules of [patch.<table>.<id>] sections, which change named fields of
// one vanilla row of a game table as the table loads. See docs/PATCHES.md.
//
// Three steps, each a pure function the loader and the desktop check share:
// ReadPatches reads every folder's sections, MergePatches decides per field
// which folder wins, and ApplyPatches writes the winners into a table's
// bytes. Only the loader touches the game; nothing here does.
namespace mod_rules {

extern const SectionInfo kPatchSection;

// One place a patch writes: a field path with every index spelled out, the
// way table_schema::ResolvePath names a Location (drawInfo.0.w, name.en,
// entries.2.texture), so one path is always one place.
struct PatchField {
    std::string path;
    // Checked against the field's type; a Mod.Key reference is already its id.
    std::string value;
    // Where it is set, for messages.
    std::string folder;
    std::string modKey;
    int line = 0;
    // The folder's place in load order: the higher one wins.
    size_t order = 0;
};

// One [patch.<table>.<id>] section as read.
struct PatchSection {
    const table_schema::TableSchema* schema = nullptr;
    std::string folder;
    std::string modKey;
    size_t order = 0;
    int line = 0;
    // The row: its id, or its enum name with id 0.
    uint64_t id = 0;
    std::string enumName;
    std::vector<PatchField> fields;
};

// Where a merged row patch was written, for messages.
struct PatchOrigin {
    std::string folder;
    std::string modKey;
    int line = 0;
};

// The patch of one row with every folder merged.
struct RowPatch {
    const table_schema::TableSchema* schema = nullptr;
    uint64_t id = 0;
    std::string enumName;
    // Each place once, with the value of the last folder that sets it, in
    // the order the places were first set.
    std::vector<PatchField> fields;
    // Every section that patches the row.
    std::vector<PatchOrigin> origins;
};

// "icon.dat 1000", or "icon.dat ICON_ID_ITEM_TEST" for a row named by its
// enum name.
std::string PatchLabel(const table_schema::TableSchema& schema, uint64_t id, const std::string& enumName);
std::string PatchLabel(const RowPatch& patch);

// Reads the [patch.] sections of folders, in folder and file order. A
// folder's order is its index in folders, which ScanMods sorts by name
// without case, turned-off folders already left out. Each bad section or
// key is reported at its line and skipped. refs answers a reference written
// as Mod.Key: items, icons and flags, the kinds the loader knows by then.
std::vector<PatchSection> ReadPatches(const std::vector<ModFolder>& folders, const Refs& refs, Report& report);

// The id of the row of schema's table named name, false when there is none.
using PatchEnumLookup =
    std::function<bool(const table_schema::TableSchema& schema, const std::string& name, uint64_t& id)>;

// Merges sections into one patch per row, in the order rows are first
// patched. Per place the last folder in load order wins; a place two folders
// set is a Warning that names both. Different places of one row from
// different folders all stay, with no message. lookup, when given, turns
// enum names into ids first, so a row written both ways is one row; without
// it a row named by enum name stays apart from the same row named by id.
// A folder that patches one row in two sections keeps the first.
std::vector<RowPatch> MergePatches(
    const std::vector<PatchSection>& sections, const PatchEnumLookup& lookup, Report& report
);

// The lookup over the game's own tables, the framework_ twin first: what
// the desktop check and the loader both merge with. vanilla must outlive
// it. A table with no enum name finds nothing.
PatchEnumLookup VanillaEnumLookup(const VanillaData& vanilla);

// What ApplyPatches did with a row patch whose row the table has.
struct PatchedRow {
    const RowPatch* patch = nullptr;
    // The row's id and its index in the table.
    uint64_t id = 0;
    size_t row = 0;
    // The fields written. A place two row patches set is written once, with
    // the later folder's value.
    std::vector<const PatchField*> applied;
    // The fields that do not fit this row, with why: an element past the
    // row's own count, or a row too short for the field.
    std::vector<std::pair<const PatchField*, std::string>> failed;
};

struct PatchResult {
    std::vector<PatchedRow> rows;
    // Row patches whose row the table does not have.
    std::vector<const RowPatch*> missing;
    size_t fields = 0;
};

// Writes patches, all of one table, into the table's bytes as out: every
// row in its place and of its size, the header's counts right, each text in
// a pool appended after the table's own, and every other byte as it was.
// False, with out empty, when no field was written.
bool ApplyPatches(
    const blob::Table& table, const std::vector<const RowPatch*>& patches, std::vector<uint8_t>& out,
    PatchResult& result
);

// "12 fields on 5 rows of 2 tables, from 3 mods".
std::string CountPatches(const std::vector<RowPatch>& patches);

// Where the game's loaded record keeps the field at path, with the field's
// type and size (its offset is 0, as only a row places it): the offset in
// the record, or -1 when it is not known or not comparable. Only numbers,
// Bools and references compare; texts, vector elements and fields without a
// recordOffset do not.
int64_t PatchRecordOffset(
    const table_schema::TableSchema& schema, const std::string& path, table_schema::Location& location
);

// The value of a reference field written as Mod.Key, as the id refs gives
// it; any other value is returned as written. False, with why, for a Mod.Key
// that names nothing.
bool ResolvePatchValue(
    const table_schema::Location& location, const std::string& value, const Refs& refs, std::string& resolved,
    std::string& why
);

// The desktop check: reads and merges the patches and, with vanilla data,
// checks that every row is in its table or its framework_ twin and that
// every field fits that row.
void CheckPatches(Context& context);

}  // namespace mod_rules
