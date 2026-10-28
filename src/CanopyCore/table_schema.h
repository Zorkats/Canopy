#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "blob.h"

// Each game table Canopy writes rows to, described once as data: where every
// field of a row sits in the table file, what type it is, which table a
// reference points into, and where the game keeps it in the loaded record.
// The loader builds and checks rows from these, the [row.<table>.<Key>]
// sections address fields by these names, and the desktop tools can show
// and edit the same fields. Nothing here touches game memory.
namespace table_schema {

constexpr int kLanguageCount = 7;
// The language order of every per-language field.
extern const char* const kLanguageKeys[kLanguageCount];

enum class FieldType : uint8_t {
    // Texts, as pool refs.
    Id,
    Enum,
    Text,
    MLText,
    MLPath,
    // Plain values.
    I32,
    U32,
    F32,
    Bool,
    U64,
    I64,
    // Ids of rows in another table, or of a flag.
    Ref64,
    Ref32,
    FlagRef,
    // Structure.
    Array,
    Vector,
    Trailer,
    Pad,
    // A group of fields kept together, addressed as <name>.<field>.
    Struct,
};

// The bytes after the last field: version, region, product and platform
// (V16), or 20 bytes the game reads the same way (V20).
enum class TrailerKind : uint8_t { None, V16, V20 };

struct FieldSpec {
    const char* name;
    // From the row start, or from its end when negative. A positive offset
    // is the place in a row with every vector empty: a field after a Vector,
    // or after an Array whose elements hold one, moves with their elements.
    // An element's field counts from the element start.
    int32_t offset;
    // Bytes in the file: 8 per text, 56 for a per-language text, the u32
    // count for a Vector, every element for an Array.
    uint32_t size;
    FieldType type;
    // The table file a reference points into, such as "texture.dat".
    const char* refTable = nullptr;
    // The curated section key that sets it, null when only raw rows can.
    const char* iniKey = nullptr;
    // The value a new row starts with, in text form; null for zero.
    const char* defaultValue = nullptr;
    // Array: the element count. Array and Vector: the element stride, the
    // size of one element with every vector in it empty, and the element's
    // fields. Struct: its fields, with size its size with every vector in
    // it empty. Elements and structs may hold vectors, arrays and structs
    // of their own, to any depth. An element with one field whose name is
    // empty is that value: feature.3 rather than feature.3.value.
    uint32_t count = 0;
    uint32_t stride = 0;
    const FieldSpec* element = nullptr;
    uint32_t elementCount = 0;
    bool editable = true;
    // The field in the game's loaded record, -1 when not known. An
    // element's or a struct's field counts from where the record keeps the
    // element or the struct.
    int32_t recordOffset = -1;
    // An Array's element size in the loaded record, 0 when it is stride.
    uint32_t recordStride = 0;
    // A Vector's most elements: the N of the game's k_vector<T, N>, which
    // keeps them inside the record, so a row with more would overrun it. 0
    // for a list without a limit (std::vector).
    uint32_t capacity = 0;
};

// A curated schema describes a mod art table, with its own id band, row
// limit, enum stem and registry file. A generated schema describes any
// other table: its vanilla rows take [patch.<table>.<id>], and mod rows
// added through [row.<table>.<Key>] share the row band (kRowBandStart) and
// kRowRegistry, so it has none of those itself.
struct TableSchema {
    // The game file, such as "texture.dat", and its record struct.
    const char* file;
    const char* structName;
    // The curated section prefix and the [row.<table>] name. For a
    // generated schema the table's stem, which names it in messages.
    const char* section;
    // CDatabase<T>::load, vtable slot 1, 0 when not known. For reference:
    // the table engine reaches it through the vtable.
    uint32_t loadRva;
    // The row size with every vector empty.
    uint32_t rowSize;
    // The enum name's text, -1 when the table has none.
    int32_t enumOffset;
    TrailerKind trailer;
    // Mod rows get idBandStart + n, n from registryFile in mods\CanopyData.
    // 0 and null for a generated schema; see BandStart.
    uint64_t idBandStart;
    uint64_t idBandSize;
    uint32_t maxRows;
    const char* enumStem;
    const char* registryFile;
    const FieldSpec* fields;
    size_t fieldCount;
    // The game's loaded record size, 0 when not known.
    uint32_t recordSize;
};

extern const TableSchema kTexture;
extern const TableSchema kIcon;
extern const TableSchema kAnimeReplaceTexture;

// The generated schemas, from the game's serializers
// (table_schema_generated.cpp): one per table whose every shipped row walks
// through it, by table file name, without the curated tables.
const std::vector<const TableSchema*>& Generated();

// True for a curated schema, false for a generated one.
bool Curated(const TableSchema& schema);

// Every curated schema.
const std::vector<const TableSchema*>& All();
// The schema whose file is name, with or without ".dat", without case.
// Null when the table has no curated schema.
const TableSchema* Find(const std::string& name);

// Every schema a [patch.<table>.<id>] or [row.<table>.<Key>] section and
// the desktop editor can address: the curated ones, then the generated
// ones. All() and Find() keep to the curated ones, which mod art appends
// rows to; rules_rows decides which generated tables take rows.
const std::vector<const TableSchema*>& Patchable();
// The Patchable() schema whose file is name, with or without ".dat",
// without case. Null when no schema covers the table.
const TableSchema* FindPatchable(const std::string& name);

// Mod rows of a generated table ([row.<table>.<Key>], docs/ROWS.md) take
// kRowBandStart + n, n from 1 below kRowBandSize, numbered across every such
// table in one registry. No vanilla row of a generated table has an id in
// the band, and it stays below 2^31 for the tables that keep ids in 32 bits.
constexpr uint64_t kRowBandStart = 2100000000;
constexpr uint64_t kRowBandSize = 1000000;
constexpr const char* kRowRegistry = "rows.ini";
// The first id and the size of the band of schema's mod rows: a curated
// schema's own, the row band for any other.
uint64_t BandStart(const TableSchema& schema);
uint64_t BandSize(const TableSchema& schema);

// ---- Rows ----------------------------------------------------------------
// A part of variable size is a Vector, or an Array or Struct that holds one.
// A row may have any number of them, at any depth: storesales.dat rows have
// several vectors, and each form of a crops.dat row holds its own. Only the
// row says how long those are, so RowFits, Layout, ResolveRow and
// ResizeVector read it.
//
// A row's vector count is the element count of its first top-level Vector.
// RowSize, VectorCount, Resize and Resolve work from that count alone, for
// the curated schemas, whose rows have one vector at most. NewRow is for
// curated schemas only; every other function takes any schema.

// The highest element index a path may name in a vector.
constexpr uint32_t kMaxPathIndex = 4095;

// The most elements a row may give vector: its capacity, else
// kMaxPathIndex + 1.
uint32_t MaxElements(const FieldSpec& vector);

// The schema's first Vector field, null for a table without one.
const FieldSpec* VectorField(const TableSchema& schema);
// The size of a row with vectorCount elements in its first Vector and every
// other part of variable size empty.
size_t RowSize(const TableSchema& schema, uint32_t vectorCount);
// The element count of a row's first Vector, 0 for a table without one.
uint32_t VectorCount(const TableSchema& schema, const uint8_t* row, size_t size);
// True when size is the size the schema gives a row like it, counting the
// elements of every part of variable size.
bool RowFits(const TableSchema& schema, const uint8_t* row, size_t size);
// A zeroed row with vectorCount elements and each default value written.
// Defaults that are texts go to pool. Empty for a generated schema, whose
// mod rows start as a copy of a vanilla row instead.
std::vector<uint8_t> NewRow(const TableSchema& schema, uint32_t vectorCount, blob::Pool& pool);
// Grows or shrinks a row's first Vector to count elements, keeping the
// others and everything after them. New elements are zero. False for a
// table without a Vector, a Vector whose elements hold parts of variable
// size, or a row that does not fit.
bool Resize(const TableSchema& schema, std::vector<uint8_t>& row, uint32_t count);
// Grows or shrinks the vector at path (as Layout names it, without case:
// form.2.growInfo) to count elements, keeping every other byte of the row.
// New elements are zero, with every text in them written empty to pool.
// False, saying why in error, when the row does not fit, path names no
// vector of the row, or count is over the vector's MaxElements.
bool ResizeVector(
    const TableSchema& schema, std::vector<uint8_t>& row, const std::string& path, uint32_t count, blob::Pool& pool,
    std::string& error
);

// True when path names a vector by the schema alone, the way Layout names
// one (form.2.growInfo): every field on the way exists and can be edited,
// and each index is in range. vector, when given, is set to its field.
// False, saying why in error, otherwise.
bool VectorPath(
    const TableSchema& schema, const std::string& path, std::string& error, const FieldSpec** vector = nullptr
);

// The element count of the vector at path in a row, path as ResizeVector
// takes it. False, saying why in error, when the row does not fit or has no
// vector at path.
bool ElementCount(
    const TableSchema& schema, const uint8_t* row, size_t size, const std::string& path, uint32_t& count,
    std::string& error
);

// Adds one element at the end of the vector at path, path as ResizeVector
// takes it, and sets values in it in order: each a path inside the element
// and its value, as [row.] sections write them (rate, condSeason.0), where
// <list>.count resizes a list inside the element first. Texts go to pool.
// index is the new element's. False, saying why in error and with row as it
// was, when the row does not fit, the vector is full (MaxElements) or a
// value does not fit its field.
bool AppendElement(
    const TableSchema& schema, std::vector<uint8_t>& row, const std::string& path,
    const std::vector<std::pair<std::string, std::string>>& values, blob::Pool& pool, uint32_t& index,
    std::string& error
);

// ---- Fields --------------------------------------------------------------

// One place in a row a field name addresses.
struct Location {
    const FieldSpec* field = nullptr;
    // The type and size at this place: a per-language text resolves to
    // one Text per language.
    FieldType type = FieldType::Pad;
    uint32_t size = 0;
    size_t offset = 0;
    // The name with every index spelled out: drawInfo.0.w.
    std::string path;
    // False for a place Layout lists that no path can set: a field that
    // cannot be edited or is inside one, a vector's count or a trailer word.
    bool editable = true;
};

// The places path names in a row of rowSize bytes with vectorCount
// elements, for the curated schemas, without case:
//   name                 a plain field, or every language of a text
//   name.<lang>          one language, by key (en) or index (1)
//   name.<i>.<field>     one element of an array or vector
//   name.<field>         that field in every element
// False, saying why in error, for a name the schema does not have, a field
// that cannot be edited, an element past the end, or a field only the row
// can place: one after a part of variable size other than the first Vector.
bool Resolve(
    const TableSchema& schema, const std::string& path, uint32_t vectorCount, size_t rowSize,
    std::vector<Location>& out, std::string& error
);
// Every place in a row, editable or not, in field order, for showing a
// whole row: each plain field, each language of a text, each element's
// fields, each struct's fields, a vector's count as <name>.count and each
// trailer word. Pad is left out. Each editable place is the one ResolveRow
// gives for its path; for a schema whose rows have one vector at most, it
// is also the one Resolve gives. False, saying why in error, when the row
// does not fit.
bool Layout(
    const TableSchema& schema, const uint8_t* row, size_t size, std::vector<Location>& out, std::string& error
);

// The places path names in any row, by the schema alone, each with its
// path spelled out the way Layout names it and its offset 0, since only a
// row places it. The same names as Resolve, without case, and also
// name.<field> for a struct's field and, for an element that is one value,
// name.<i>. Every element of an array may be named at once; an element of
// a vector only by its index, up to kMaxPathIndex. False, saying why in
// error, for a name the schema does not have or a field that cannot be
// edited.
bool ResolvePath(const TableSchema& schema, const std::string& path, std::vector<Location>& out, std::string& error);
// The places path names in this row: ResolvePath, then each place where
// Layout puts it. False, saying why in error, when the row does not fit or
// lacks an element the path names.
bool ResolveRow(
    const TableSchema& schema, const std::string& path, const uint8_t* row, size_t size, std::vector<Location>& out,
    std::string& error
);
// The place layout (a row's Layout) has at path, spelled as ResolvePath
// spells it, with case. False, saying why in error, when the row lacks it or
// it cannot be edited.
bool Locate(const std::vector<Location>& layout, const std::string& path, Location& out, std::string& error);
// Where the game's loaded record keeps the place at path, spelled as
// ResolvePath spells it: -1 when a field on the way has no recordOffset or
// is a vector, whose elements the record keeps elsewhere.
int64_t RecordOffset(const TableSchema& schema, const std::string& path);

// True when value is valid text for the location's type: a number in range
// for numbers and references, 0, 1, true or false for Bool, and one line
// of printable text for texts.
bool Check(const Location& location, const std::string& value, std::string& error);
// Checks value and writes it into row, texts through pool.
bool Write(
    std::vector<uint8_t>& row, const Location& location, const std::string& value, blob::Pool& pool,
    std::string& error
);
// The value at location in a row of rowSize bytes, as the text Write takes:
// decimal for numbers, references and ids, 0 or 1 for Bool, for a float
// the shortest text that reads back to the same bits, and for a text the
// text itself, from table's pool or, when its offset is at or past added's
// base, from added (the texts Write gave the row; null when there are
// none). Writing the value back leaves a non-text location's bytes as they
// were. False, saying why in error, when the location is outside the row,
// a text is outside both pools, or the value is one Write refuses (a Bool
// other than 0 or 1, a float that is not finite); value then still holds
// it, for display.
bool Read(
    const uint8_t* row, size_t rowSize, const Location& location, const blob::Table& table, const blob::Pool* added,
    std::string& value, std::string& error
);
// The numeric value at a reference or number location, as u64.
uint64_t ReadNumber(const uint8_t* row, const Location& location);

}  // namespace table_schema
