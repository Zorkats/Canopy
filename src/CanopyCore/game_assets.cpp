#include "game_assets.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include "archive.h"
#include "blob.h"
#include "canopy_ini.h"
#include "game_browse.h"
#include "image_io.h"
#include "json_out.h"
#include "rules_items.h"
#include "rules_objects.h"
#include "rules_report.h"
#include "table_schema.h"

// =============================================================
// GAME ASSETS
// =============================================================
// What a picker or grid shows of a vanilla thing, read the way the game
// reads it. Icons follow the game's chain: icon.dat row, its textureID,
// texture.dat's file for the language, the .nltx, and the row's drawInfo
// rect. Names are the per-language texts of each table's own rows; a recipe
// is shown as its output item, as the craft and cooking menus do. Sounds
// are the Ogg Vorbis stream an .nlsd wraps. Offsets come from the loader's
// own row builders (mod_items, mod_villagers, mod_whistles, mod_recipes)
// and the table catalog in docs, checked against every shipped row.

namespace game_assets {
namespace {

using json_out::PutKey;
using json_out::PutOptional;
using json_out::PutString;

constexpr const char* kDatabaseFolder = "data/database/";
constexpr int kEnglish = 1;

// icon.dat: the texture id, then one 32-byte drawInfo per language whose
// first four floats are the cell's x, y, w and h in texture pixels.
constexpr size_t kIconTexture = 24;
constexpr size_t kIconDrawInfo = 32;
constexpr size_t kDrawInfoSize = 32;
// texture.dat: the file the game opens, one text per language.
constexpr size_t kTextureFile = 24;

// item.dat: the name per language, the category, and the icon 52 bytes
// before the row's end. Every row is at least kItemMinRow bytes.
constexpr size_t kItemNames = 32;
constexpr size_t kItemCategory = 320;
constexpr size_t kItemMinRow = 536;
constexpr size_t kItemTailIcon = 52;
constexpr size_t kCharaNames = 24;
constexpr size_t kWhistleNames = 16;
constexpr size_t kWhistleScore = 80;
constexpr size_t kCraftOutput = 92;
constexpr size_t kCookingOutput = 24;
constexpr size_t kGimmickNames = 24;
constexpr size_t kEnumOffset = 8;

// An .nlsd's header and codecs, and an Ogg page's fixed header.
constexpr size_t kNlsdHeader = 24;
constexpr uint32_t kCodecWave = 5;
constexpr size_t kOggPageHeader = 27;
constexpr uint8_t kOggFirstPage = 2;

const char* const kKinds[] = {"item", "villager", "whistle", "craft", "cooking", "gimmick", "area"};

std::string Number(uint64_t value) {
    return std::to_string(value);
}

// why, as read inside a sentence: without its closing period.
std::string Clause(std::string why) {
    while (!why.empty() && why.back() == '.') {
        why.pop_back();
    }
    return why;
}

std::string Real(float value) {
    char text[32] = {};
    std::snprintf(text, sizeof(text), "%g", static_cast<double>(value));
    return text;
}

// ---- Tables --------------------------------------------------------------

struct LoadedTable {
    std::vector<uint8_t> bytes;
    blob::Table table;
};

// The archives of one install and the tables read from them so far.
class GameTables {
public:
    bool Open(const std::wstring& gameFolder, std::string& error) {
        if (gameFolder.empty()) {
            error = "No game folder was given.";
            return false;
        }
        return data_.Open(gameFolder, error);
    }

    // data/database/<file>, parsed once. Null, with error set, when it
    // cannot be read.
    const blob::Table* Table(const std::string& file, std::string& error) {
        const auto found = tables_.find(file);
        if (found != tables_.end()) {
            return &found->second->table;
        }
        auto loaded = std::make_unique<LoadedTable>();
        const std::string path = kDatabaseFolder + file;
        std::string why;
        if (!data_.FindEntry(path)) {
            error = "The game's archives have no " + path + ".";
            return nullptr;
        }
        if (!data_.Read(path, loaded->bytes, why)) {
            error = path + " could not be read (" + Clause(why) + ").";
            return nullptr;
        }
        if (!blob::Parse(loaded->bytes.data(), loaded->bytes.size(), loaded->table, why)) {
            error = path + " is not a table (" + Clause(why) + ").";
            return nullptr;
        }
        const blob::Table* table = &loaded->table;
        tables_.emplace(file, std::move(loaded));
        return table;
    }

private:
    archive::GameData data_;
    std::map<std::string, std::unique_ptr<LoadedTable>> tables_;
};

const blob::Row* FindRow(const blob::Table& table, uint64_t id) {
    for (const blob::Row& row : table.rows) {
        if (blob::RowId(row) == id) {
            return &row;
        }
    }
    return nullptr;
}

// The text whose ref is at offset, empty when the row or pool cannot hold it.
std::string TextAt(const blob::Table& table, const blob::Row& row, size_t offset) {
    std::string text;
    blob::ReadText(table, row, offset, text);
    return text;
}

// ---- Icons ---------------------------------------------------------------

// A cell edge in whole pixels, clamped to 0..limit. Not a number reads as 0.
uint32_t Edge(float value, uint32_t limit) {
    if (!(value > 0.0f)) {
        return 0;
    }
    if (value >= static_cast<float>(limit)) {
        return limit;
    }
    return static_cast<uint32_t>(std::lround(value));
}

// The rect x, y, w, h of texture, a textureWidth x textureHeight BGRA8
// image, clamped to it. False when nothing of it is left.
bool CutCell(
    const std::vector<uint8_t>& texture, uint32_t textureWidth, uint32_t textureHeight, const float (&rect)[4],
    uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels
) {
    const uint32_t left = Edge(rect[0], textureWidth);
    const uint32_t top = Edge(rect[1], textureHeight);
    const uint32_t right = Edge(rect[0] + rect[2], textureWidth);
    const uint32_t bottom = Edge(rect[1] + rect[3], textureHeight);
    if (right <= left || bottom <= top) {
        return false;
    }
    width = right - left;
    height = bottom - top;
    pixels.resize(static_cast<size_t>(width) * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const size_t from = (static_cast<size_t>(top + y) * textureWidth + left) * 4;
        std::memcpy(pixels.data() + static_cast<size_t>(y) * width * 4, texture.data() + from,
                    static_cast<size_t>(width) * 4);
    }
    return true;
}

// Where an icon.dat row's cell is: the texture the row names, that
// texture's file and the cell's rect, all taken in English.
struct CellPlace {
    uint64_t texture = 0;
    std::string path;
    float rect[4] = {};
};

std::string IconName(uint32_t iconId) {
    return "Icon " + Number(iconId);
}

std::string TextureName(uint32_t iconId, uint64_t texture) {
    return IconName(iconId) + " names texture " + Number(texture);
}

// The cell of icon iconId, or false with error set when the row or its
// texture's file cannot be found.
bool FindCell(GameTables& game, uint32_t iconId, CellPlace& place, std::string& error) {
    const blob::Table* icons = game.Table(table_schema::kIcon.file, error);
    if (!icons) {
        return false;
    }
    const std::string what = IconName(iconId);
    const blob::Row* icon = FindRow(*icons, iconId);
    if (!icon) {
        error = what + " is not in the game's icon table.";
        return false;
    }
    const size_t cell = kIconDrawInfo + kDrawInfoSize * kEnglish;
    if (icon->size < cell + 16) {
        error = what + " has a row of " + Number(icon->size) + " bytes, too short for its texture and cell.";
        return false;
    }
    place.texture = blob::Get<uint64_t>(icon->data, kIconTexture);
    for (size_t index = 0; index < 4; ++index) {
        place.rect[index] = blob::Get<float>(icon->data, cell + 4 * index);
    }
    const blob::Table* textures = game.Table(table_schema::kTexture.file, error);
    if (!textures) {
        return false;
    }
    const std::string texture = TextureName(iconId, place.texture);
    const blob::Row* row = FindRow(*textures, place.texture);
    if (!row) {
        error = texture + ", which is not in the game's texture table.";
        return false;
    }
    place.path = TextAt(*textures, *row, kTextureFile + blob::kTextSize * kEnglish);
    if (place.path.empty()) {
        error = texture + ", which has no file.";
        return false;
    }
    return true;
}

std::string TextureUnreadable(uint32_t iconId, const CellPlace& place, const std::string& why) {
    return TextureName(iconId, place.texture) + ", whose file " + place.path + " could not be read (" + Clause(why) +
           ").";
}

std::string EmptyCell(uint32_t iconId, const CellPlace& place, uint32_t textureWidth, uint32_t textureHeight) {
    return IconName(iconId) + " has an empty cell: its rect " + Real(place.rect[0]) + ", " + Real(place.rect[1]) +
           ", " + Real(place.rect[2]) + ", " + Real(place.rect[3]) + " leaves nothing of its " +
           Number(textureWidth) + "x" + Number(textureHeight) + " texture.";
}

// ---- Names ---------------------------------------------------------------

struct Entry {
    uint64_t id = 0;
    std::string enumName;
    std::string name;
    uint64_t icon = 0;
    bool hasIcon = false;
    uint64_t group = 0;
    bool hasGroup = false;
};

bool LanguageIndex(const std::string& code, int& language) {
    const std::string lower = canopy_ini::Lower(code);
    for (int index = 0; index < table_schema::kLanguageCount; ++index) {
        if (lower == table_schema::kLanguageKeys[index]) {
            language = index;
            return true;
        }
    }
    return false;
}

// Every row of table with its id, enum name and the name at nameOffset.
std::vector<Entry> Rows(const blob::Table& table, size_t nameOffset, int language) {
    std::vector<Entry> entries;
    entries.reserve(table.rows.size());
    for (const blob::Row& row : table.rows) {
        Entry entry;
        entry.id = blob::RowId(row);
        entry.enumName = TextAt(table, row, kEnumOffset);
        entry.name = TextAt(table, row, nameOffset + blob::kTextSize * static_cast<size_t>(language));
        entries.push_back(std::move(entry));
    }
    return entries;
}

void SetIcon(Entry& entry, uint64_t icon, const std::set<uint64_t>& icons) {
    if (icon != 0 && icons.count(icon)) {
        entry.icon = icon;
        entry.hasIcon = true;
    }
}

// An item row's name, icon and category into entry.
void ItemFields(
    const blob::Table& items, const blob::Row& row, int language, const std::set<uint64_t>& icons, Entry& entry
) {
    entry.name = TextAt(items, row, kItemNames + blob::kTextSize * static_cast<size_t>(language));
    if (row.size >= kItemMinRow) {
        SetIcon(entry, blob::Get<uint64_t>(row.data, row.size - kItemTailIcon), icons);
        entry.group = blob::Get<uint64_t>(row.data, kItemCategory);
        entry.hasGroup = true;
    }
}

// The kind's entries, or false with error set when a table cannot be read.
bool KindEntries(
    GameTables& game, const std::string& kind, int language, std::vector<Entry>& entries, std::string& error
) {
    const blob::Table* iconTable = game.Table(table_schema::kIcon.file, error);
    if (!iconTable) {
        return false;
    }
    std::set<uint64_t> icons;
    for (const blob::Row& row : iconTable->rows) {
        icons.insert(blob::RowId(row));
    }
    if (kind == "villager" || kind == "whistle") {
        const bool villager = kind == "villager";
        const blob::Table* table = game.Table(villager ? "character.dat" : "whistle.dat", error);
        if (!table) {
            return false;
        }
        entries = Rows(*table, villager ? kCharaNames : kWhistleNames, language);
        for (size_t index = 0; !villager && index < entries.size(); ++index) {
            const blob::Row& row = table->rows[index];
            if (row.size >= kWhistleScore + 8) {
                SetIcon(entries[index], blob::Get<uint64_t>(row.data, kWhistleScore), icons);
            }
        }
        return true;
    }
    const blob::Table* items = game.Table("item.dat", error);
    if (!items) {
        return false;
    }
    std::unordered_map<uint64_t, const blob::Row*> byId;
    for (const blob::Row& row : items->rows) {
        byId.emplace(blob::RowId(row), &row);
    }
    if (kind == "gimmick") {
        // Furniture only: a row its own item places and gets back. The icon
        // and group are that item's, and so is the name when the row has none.
        const blob::Table* table = game.Table("gimmick.dat", error);
        if (!table) {
            return false;
        }
        std::vector<Entry> rows = Rows(*table, kGimmickNames, language);
        for (size_t index = 0; index < rows.size(); ++index) {
            const auto item = byId.find(mod_rules::GimmickItem(table->rows[index]));
            if (item == byId.end() || !mod_rules::PlacesGimmick(*item->second, rows[index].id)) {
                continue;
            }
            Entry& entry = rows[index];
            const std::string name = std::move(entry.name);
            ItemFields(*items, *item->second, language, icons, entry);
            if (!name.empty()) {
                entry.name = name;
            }
            entries.push_back(std::move(entry));
        }
        return true;
    }
    if (kind == "item") {
        entries = Rows(*items, kItemNames, language);
        for (size_t index = 0; index < entries.size(); ++index) {
            ItemFields(*items, items->rows[index], language, icons, entries[index]);
        }
        return true;
    }
    // A recipe is listed as its output item.
    const bool craft = kind == "craft";
    const blob::Table* table = game.Table(craft ? "craft.dat" : "cooking.dat", error);
    if (!table) {
        return false;
    }
    const size_t output = craft ? kCraftOutput : kCookingOutput;
    entries = Rows(*table, 0, 0);
    for (size_t index = 0; index < entries.size(); ++index) {
        Entry& entry = entries[index];
        entry.name.clear();
        const blob::Row& row = table->rows[index];
        if (row.size < output + 8) {
            continue;
        }
        const auto item = byId.find(blob::Get<uint64_t>(row.data, output));
        if (item != byId.end()) {
            ItemFields(*items, *item->second, language, icons, entry);
        }
    }
    return true;
}

void PutEntries(std::string& out, const std::vector<Entry>& entries) {
    out += '[';
    bool first = true;
    for (const Entry& entry : entries) {
        out += first ? "{" : ",{";
        first = false;
        PutKey(out, "id");
        out += Number(entry.id) + ",";
        PutKey(out, "enum");
        PutOptional(out, entry.enumName.empty() ? nullptr : entry.enumName.c_str());
        out += ',';
        PutKey(out, "name");
        PutString(out, entry.name);
        out += ',';
        PutKey(out, "icon");
        out += (entry.hasIcon ? Number(entry.icon) : std::string("null")) + ",";
        PutKey(out, "group");
        out += (entry.hasGroup ? Number(entry.group) : std::string("null")) + "}";
    }
    out += ']';
}

// ---- Sounds --------------------------------------------------------------

// The size of the Ogg page at data[at], or 0 when no whole page starts there.
size_t OggPageSize(const uint8_t* data, size_t size, size_t at) {
    if (size - at < kOggPageHeader || std::memcmp(data + at, "OggS", 4) != 0 || data[at + 4] != 0) {
        return 0;
    }
    const size_t segments = data[at + 26];
    if (size - at < kOggPageHeader + segments) {
        return 0;
    }
    size_t page = kOggPageHeader + segments;
    for (size_t index = 0; index < segments; ++index) {
        page += data[at + kOggPageHeader + index];
    }
    return page <= size - at ? page : 0;
}

// The Ogg Vorbis stream in an .nlsd's bytes, from its first page to the end.
bool OggOf(const std::string& gamePath, const std::vector<uint8_t>& file, std::vector<uint8_t>& ogg, std::string& error) {
    const uint8_t* data = file.data();
    const size_t size = file.size();
    if (size >= kNlsdHeader + 4 && blob::Get<uint32_t>(data, 0) == kCodecWave &&
        std::memcmp(data + kNlsdHeader, "RIFF", 4) == 0) {
        error = gamePath + " holds a RIFF WAVE sound, not an Ogg Vorbis stream.";
        return false;
    }
    size_t start = 0;
    while (start + 4 <= size && std::memcmp(data + start, "OggS", 4) != 0) {
        ++start;
    }
    if (start + 4 > size) {
        error = gamePath + " holds no Ogg stream.";
        return false;
    }
    // The first page opens a Vorbis stream: its first packet is the
    // identification header.
    const size_t first = OggPageSize(data, size, start);
    const size_t body = first ? start + kOggPageHeader + data[start + 26] : 0;
    if (!first || (data[start + 5] & kOggFirstPage) == 0 || start + first - body < 7 ||
        std::memcmp(data + body, "\x01vorbis", 7) != 0) {
        error = gamePath + " has an Ogg page at byte " + Number(start) + " that does not start a Vorbis stream.";
        return false;
    }
    size_t at = start;
    while (at < size) {
        const size_t page = OggPageSize(data, size, at);
        if (!page) {
            error = gamePath + " is not a whole chain of Ogg pages: byte " + Number(at) + " of " + Number(size) +
                    " does not start one that fits.";
            return false;
        }
        at += page;
    }
    ogg.assign(file.begin() + static_cast<std::ptrdiff_t>(start), file.end());
    return true;
}

}  // namespace

bool Icon(
    const std::wstring& gameFolder,
    uint32_t iconId,
    uint32_t& width,
    uint32_t& height,
    std::vector<uint8_t>& pixels,
    std::string& error
) {
    width = 0;
    height = 0;
    pixels.clear();
    GameTables game;
    if (!game.Open(gameFolder, error)) {
        return false;
    }
    CellPlace place;
    if (!FindCell(game, iconId, place, error)) {
        return false;
    }
    std::vector<uint8_t> image;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    std::string why;
    if (!game_browse::Texture(gameFolder, place.path, textureWidth, textureHeight, image, why)) {
        error = TextureUnreadable(iconId, place, why);
        return false;
    }
    if (!CutCell(image, textureWidth, textureHeight, place.rect, width, height, pixels)) {
        error = EmptyCell(iconId, place, textureWidth, textureHeight);
        return false;
    }
    return true;
}

std::string IconsJson(const std::wstring& gameFolder, const std::vector<uint32_t>& ids, const std::wstring& outFolder) {
    GameTables game;
    std::string error;
    if (!game.Open(gameFolder, error) || !game.Table(table_schema::kIcon.file, error) ||
        !game.Table(table_schema::kTexture.file, error)) {
        return game_browse::FailureJson(error);
    }
    if (outFolder.empty()) {
        return game_browse::FailureJson("No folder for the icons was given.");
    }
    const std::filesystem::path out(outFolder);
    std::error_code failed;
    std::filesystem::create_directories(out, failed);
    if (!std::filesystem::is_directory(out, failed)) {
        return game_browse::FailureJson("The folder for the icons could not be made.");
    }

    // The cells grouped by texture file, so each file is decoded once.
    std::map<std::string, std::vector<std::pair<uint32_t, CellPlace>>> byFile;
    std::vector<std::string> problems;
    std::set<uint32_t> seen;
    for (const uint32_t id : ids) {
        if (!seen.insert(id).second) {
            continue;
        }
        CellPlace place;
        std::string why;
        if (FindCell(game, id, place, why)) {
            byFile[place.path].emplace_back(id, place);
        } else {
            problems.push_back(why);
        }
    }
    size_t written = 0;
    for (const auto& file : byFile) {
        std::vector<uint8_t> image;
        uint32_t textureWidth = 0;
        uint32_t textureHeight = 0;
        std::string why;
        const bool decoded = game_browse::Texture(gameFolder, file.first, textureWidth, textureHeight, image, why);
        for (const auto& icon : file.second) {
            if (!decoded) {
                problems.push_back(TextureUnreadable(icon.first, icon.second, why));
                continue;
            }
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<uint8_t> pixels;
            if (!CutCell(image, textureWidth, textureHeight, icon.second.rect, width, height, pixels)) {
                problems.push_back(EmptyCell(icon.first, icon.second, textureWidth, textureHeight));
                continue;
            }
            std::string saveError;
            const std::wstring target = (out / (std::to_wstring(icon.first) + L".png")).wstring();
            if (!image_io::SavePng(target, width, height, pixels, saveError)) {
                problems.push_back(IconName(icon.first) + " could not be saved (" + Clause(saveError) + ").");
                continue;
            }
            ++written;
        }
    }

    std::string json = "{";
    PutKey(json, "abi");
    json += std::to_string(mod_rules::kJsonAbi) + ",";
    PutKey(json, "ok");
    json += "true,";
    PutKey(json, "error");
    json += "null,";
    PutKey(json, "written");
    json += Number(written) + ",";
    PutKey(json, "problems");
    json += '[';
    for (size_t index = 0; index < problems.size(); ++index) {
        if (index > 0) {
            json += ',';
        }
        PutString(json, problems[index]);
    }
    json += "]}";
    return json;
}

std::string NamesJson(const std::wstring& gameFolder, const std::string& kindName, const std::string& languageCode) {
    int language = 0;
    if (!LanguageIndex(languageCode, language)) {
        return game_browse::FailureJson(
            "\"" + languageCode + "\" is not a language code; use ja, en, fr, es, zh-tw, ko or zh-cn."
        );
    }
    const std::string kind = canopy_ini::Lower(kindName);
    bool known = false;
    for (const char* name : kKinds) {
        known = known || kind == name;
    }
    if (!known) {
        return game_browse::FailureJson(
            "\"" + kindName + "\" is not a kind of name; use item, villager, whistle, craft, cooking, gimmick or area."
        );
    }
    if (kind == "area") {
        return game_browse::FailureJson(
            "Area names cannot be listed: the game's area table, map.dat, holds no name per language, only a "
            "one-language comment."
        );
    }
    GameTables game;
    std::string error;
    std::vector<Entry> entries;
    if (!game.Open(gameFolder, error) || !KindEntries(game, kind, language, entries, error)) {
        return game_browse::FailureJson(error);
    }
    std::string out = "{";
    PutKey(out, "abi");
    out += std::to_string(mod_rules::kJsonAbi) + ",";
    PutKey(out, "ok");
    out += "true,";
    PutKey(out, "error");
    out += "null,";
    PutKey(out, "kind");
    PutString(out, kind);
    out += ',';
    PutKey(out, "language");
    PutString(out, table_schema::kLanguageKeys[language]);
    out += ',';
    PutKey(out, "entries");
    PutEntries(out, entries);
    out += '}';
    return out;
}

bool Sound(
    const std::wstring& gameFolder,
    const std::string& gamePath,
    std::vector<uint8_t>& ogg,
    std::string& error
) {
    ogg.clear();
    if (gameFolder.empty()) {
        error = "No game folder was given.";
        return false;
    }
    std::vector<uint8_t> file;
    std::string why;
    if (!game_browse::File(gameFolder, gamePath, file, why)) {
        error = gamePath + " could not be read (" + Clause(why) + ").";
        return false;
    }
    return OggOf(gamePath, file, ogg, error);
}

}  // namespace game_assets
