#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Read-only access to the game's FAFULLFS archives: data.dat in the game
// folder and the data\*_1_00.dat files that data/database/archive.dat lists.
// Lookups follow the game's own rule (FindArchiveEntry, RVA 0x95CD00), so a
// path found here is the file the game reads, unless a loose or mod file
// overrides it at run time.
//
// Files are opened for reading only, sharing read access, the way the game
// opens them, so the game can mount the same files while they are open here.
// Reads are positional, so after Open returns every const member may be
// called from any number of threads at once. Open and Close must not race
// with anything else on the same object. Every function that can fail
// returns false and says why in error; none lets an exception escape.
namespace archive {

// Paths of this many bytes or more never match. The game copies the path
// into a 512-byte stack buffer before it hashes it.
constexpr size_t kMaxPathLength = 511;

// One row of an archive's entry table.
struct Entry {
    std::string name;        // the stored path, lowercase, forward slashes
    uint64_t hash = 0;       // HashPath(name), the table's sort key
    uint64_t size = 0;       // stored bytes
    uint64_t offset = 0;     // from the start of the archive file
    uint64_t timestamp = 0;  // Unix seconds
};

// The entry table's key for a folded path, one the game has lowercased:
// cut at the first NUL, ASCII A-Z to a-z (_strlwr in the C locale), nothing
// else changed, so a backslash path never matches. Eight bytes, each the low byte of
// the length XOR a constant, then every character XORed into byte index & 7,
// read as a little-endian u64.
uint64_t HashPath(std::string_view folded);

// True when data starts with a YKCMP_V1 header.
bool IsYkcmp(const uint8_t* data, size_t size);

// One FAFULLFS file. The header and both tables are read once by Open and
// never change after that; entry bytes are read from the file on demand.
class Archive {
public:
    static constexpr size_t kNotFound = static_cast<size_t>(-1);

    Archive();
    ~Archive();
    Archive(Archive&& other) noexcept;
    Archive& operator=(Archive&& other) noexcept;
    Archive(const Archive&) = delete;
    Archive& operator=(const Archive&) = delete;

    // Opens path and reads its tables. Closes whatever was open first.
    bool Open(const std::wstring& path, std::string& error);
    void Close();
    bool IsOpen() const;

    const std::wstring& Path() const;
    // The header's build tag, 0x00204EAC in the shipped files.
    uint32_t Version() const;
    // In table order, which is ascending hash.
    const std::vector<Entry>& Entries() const;

    // The index of gamePath in Entries(), or kNotFound. gamePath is folded
    // as HashPath describes first. Entries that share a hash are told apart
    // by name, and when two carry the same name the later one wins, as in
    // the game.
    size_t Find(std::string_view gamePath) const;

    // The entry's bytes exactly as stored.
    bool ReadRaw(size_t index, std::vector<uint8_t>& bytes, std::string& error) const;
    // The stored bytes, unpacked when they are a YKCMP_V1 container. Fails
    // on a container type nltx::YkcmpUnpack does not support (map .head
    // files are type 7). wrapped, when given, says whether it unpacked.
    bool Read(size_t index, std::vector<uint8_t>& bytes, std::string& error, bool* wrapped = nullptr) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

// Where a game path lives: which mounted archive, and which entry in it.
struct Location {
    size_t archive = Archive::kNotFound;
    size_t entry = Archive::kNotFound;

    bool Found() const {
        return archive != Archive::kNotFound;
    }
};

// Every archive the game mounts from a game folder, in its mount order:
// data.dat first, then the rows of data/database/archive.dat (read from
// data.dat) with a non-empty path, highest ARCHIVE_ID first, skipping files
// that do not exist. A lookup asks each archive in that order and the first
// hit wins. Loose files and mod overrides are not considered.
class GameData {
public:
    GameData();
    ~GameData();
    GameData(GameData&& other) noexcept;
    GameData& operator=(GameData&& other) noexcept;
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;

    // Opens gameFolder, the folder that holds village.exe and data.dat.
    // Closes whatever was open first.
    bool Open(const std::wstring& gameFolder, std::string& error);
    void Close();
    bool IsOpen() const;

    const std::wstring& Folder() const;
    size_t ArchiveCount() const;
    // index below ArchiveCount(), in mount order.
    const Archive& ArchiveAt(size_t index) const;

    // Location of a path such as "data/database/item.dat".
    Location Find(std::string_view gamePath) const;
    const Entry* FindEntry(std::string_view gamePath) const;

    bool ReadRaw(std::string_view gamePath, std::vector<uint8_t>& bytes, std::string& error) const;
    bool Read(
        std::string_view gamePath, std::vector<uint8_t>& bytes, std::string& error, bool* wrapped = nullptr
    ) const;

private:
    std::wstring folder_;
    std::vector<Archive> archives_;
};

}  // namespace archive
