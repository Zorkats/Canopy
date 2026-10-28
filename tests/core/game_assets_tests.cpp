#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "archive.h"
#include "blob.h"
#include "game_assets.h"
#include "image_io.h"
#include "nltx.h"
#include "test_support.h"

// Checks of game_assets.h. A synthetic game folder is built in a temporary
// folder: a data.dat holding small icon, texture, item, character, whistle,
// craft and cooking tables, a texture and a few .nlsd files. The real
// install is then read, never written; when it is missing those checks
// print a note and are skipped. Set CANOPY_GAME_DIR to use another install.
// Only ids, counts and sizes are printed, never game text.
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

void WriteBytes(const fs::path& path, const Bytes& data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

Bytes Text(const std::string& text) {
    return Bytes(text.begin(), text.end());
}

void AppendU32(Bytes& data, uint32_t value) {
    data.resize(data.size() + 4);
    blob::Put<uint32_t>(data.data(), data.size() - 4, value);
}

void AppendU64(Bytes& data, uint64_t value) {
    data.resize(data.size() + 8);
    blob::Put<uint64_t>(data.data(), data.size() - 8, value);
}

// One error sentence: something, ending in a period.
bool Sentence(const std::string& error) {
    return error.size() > 1 && error.back() == '.';
}

bool Has(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

// ---- Synthetic archives (as in archive_tests.cpp) --------------------------

struct FakeFile {
    std::string name;
    Bytes data;
};

// A FAFULLFS file: header, entry data, name table, then the entry table
// sorted by hash.
Bytes BuildArchive(std::vector<FakeFile> files) {
    std::stable_sort(files.begin(), files.end(), [](const FakeFile& left, const FakeFile& right) {
        return archive::HashPath(left.name) < archive::HashPath(right.name);
    });
    Bytes out(0x30, 0);
    std::memcpy(out.data(), "FAFULLFS", 8);
    blob::Put<uint32_t>(out.data(), 0x08, static_cast<uint32_t>(files.size()));
    blob::Put<uint32_t>(out.data(), 0x0C, 0x00204EAC);
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
        AppendU64(out, 1);
    }
    blob::Put<uint64_t>(out.data(), 0x10, nameOffset);
    blob::Put<uint64_t>(out.data(), 0x18, nameSize);
    blob::Put<uint64_t>(out.data(), 0x20, tableOffset);
    return out;
}

// ---- Synthetic tables ------------------------------------------------------

// Rows of one table and their texts, built into a blob.
struct TableBuilder {
    std::vector<Bytes> rows;
    blob::Pool pool{0};

    Bytes& Row(size_t size, uint64_t id, const std::string& enumName) {
        rows.emplace_back(size, 0);
        blob::Put<uint64_t>(rows.back().data(), 0, id);
        pool.Write(rows.back(), 8, enumName);
        return rows.back();
    }
    // Seven texts from offset, one per language; missing ones are empty.
    void Texts(Bytes& row, size_t offset, const std::vector<std::string>& texts) {
        for (size_t language = 0; language < 7; ++language) {
            pool.Write(row, offset + 8 * language, language < texts.size() ? texts[language] : std::string());
        }
    }
    Bytes Build() const {
        Bytes out;
        blob::Build(rows, pool.Bytes(), out);
        return out;
    }
};

struct Rect {
    float x, y, w, h;
};

// An icon.dat row: the English cell is rect, the other six languages a
// different one, so a test sees which is read.
void IconRow(TableBuilder& icons, uint64_t id, uint64_t texture, Rect rect) {
    Bytes& row = icons.Row(272, id, "ICON_ID_" + std::to_string(id));
    blob::Put<uint64_t>(row.data(), 24, texture);
    for (size_t language = 0; language < 7; ++language) {
        const Rect cell = language == 1 ? rect : Rect{0, 0, 1, 1};
        const size_t at = 32 + 32 * language;
        blob::Put<float>(row.data(), at, cell.x);
        blob::Put<float>(row.data(), at + 4, cell.y);
        blob::Put<float>(row.data(), at + 8, cell.w);
        blob::Put<float>(row.data(), at + 12, cell.h);
    }
}

void ItemRow(TableBuilder& items, uint64_t id, const std::vector<std::string>& names, uint64_t icon, uint64_t group) {
    Bytes& row = items.Row(536, id, "ITEM_ID_" + std::to_string(id));
    items.Texts(row, 32, names);
    blob::Put<uint64_t>(row.data(), 320, group);
    blob::Put<uint64_t>(row.data(), 536 - 52, icon);
}

// A pixel whose bytes say where it is.
uint32_t PixelAt(uint32_t x, uint32_t y) {
    return 0xFF000000u | (x << 16) | (y << 8) | 0x5A;
}

// An Ogg page: the fixed header, the lacing for body, then body.
Bytes OggPage(uint8_t flags, uint32_t serial, uint32_t sequence, const Bytes& body) {
    Bytes page = Text("OggS");
    page.push_back(0);
    page.push_back(flags);
    AppendU64(page, body.size());
    AppendU32(page, serial);
    AppendU32(page, sequence);
    AppendU32(page, 0);
    size_t left = body.size();
    Bytes lacing;
    do {
        const size_t lace = std::min<size_t>(left, 255);
        lacing.push_back(static_cast<uint8_t>(lace));
        left -= lace;
    } while (left > 0 || lacing.back() == 255);
    page.push_back(static_cast<uint8_t>(lacing.size()));
    page.insert(page.end(), lacing.begin(), lacing.end());
    page.insert(page.end(), body.begin(), body.end());
    return page;
}

Bytes NlsdHeader(uint32_t codec, size_t after) {
    Bytes header;
    AppendU32(header, codec);
    AppendU32(header, static_cast<uint32_t>(after));
    AppendU32(header, 48000 | (1u << 16));
    AppendU32(header, 1000);
    AppendU32(header, 0);
    AppendU32(header, static_cast<uint32_t>(after));
    return header;
}

Bytes Joined(const std::vector<Bytes>& parts) {
    Bytes out;
    for (const Bytes& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

// Ids of a NamesJson result's entries, in order.
std::vector<uint64_t> EntryIds(const std::string& json) {
    std::vector<uint64_t> ids;
    const std::string key = "{\"id\":";
    for (size_t at = json.find(key); at != std::string::npos; at = json.find(key, at + 1)) {
        ids.push_back(std::strtoull(json.c_str() + at + key.size(), nullptr, 10));
    }
    return ids;
}

size_t Count(const std::string& text, const std::string& part) {
    size_t count = 0;
    for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1)) {
        ++count;
    }
    return count;
}

fs::path BuildGame(const fs::path& game, Bytes& ogg) {
    fs::create_directories(game / "data");

    TableBuilder icons;
    IconRow(icons, 100, 10, {2, 1, 3, 4});
    IconRow(icons, 101, 10, {6, 4, 5, 5});
    IconRow(icons, 102, 10, {9, 0, 2, 2});
    IconRow(icons, 103, 11, {0, 0, 2, 2});
    IconRow(icons, 104, 12, {0, 0, 2, 2});
    IconRow(icons, 105, 10, {std::nanf(""), 0, 2, 2});
    IconRow(icons, 106, 13, {0, 0, 2, 2});
    IconRow(icons, 107, 10, {2.4f, 0.6f, 1.2f, 1.0f});

    TableBuilder textures;
    for (const uint64_t id : {10, 12, 13}) {
        Bytes& row = textures.Row(116, id, "TEXTURE_ID_" + std::to_string(id));
        const std::string path = id == 10 ? "data/texture/cells.nltx" : id == 12 ? "data/texture/gone.nltx" : "";
        textures.Texts(row, 24, {"", path, "", "", "", "", ""});
    }

    TableBuilder items;
    ItemRow(items, 1000, {"ja-a", "en-a", "fr-a", "es-a", "tw-a", "ko-a", "cn-a"}, 100, 7);
    ItemRow(items, 1001, {"ja-b", "en-\"b\""}, 5555, 3);
    ItemRow(items, 1002, {}, 0, 0);

    TableBuilder characters;
    for (const uint64_t id : {1, 2}) {
        Bytes& row = characters.Row(220, id, "CHARA_ID_" + std::to_string(id));
        characters.Texts(row, 24, {"ja-c", id == 1 ? "en-c" : ""});
    }

    TableBuilder whistles;
    for (const uint64_t id : {1, 2}) {
        Bytes& row = whistles.Row(96, id, "WHISTLE_ID_" + std::to_string(id));
        whistles.Texts(row, 16, {"ja-w", "en-w"});
        blob::Put<uint64_t>(row.data(), 80, id == 1 ? 101 : 777);
    }

    TableBuilder crafts;
    blob::Put<uint64_t>(crafts.Row(148, 50, "CRAFT_ID_A").data(), 92, 1000);
    blob::Put<uint64_t>(crafts.Row(148, 51, "CRAFT_ID_B").data(), 92, 1001);
    TableBuilder cooking;
    blob::Put<uint64_t>(cooking.Row(176, 60, "COOKING_ID_A").data(), 24, 1000);
    blob::Put<uint64_t>(cooking.Row(176, 61, "COOKING_ID_B").data(), 24, 9999);

    std::vector<uint8_t> pixels(8 * 6 * 4);
    for (uint32_t y = 0; y < 6; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            blob::Put<uint32_t>(pixels.data(), (y * 8 + x) * 4, PixelAt(x, y));
        }
    }
    Bytes texture;
    std::string error;
    CHECK(nltx::Encode(8, 6, pixels, texture, error, nltx::Packing::Literals));

    // A chained Ogg of two links, as the looping sounds are.
    const Bytes identification = Joined({Text("\x01vorbis"), Bytes(23, 1)});
    ogg = Joined({
        OggPage(2, 7, 0, identification),
        OggPage(0, 7, 1, Bytes(600, 2)),
        OggPage(4, 7, 2, Bytes(40, 3)),
        OggPage(2, 8, 0, identification),
        OggPage(4, 8, 1, Bytes(10, 4)),
    });
    const Bytes good = Joined({NlsdHeader(7, ogg.size()), ogg});
    Bytes cut = good;
    cut.pop_back();
    const Bytes tail = Joined({good, Text("junk")});
    const Bytes notFirst = Joined({NlsdHeader(7, 0), OggPage(0, 7, 0, identification)});
    const Bytes notVorbis = Joined({NlsdHeader(7, 0), OggPage(2, 7, 0, Bytes(30, 9))});
    const Bytes wave = Joined({NlsdHeader(5, 12), Text("RIFF"), Bytes(4, 0), Text("WAVE")});

    // An archive list with no rows: data.dat is the only archive.
    const Bytes list(blob::kHeaderSize, 0);
    WriteBytes(
        game / "data.dat",
        BuildArchive({
            {"data/database/archive.dat", list},
            {"data/database/icon.dat", icons.Build()},
            {"data/database/texture.dat", textures.Build()},
            {"data/database/item.dat", items.Build()},
            {"data/database/character.dat", characters.Build()},
            {"data/database/whistle.dat", whistles.Build()},
            {"data/database/craft.dat", crafts.Build()},
            {"data/database/cooking.dat", cooking.Build()},
            {"data/texture/cells.nltx", texture},
            {"data/bgm/good.nlsd", good},
            {"data/bgm/cut.nlsd", cut},
            {"data/bgm/tail.nlsd", tail},
            {"data/bgm/notfirst.nlsd", notFirst},
            {"data/bgm/notvorbis.nlsd", notVorbis},
            {"data/se/wave.nlsd", wave},
            {"data/se/none.nlsd", Bytes(64, 0)},
        })
    );
    return game;
}

void TestSyntheticIcons(const std::wstring& game) {
    std::printf("synthetic icons\n");
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
    std::string error;

    // The English cell, 3 x 4 from (2, 1).
    if (CHECK(game_assets::Icon(game, 100, width, height, pixels, error))) {
        CHECK(width == 3 && height == 4);
        CHECK(pixels.size() == 3 * 4 * 4);
        bool same = pixels.size() == 3 * 4 * 4;
        for (uint32_t y = 0; same && y < 4; ++y) {
            for (uint32_t x = 0; x < 3; ++x) {
                same = same && blob::Get<uint32_t>(pixels.data(), (y * 3 + x) * 4) == PixelAt(2 + x, 1 + y);
            }
        }
        CHECK(same);
    } else {
        std::printf("  %s\n", error.c_str());
    }
    // Clamped at the right and bottom edges: 2 x 2 from (6, 4).
    if (CHECK(game_assets::Icon(game, 101, width, height, pixels, error))) {
        CHECK(width == 2 && height == 2);
        CHECK(pixels.size() == 16 && blob::Get<uint32_t>(pixels.data(), 0) == PixelAt(6, 4));
        CHECK(blob::Get<uint32_t>(pixels.data(), 12) == PixelAt(7, 5));
    }
    // Rounded to whole pixels: x 2.4 to 3.6 is 2 to 4, y 0.6 to 1.6 is 1 to 2.
    if (CHECK(game_assets::Icon(game, 107, width, height, pixels, error))) {
        CHECK(width == 2 && height == 1);
        CHECK(pixels.size() == 8 && blob::Get<uint32_t>(pixels.data(), 0) == PixelAt(2, 1));
    }

    const struct {
        uint32_t id;
        const char* part;
    } failures[] = {
        {999, "Icon 999 is not in"},
        {102, "empty cell"},
        {105, "empty cell"},
        {103, "texture 11, which is not in"},
        {104, "data/texture/gone.nltx could not be read"},
        {106, "texture 13, which has no file"},
    };
    for (const auto& failure : failures) {
        pixels.assign(4, 1);
        width = 9;
        error.clear();
        const bool ok = game_assets::Icon(game, failure.id, width, height, pixels, error);
        if (!CHECK(!ok && Sentence(error) && Has(error, failure.part))) {
            std::printf("  icon %u: %s\n", failure.id, error.c_str());
        }
        CHECK(width == 0 && height == 0 && pixels.empty());
        CHECK(Count(error, ". ") == 0);
    }
    CHECK(!game_assets::Icon(L"", 100, width, height, pixels, error) && Sentence(error));
}

void TestSyntheticIconBatch(const std::wstring& game, const fs::path& root) {
    std::printf("synthetic icon batch\n");
    const fs::path out = root / L"icons é";
    // 100 twice, 101 clamped, 999 missing, 102 an empty cell.
    const std::string json = game_assets::IconsJson(game, {100, 101, 100, 999, 102}, out.wstring());
    CHECK(Has(json, "\"abi\":1,\"ok\":true,\"error\":null,\"written\":2,\"problems\":["));
    CHECK(Has(json, "Icon 999 is not in") && Has(json, "Icon 102 has an empty cell"));
    CHECK(Count(json, "\",\"") == 1);
    for (const uint32_t id : {100u, 101u}) {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> cut;
        std::vector<uint8_t> saved;
        std::string error;
        uint32_t savedWidth = 0;
        uint32_t savedHeight = 0;
        CHECK(game_assets::Icon(game, id, width, height, cut, error));
        if (CHECK(image_io::Load((out / (std::to_wstring(id) + L".png")).wstring(), savedWidth, savedHeight, saved,
                                 error))) {
            CHECK(savedWidth == width && savedHeight == height && saved == cut);
        } else {
            std::printf("  %s\n", error.c_str());
        }
    }
    CHECK(!fs::exists(out / L"999.png") && !fs::exists(out / L"102.png"));
    CHECK(Has(game_assets::IconsJson(game, {}, out.wstring()), "\"ok\":true,\"error\":null,\"written\":0,\"problems\":[]"));
    CHECK(Has(game_assets::IconsJson(game, {100}, L""), "\"ok\":false"));
    CHECK(Has(game_assets::IconsJson(L"", {100}, out.wstring()), "\"ok\":false"));
}

void TestSyntheticNames(const std::wstring& game) {
    std::printf("synthetic names\n");
    std::string json = game_assets::NamesJson(game, "item", "en");
    CHECK(Has(json, "\"abi\":1,\"ok\":true,\"error\":null,\"kind\":\"item\",\"language\":\"en\""));
    CHECK(Has(json, "{\"id\":1000,\"enum\":\"ITEM_ID_1000\",\"name\":\"en-a\",\"icon\":100,\"group\":7}"));
    // An icon icon.dat lacks is null; quotes in a name are escaped.
    CHECK(Has(json, "{\"id\":1001,\"enum\":\"ITEM_ID_1001\",\"name\":\"en-\\\"b\\\"\",\"icon\":null,\"group\":3}"));
    CHECK(Has(json, "{\"id\":1002,\"enum\":\"ITEM_ID_1002\",\"name\":\"\",\"icon\":null,\"group\":0}"));
    CHECK((EntryIds(json) == std::vector<uint64_t>{1000, 1001, 1002}));

    // Each code picks its own column, in the game's order; case does not matter.
    const char* codes[] = {"ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"};
    const char* names[] = {"ja-a", "en-a", "fr-a", "es-a", "tw-a", "ko-a", "cn-a"};
    for (size_t index = 0; index < 7; ++index) {
        json = game_assets::NamesJson(game, "item", codes[index]);
        CHECK(Has(json, std::string("\"id\":1000,\"enum\":\"ITEM_ID_1000\",\"name\":\"") + names[index] + "\""));
    }
    json = game_assets::NamesJson(game, "Item", "ZH-TW");
    CHECK(Has(json, "\"ok\":true") && Has(json, "\"language\":\"zh-tw\"") && Has(json, "\"name\":\"tw-a\""));
    // No French name: empty, not another language.
    CHECK(Has(game_assets::NamesJson(game, "item", "fr"), "\"id\":1001,\"enum\":\"ITEM_ID_1001\",\"name\":\"\""));

    json = game_assets::NamesJson(game, "villager", "en");
    CHECK(Has(json, "{\"id\":1,\"enum\":\"CHARA_ID_1\",\"name\":\"en-c\",\"icon\":null,\"group\":null}"));
    CHECK(Has(json, "{\"id\":2,\"enum\":\"CHARA_ID_2\",\"name\":\"\",\"icon\":null,\"group\":null}"));

    json = game_assets::NamesJson(game, "whistle", "ja");
    CHECK(Has(json, "{\"id\":1,\"enum\":\"WHISTLE_ID_1\",\"name\":\"ja-w\",\"icon\":101,\"group\":null}"));
    CHECK(Has(json, "{\"id\":2,\"enum\":\"WHISTLE_ID_2\",\"name\":\"ja-w\",\"icon\":null,\"group\":null}"));

    // A recipe is its output item; an output that is no item gives nothing.
    json = game_assets::NamesJson(game, "craft", "en");
    CHECK(Has(json, "{\"id\":50,\"enum\":\"CRAFT_ID_A\",\"name\":\"en-a\",\"icon\":100,\"group\":7}"));
    CHECK(Has(json, "{\"id\":51,\"enum\":\"CRAFT_ID_B\",\"name\":\"en-\\\"b\\\"\",\"icon\":null,\"group\":3}"));
    json = game_assets::NamesJson(game, "cooking", "ko");
    CHECK(Has(json, "{\"id\":60,\"enum\":\"COOKING_ID_A\",\"name\":\"ko-a\",\"icon\":100,\"group\":7}"));
    CHECK(Has(json, "{\"id\":61,\"enum\":\"COOKING_ID_B\",\"name\":\"\",\"icon\":null,\"group\":null}"));

    const struct {
        const char* kind;
        const char* language;
        const char* part;
    } failures[] = {
        {"item", "de", "\\\"de\\\" is not a language code"},
        {"item", "", "is not a language code"},
        {"fish", "en", "\\\"fish\\\" is not a kind of name"},
        {"area", "en", "Area names cannot be listed"},
        {"gimmick", "en", "The game's archives have no data/database/gimmick.dat"},
    };
    for (const auto& failure : failures) {
        json = game_assets::NamesJson(game, failure.kind, failure.language);
        CHECK(Has(json, "\"abi\":1,\"ok\":false,\"error\":\"") && Has(json, failure.part) && Has(json, ".\""));
    }
    // The language is checked before the folder is opened.
    CHECK(Has(game_assets::NamesJson(L"", "item", "xx"), "is not a language code"));
    CHECK(Has(game_assets::NamesJson(L"", "item", "en"), "\"ok\":false"));
}

void TestSyntheticSounds(const std::wstring& game, const Bytes& expected) {
    std::printf("synthetic sounds\n");
    std::vector<uint8_t> ogg;
    std::string error;
    if (!CHECK(game_assets::Sound(game, "data/bgm/good.nlsd", ogg, error))) {
        std::printf("  %s\n", error.c_str());
    }
    CHECK(ogg == expected);

    const struct {
        const char* path;
        const char* part;
    } failures[] = {
        {"data/bgm/cut.nlsd", "not a whole chain of Ogg pages"},
        {"data/bgm/tail.nlsd", "not a whole chain of Ogg pages"},
        {"data/bgm/notfirst.nlsd", "does not start a Vorbis stream"},
        {"data/bgm/notvorbis.nlsd", "does not start a Vorbis stream"},
        {"data/se/wave.nlsd", "RIFF WAVE"},
        {"data/se/none.nlsd", "holds no Ogg stream"},
        {"data/bgm/missing.nlsd", "could not be read"},
    };
    for (const auto& failure : failures) {
        ogg.assign(3, 1);
        error.clear();
        const bool ok = game_assets::Sound(game, failure.path, ogg, error);
        if (!CHECK(!ok && ogg.empty() && Sentence(error) && Has(error, failure.part))) {
            std::printf("  %s: %s\n", failure.path, error.c_str());
        }
    }
}

// ---- The real install, read only -------------------------------------------

void TestRealGame() {
    std::printf("real game\n");
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return;
    }
    const std::wstring game = folder.wstring();
    archive::GameData data;
    std::string error;
    if (!CHECK(data.Open(game, error))) {
        std::printf("  %s\n", error.c_str());
        return;
    }

    // Names: unique ids in every kind, and English item names on most rows.
    for (const char* kind : {"item", "villager", "whistle", "craft", "cooking", "gimmick"}) {
        const std::string json = game_assets::NamesJson(game, kind, "en");
        const std::vector<uint64_t> ids = EntryIds(json);
        const std::set<uint64_t> unique(ids.begin(), ids.end());
        const size_t empty = Count(json, "\"name\":\"\"");
        const size_t icons = ids.size() - Count(json, "\"icon\":null");
        CHECK(Has(json, "\"ok\":true"));
        CHECK(!ids.empty() && unique.size() == ids.size());
        std::printf("  %s: %zu entries, %zu unique ids, %zu without an en name, %zu with an icon\n", kind,
                    ids.size(), unique.size(), empty, icons);
        if (std::strcmp(kind, "item") == 0) {
            CHECK(empty * 10 < ids.size());
        }
        // Furniture: every row its own item places, each drawn by that item.
        if (std::strcmp(kind, "gimmick") == 0) {
            CHECK(ids.size() == 1783 && icons == ids.size());
        }
    }
    CHECK(Has(game_assets::NamesJson(game, "area", "en"), "\"ok\":false"));

    // Icons: the first items' icons decode to their cells' sizes.
    std::vector<uint8_t> bytes;
    blob::Table items;
    blob::Table icons;
    std::vector<uint8_t> iconBytes;
    if (!CHECK(data.Read("data/database/item.dat", bytes, error) &&
               blob::Parse(bytes.data(), bytes.size(), items, error) &&
               data.Read("data/database/icon.dat", iconBytes, error) &&
               blob::Parse(iconBytes.data(), iconBytes.size(), icons, error))) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    size_t tried = 0;
    size_t matched = 0;
    for (size_t index = 0; index < items.rows.size() && tried < 12; index += items.rows.size() / 12 + 1) {
        const blob::Row& item = items.rows[index];
        const uint64_t iconId = blob::Get<uint64_t>(item.data, item.size - 52);
        const blob::Row* icon = nullptr;
        for (const blob::Row& row : icons.rows) {
            icon = blob::RowId(row) == iconId ? &row : icon;
        }
        if (!icon) {
            continue;
        }
        ++tried;
        const float w = blob::Get<float>(icon->data, 64 + 8);
        const float h = blob::Get<float>(icon->data, 64 + 12);
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
        const bool ok = game_assets::Icon(game, static_cast<uint32_t>(iconId), width, height, pixels, error);
        const bool sized = ok && width == static_cast<uint32_t>(std::lround(w)) &&
                           height == static_cast<uint32_t>(std::lround(h)) && pixels.size() == width * height * 4u;
        matched += sized;
        if (!sized) {
            std::printf("  icon %llu: %ux%u, %s\n", static_cast<unsigned long long>(iconId), width, height,
                        ok ? "not the cell's size" : error.c_str());
        }
    }
    CHECK(tried > 0 && matched == tried);
    std::printf("  item icons: %zu of %zu decode to their cell's size\n", matched, tried);

    // Every item's icon in one batch, into a temporary folder.
    {
        const std::string names = game_assets::NamesJson(game, "item", "en");
        std::vector<uint32_t> iconIds;
        const std::string key = "\"icon\":";
        for (size_t at = names.find(key); at != std::string::npos; at = names.find(key, at + 1)) {
            if (names.compare(at + key.size(), 4, "null") != 0) {
                iconIds.push_back(static_cast<uint32_t>(std::strtoul(names.c_str() + at + key.size(), nullptr, 10)));
            }
        }
        const fs::path out =
            fs::temp_directory_path() / ("canopy_icon_batch_" + std::to_string(test_support::ProcessId()));
        const auto batchStarted = std::chrono::steady_clock::now();
        const std::string json = game_assets::IconsJson(game, iconIds, out.wstring());
        const double batchSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - batchStarted).count();
        const std::set<uint32_t> unique(iconIds.begin(), iconIds.end());
        size_t files = 0;
        std::error_code ignored;
        for (const auto& entry : fs::directory_iterator(out, ignored)) {
            files += entry.path().extension() == ".png";
        }
        CHECK(Has(json, "\"ok\":true") && Has(json, "\"written\":" + std::to_string(files) + ","));
        CHECK(files * 100 >= unique.size() * 99);
        std::printf("  item icon batch: %zu ids, %zu unique, %zu saved, %zu problems, %.1f s\n", iconIds.size(),
                    unique.size(), files, Count(json, "Icon "), batchSeconds);
        fs::remove_all(out, ignored);
    }

    // Sounds: every Ogg .nlsd yields an Ogg stream, every other is refused.
    std::set<std::string> paths;
    for (size_t index = 0; index < data.ArchiveCount(); ++index) {
        for (const archive::Entry& entry : data.ArchiveAt(index).Entries()) {
            if (entry.name.size() > 5 && entry.name.compare(entry.name.size() - 5, 5, ".nlsd") == 0) {
                paths.insert(entry.name);
            }
        }
    }
    size_t ogg = 0;
    size_t wave = 0;
    size_t other = 0;
    size_t bgm = 0;
    const auto started = std::chrono::steady_clock::now();
    for (const std::string& path : paths) {
        std::vector<uint8_t> stream;
        if (game_assets::Sound(game, path, stream, error)) {
            ogg += stream.size() >= 4 && std::memcmp(stream.data(), "OggS", 4) == 0;
            bgm += path.compare(0, 9, "data/bgm/") == 0;
        } else if (Has(error, "RIFF WAVE")) {
            ++wave;
        } else {
            ++other;
        }
    }
    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    CHECK(ogg > 0 && bgm > 0 && other == 0);
    std::printf("  sounds: %zu .nlsd, %zu Ogg (%zu bgm), %zu WAVE refused, %zu other, %.1f s\n", paths.size(), ogg,
                bgm, wave, other, seconds);
}

}  // namespace

void TestGameAssets(int& checks, int& failures) {
    const fs::path root =
        fs::temp_directory_path() / ("canopy_game_assets_tests_" + std::to_string(test_support::ProcessId()));
    std::error_code ignored;
    fs::remove_all(root, ignored);
    Bytes ogg;
    const std::wstring game = BuildGame(root / L"game \u00e9", ogg).wstring();
    TestSyntheticIcons(game);
    TestSyntheticIconBatch(game, root);
    TestSyntheticNames(game);
    TestSyntheticSounds(game, ogg);
    TestRealGame();
    fs::remove_all(root, ignored);
    checks += g_checks;
    failures += g_failures;
}
