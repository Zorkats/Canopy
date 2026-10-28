#include "blob.h"

#include <array>

namespace blob {

bool Parse(const uint8_t* data, size_t size, Table& table, std::string& error) {
    table = Table();
    if (!data || size < kHeaderSize) {
        error = "shorter than the 12-byte header";
        return false;
    }
    const uint32_t count = Get<uint32_t>(data, 0);
    table.rowBytes = Get<uint32_t>(data, 4);
    table.poolBytes = Get<uint32_t>(data, 8);
    if (count > kMaxRows || table.rowBytes > kMaxPart || table.poolBytes > kMaxPart) {
        error = "the header counts are out of range";
        return false;
    }
    table.rowsSize = 4 * static_cast<size_t>(count) + table.rowBytes;
    if (kHeaderSize + table.rowsSize + table.poolBytes > size) {
        error = "the header counts run past the end";
        return false;
    }
    table.rowsStart = data + kHeaderSize;
    table.pool = table.rowsStart + table.rowsSize;
    const size_t end = kHeaderSize + table.rowsSize;
    size_t offset = kHeaderSize;
    table.rows.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        if (offset + 4 > end) {
            error = "row " + std::to_string(index) + " starts past the rows";
            return false;
        }
        const uint32_t length = Get<uint32_t>(data, offset);
        if (length > end - offset - 4) {
            error = "row " + std::to_string(index) + " runs past the rows";
            return false;
        }
        table.rows.push_back({data + offset + 4, length});
        offset += 4 + length;
    }
    if (offset != end) {
        error = "the row lengths do not add up to the header's row bytes";
        return false;
    }
    return true;
}

uint64_t RowId(const Row& row) {
    return row.size >= 8 ? Get<uint64_t>(row.data, 0) : 0;
}

bool ReadText(const Table& table, const Row& row, size_t offset, std::string& text) {
    text.clear();
    // Written so it cannot wrap: a schema with no enum name has offset -1,
    // which is SIZE_MAX here.
    if (offset > row.size || row.size - offset < kTextSize) {
        return false;
    }
    const uint32_t start = Get<uint32_t>(row.data, offset);
    const uint32_t length = Get<uint32_t>(row.data, offset + 4);
    if (static_cast<uint64_t>(start) + length > table.poolBytes) {
        return false;
    }
    text.assign(reinterpret_cast<const char*>(table.pool + start), length);
    return true;
}

uint32_t EnumCrc(const char* text, size_t length) {
    // Built once, safely, by whichever load thread asks first.
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values = {};
        for (uint32_t index = 0; index < 256; ++index) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ ((value & 1) ? 0xEDB88320u : 0u);
            }
            values[index] = value;
        }
        return values;
    }();
    uint32_t crc = static_cast<uint32_t>(length);
    for (size_t index = 0; index < length; ++index) {
        crc = table[(crc ^ static_cast<uint8_t>(text[index])) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

// ---- Pool ----------------------------------------------------------------

Pool::Pool(uint32_t base) : base_(base), bytes_(1, '\0') {}

Text Pool::Add(const std::string& text) {
    if (text.empty()) {
        return {base_, 0};
    }
    const auto found = offsets_.find(text);
    if (found != offsets_.end()) {
        return {base_ + found->second, static_cast<uint32_t>(text.size())};
    }
    const uint32_t position = static_cast<uint32_t>(bytes_.size());
    bytes_ += text;
    bytes_ += '\0';
    offsets_.emplace(text, position);
    return {base_ + position, static_cast<uint32_t>(text.size())};
}

void Pool::Write(std::vector<uint8_t>& row, size_t offset, const std::string& text) {
    const Text ref = Add(text);
    Put<uint32_t>(row.data(), offset, ref.offset);
    Put<uint32_t>(row.data(), offset + 4, ref.length);
}

// ---- Enum names ----------------------------------------------------------

EnumNames::EnumNames(const Table& table, size_t enumOffset) {
    std::string name;
    for (const Row& row : table.rows) {
        if (ReadText(table, row, enumOffset, name) && !name.empty()) {
            crcs_.insert(EnumCrc(name.data(), name.size()));
            names_.insert(name);
        }
    }
}

std::string EnumNames::Unique(const std::string& stem) {
    std::string name = stem;
    for (int suffix = 2; names_.count(name) || crcs_.count(EnumCrc(name.data(), name.size())); ++suffix) {
        name = stem + "_" + std::to_string(suffix);
    }
    names_.insert(name);
    crcs_.insert(EnumCrc(name.data(), name.size()));
    return name;
}

bool EnumNames::Has(const std::string& name) const {
    return names_.count(name) > 0;
}

// ---- Building ------------------------------------------------------------

void Appender::Add(const std::vector<uint8_t>& row) {
    const size_t at = rows_.size();
    rows_.resize(at + 4);
    Put<uint32_t>(rows_.data(), at, static_cast<uint32_t>(row.size()));
    rows_.insert(rows_.end(), row.begin(), row.end());
    ++count_;
}

void Appender::Merge(std::vector<uint8_t>& merged) const {
    const std::string& pool = pool_.Bytes();
    merged.clear();
    merged.reserve(kHeaderSize + table_.rowsSize + rows_.size() + table_.poolBytes + pool.size());
    merged.resize(kHeaderSize);
    Put<uint32_t>(merged.data(), 0, static_cast<uint32_t>(table_.rows.size() + count_));
    Put<uint32_t>(merged.data(), 4, table_.rowBytes + static_cast<uint32_t>(rows_.size() - 4 * count_));
    Put<uint32_t>(merged.data(), 8, table_.poolBytes + static_cast<uint32_t>(pool.size()));
    merged.insert(merged.end(), table_.rowsStart, table_.rowsStart + table_.rowsSize);
    merged.insert(merged.end(), rows_.begin(), rows_.end());
    merged.insert(merged.end(), table_.pool, table_.pool + table_.poolBytes);
    merged.insert(merged.end(), pool.begin(), pool.end());
}

void Build(const std::vector<std::vector<uint8_t>>& rows, const std::string& pool, std::vector<uint8_t>& out) {
    size_t rowBytes = 0;
    for (const std::vector<uint8_t>& row : rows) {
        rowBytes += row.size();
    }
    out.assign(kHeaderSize, 0);
    out.reserve(kHeaderSize + 4 * rows.size() + rowBytes + pool.size());
    Put<uint32_t>(out.data(), 0, static_cast<uint32_t>(rows.size()));
    Put<uint32_t>(out.data(), 4, static_cast<uint32_t>(rowBytes));
    Put<uint32_t>(out.data(), 8, static_cast<uint32_t>(pool.size()));
    for (const std::vector<uint8_t>& row : rows) {
        const size_t at = out.size();
        out.resize(at + 4);
        Put<uint32_t>(out.data(), at, static_cast<uint32_t>(row.size()));
        out.insert(out.end(), row.begin(), row.end());
    }
    out.insert(out.end(), pool.begin(), pool.end());
}

}  // namespace blob
