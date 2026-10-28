#include "archive.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <new>
#include <utility>

#include "nltx.h"
#include "platform_files.h"

// Evidence for the rules below, from retail village.exe and the shipped
// archives:
//
// - FindArchiveEntry (RVA 0x95CD00) copies the path, runs _strlwr on it,
//   builds the eight key bytes from the length and the characters, binary
//   searches each mounted archive's entry table for the first hash not below
//   the key, then walks the run of equal hashes comparing names with strcmp.
//   The last equal name in the run wins. Archives are tried in mount order
//   and the first one with a match answers. The stored hash of every entry
//   in all nine shipped archives (11,031 entries) equals HashPath(name), and
//   data.dat and texture_1_00.dat hold 70 and 158 entries whose hash another
//   entry shares, so the name comparison matters.
// - The mount function (0x95DAB0) appends to the archive list, data.dat is
//   mounted first by game init (0x833520), and 0x6D73B0 mounts the rest: it
//   collects the archive.dat rows whose path is not empty, sorts them with
//   std::list::sort by the row id, greatest first, and mounts each path that
//   is in an archive or exists as a file. msg_1_00.dat is not listed, so the
//   game never mounts it.
// - The game opens archives with GENERIC_READ and FILE_SHARE_READ (0x9651B0).
// - No shipped table or texture entry is stored inside a YKCMP container.
//   The 198 map .head entries are YKCMP type 7, which nltx does not unpack.
namespace archive {
namespace {

constexpr char kMagic[] = "FAFULLFS";
constexpr char kSonyMagic[] = "PS_FS_V1";
constexpr char kYkcmpMagic[] = "YKCMP_V1";
constexpr size_t kMagicSize = 8;
constexpr size_t kHeaderSize = 0x28;
constexpr size_t kEntrySize = 0x30;
// Far above anything shipped (6255 entries, 317 KB of names), low enough
// that a corrupt header cannot ask for gigabytes of table.
constexpr uint64_t kMaxEntries = 1u << 22;
constexpr uint64_t kMaxNameTable = 1u << 28;
// The key bytes before the length is XORed in.
constexpr uint8_t kHashSeed[8] = {0x54, 0xA3, 0x8A, 0x2B, 0x36, 0x70, 0xDE, 0xEF};

constexpr const wchar_t* kDataArchive = L"data.dat";
constexpr std::string_view kArchiveList = "data/database/archive.dat";
// archive.dat: u32 row count, u32 row bytes, u32 pool bytes, then each row
// as a u32 size and its fields, then the string pool. A row holds the u64
// id at +0, then (offset, length) text refs for the enum name at +8 and the
// archive path at +16.
constexpr size_t kTableHeaderSize = 12;
constexpr size_t kArchiveRowMin = 24;
constexpr size_t kArchiveIdField = 0;
constexpr size_t kArchivePathField = 16;

uint32_t ReadU32(const uint8_t* data, size_t offset) {
    uint32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

uint64_t ReadU64(const uint8_t* data, size_t offset) {
    uint64_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

// Runs body, turning an exception into a false return and an error, so none
// leaves the library. Only allocation can throw in practice.
template <typename Body>
bool Guarded(std::string& error, Body&& body) {
    try {
        return body();
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    } catch (const std::exception& exception) {
        error = exception.what();
    } catch (...) {
        error = "unexpected exception";
    }
    return false;
}

// Folds path into buffer, which has room for kMaxPathLength + 1 bytes.
// False when the folded path is too long for the game to look up.
bool FoldInto(std::string_view path, char* buffer, size_t& length) {
    const size_t end = path.find('\0');
    if (end != std::string_view::npos) {
        path = path.substr(0, end);
    }
    if (path.size() > kMaxPathLength) {
        return false;
    }
    for (size_t index = 0; index < path.size(); ++index) {
        const char c = path[index];
        buffer[index] = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    buffer[path.size()] = '\0';
    length = path.size();
    return true;
}

std::wstring JoinPath(const std::wstring& folder, const std::wstring& relative) {
    std::wstring result = folder;
    if (!result.empty() && !platform::IsSeparator(result.back())) {
        result += platform::kWideSeparator;
    }
    return result + platform::NativeSeparators(relative);
}

struct ListedArchive {
    uint64_t id = 0;
    std::string path;
};

// The rows of archive.dat that name an archive.
bool ParseArchiveList(const std::vector<uint8_t>& table, std::vector<ListedArchive>& listed, std::string& error) {
    listed.clear();
    if (table.size() < kTableHeaderSize) {
        error = "the table is " + std::to_string(table.size()) + " bytes, shorter than its header";
        return false;
    }
    const uint32_t count = ReadU32(table.data(), 0);
    const uint32_t poolSize = ReadU32(table.data(), 8);
    size_t position = kTableHeaderSize;
    std::vector<std::pair<size_t, size_t>> rows;
    for (uint32_t row = 0; row < count; ++row) {
        if (table.size() - position < 4) {
            error = "row " + std::to_string(row) + " runs past the end of the table";
            return false;
        }
        const uint32_t rowSize = ReadU32(table.data(), position);
        position += 4;
        if (rowSize < kArchiveRowMin || table.size() - position < rowSize) {
            error = "row " + std::to_string(row) + " has a bad size of " + std::to_string(rowSize);
            return false;
        }
        rows.emplace_back(position, rowSize);
        position += rowSize;
    }
    if (table.size() - position < poolSize) {
        error = "the string pool runs past the end of the table";
        return false;
    }
    const uint8_t* pool = table.data() + position;
    for (const auto& [start, size] : rows) {
        const uint8_t* row = table.data() + start;
        const uint32_t textOffset = ReadU32(row, kArchivePathField);
        const uint32_t textLength = ReadU32(row, kArchivePathField + 4);
        if (textLength == 0) {
            continue;
        }
        if (textOffset > poolSize || poolSize - textOffset < textLength) {
            error = "the path of row id " + std::to_string(ReadU64(row, kArchiveIdField)) +
                    " points outside the string pool";
            return false;
        }
        const char* text = reinterpret_cast<const char*>(pool + textOffset);
        const size_t length = std::find(text, text + textLength, '\0') - text;
        if (length == 0) {
            continue;
        }
        listed.push_back({ReadU64(row, kArchiveIdField), std::string(text, length)});
    }
    return true;
}

bool NotFound(std::string_view gamePath, std::string& error) {
    Guarded(error, [&] {
        error = std::string(gamePath) + " is in no mounted archive";
        return true;
    });
    return false;
}

const std::wstring kEmptyPath;
const std::vector<Entry> kNoEntries;

}  // namespace

uint64_t HashPath(std::string_view folded) {
    uint8_t key[8] = {};
    const uint8_t length = static_cast<uint8_t>(folded.size());
    for (size_t index = 0; index < 8; ++index) {
        key[index] = static_cast<uint8_t>(length ^ kHashSeed[index]);
    }
    for (size_t index = 0; index < folded.size(); ++index) {
        key[index & 7] ^= static_cast<uint8_t>(folded[index]);
    }
    uint64_t hash = 0;
    std::memcpy(&hash, key, sizeof(hash));
    return hash;
}

bool IsYkcmp(const uint8_t* data, size_t size) {
    return data && size >= nltx::kYkcmpHeaderSize && std::memcmp(data, kYkcmpMagic, kMagicSize) == 0;
}

// ---- Archive -------------------------------------------------------------

struct Archive::State {
    platform::ReadOnlyFile file;
    uint64_t fileSize = 0;
    std::wstring path;
    uint32_t version = 0;
    std::vector<Entry> entries;

    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
};

Archive::Archive() = default;
Archive::~Archive() = default;
Archive::Archive(Archive&& other) noexcept = default;
Archive& Archive::operator=(Archive&& other) noexcept = default;

bool Archive::Open(const std::wstring& path, std::string& error) {
    Close();
    const bool opened = Guarded(error, [&] {
        auto state = std::make_unique<State>();
        state->path = path;
        const std::string name = platform::WideToUtf8(path);
        if (!state->file.Open(path, name, error)) {
            return false;
        }
        state->fileSize = state->file.Size();
        if (state->fileSize < kHeaderSize) {
            error = name + " is " + std::to_string(state->fileSize) + " bytes, too short for an archive";
            return false;
        }
        uint8_t header[kHeaderSize] = {};
        if (!state->file.ReadAt(0, header, kHeaderSize, error)) {
            error = name + ": " + error;
            return false;
        }
        if (std::memcmp(header, kSonyMagic, kMagicSize) == 0) {
            error = name + " is a PS_FS_V1 archive, which is not supported";
            return false;
        }
        if (std::memcmp(header, kMagic, kMagicSize) != 0) {
            error = name + " is not a FAFULLFS archive";
            return false;
        }
        const uint64_t count = ReadU32(header, 0x08);
        state->version = ReadU32(header, 0x0C);
        const uint64_t nameOffset = ReadU64(header, 0x10);
        const uint64_t nameSize = ReadU64(header, 0x18);
        const uint64_t tableOffset = ReadU64(header, 0x20);
        const uint64_t fileSize = state->fileSize;
        // msg_1_00.dat is a bare 48-byte header whose empty tables point past
        // its end, so an empty table is never range checked.
        if (count > kMaxEntries ||
            (count > 0 && (tableOffset > fileSize || (fileSize - tableOffset) / kEntrySize < count))) {
            error = name + ": the table of " + std::to_string(count) + " entries at " + std::to_string(tableOffset) +
                    " does not fit the file";
            return false;
        }
        if (nameSize > kMaxNameTable || (nameSize > 0 && (nameOffset > fileSize || fileSize - nameOffset < nameSize))) {
            error = name + ": the name table of " + std::to_string(nameSize) + " bytes at " +
                    std::to_string(nameOffset) + " does not fit the file";
            return false;
        }
        std::vector<uint8_t> table(static_cast<size_t>(count * kEntrySize));
        std::vector<uint8_t> names(static_cast<size_t>(nameSize));
        if (!state->file.ReadAt(tableOffset, table.data(), table.size(), error) ||
            !state->file.ReadAt(nameOffset, names.data(), names.size(), error)) {
            error = name + ": " + error;
            return false;
        }
        state->entries.resize(static_cast<size_t>(count));
        for (size_t index = 0; index < state->entries.size(); ++index) {
            const uint8_t* row = table.data() + index * kEntrySize;
            Entry& entry = state->entries[index];
            entry.hash = ReadU64(row, 0x00);
            const uint64_t textOffset = ReadU64(row, 0x08);
            entry.size = ReadU64(row, 0x18);
            entry.offset = ReadU64(row, 0x20);
            entry.timestamp = ReadU64(row, 0x28);
            const char* text = reinterpret_cast<const char*>(names.data());
            const void* textEnd = nullptr;
            if (textOffset < names.size()) {
                const size_t start = static_cast<size_t>(textOffset);
                textEnd = std::memchr(text + start, 0, names.size() - start);
            }
            if (!textEnd) {
                error = name + ": entry " + std::to_string(index) + " has no terminated name in the name table";
                return false;
            }
            entry.name.assign(text + textOffset, static_cast<const char*>(textEnd));
            if (entry.offset > fileSize || fileSize - entry.offset < entry.size) {
                error = name + ": entry " + entry.name + " runs past the end of the file";
                return false;
            }
        }
        state_ = std::move(state);
        return true;
    });
    if (!opened) {
        state_.reset();
    }
    return opened;
}

void Archive::Close() {
    state_.reset();
}

bool Archive::IsOpen() const {
    return state_ != nullptr;
}

const std::wstring& Archive::Path() const {
    return state_ ? state_->path : kEmptyPath;
}

uint32_t Archive::Version() const {
    return state_ ? state_->version : 0;
}

const std::vector<Entry>& Archive::Entries() const {
    return state_ ? state_->entries : kNoEntries;
}

size_t Archive::Find(std::string_view gamePath) const {
    if (!state_) {
        return kNotFound;
    }
    char folded[kMaxPathLength + 1];
    size_t length = 0;
    if (!FoldInto(gamePath, folded, length)) {
        return kNotFound;
    }
    const std::string_view key(folded, length);
    const uint64_t hash = HashPath(key);
    const std::vector<Entry>& entries = state_->entries;
    auto it = std::lower_bound(entries.begin(), entries.end(), hash, [](const Entry& entry, uint64_t value) {
        return entry.hash < value;
    });
    size_t found = kNotFound;
    for (; it != entries.end() && it->hash == hash; ++it) {
        if (it->name == key) {
            found = static_cast<size_t>(it - entries.begin());
        }
    }
    return found;
}

bool Archive::ReadRaw(size_t index, std::vector<uint8_t>& bytes, std::string& error) const {
    if (!state_) {
        error = "the archive is not open";
        return false;
    }
    if (index >= state_->entries.size()) {
        error = "entry " + std::to_string(index) + " is out of range";
        return false;
    }
    return Guarded(error, [&] {
        const Entry& entry = state_->entries[index];
        bytes.resize(static_cast<size_t>(entry.size));
        if (!state_->file.ReadAt(entry.offset, bytes.data(), bytes.size(), error)) {
            error = entry.name + ": " + error;
            bytes.clear();
            return false;
        }
        return true;
    });
}

bool Archive::Read(size_t index, std::vector<uint8_t>& bytes, std::string& error, bool* wrapped) const {
    if (wrapped) {
        *wrapped = false;
    }
    std::vector<uint8_t> stored;
    if (!ReadRaw(index, stored, error)) {
        return false;
    }
    if (!IsYkcmp(stored.data(), stored.size())) {
        bytes = std::move(stored);
        return true;
    }
    return Guarded(error, [&] {
        if (!nltx::YkcmpUnpack(stored.data(), stored.size(), bytes, error)) {
            error = state_->entries[index].name + ": " + error;
            bytes.clear();
            return false;
        }
        if (wrapped) {
            *wrapped = true;
        }
        return true;
    });
}

// ---- GameData ------------------------------------------------------------

GameData::GameData() = default;
GameData::~GameData() = default;
GameData::GameData(GameData&& other) noexcept = default;
GameData& GameData::operator=(GameData&& other) noexcept = default;

bool GameData::Open(const std::wstring& gameFolder, std::string& error) {
    Close();
    const bool opened = Guarded(error, [&] {
        folder_ = gameFolder;
        // The files as they are spelled on disk, whatever case data.dat and
        // archive.dat are written in there.
        platform::PathLookup lookup;
        Archive data;
        if (!data.Open(lookup.Find(JoinPath(gameFolder, kDataArchive)), error)) {
            return false;
        }
        archives_.push_back(std::move(data));

        std::vector<uint8_t> table;
        std::string detail;
        if (!ReadRaw(kArchiveList, table, detail)) {
            error = "cannot read " + std::string(kArchiveList) + ": " + detail;
            return false;
        }
        std::vector<ListedArchive> listed;
        if (!ParseArchiveList(table, listed, detail)) {
            error = std::string(kArchiveList) + ": " + detail;
            return false;
        }
        // std::list::sort is stable, so equal ids keep their row order.
        std::stable_sort(listed.begin(), listed.end(), [](const ListedArchive& left, const ListedArchive& right) {
            return left.id > right.id;
        });
        for (const ListedArchive& entry : listed) {
            std::wstring relative;
            if (!platform::Utf8ToWide(entry.path, relative)) {
                error = std::string(kArchiveList) + ": archive id " + std::to_string(entry.id) +
                        " has a path that is not UTF-8";
                return false;
            }
            const std::wstring path = lookup.Find(JoinPath(gameFolder, relative));
            // The game also accepts a path that is itself an archive entry,
            // which it then cannot mount; no shipped row does that.
            if (!platform::IsFile(path)) {
                continue;
            }
            Archive archive;
            if (!archive.Open(path, error)) {
                return false;
            }
            archives_.push_back(std::move(archive));
        }
        return true;
    });
    if (!opened) {
        Close();
    }
    return opened;
}

void GameData::Close() {
    archives_.clear();
    folder_.clear();
}

bool GameData::IsOpen() const {
    return !archives_.empty();
}

const std::wstring& GameData::Folder() const {
    return folder_;
}

size_t GameData::ArchiveCount() const {
    return archives_.size();
}

const Archive& GameData::ArchiveAt(size_t index) const {
    return archives_[index];
}

Location GameData::Find(std::string_view gamePath) const {
    for (size_t index = 0; index < archives_.size(); ++index) {
        const size_t entry = archives_[index].Find(gamePath);
        if (entry != Archive::kNotFound) {
            return {index, entry};
        }
    }
    return {};
}

const Entry* GameData::FindEntry(std::string_view gamePath) const {
    const Location location = Find(gamePath);
    return location.Found() ? &archives_[location.archive].Entries()[location.entry] : nullptr;
}

bool GameData::ReadRaw(std::string_view gamePath, std::vector<uint8_t>& bytes, std::string& error) const {
    const Location location = Find(gamePath);
    if (!location.Found()) {
        return NotFound(gamePath, error);
    }
    return archives_[location.archive].ReadRaw(location.entry, bytes, error);
}

bool GameData::Read(std::string_view gamePath, std::vector<uint8_t>& bytes, std::string& error, bool* wrapped) const {
    if (wrapped) {
        *wrapped = false;
    }
    const Location location = Find(gamePath);
    if (!location.Found()) {
        return NotFound(gamePath, error);
    }
    return archives_[location.archive].Read(location.entry, bytes, error, wrapped);
}

}  // namespace archive
