#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "archive.h"
#include "nltx.h"
#include "test_support.h"

// Checks of the CanopyCore archive reader. Synthetic archives are built in
// memory and written to a temporary folder. The real game folder is only
// opened for reading; when it is missing those checks print a note and are
// skipped. Set CANOPY_GAME_DIR to use another install. Only paths, ids,
// sizes and counts are printed, never file contents.
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED line %d: %s\n", line, text);
    }
    return passed;
}

const fs::path kSourceDir = fs::u8path(CANOPY_SOURCE_DIR);

Bytes ReadBytes(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const Bytes& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes Text(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

void PutU32(Bytes& data, size_t offset, uint32_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

void PutU64(Bytes& data, size_t offset, uint64_t value) {
    std::memcpy(data.data() + offset, &value, sizeof(value));
}

void AppendU32(Bytes& data, uint32_t value) {
    data.resize(data.size() + 4);
    PutU32(data, data.size() - 4, value);
}

void AppendU64(Bytes& data, uint64_t value) {
    data.resize(data.size() + 8);
    PutU64(data, data.size() - 8, value);
}

std::string Upper(std::string text) {
    for (char& c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return text;
}

// ---- Synthetic archives ----------------------------------------------------

struct FakeFile {
    std::string name;
    Bytes data;
    uint64_t timestamp = 0;
};

// A FAFULLFS file laid out like the shipped ones: header, entry data, name
// table, then the entry table sorted by hash. Equal hashes keep their order.
Bytes BuildArchive(std::vector<FakeFile> files) {
    std::stable_sort(files.begin(), files.end(), [](const FakeFile& left, const FakeFile& right) {
        return archive::HashPath(left.name) < archive::HashPath(right.name);
    });
    Bytes out(0x30, 0);
    std::memcpy(out.data(), "FAFULLFS", 8);
    PutU32(out, 0x08, static_cast<uint32_t>(files.size()));
    PutU32(out, 0x0C, 0x00204EAC);
    std::vector<uint64_t> offsets;
    for (const FakeFile& file : files) {
        offsets.push_back(out.size());
        out.insert(out.end(), file.data.begin(), file.data.end());
    }
    const uint64_t nameOffset = out.size();
    std::vector<uint64_t> nameOffsets;
    for (const FakeFile& file : files) {
        nameOffsets.push_back(out.size() - nameOffset);
        out.insert(out.end(), file.name.begin(), file.name.end());
        out.push_back(0);
    }
    const uint64_t nameSize = out.size() - nameOffset;
    const uint64_t tableOffset = out.size();
    for (size_t index = 0; index < files.size(); ++index) {
        AppendU64(out, archive::HashPath(files[index].name));
        AppendU64(out, nameOffsets[index]);
        AppendU64(out, 0);
        AppendU64(out, files[index].data.size());
        AppendU64(out, offsets[index]);
        AppendU64(out, files[index].timestamp);
    }
    PutU64(out, 0x10, nameOffset);
    PutU64(out, 0x18, nameSize);
    PutU64(out, 0x20, tableOffset);
    return out;
}

struct ListRow {
    uint64_t id = 0;
    std::string path;
};

// An archive.dat table: 40-byte rows of id, enum name and path refs.
Bytes BuildArchiveList(const std::vector<ListRow>& rows) {
    Bytes pool;
    Bytes body;
    for (const ListRow& row : rows) {
        const std::string enumName = "ARCHIVE_ID_" + std::to_string(row.id);
        const uint32_t enumOffset = static_cast<uint32_t>(pool.size());
        pool.insert(pool.end(), enumName.begin(), enumName.end());
        pool.push_back(0);
        const uint32_t pathOffset = static_cast<uint32_t>(pool.size());
        pool.insert(pool.end(), row.path.begin(), row.path.end());
        pool.push_back(0);
        AppendU32(body, 40);
        AppendU64(body, row.id);
        AppendU32(body, enumOffset);
        AppendU32(body, static_cast<uint32_t>(enumName.size()));
        AppendU32(body, row.path.empty() ? 0 : pathOffset);
        AppendU32(body, static_cast<uint32_t>(row.path.size()));
        body.resize(body.size() + 16, 0);
    }
    Bytes out;
    AppendU32(out, static_cast<uint32_t>(rows.size()));
    AppendU32(out, static_cast<uint32_t>(rows.size() * 40));
    AppendU32(out, static_cast<uint32_t>(pool.size()));
    out.insert(out.end(), body.begin(), body.end());
    out.insert(out.end(), pool.begin(), pool.end());
    return out;
}

Bytes YkcmpHeader(uint32_t type, uint32_t total, uint32_t raw) {
    Bytes out(nltx::kYkcmpHeaderSize, 0);
    std::memcpy(out.data(), "YKCMP_V1", 8);
    PutU32(out, 8, type);
    PutU32(out, 12, total);
    PutU32(out, 16, raw);
    return out;
}

fs::path TempFolder() {
    const fs::path folder =
        fs::temp_directory_path() / ("canopy_archive_tests_" + std::to_string(test_support::ProcessId()));
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::create_directories(folder / "data");
    return folder;
}

void TestHash() {
    std::printf("hash\n");
    // Stored in the shipped data.dat for this path.
    CHECK(archive::HashPath("data/database/storesales.dat") == 0x80B54B012287AA0Cull);
    // Bytes at i and i + 8 land in the same key byte, so swapping them keeps
    // the hash.
    const std::string first = "data/k0abcdefgh1z.dat";
    std::string second = first;
    std::swap(second[6], second[14]);
    CHECK(first != second);
    CHECK(archive::HashPath(first) == archive::HashPath(second));
    // The length is part of the key.
    CHECK(archive::HashPath("data/a.dat") != archive::HashPath("data/a.dat\x01"));
}

void TestSyntheticArchive(const fs::path& folder) {
    std::printf("synthetic archive\n");
    const std::string first = "data/k0abcdefgh1z.dat";
    std::string second = first;
    std::swap(second[6], second[14]);

    Bytes plain;
    for (int index = 0; index < 4000; ++index) {
        plain.push_back(static_cast<uint8_t>("canopy"[index % 6] + index / 600));
    }
    Bytes block;
    nltx::Lz4Encode(plain.data(), plain.size(), block);
    Bytes packed;
    nltx::YkcmpPack(block, static_cast<uint32_t>(plain.size()), packed);
    Bytes type7 = YkcmpHeader(7, nltx::kYkcmpHeaderSize + 4, 16);
    type7.insert(type7.end(), {1, 2, 3, 4});

    const Bytes archiveBytes = BuildArchive({
        {first, Text("first"), 1000},
        {second, Text("second"), 1001},
        {"data/dup.dat", Text("older"), 1},
        {"data/dup.dat", Text("newer"), 2},
        {"data/packed.dat", packed, 3},
        {"data/type7.head", type7, 4},
        {"data/empty.dat", Bytes(), 5},
    });
    const fs::path path = folder / "synthetic.dat";
    WriteBytes(path, archiveBytes);

    archive::Archive arc;
    std::string error;
    CHECK(arc.Open(path.wstring(), error));
    if (!CHECK(arc.IsOpen())) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(arc.Version() == 0x00204EAC);
    CHECK(arc.Entries().size() == 7);
    CHECK(std::is_sorted(arc.Entries().begin(), arc.Entries().end(), [](const auto& left, const auto& right) {
        return left.hash < right.hash;
    }));

    Bytes bytes;
    auto readText = [&](std::string_view name) {
        const size_t index = arc.Find(name);
        if (index == archive::Archive::kNotFound || !arc.ReadRaw(index, bytes, error)) {
            return std::string("<missing>");
        }
        return std::string(bytes.begin(), bytes.end());
    };
    CHECK(readText(first) == "first");
    CHECK(readText(second) == "second");
    CHECK(readText(Upper(first)) == "first");
    CHECK(readText("Data/K0ABCdefgh1Z.DAT") == "first");
    CHECK(readText("data/dup.dat") == "newer");
    CHECK(readText(std::string_view("data/dup.dat\0junk", 17)) == "newer");
    CHECK(arc.Find("data\\dup.dat") == archive::Archive::kNotFound);
    CHECK(arc.Find("/data/dup.dat") == archive::Archive::kNotFound);
    CHECK(arc.Find("data/dup.da") == archive::Archive::kNotFound);
    CHECK(arc.Find("") == archive::Archive::kNotFound);
    CHECK(arc.Find(std::string(600, 'a')) == archive::Archive::kNotFound);

    const size_t emptyIndex = arc.Find("data/empty.dat");
    CHECK(emptyIndex != archive::Archive::kNotFound);
    CHECK(arc.ReadRaw(emptyIndex, bytes, error) && bytes.empty());
    CHECK(arc.Entries()[emptyIndex].timestamp == 5);

    const size_t packedIndex = arc.Find("data/packed.dat");
    bool wrapped = false;
    CHECK(arc.ReadRaw(packedIndex, bytes, error) && bytes == packed);
    CHECK(arc.Read(packedIndex, bytes, error, &wrapped) && wrapped && bytes == plain);
    CHECK(arc.Read(arc.Find(first), bytes, error, &wrapped) && !wrapped && bytes == Text("first"));

    const size_t type7Index = arc.Find("data/type7.head");
    CHECK(arc.ReadRaw(type7Index, bytes, error) && bytes == type7);
    error.clear();
    CHECK(!arc.Read(type7Index, bytes, error, &wrapped) && !wrapped);
    CHECK(error.find("data/type7.head") != std::string::npos && error.find("type 7") != std::string::npos);
    CHECK(!arc.ReadRaw(arc.Entries().size(), bytes, error));

    // Moving keeps the open file.
    archive::Archive moved(std::move(arc));
    CHECK(moved.IsOpen() && moved.Find("data/dup.dat") != archive::Archive::kNotFound);
    moved.Close();
    CHECK(!moved.IsOpen() && moved.Entries().empty() && moved.Find("data/dup.dat") == archive::Archive::kNotFound);
    CHECK(!moved.ReadRaw(0, bytes, error));

    // An archive with no entries, like msg_1_00.dat.
    WriteBytes(folder / "empty.dat", BuildArchive({}));
    CHECK(moved.Open((folder / "empty.dat").wstring(), error) && moved.Entries().empty());
    // msg_1_00.dat is only a header, and its empty tables sit at 512.
    Bytes bare = BuildArchive({});
    PutU64(bare, 0x10, 512);
    PutU64(bare, 0x20, 512);
    WriteBytes(folder / "bare.dat", bare);
    CHECK(moved.Open((folder / "bare.dat").wstring(), error) && moved.Entries().empty());
}

void TestBrokenArchives(const fs::path& folder) {
    std::printf("broken archives\n");
    const Bytes good = BuildArchive({{"data/a.dat", Text("abc"), 1}});
    archive::Archive arc;
    std::string error;
    auto openFails = [&](const char* name, const Bytes& data, const char* expected) {
        const fs::path path = folder / name;
        WriteBytes(path, data);
        error.clear();
        const bool failed = !arc.Open(path.wstring(), error) && !arc.IsOpen();
        const bool said = error.find(expected) != std::string::npos;
        if (!failed || !said) {
            std::printf("  %s: %s\n", name, error.c_str());
        }
        return failed && said;
    };

    Bytes badMagic = good;
    badMagic[0] = 'X';
    CHECK(openFails("bad_magic.dat", badMagic, "not a FAFULLFS"));
    Bytes sony = good;
    std::memcpy(sony.data(), "PS_FS_V1", 8);
    CHECK(openFails("sony.dat", sony, "PS_FS_V1"));
    CHECK(openFails("short.dat", Bytes(good.begin(), good.begin() + 0x10), "too short"));
    Bytes truncatedTable = good;
    truncatedTable.resize(truncatedTable.size() - 1);
    CHECK(openFails("truncated_table.dat", truncatedTable, "does not fit"));
    Bytes hugeCount = good;
    PutU32(hugeCount, 0x08, 0xFFFFFFFFu);
    CHECK(openFails("huge_count.dat", hugeCount, "does not fit"));
    // The one entry's data offset field is at table + 0x20.
    Bytes pastEnd = good;
    uint64_t tableOffset = 0;
    std::memcpy(&tableOffset, good.data() + 0x20, 8);
    PutU64(pastEnd, static_cast<size_t>(tableOffset + 0x20), good.size());
    CHECK(openFails("past_end.dat", pastEnd, "runs past the end"));
    Bytes unterminated = good;
    PutU64(unterminated, static_cast<size_t>(tableOffset + 0x08), 0x10000);
    CHECK(openFails("unterminated.dat", unterminated, "no terminated name"));

    error.clear();
    CHECK(!arc.Open((folder / "no_such_file.dat").wstring(), error));
    CHECK(error.find("cannot open") != std::string::npos);
}

void TestSyntheticGameData(const fs::path& root) {
    std::printf("synthetic game folder\n");
    const fs::path game = root / L"game \u00e9";
    fs::create_directories(game / "data");
    const Bytes list = BuildArchiveList({
        {1, "data/a_1_00.dat"},
        {2, "data/b_1_00.dat"},
        {3, "data/missing_1_00.dat"},
        {4, ""},
    });
    WriteBytes(
        game / "data.dat",
        BuildArchive({{"data/database/archive.dat", list, 1}, {"data/database/item.dat", Text("table"), 1}})
    );
    WriteBytes(
        game / "data" / "a_1_00.dat",
        BuildArchive({{"data/shared.dat", Text("from a"), 1}, {"data/only_a.dat", Text("only a"), 1}})
    );
    WriteBytes(
        game / "data" / "b_1_00.dat",
        BuildArchive({{"data/shared.dat", Text("from b"), 1}, {"data/database/item.dat", Text("shadowed"), 1}})
    );

    archive::GameData data;
    std::string error;
    if (!CHECK(data.Open(game.wstring(), error))) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(data.ArchiveCount() == 3);
    if (data.ArchiveCount() == 3) {
        CHECK(fs::path(data.ArchiveAt(0).Path()).filename() == "data.dat");
        CHECK(fs::path(data.ArchiveAt(1).Path()).filename() == "b_1_00.dat");
        CHECK(fs::path(data.ArchiveAt(2).Path()).filename() == "a_1_00.dat");
    }
    Bytes bytes;
    auto readText = [&](std::string_view name) {
        return data.ReadRaw(name, bytes, error) ? std::string(bytes.begin(), bytes.end()) : std::string("<missing>");
    };
    CHECK(readText("data/shared.dat") == "from b");
    CHECK(readText("data/only_a.dat") == "only a");
    CHECK(readText("data/database/item.dat") == "table");
    CHECK(data.Find("data/shared.dat").archive == 1);
    CHECK(data.FindEntry("DATA/ONLY_A.DAT") && data.FindEntry("DATA/ONLY_A.DAT")->name == "data/only_a.dat");
    error.clear();
    CHECK(readText("data/nothing.dat") == "<missing>" && error.find("no mounted archive") != std::string::npos);
    CHECK(!data.Find("data/nothing.dat").Found() && !data.FindEntry("data/nothing.dat"));

    // A data.dat without the archive list fails with the path it wanted.
    const fs::path bare = root / "bare";
    fs::create_directories(bare);
    WriteBytes(bare / "data.dat", BuildArchive({{"data/x.dat", Text("x"), 1}}));
    error.clear();
    CHECK(!data.Open(bare.wstring(), error) && !data.IsOpen());
    CHECK(error.find("data/database/archive.dat") != std::string::npos);
    error.clear();
    CHECK(!data.Open((root / "nowhere").wstring(), error) && error.find("cannot open") != std::string::npos);
}

// A game folder whose files differ in case from the names data.dat and
// archive.dat use, opened through a path with '\' separators, as a Windows
// path is written. Windows ignores the case itself; elsewhere the lookup
// finds DATA.DAT and Data/A_1_00.Dat.
void TestCaseGameData(const fs::path& root) {
    std::printf("game folder names in another case\n");
    const fs::path game = root / "case game";
    fs::create_directories(game / "Data");
    const Bytes list = BuildArchiveList({{1, "data/a_1_00.dat"}});
    WriteBytes(game / "DATA.DAT", BuildArchive({{"data/database/archive.dat", list, 1}}));
    WriteBytes(game / "Data" / "A_1_00.Dat", BuildArchive({{"data/only_a.dat", Text("only a"), 1}}));
    std::wstring path = game.wstring();
    std::replace(path.begin(), path.end(), L'/', L'\\');

    archive::GameData data;
    std::string error;
    if (!CHECK(data.Open(path, error))) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(data.ArchiveCount() == 2);
    Bytes bytes;
    CHECK(data.ReadRaw("data/only_a.dat", bytes, error) && std::string(bytes.begin(), bytes.end()) == "only a");
}

// ---- The real game ---------------------------------------------------------

struct ExpectedArchive {
    const char* file;
    size_t entries;
};

// Mount order and entry counts of the shipped files.
const ExpectedArchive kExpected[] = {
    {"data.dat", 3175},       {"fairy_1_00.dat", 14},    {"se_1_00.dat", 1068},       {"bgm_1_00.dat", 38},
    {"map_1_00.dat", 396},    {"shader_1_00.dat", 70},   {"texture_1_00.dat", 6255}, {"misc_1_00.dat", 15},
};

size_t ArchiveIndex(const archive::GameData& data, const char* file) {
    for (size_t index = 0; index < data.ArchiveCount(); ++index) {
        if (fs::path(data.ArchiveAt(index).Path()).filename() == file) {
            return index;
        }
    }
    return archive::Archive::kNotFound;
}

void DecodeTexture(const archive::GameData& data, const std::string& name) {
    Bytes file;
    std::string error;
    bool wrapped = true;
    if (!CHECK(data.Read(name, file, error, &wrapped))) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(!wrapped);
    nltx::Header header;
    uint32_t width = 0;
    uint32_t height = 0;
    Bytes pixels;
    const bool parsed = nltx::ParseHeader(file.data(), file.size(), header, error);
    const bool decoded = parsed && nltx::Decode(file.data(), file.size(), width, height, pixels, error);
    CHECK(decoded);
    CHECK(width == header.width && height == header.height && width > 0 && height > 0);
    CHECK(pixels.size() == static_cast<size_t>(width) * height * 4);
    std::printf(
        "  %s: format 0x%08X, %ux%u, %s\n", name.c_str(), header.format, width, height,
        decoded ? "decoded" : error.c_str()
    );
}

void TestRealGame() {
    std::printf("real game archives\n");
    const fs::path game = test_support::GameDir();
    if (!fs::exists(game / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", game.u8string().c_str());
        return;
    }

    archive::GameData data;
    std::string error;
    const auto start = std::chrono::steady_clock::now();
    const bool opened = data.Open(game.wstring(), error);
    const auto openMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    if (!CHECK(opened)) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    std::printf("  opened %zu archives in %lld ms\n", data.ArchiveCount(), static_cast<long long>(openMs));

    CHECK(data.ArchiveCount() == std::size(kExpected));
    size_t total = 0;
    for (size_t index = 0; index < data.ArchiveCount() && index < std::size(kExpected); ++index) {
        const archive::Archive& arc = data.ArchiveAt(index);
        const auto& entries = arc.Entries();
        CHECK(fs::path(arc.Path()).filename() == kExpected[index].file);
        CHECK(entries.size() == kExpected[index].entries);
        CHECK(arc.Version() == 0x00204EAC);
        size_t hashed = 0;
        size_t found = 0;
        size_t foundUpper = 0;
        size_t firstHit = 0;
        for (size_t entry = 0; entry < entries.size(); ++entry) {
            hashed += archive::HashPath(entries[entry].name) == entries[entry].hash;
            found += arc.Find(entries[entry].name) == entry;
            foundUpper += arc.Find(Upper(entries[entry].name)) == entry;
            // No path is in two archives, so the first hit is this one.
            firstHit += data.Find(entries[entry].name).archive == index;
        }
        CHECK(hashed == entries.size());
        CHECK(found == entries.size());
        CHECK(foundUpper == entries.size());
        CHECK(firstHit == entries.size());
        std::printf(
            "  [%zu] %s: %zu entries, %zu hashes match, %zu found by name\n", index,
            fs::path(arc.Path()).filename().u8string().c_str(), entries.size(), hashed, found
        );
        total += entries.size();
    }
    std::printf("  %zu entries in all\n", total);

    // Not in archive.dat, so never mounted, but it opens.
    archive::Archive msg;
    CHECK(msg.Open((game / "data" / "msg_1_00.dat").wstring(), error) && msg.Entries().empty());
    CHECK(ArchiveIndex(data, "msg_1_00.dat") == archive::Archive::kNotFound);

    // Known lookups.
    CHECK(data.Find("data/database/texture.dat").archive == 0);
    CHECK(data.Find("data/database/icon.dat").archive == 0);
    CHECK(data.Find("data/database/archive.dat").archive == 0);
    CHECK(data.Find("Data/Database/Texture.DAT").entry == data.Find("data/database/texture.dat").entry);
    CHECK(!data.Find("data\\database\\texture.dat").Found());
    CHECK(!data.Find("data/database/no_such_table.dat").Found());
    CHECK(!data.Find("database/texture.dat").Found());
    CHECK(data.Find("data/misc/map_9999_fisharea_0001.tga").archive == ArchiveIndex(data, "misc_1_00.dat"));

    // Tables against the extracted copies.
    const fs::path extracted = kSourceDir / "_work" / "x" / "data" / "database";
    for (const char* table : {"texture.dat", "icon.dat"}) {
        const std::string name = std::string("data/database/") + table;
        Bytes raw;
        Bytes read;
        bool wrapped = true;
        CHECK(data.ReadRaw(name, raw, error));
        CHECK(data.Read(name, read, error, &wrapped) && !wrapped && read == raw);
        CHECK(data.FindEntry(name) && raw.size() == data.FindEntry(name)->size);
        if (fs::exists(extracted / table)) {
            const Bytes copy = ReadBytes(extracted / table);
            CHECK(raw == copy);
            std::printf("  %s: %zu bytes, %s the extracted copy\n", name.c_str(), raw.size(),
                        raw == copy ? "equal to" : "DIFFERENT from");
        } else {
            std::printf("  note: no extracted %s under _work, compare skipped\n", table);
        }
    }

    // One BGRA8 and one BC7 texture, the first of each by table order. Only
    // fairy_player files are BGRA8, and not all of them.
    const size_t textures = ArchiveIndex(data, "texture_1_00.dat");
    std::string bgra;
    std::string bc7;
    if (CHECK(textures != archive::Archive::kNotFound)) {
        const archive::Archive& arc = data.ArchiveAt(textures);
        Bytes file;
        nltx::Header header;
        for (size_t index = 0; index < arc.Entries().size() && (bgra.empty() || bc7.empty()); ++index) {
            const std::string& name = arc.Entries()[index].name;
            const bool player = name.rfind("data/texture/fairy_player/", 0) == 0;
            if ((player && bgra.empty()) || (!player && bc7.empty())) {
                if (!arc.ReadRaw(index, file, error) || !nltx::ParseHeader(file.data(), file.size(), header, error)) {
                    continue;
                }
                if (header.format == nltx::kFormatBgra8 && bgra.empty()) {
                    bgra = name;
                }
                if (header.format == nltx::kFormatBc7 && bc7.empty()) {
                    bc7 = name;
                }
            }
        }
    }
    if (CHECK(!bgra.empty() && !bc7.empty())) {
        DecodeTexture(data, bgra);
        DecodeTexture(data, bc7);
    }

    // Map .head entries are YKCMP type 7: raw reads work, unpacking says why not.
    const size_t maps = ArchiveIndex(data, "map_1_00.dat");
    if (CHECK(maps != archive::Archive::kNotFound)) {
        for (const archive::Entry& entry : data.ArchiveAt(maps).Entries()) {
            if (entry.name.size() > 5 && entry.name.compare(entry.name.size() - 5, 5, ".head") == 0) {
                Bytes raw;
                Bytes read;
                CHECK(data.ReadRaw(entry.name, raw, error) && archive::IsYkcmp(raw.data(), raw.size()));
                error.clear();
                CHECK(!data.Read(entry.name, read, error));
                CHECK(error.find("type 7") != std::string::npos);
                std::printf("  %s: raw %zu bytes, unpack refused: %s\n", entry.name.c_str(), raw.size(), error.c_str());
                break;
            }
        }
    }

    // Concurrent reads of the same files match a single-threaded read.
    std::vector<std::string> names = {"data/database/texture.dat", "data/database/icon.dat", bc7, bgra};
    std::vector<Bytes> expected(names.size());
    for (size_t index = 0; index < names.size(); ++index) {
        data.ReadRaw(names[index], expected[index], error);
    }
    std::vector<int> mismatches(8, 0);
    std::vector<std::thread> threads;
    for (size_t thread = 0; thread < mismatches.size(); ++thread) {
        threads.emplace_back([&, thread] {
            Bytes bytes;
            std::string threadError;
            for (int round = 0; round < 6; ++round) {
                for (size_t index = 0; index < names.size(); ++index) {
                    const size_t pick = (index + thread + round) % names.size();
                    if (!data.Read(names[pick], bytes, threadError) || bytes != expected[pick]) {
                        ++mismatches[thread];
                    }
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    int mismatched = 0;
    for (const int count : mismatches) {
        mismatched += count;
    }
    CHECK(mismatched == 0);
    std::printf("  %zu threads x 24 reads, %d mismatches\n", threads.size(), mismatched);
}

}  // namespace

int main() {
    const fs::path folder = TempFolder();
    TestHash();
    TestSyntheticArchive(folder);
    TestBrokenArchives(folder);
    TestSyntheticGameData(folder);
    TestCaseGameData(folder);
    TestRealGame();
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
