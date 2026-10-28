#pragma once
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "mod_rules.h"
#include "rules_rows.h"

// Shared pieces for the modules that append rows to a game table from
// canopy.ini sections: the table blob format, the id registries in
// mods\CanopyData, the mod folder scan, value parsing and database lookups.
// The canopy.ini rules themselves live in CanopyCore's mod_rules, shared with
// the desktop tools; the value helpers here are theirs.
namespace mod_tables {

// ---- Values --------------------------------------------------------------

constexpr size_t kTextRef = 8;
constexpr const char* kLanguageKeys[retail::kLanguageCount] = {"ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"};

using mod_rules::EnumPart;
using mod_rules::FullKey;
using mod_rules::IdentifierValid;
using mod_rules::ModulePart;
using mod_rules::ParseI32;
using mod_rules::ParseU64;
using mod_rules::Plural;
using mod_rules::ReadTexts;
using mod_rules::ScriptPathValid;
using mod_rules::SectionKey;
using mod_rules::SplitList;
using mod_rules::TextValid;

// ---- References ----------------------------------------------------------
// Startup thread, after mod items and mod flags are installed.

// A flag: a flag of modKey, Mod.Flag, or a vanilla flag id outside the mod
// flag band. 0 when there is no such flag.
int32_t FlagId(const std::string& modKey, const std::string& ref);
// An item: an item of modKey, Mod.Item, or a vanilla item id below the mod
// id ranges. 0 when there is no such mod item. A vanilla id is checked
// against the item table only at the title.
uint64_t ItemId(const std::string& modKey, const std::string& ref);

// What the loader installed, for the rules: mod flags, items, objects,
// textures, icons, skins, villagers and whistles by Mod.Key. Startup thread, after each is installed.
const mod_rules::Refs& LoaderRefs();
// A report that writes each line to Canopy.log as it comes.
mod_rules::Report LogReport();

// ---- Mod folders ---------------------------------------------------------

using Folder = mod_rules::ModFolder;

// The folders in the mods folder whose canopy.ini has a section starting
// with one of prefixes, in name order without case.
std::vector<Folder> FindFolders(const std::vector<const char*>& prefixes);

// ---- Id registries -------------------------------------------------------
// mods\CanopyData\<file>: [ids], next = N, then Key = n lines. A number is
// given out once and never again, even after its key is gone.

// Gives every key its number below limit, giving new keys the next free
// one and writing the file when one was given. numbers[i] is 0 for a key
// that got none. False, logged under label, when the file cannot be read
// or written, so no number is ever reused.
bool AssignNumbers(
    const char* file, const char* header, const char* label, uint64_t limit, const std::vector<std::string>& keys,
    std::vector<uint64_t>& numbers
);

// ---- Table blobs (boot worker) -------------------------------------------
// u32 row count, u32 row bytes, u32 pool bytes, each row as a u32 length
// and its bytes, then the string pool. A text is a u32 pool offset and a
// u32 length.

struct Row {
    const uint8_t* data;
    uint32_t size;
};

struct Table {
    uint32_t count = 0;
    uint32_t rowBytes = 0;
    uint32_t poolBytes = 0;
    size_t rowsSize = 0;
    const uint8_t* pool = nullptr;
    std::unordered_map<uint64_t, Row> rows;
    std::vector<uint64_t> order;
    std::set<uint32_t> enumCrcs;
    std::set<std::string> enums;
};

class TextPool {
public:
    explicit TextPool(uint32_t base) : base_(base), bytes_(1, '\0') {}

    // A text ref to text, or to the leading NUL when it is empty.
    void Write(std::vector<uint8_t>& row, size_t offset, const std::string& text);

    const std::string& Bytes() const {
        return bytes_;
    }

private:
    uint32_t base_;
    std::string bytes_;
};

// The rows of one table that a module appends, and the enum names it gave.
class Appender {
public:
    explicit Appender(const Table& table) : table_(table), pool_(table.poolBytes) {}

    TextPool& Pool() {
        return pool_;
    }

    // stem, or stem_2, stem_3 and so on, whichever no row of the table or
    // of this appender has.
    std::string UniqueEnum(const std::string& stem);
    void Add(const std::vector<uint8_t>& row);
    size_t Count() const {
        return count_;
    }
    // The vanilla blob, then the appended rows, then the vanilla pool and
    // the appended pool.
    void Merge(const uint8_t* blob, std::vector<uint8_t>& merged) const;

private:
    const Table& table_;
    TextPool pool_;
    std::vector<uint8_t> rows_;
    std::set<std::string> enums_;
    std::set<uint32_t> crcs_;
    size_t count_ = 0;
};

// Splits blob into rows keyed by the u64 id at +0, and collects the enum
// names at enumOffset. False when the lengths do not add up or a row is
// shorter than minRow.
bool ParseTable(const uint8_t* blob, size_t minRow, size_t enumOffset, Table& table);
std::string PoolText(const Table& table, const uint8_t* row, size_t offset);
// True when the table name's file part is file, without case.
bool IsTable(const char* name, const char* file);
// True when the database's id map already holds id. The map is empty on the
// first load.
bool DatabaseHas(uint8_t* db, uint64_t id);

// ---- Tables as they load (game workers) ---------------------------------

// The game's tables as the table engine sees them while own loads, for the
// rules that build rows or lists. An enum name waits up to five seconds for
// its table. A vanilla id is checked against a table that loaded in an
// earlier batch, which is known in full; any other is left for the title.
class LoadingTables : public mod_rules::RowTables {
public:
    explicit LoadingTables(const char* own) : own_(own) {}

    bool EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const override;
    bool VanillaFine(const char* file, uint64_t id, std::string& why) const override;

private:
    const char* own_;
};

// ---- Databases (game thread) ---------------------------------------------

// The record of id, null when the database does not have it.
uint8_t* FindRecord(uint8_t* database, uint64_t id);
// Where the database keeps the record of id: the value slot of its id map
// node, which a status's data pointer points at. Null when there is none.
uint8_t** FindHolder(uint8_t* database, uint64_t id);
// The game's item database, null before it exists.
uint8_t* ItemDatabase();

}  // namespace mod_tables
