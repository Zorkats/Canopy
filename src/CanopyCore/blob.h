#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// The game's database table file (data/database/*.dat): u32 row count, u32
// row bytes (the rows without their length prefixes), u32 pool bytes, each
// row as a u32 length and its bytes, then the string pool. A text field is
// 8 bytes, a u32 pool offset and a u32 length; the pool holds each text
// NUL-terminated, and an empty text may point at any NUL. The game's loader
// trusts the header and every length, so whatever builds a blob must make
// them add up exactly. Nothing here touches game memory, so the desktop
// tools link it too.
namespace blob {

constexpr size_t kHeaderSize = 12;
constexpr size_t kTextSize = 8;
// What Parse accepts: more rows or a larger part means a damaged file.
constexpr uint32_t kMaxRows = 100000;
constexpr uint32_t kMaxPart = 256u * 1024 * 1024;

template <typename T>
T Get(const uint8_t* data, size_t offset) {
    T value;
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

template <typename T>
void Put(uint8_t* data, size_t offset, T value) {
    std::memcpy(data + offset, &value, sizeof(T));
}

struct Text {
    uint32_t offset = 0;
    uint32_t length = 0;
};

// One row inside the blob it was parsed from.
struct Row {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

// A parsed blob. Every pointer points into the parsed bytes, which must
// outlive it.
struct Table {
    std::vector<Row> rows;
    // The first row's length prefix, and the rows with their prefixes.
    const uint8_t* rowsStart = nullptr;
    size_t rowsSize = 0;
    uint32_t rowBytes = 0;
    const uint8_t* pool = nullptr;
    uint32_t poolBytes = 0;
};

// Splits the size bytes at data into rows and pool. False, saying why in
// error, when the header or a length does not add up.
bool Parse(const uint8_t* data, size_t size, Table& table, std::string& error);

// The u64 id at +0, 0 for a row shorter than 8 bytes.
uint64_t RowId(const Row& row);

// The text whose ref is at offset in row. False when the ref is outside the
// row or the text outside the pool.
bool ReadText(const Table& table, const Row& row, size_t offset, std::string& text);

// The game's enum name hash, which keys its enum map: CRC-32's table,
// seeded with the length, no inversions.
uint32_t EnumCrc(const char* text, size_t length);

// The texts a table appends to its pool. Offsets start at base, the pool
// size of the blob they go after. The pool starts with a NUL, which every
// empty text points at, and a text added twice is stored once.
class Pool {
public:
    explicit Pool(uint32_t base);

    Text Add(const std::string& text);
    // Adds text and writes its ref at offset in row.
    void Write(std::vector<uint8_t>& row, size_t offset, const std::string& text);

    const std::string& Bytes() const {
        return bytes_;
    }
    uint32_t Base() const {
        return base_;
    }

private:
    uint32_t base_;
    std::string bytes_;
    std::unordered_map<std::string, uint32_t> offsets_;
};

// The enum names of a table's rows, read from the text at enumOffset, and
// the ones given out since. Unique hands out a name that neither the table
// nor an earlier Unique has, by name or by hash.
class EnumNames {
public:
    EnumNames() = default;
    EnumNames(const Table& table, size_t enumOffset);

    // stem, or stem_2, stem_3 and so on.
    std::string Unique(const std::string& stem);
    bool Has(const std::string& name) const;

private:
    std::set<std::string> names_;
    std::set<uint32_t> crcs_;
};

// Rows appended to a parsed table, and their texts.
class Appender {
public:
    explicit Appender(const Table& table) : table_(table), pool_(table.poolBytes) {}

    Pool& Texts() {
        return pool_;
    }
    void Add(const std::vector<uint8_t>& row);
    size_t Count() const {
        return count_;
    }
    // The table's rows, then the appended rows, then the table's pool and
    // the appended pool, with the header counting all of them.
    void Merge(std::vector<uint8_t>& merged) const;

private:
    const Table& table_;
    Pool pool_;
    std::vector<uint8_t> rows_;
    size_t count_ = 0;
};

// A whole blob from rows, without their length prefixes, and a pool.
void Build(const std::vector<std::vector<uint8_t>>& rows, const std::string& pool, std::vector<uint8_t>& out);

}  // namespace blob
