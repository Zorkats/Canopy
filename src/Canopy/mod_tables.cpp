#define NOMINMAX
#include "mod_tables.h"

#include <windows.h>

#include <algorithm>
#include <utility>

#include "mod_art.h"
#include "mod_flags.h"
#include "mod_items.h"
#include "mod_objects.h"
#include "mod_villagers.h"
#include "mod_whistles.h"
#include "save_data.h"
#include "save_overlay.h"
#include "table_engine.h"

namespace mod_tables {
namespace {

using namespace core;

using MapFindFn = void*(__fastcall*)(void* map, uint8_t** node, const uint64_t* key);
using DatabaseFn = uint8_t*(__fastcall*)(void* registry);

constexpr size_t kBlobHeader = 12;
constexpr uint32_t kMaxRows = 100000;
constexpr uint32_t kMaxBlobPart = 256u * 1024 * 1024;
constexpr size_t kMaxIdentifier = 64;
// The lowest id Canopy gives out, the mod item range's first.
constexpr uint64_t kFirstModId = 1500000000;
constexpr uint32_t kEnumWaitMs = 5000;

std::string DataPath(const std::string& name) {
    return ModFilePath((std::string("mods\\CanopyData\\") + name).c_str());
}

uint32_t Crc(const char* text, size_t length) {
    static uint32_t table[256] = {};
    static bool built = false;
    if (!built) {
        for (uint32_t index = 0; index < 256; ++index) {
            uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value >> 1) ^ ((value & 1) ? 0xEDB88320u : 0u);
            }
            table[index] = value;
        }
        built = true;
    }
    // The game's enum name hash: seeded with the length, no inversions.
    uint32_t crc = static_cast<uint32_t>(length);
    for (size_t index = 0; index < length; ++index) {
        crc = table[(crc ^ static_cast<uint8_t>(text[index])) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

struct Registry {
    uint64_t next = 1;
    std::vector<std::pair<std::string, uint64_t>> ids;
};

bool ReadRegistry(const char* file, uint64_t limit, Registry& registry) {
    const std::string path = DataPath(file);
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return true;
    }
    std::string text;
    if (!save_data::ReadFileText(path, text)) {
        return false;
    }
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = canopy_ini::Trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || equals == std::string::npos) {
            continue;
        }
        const std::string key = canopy_ini::Trim(line.substr(0, equals));
        uint64_t number = 0;
        if (!ParseU64(canopy_ini::Trim(line.substr(equals + 1)), number)) {
            continue;
        }
        if (_stricmp(key.c_str(), "next") == 0) {
            registry.next = std::max(registry.next, number);
        } else if (number > 0 && number < limit) {
            registry.ids.emplace_back(key, number);
            registry.next = std::max(registry.next, number + 1);
        }
    }
    return true;
}

bool WriteRegistry(const char* file, const char* header, const char* label, const Registry& registry) {
    std::string text = std::string(header) + "\r\n[ids]\r\nnext = " + std::to_string(registry.next) + "\r\n";
    for (const auto& [key, number] : registry.ids) {
        text += key + " = " + std::to_string(number) + "\r\n";
    }
    CreateDirectoryA(ModFilePath("mods\\CanopyData").c_str(), nullptr);
    if (const uint32_t error = save_data::WriteFileAtomically(DataPath(file), text)) {
        Log(std::string(label) + ": " + file + " could not be written, error " + std::to_string(error) + ".");
        return false;
    }
    return true;
}

}  // namespace

// ---- References ----------------------------------------------------------

namespace {

class InstalledRefs final : public mod_rules::Refs {
public:
    uint64_t Find(mod_rules::RefKind kind, const std::string& fullKey) const override {
        switch (kind) {
            case mod_rules::RefKind::Flag:
                return static_cast<uint64_t>(std::max<int32_t>(mod_flags::IdOf(fullKey), 0));
            case mod_rules::RefKind::Item:
                return mod_items::IdOf(fullKey);
            case mod_rules::RefKind::Icon:
                return mod_art::IconId(std::string(), fullKey);
            case mod_rules::RefKind::Texture:
                return mod_art::TextureId(std::string(), fullKey);
            case mod_rules::RefKind::Skin:
                return mod_art::SkinId(std::string(), fullKey);
            case mod_rules::RefKind::Object:
                return mod_objects::IdOf(fullKey);
            case mod_rules::RefKind::Villager:
                return mod_villagers::IdOf(fullKey);
            case mod_rules::RefKind::Whistle:
                return mod_whistles::IdOf(fullKey);
            default:
                return 0;
        }
    }
};

}  // namespace

int32_t FlagId(const std::string& modKey, const std::string& ref) {
    return mod_rules::FlagRef(LoaderRefs(), modKey, ref);
}

uint64_t ItemId(const std::string& modKey, const std::string& ref) {
    return mod_rules::ItemRef(LoaderRefs(), modKey, ref);
}

const mod_rules::Refs& LoaderRefs() {
    static const InstalledRefs refs;
    return refs;
}

mod_rules::Report LogReport() {
    return mod_rules::Report([](const mod_rules::Diagnostic& diagnostic) { Log(diagnostic.message); });
}

// ---- Mod folders ---------------------------------------------------------

std::vector<Folder> FindFolders(const std::vector<const char*>& prefixes) {
    return mod_rules::WithSections(mod_rules::ScanMods(ModFilePath("mods")), prefixes);
}

// ---- Id registries -------------------------------------------------------

bool AssignNumbers(
    const char* file, const char* header, const char* label, uint64_t limit, const std::vector<std::string>& keys,
    std::vector<uint64_t>& numbers
) {
    numbers.assign(keys.size(), 0);
    Registry registry;
    if (!ReadRegistry(file, limit, registry)) {
        Log(std::string(label) + ": mods\\CanopyData\\" + file + " could not be read, so no ids are given out.");
        return false;
    }
    bool changed = false;
    for (size_t index = 0; index < keys.size(); ++index) {
        const auto found = std::find_if(registry.ids.begin(), registry.ids.end(), [&](const auto& entry) {
            return _stricmp(entry.first.c_str(), keys[index].c_str()) == 0;
        });
        if (found != registry.ids.end()) {
            numbers[index] = found->second;
            continue;
        }
        if (registry.next >= limit) {
            Log(std::string(label) + ": the id range is used up, so " + keys[index] + " gets none.");
            continue;
        }
        numbers[index] = registry.next++;
        registry.ids.emplace_back(keys[index], numbers[index]);
        changed = true;
    }
    return !changed || WriteRegistry(file, header, label, registry);
}

// ---- Table blobs ---------------------------------------------------------

void TextPool::Write(std::vector<uint8_t>& row, size_t offset, const std::string& text) {
    uint32_t position = 0;
    if (!text.empty()) {
        position = static_cast<uint32_t>(bytes_.size());
        bytes_ += text;
        bytes_ += '\0';
    }
    WriteAt<uint32_t>(row.data(), offset, base_ + position);
    WriteAt<uint32_t>(row.data(), offset + 4, static_cast<uint32_t>(text.size()));
}

std::string Appender::UniqueEnum(const std::string& stem) {
    std::string name = stem;
    for (int suffix = 2; table_.enums.count(name) || enums_.count(name) || table_.enumCrcs.count(Crc(name.data(), name.size())) ||
                         crcs_.count(Crc(name.data(), name.size()));
         ++suffix) {
        name = stem + "_" + std::to_string(suffix);
    }
    enums_.insert(name);
    crcs_.insert(Crc(name.data(), name.size()));
    return name;
}

void Appender::Add(const std::vector<uint8_t>& row) {
    const uint32_t size = static_cast<uint32_t>(row.size());
    rows_.insert(rows_.end(), reinterpret_cast<const uint8_t*>(&size), reinterpret_cast<const uint8_t*>(&size) + 4);
    rows_.insert(rows_.end(), row.begin(), row.end());
    ++count_;
}

void Appender::Merge(const uint8_t* blob, std::vector<uint8_t>& merged) const {
    const std::string& pool = pool_.Bytes();
    merged.reserve(kBlobHeader + table_.rowsSize + rows_.size() + table_.poolBytes + pool.size());
    merged.resize(kBlobHeader);
    WriteAt<uint32_t>(merged.data(), 0, table_.count + static_cast<uint32_t>(count_));
    WriteAt<uint32_t>(merged.data(), 4, table_.rowBytes + static_cast<uint32_t>(rows_.size() - 4 * count_));
    WriteAt<uint32_t>(merged.data(), 8, table_.poolBytes + static_cast<uint32_t>(pool.size()));
    merged.insert(merged.end(), blob + kBlobHeader, blob + kBlobHeader + table_.rowsSize);
    merged.insert(merged.end(), rows_.begin(), rows_.end());
    merged.insert(merged.end(), table_.pool, table_.pool + table_.poolBytes);
    merged.insert(merged.end(), pool.begin(), pool.end());
}

bool ParseTable(const uint8_t* blob, size_t minRow, size_t enumOffset, Table& table) {
    table.count = ReadAt<uint32_t>(blob, 0);
    table.rowBytes = ReadAt<uint32_t>(blob, 4);
    table.poolBytes = ReadAt<uint32_t>(blob, 8);
    if (table.count == 0 || table.count > kMaxRows || table.rowBytes > kMaxBlobPart || table.poolBytes > kMaxBlobPart) {
        return false;
    }
    table.rowsSize = 4 * static_cast<size_t>(table.count) + table.rowBytes;
    table.pool = blob + kBlobHeader + table.rowsSize;
    size_t offset = kBlobHeader;
    for (uint32_t index = 0; index < table.count; ++index) {
        const uint32_t size = ReadAt<uint32_t>(blob, offset);
        if (offset + 4 + size > kBlobHeader + table.rowsSize || size < minRow) {
            return false;
        }
        const Row row{blob + offset + 4, size};
        const uint64_t id = ReadAt<uint64_t>(row.data, 0);
        table.rows.emplace(id, row);
        table.order.push_back(id);
        const uint32_t enumStart = ReadAt<uint32_t>(row.data, enumOffset);
        const uint32_t enumLength = ReadAt<uint32_t>(row.data, enumOffset + 4);
        if (static_cast<uint64_t>(enumStart) + enumLength <= table.poolBytes) {
            const char* text = reinterpret_cast<const char*>(table.pool + enumStart);
            table.enumCrcs.insert(Crc(text, enumLength));
            table.enums.emplace(text, enumLength);
        }
        offset += 4 + size;
    }
    return offset == kBlobHeader + table.rowsSize;
}

std::string PoolText(const Table& table, const uint8_t* row, size_t offset) {
    const uint32_t start = ReadAt<uint32_t>(row, offset);
    const uint32_t length = ReadAt<uint32_t>(row, offset + 4);
    if (static_cast<uint64_t>(start) + length > table.poolBytes) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(table.pool + start), length);
}

bool IsTable(const char* name, const char* file) {
    if (!name) {
        return false;
    }
    const char* part = name;
    for (const char* c = name; *c; ++c) {
        if (*c == '/' || *c == '\\') {
            part = c + 1;
        }
    }
    return _stricmp(part, file) == 0;
}

bool DatabaseHas(uint8_t* db, uint64_t id) {
    uint8_t* map = db + retail::kDatabaseMap;
    return ReadAt<size_t>(map, retail::kMapSize) > 0 && save_overlay::MapFind(map, id);
}

// ---- Tables as they load -------------------------------------------------

bool LoadingTables::EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const {
    if (!table_engine::WaitKnown(file, kEnumWaitMs)) {
        why = std::string(file) + " did not load in time to look up " + name;
        return false;
    }
    if (!table_engine::EnumId(file, name, id)) {
        why = std::string(file) + " has no " + name;
        return false;
    }
    return true;
}

bool LoadingTables::VanillaFine(const char* file, uint64_t id, std::string& why) const {
    const int own = table_engine::Batch(own_);
    const int other = table_engine::Batch(file);
    if (_stricmp(file, "gameflag.dat") == 0 || other == 0 || other >= own || !table_engine::Known(file) ||
        table_engine::HasId(file, id)) {
        return true;
    }
    const std::string name = file;
    why = "it names " + name.substr(0, name.size() > 4 ? name.size() - 4 : name.size()) + " " + std::to_string(id) +
          ", which is not in " + file;
    return false;
}

// ---- Databases -----------------------------------------------------------

uint8_t** FindHolder(uint8_t* database, uint64_t id) {
    if (!database) {
        return nullptr;
    }
    uint8_t* node = nullptr;
    GameFunction<MapFindFn>(retail::kInt64MapFind)(database + retail::kDatabaseMap, &node, &id);
    if (!node || node == ReadAt<uint8_t*>(database, retail::kDatabaseMap + retail::kMapListHead)) {
        return nullptr;
    }
    return reinterpret_cast<uint8_t**>(node + retail::kMapNodeValue);
}

uint8_t* FindRecord(uint8_t* database, uint64_t id) {
    uint8_t** holder = FindHolder(database, id);
    return holder ? *holder : nullptr;
}

uint8_t* ItemDatabase() {
    void* registry = ReadGlobal<void*>(retail::kDatabaseRegistry);
    return registry ? GameFunction<DatabaseFn>(retail::kItemDatabase)(registry) : nullptr;
}

}  // namespace mod_tables
