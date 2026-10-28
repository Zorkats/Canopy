#define NOMINMAX
#include "mod_villagers.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "hook_chain.h"
#include "mod_config.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_villagers.h"
#include "save_data.h"
#include "save_overlay.h"

// =============================================================
// CUSTOM VILLAGERS DESIGN
// =============================================================
// A mod defines villagers in its canopy.ini, one [villager.Key] section
// each, as a copy of a vanilla villager with its own name, job, help, place
// and, optionally, its own text Lua AI module. A folder with villagers and
// no Lua or DLL is still registered as a mod. With no villagers anywhere,
// nothing below is hooked.
//
// Rules. What a [villager.Key] section accepts is CanopyCore's
// rules_villagers, shared with the desktop tools; this file installs what
// they read.
//
// Ids. Each villager gets 1600000000 + n, where n comes from
// mods\CanopyData\villagers.ini, given out once and never reused, like the
// item ids. The range sits above the item range and inside int32.
//
// Tables. CDatabase<CharaData>::load 0x52870 and
// CDatabase<CharaScheduleData>::load 0x55960 take (db, name, blob) like the
// item table's load. Each detour builds one blob with the vanilla rows,
// then the mod rows, and calls the original once with it. A character row
// is the base villager's row with a new id, enum name, name, job and help,
// the mod's script module when it has one, its own place, and no story
// hooks: love events, love flags, request, detail release and fair
// attendance flags are 0, as on vanilla villagers that have none. Its
// look, gifts and birthday stay the base's. A schedule row has one task
// with no conditions that runs the routine entry (none by default) of the
// villager's module. The new game reset 0x158920 makes one CNpcStatus for
// every character row, so a new game holds the mod villagers with no more
// work. At the title the tick looks each id up in both tables; a villager
// that fails is set to start disabled.
//
// Scripts. The game's script manager only knows data/script, so the module
// loader 0x73BA90 is detoured, chained after the Lua host's own detour
// (which lets these names through, see OwnsModule). A villager module is
// villager_<mod>_<key>; its text is compiled from the mod folder in text
// mode and called with its module name, like require does. A module that
// does not compile or returns no table falls back to its base villager's
// module, so the villager still behaves. Villager scripts are game scripts:
// they run with every game command, outside the Lua host's sandbox.
//
// Save. Villagers are one adapter of the overlay save core. The strip
// unlinks every mod villager's node from the save data's villager list,
// so the written save never holds one; the restore links it back. The
// sidecar mods\CanopyData\slot<N>.villagers.ini keeps each villager's
// love and enabled state. A loaded save holds no mod villager, so after
// the load each installed villager is created exactly the way the reset
// does it, at its starting place, and its sidecar state applied. Lines for
// villagers that are not installed are kept and written back. Continue
// mode reads back a snapshot taken before any strip, so it only needs the
// villagers it lacks created.
//
// Threads. The table detours run on the boot worker and only touch the
// blobs, the villager definitions and the log. Everything else runs on the
// main thread.

namespace mod_villagers {
namespace {

using namespace core;
using mod_tables::EnumPart;
using mod_tables::ParseI32;
using mod_tables::ParseU64;
using mod_tables::Plural;

using DatabaseLoadFn = int64_t(__fastcall*)(uint8_t* db, const char* name, const uint8_t* blob);
using MapFindFn = void*(__fastcall*)(void* map, uint8_t** node, const uint64_t* key);
using MapObjectStatusCtorFn = void*(__fastcall*)(uint8_t* status, uint8_t* saveData, uint8_t** unitHolder);
using MemberInitFn = void*(__fastcall*)(uint8_t* member);
using AreaOriginFn = void*(__fastcall*)(float* origin, uint8_t* unitHolder, uint64_t areaId);
using SetPositionFn = void(__fastcall*)(uint8_t* status, const float* position, int32_t mode);
using StatusInitFn = void(__fastcall*)(uint8_t* status);

struct lua_State;
using LuaCFunction = int (*)(lua_State* state);
using LuaSetTopFn = void(__fastcall*)(lua_State* state, int index);
using LuaTypeFn = int(__fastcall*)(lua_State* state, int index);
using LuaToLStringFn = const char*(__fastcall*)(lua_State* state, int index, size_t* length);
using LuaPushStringFn = const char*(__fastcall*)(lua_State* state, const char* text);
using LuaPCallKFn = int(__fastcall*)(
    lua_State* state, int argumentCount, int resultCount, int handler, intptr_t context, void* continuation
);
using LuaLoadBufferXFn = int(__fastcall*)(
    lua_State* state, const char* buffer, size_t size, const char* name, const char* mode
);

// ---- Table row formats ---------------------------------------------------
// Blobs as in mod_items.cpp: u32 row count, u32 row bytes, u32 pool bytes,
// each row as a u32 length and its bytes, then the string pool. A text is a
// u32 pool offset and a u32 length.

constexpr size_t kBlobHeader = 12;
constexpr size_t kTextRef = 8;
constexpr uint32_t kMaxRows = 100000;
constexpr uint32_t kMaxBlobPart = 256u * 1024 * 1024;

// character.dat: 732 bytes plus 8 per forbidden present feature.
constexpr size_t kCharaRowId = 0;
constexpr size_t kCharaRowEnum = 8;
constexpr size_t kCharaRowNames = 24;
constexpr size_t kCharaRowJobs = 80;
constexpr size_t kCharaRowScript = 160;
constexpr size_t kCharaRowArea = 192;
constexpr size_t kCharaRowHelps = 280;
constexpr size_t kHelpBlocks = 3;
constexpr size_t kCharaRowInitEnabled = 448;
constexpr size_t kCharaRowInitX = 452;
constexpr size_t kCharaRowInitY = 456;
constexpr size_t kCharaRowLoveEvents = 468;
constexpr size_t kCharaRowLoveFlags = 516;
constexpr size_t kLoveFlagCount = 6;
constexpr size_t kCharaRowRequestFlag = 624;
constexpr size_t kCharaRowForbidCount = 640;
constexpr size_t kCharaRowForbid = 644;
// After the forbidden features: detail release flag, fair attendance flag
// and the request message texts.
constexpr size_t kCharaTailDetail = 0;
constexpr size_t kCharaTailFair = 8;
constexpr size_t kCharaTailRequest = 16;
constexpr size_t kCharaRowBase = 732;

// characterschedule.dat: id, comment, task count, the tasks, then a 16-byte
// trailer. A task: name, on flags (u32 count, u64s), off flags, passed day
// (16 bytes), seasons (u32 count, i32s), day, weekdays (u32 count, i32s),
// weathers (u32 count, u64s), time, on areas, off areas (u32 count, u64s),
// then module, entry and argument.
constexpr size_t kScheduleRowId = 0;
constexpr size_t kScheduleRowComment = 8;
constexpr size_t kScheduleRowTaskCount = 16;
constexpr size_t kScheduleRowTasks = 20;
constexpr size_t kScheduleTrailer = 16;
constexpr size_t kScheduleMinRow = kScheduleRowTasks + kScheduleTrailer;
constexpr size_t kTaskPassedDay = 16;

constexpr const char* kCharaTableFile = "character.dat";
constexpr const char* kScheduleTableFile = "characterschedule.dat";
constexpr uint64_t kBandBase = 1600000000;
constexpr uint64_t kBandSize = 100000000;
constexpr size_t kMaxSidecarLines = 4096;
constexpr int32_t kLoveMax = 2100;
constexpr const char* kTaskName = "canopy";
constexpr const char* kModulePrefix = "villager_";
// Between mod flags (40) and mod items (100).
constexpr int kOverlayOrder = 60;
// The loader's chain owner. DetourSet's hooks are the loader's own, owner
// -1, and the Lua host already holds that owner on the module loader.
constexpr int64_t kLoaderChainOwner = -2;
constexpr int kTypeString = 4;
constexpr int kTypeTable = 5;
constexpr const char* kRegistryName = "villagers.ini";

// What the rules read, and what installing it adds.
struct VillagerDef : mod_rules::VillagerSpec {
    uint64_t id = 0;
    std::string enumName;
    // The base villager's module, set by the character table detour.
    std::string baseModule;
    // Set by the table detours before their done flags.
    bool appended = false;
    bool scheduled = false;
    // Set at the title once both tables have it.
    bool enabled = false;
    bool loggedScript = false;
};

// A stripped villager: its list node and status.
struct Held {
    uint8_t* node;
    uint8_t* status;
};

// One line of a sidecar.
struct Entry {
    std::string fullKey;
    int32_t love = 0;
    int32_t enabled = 1;
    std::string raw;
};

struct LuaApi {
    LuaSetTopFn setTop;
    LuaTypeFn type;
    LuaToLStringFn toLString;
    LuaPushStringFn pushString;
    LuaPCallKFn pcallK;
    LuaLoadBufferXFn loadBufferX;
};

std::vector<VillagerDef> g_Villagers;
std::unordered_map<uint64_t, size_t> g_ById;
size_t g_ModCount = 0;
bool g_Active = false;
bool g_LoaderHooked = false;
LuaApi g_Lua = {};

DatabaseLoadFn g_OriginalCharaLoad = nullptr;
DatabaseLoadFn g_OriginalScheduleLoad = nullptr;
LuaCFunction g_OriginalLoader = nullptr;

// Table loads, boot worker.
std::atomic<bool> g_CharaSeen{false};
std::atomic<bool> g_CharaDone{false};
std::atomic<bool> g_ScheduleSeen{false};
std::atomic<bool> g_ScheduleDone{false};
std::atomic<bool> g_InLoad{false};
std::atomic<uint8_t*> g_CharaDb{nullptr};
std::atomic<uint8_t*> g_ScheduleDb{nullptr};
bool g_Checked = false;

// Save splice, main thread.
std::vector<Held> g_Stripped;
std::vector<Entry> g_Carried;
std::vector<Entry> g_Loaded;
int32_t g_LoadedSlot = 0;

// ---- Small helpers -------------------------------------------------------

bool IsBand(uint64_t id) {
    return id >= kBandBase && id < kBandBase + kBandSize;
}

const VillagerDef* FindByKey(const std::string& fullKey) {
    for (const VillagerDef& villager : g_Villagers) {
        if (_stricmp(villager.fullKey.c_str(), fullKey.c_str()) == 0) {
            return &villager;
        }
    }
    return nullptr;
}

const VillagerDef* FindById(uint64_t id) {
    const auto found = g_ById.find(id);
    return found == g_ById.end() ? nullptr : &g_Villagers[found->second];
}

uint8_t* LiveSaveData() {
    uint8_t* manager = ReadGlobal<uint8_t*>(retail::kGameManager);
    return manager ? ReadAt<uint8_t*>(manager, retail::kManagerSaveData) : nullptr;
}

// The character id of a CNpcStatus, false for anything else.
bool StatusId(const uint8_t* status, uint64_t& id) {
    if (!status || ReadAt<uintptr_t>(status, 0) != g_ModuleBase + retail::kNpcStatusVtable) {
        return false;
    }
    const uint8_t* holder = ReadAt<const uint8_t*>(status, retail::kNpcStatusChara);
    const uint8_t* record = holder ? ReadAt<const uint8_t*>(holder, 0) : nullptr;
    if (!record) {
        return false;
    }
    id = ReadAt<uint64_t>(record, retail::kCharaRecordId);
    return true;
}

bool IsModStatus(const uint8_t* status) {
    uint64_t id = 0;
    return StatusId(status, id) && IsBand(id);
}

// Drops a reference to a status. A status of an older save data, or one the
// live uid map no longer points at, forgets its save data first, so its
// destructor cannot unregister another status with the same uid.
void SafeRelease(uint8_t* status) {
    uint8_t* owner = ReadAt<uint8_t*>(status, retail::kNpcStatusSaveData);
    uint8_t* live = LiveSaveData();
    if (owner) {
        uint8_t* node = owner == live ? save_overlay::MapFind(
                                            live + retail::kSaveDataUidMap, ReadAt<uint64_t>(status, retail::kNpcStatusUid)
                                        )
                                      : nullptr;
        if (!node || ReadAt<uint8_t*>(node, retail::kMapNodeValue) != status) {
            WriteAt<uint8_t*>(status, retail::kNpcStatusSaveData, nullptr);
        }
    }
    Release(status, retail::kNpcStatusRefCount);
}

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

std::string DataPath(const std::string& name) {
    return ModFilePath((std::string("mods\\CanopyData\\") + name).c_str());
}

// Calls visit(node, status) for every node of the save data's villager
// list. visit may unlink the node it is given.
template <typename Visit>
void ForEachVillager(uint8_t* saveData, Visit visit) {
    uint8_t* list = saveData + retail::kSaveDataNpcList;
    uint8_t* head = ReadAt<uint8_t*>(list, retail::kListHead);
    size_t steps = ReadAt<size_t>(list, retail::kListSize);
    for (uint8_t* node = head ? ReadAt<uint8_t*>(head, retail::kListNodeNext) : nullptr;
         node && node != head && steps-- > 0;) {
        uint8_t* next = ReadAt<uint8_t*>(node, retail::kListNodeNext);
        visit(node, ReadAt<uint8_t*>(node, retail::kListNodeValue));
        node = next;
    }
}

// Links node at the end of the save data's villager list.
void LinkAtEnd(uint8_t* saveData, uint8_t* node) {
    uint8_t* list = saveData + retail::kSaveDataNpcList;
    uint8_t* head = ReadAt<uint8_t*>(list, retail::kListHead);
    uint8_t* tail = ReadAt<uint8_t*>(head, retail::kListNodePrev);
    WriteAt(node, retail::kListNodeNext, head);
    WriteAt(node, retail::kListNodePrev, tail);
    WriteAt(tail, retail::kListNodeNext, node);
    WriteAt(head, retail::kListNodePrev, node);
    WriteAt(list, retail::kListSize, ReadAt<size_t>(list, retail::kListSize) + 1);
}

// ---- Id registry ---------------------------------------------------------
// mods\CanopyData\villagers.ini: [ids], next = N, then Mod.Key = n lines.

struct Registry {
    uint64_t next = 1;
    std::vector<std::pair<std::string, uint64_t>> ids;
};

bool ReadRegistry(Registry& registry) {
    const std::string path = DataPath(kRegistryName);
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
        } else if (number > 0 && number < kBandSize) {
            registry.ids.emplace_back(key, number);
            registry.next = std::max(registry.next, number + 1);
        }
    }
    return true;
}

bool WriteRegistry(const Registry& registry) {
    std::string text =
        "; Canopy custom villager ids. Each mod villager keeps its number for good, and a number is never given\r\n"
        "; out twice. Do not edit or delete this file: saved mod villagers refer to these numbers.\r\n\r\n[ids]\r\n"
        "next = " +
        std::to_string(registry.next) + "\r\n";
    for (const auto& [key, number] : registry.ids) {
        text += key + " = " + std::to_string(number) + "\r\n";
    }
    CreateDirectoryA(ModFilePath("mods\\CanopyData").c_str(), nullptr);
    if (const uint32_t error = save_data::WriteFileAtomically(DataPath(kRegistryName), text)) {
        Log("Custom villagers: " + std::string(kRegistryName) + " could not be written, error " + std::to_string(error) +
            ".");
        return false;
    }
    return true;
}

// Gives every villager its id, assigning new numbers when needed. False when
// the registry cannot be read or written, so no id is ever reused.
bool AssignIds() {
    Registry registry;
    if (!ReadRegistry(registry)) {
        Log("Custom villagers: mods\\CanopyData\\" + std::string(kRegistryName) +
            " could not be read, so no ids are given out.");
        return false;
    }
    bool changed = false;
    for (VillagerDef& villager : g_Villagers) {
        const auto found = std::find_if(registry.ids.begin(), registry.ids.end(), [&](const auto& entry) {
            return _stricmp(entry.first.c_str(), villager.fullKey.c_str()) == 0;
        });
        uint64_t number = 0;
        if (found != registry.ids.end()) {
            number = found->second;
        } else {
            if (registry.next >= kBandSize) {
                Log("Custom villagers: the id range is used up, so " + villager.fullKey + " gets none.");
                continue;
            }
            number = registry.next++;
            registry.ids.emplace_back(villager.fullKey, number);
            changed = true;
        }
        villager.id = kBandBase + number;
    }
    if (changed && !WriteRegistry(registry)) {
        return false;
    }
    g_Villagers.erase(
        std::remove_if(
            g_Villagers.begin(), g_Villagers.end(), [](const VillagerDef& villager) { return villager.id == 0; }
        ),
        g_Villagers.end()
    );
    return true;
}

// ---- Table append (boot worker) ------------------------------------------

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

struct Row {
    const uint8_t* data;
    uint32_t size;
};

class TextPool {
public:
    explicit TextPool(uint32_t base) : base_(base), bytes_(1, '\0') {}

    // A text ref to text, or to the leading NUL when it is empty.
    void Write(std::vector<uint8_t>& row, size_t offset, const std::string& text) {
        uint32_t position = 0;
        if (!text.empty()) {
            position = static_cast<uint32_t>(bytes_.size());
            bytes_ += text;
            bytes_ += '\0';
        }
        WriteAt<uint32_t>(row.data(), offset, base_ + position);
        WriteAt<uint32_t>(row.data(), offset + 4, static_cast<uint32_t>(text.size()));
    }

    const std::string& Bytes() const {
        return bytes_;
    }

private:
    uint32_t base_;
    std::string bytes_;
};

// A table blob split into its rows.
struct Table {
    uint32_t count = 0;
    uint32_t rowBytes = 0;
    uint32_t poolBytes = 0;
    size_t rowsSize = 0;
    const uint8_t* pool = nullptr;
    std::unordered_map<uint64_t, Row> rows;
    std::set<uint32_t> enumCrcs;
    std::set<std::string> enums;
};

// Splits blob into rows keyed by the u64 id at +0, and collects the enum
// names at enumOffset, when nonzero. False when the lengths do not add up.
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
        table.rows.emplace(ReadAt<uint64_t>(row.data, 0), row);
        if (enumOffset) {
            const uint32_t enumStart = ReadAt<uint32_t>(row.data, enumOffset);
            const uint32_t enumLength = ReadAt<uint32_t>(row.data, enumOffset + 4);
            if (static_cast<uint64_t>(enumStart) + enumLength <= table.poolBytes) {
                const char* text = reinterpret_cast<const char*>(table.pool + enumStart);
                table.enumCrcs.insert(Crc(text, enumLength));
                table.enums.emplace(text, enumLength);
            }
        }
        offset += 4 + size;
    }
    return offset == kBlobHeader + table.rowsSize;
}

// The vanilla blob, then the mod rows (each with its length), then the
// vanilla pool and the mod pool.
void Merge(
    const uint8_t* blob, const Table& table, const std::vector<uint8_t>& modRows, size_t appended,
    const std::string& modPool, std::vector<uint8_t>& merged
) {
    merged.reserve(kBlobHeader + table.rowsSize + modRows.size() + table.poolBytes + modPool.size());
    merged.resize(kBlobHeader);
    WriteAt<uint32_t>(merged.data(), 0, table.count + static_cast<uint32_t>(appended));
    WriteAt<uint32_t>(merged.data(), 4, table.rowBytes + static_cast<uint32_t>(modRows.size() - 4 * appended));
    WriteAt<uint32_t>(merged.data(), 8, table.poolBytes + static_cast<uint32_t>(modPool.size()));
    merged.insert(merged.end(), blob + kBlobHeader, blob + kBlobHeader + table.rowsSize);
    merged.insert(merged.end(), modRows.begin(), modRows.end());
    merged.insert(merged.end(), table.pool, table.pool + table.poolBytes);
    merged.insert(merged.end(), modPool.begin(), modPool.end());
}

void AppendRow(std::vector<uint8_t>& modRows, const std::vector<uint8_t>& row) {
    const uint32_t size = static_cast<uint32_t>(row.size());
    modRows.insert(modRows.end(), reinterpret_cast<const uint8_t*>(&size), reinterpret_cast<const uint8_t*>(&size) + 4);
    modRows.insert(modRows.end(), row.begin(), row.end());
}

std::string PoolText(const Table& table, const uint8_t* row, size_t offset) {
    const uint32_t start = ReadAt<uint32_t>(row, offset);
    const uint32_t length = ReadAt<uint32_t>(row, offset + 4);
    if (static_cast<uint64_t>(start) + length > table.poolBytes) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(table.pool + start), length);
}

// The base villager's row with the mod villager's id, texts, script, place
// and no story hooks.
std::vector<uint8_t> BuildCharaRow(const VillagerDef& villager, const Row& base, size_t tail, TextPool& pool) {
    std::vector<uint8_t> row(base.data, base.data + base.size);
    WriteAt<uint64_t>(row.data(), kCharaRowId, villager.id);
    pool.Write(row, kCharaRowEnum, villager.enumName);
    for (size_t language = 0; language < retail::kLanguageCount; ++language) {
        pool.Write(row, kCharaRowNames + kTextRef * language, villager.names[language]);
        pool.Write(row, kCharaRowJobs + kTextRef * language, villager.jobs[language]);
        for (size_t block = 0; block < kHelpBlocks; ++block) {
            const size_t offset = kCharaRowHelps + kTextRef * (retail::kLanguageCount * block + language);
            pool.Write(row, offset, block == 0 ? villager.helps[language] : "");
        }
        pool.Write(row, tail + kCharaTailRequest + kTextRef * language, "");
    }
    if (!villager.module.empty()) {
        pool.Write(row, kCharaRowScript, villager.module);
    }
    if (villager.area != 0) {
        WriteAt<uint64_t>(row.data(), kCharaRowArea, villager.area);
    }
    if (villager.hasX) {
        WriteAt<int32_t>(row.data(), kCharaRowInitX, villager.x);
    }
    if (villager.hasY) {
        WriteAt<int32_t>(row.data(), kCharaRowInitY, villager.y);
    }
    WriteAt<int32_t>(row.data(), kCharaRowInitEnabled, villager.startEnabled ? 1 : 0);
    for (size_t flag = 0; flag < kLoveFlagCount; ++flag) {
        WriteAt<uint64_t>(row.data(), kCharaRowLoveEvents + 8 * flag, 0);
        WriteAt<uint64_t>(row.data(), kCharaRowLoveFlags + 8 * flag, 0);
    }
    WriteAt<uint64_t>(row.data(), kCharaRowRequestFlag, 0);
    WriteAt<uint64_t>(row.data(), tail + kCharaTailDetail, 0);
    WriteAt<uint64_t>(row.data(), tail + kCharaTailFair, 0);
    return row;
}

// Builds the vanilla character table plus every usable mod row into merged.
// Returns how many mod rows it holds; 0 leaves merged empty.
size_t BuildCharaBlob(uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged) {
    Table table;
    if (!ParseTable(blob, kCharaRowBase, kCharaRowEnum, table)) {
        Log("Mod villagers: the character table rows do not add up, so no villagers were appended.");
        return 0;
    }
    // The table is normally empty here; a filled one must not hold the ids.
    uint8_t* map = db + retail::kDatabaseMap;
    const bool tableFilled = ReadAt<size_t>(map, retail::kMapSize) > 0;
    TextPool modPool(table.poolBytes);
    std::vector<uint8_t> modRows;
    std::set<std::string> modEnums;
    std::set<std::string> mods;
    size_t appended = 0;
    mod_rules::Report report = mod_tables::LogReport();
    for (VillagerDef& villager : g_Villagers) {
        const std::string where = "Mod villager " + villager.fullKey;
        const auto base = table.rows.find(villager.baseId);
        const bool found = base != table.rows.end();
        const blob::Row baseRow = found ? blob::Row{base->second.data, base->second.size} : blob::Row{};
        if (!mod_rules::CheckAgainstCharacters(villager, found ? &baseRow : nullptr, report)) {
            continue;
        }
        const uint32_t forbid = ReadAt<uint32_t>(base->second.data, kCharaRowForbidCount);
        if (table.rows.count(villager.id) || (tableFilled && save_overlay::MapFind(map, villager.id))) {
            Log(where + " skipped, its id " + std::to_string(villager.id) + " is already in the character table.");
            continue;
        }
        std::string name = "CHARA_ID_CANOPY_" + EnumPart(villager.modKey) + "_" + EnumPart(villager.key);
        const std::string stem = name;
        for (int suffix = 2; table.enums.count(name) || modEnums.count(name) ||
                             table.enumCrcs.count(Crc(name.data(), name.size()));
             ++suffix) {
            name = stem + "_" + std::to_string(suffix);
        }
        villager.enumName = name;
        villager.baseModule = PoolText(table, base->second.data, kCharaRowScript);
        const size_t tail = kCharaRowForbid + 8 * static_cast<size_t>(forbid);
        AppendRow(modRows, BuildCharaRow(villager, base->second, tail, modPool));
        modEnums.insert(name);
        table.enumCrcs.insert(Crc(name.data(), name.size()));
        villager.appended = true;
        mods.insert(villager.modKey);
        ++appended;
    }
    if (appended == 0) {
        return 0;
    }
    Merge(blob, table, modRows, appended, modPool.Bytes(), merged);
    g_ModCount = mods.size();
    return appended;
}

// The module of a schedule row's first task, empty when it has none.
std::string FirstTaskModule(const Table& table, const Row& row) {
    if (ReadAt<uint32_t>(row.data, kScheduleRowTaskCount) == 0) {
        return {};
    }
    const size_t end = row.size - kScheduleTrailer;
    size_t offset = kScheduleRowTasks + kTextRef;
    // Skips a u32 count and count values of width bytes.
    const auto skip = [&](size_t width) {
        if (offset + 4 > end) {
            return false;
        }
        offset += 4 + width * ReadAt<uint32_t>(row.data, offset);
        return offset <= end;
    };
    if (!skip(8) || !skip(8)) {
        return {};
    }
    offset += kTaskPassedDay;
    if (offset > end || !skip(4)) {
        return {};
    }
    offset += 4;
    if (offset > end || !skip(4) || !skip(8)) {
        return {};
    }
    offset += kTextRef;
    if (offset > end || !skip(8) || !skip(8) || offset + kTextRef > end) {
        return {};
    }
    return PoolText(table, row.data, offset);
}

// One task with no conditions that runs module.routine, or no task when
// module is empty.
std::vector<uint8_t> BuildScheduleRow(
    const VillagerDef& villager, const Row& base, const std::string& module, TextPool& pool
) {
    std::vector<uint8_t> row(kScheduleRowTasks);
    WriteAt<uint64_t>(row.data(), kScheduleRowId, villager.id);
    pool.Write(row, kScheduleRowComment, "");
    WriteAt<uint32_t>(row.data(), kScheduleRowTaskCount, module.empty() ? 0 : 1);
    if (!module.empty()) {
        const size_t task = row.size();
        // name, on and off flag counts, passed day, season count, day,
        // weekday and weather counts, time, on and off area counts, then
        // module, entry and argument.
        row.resize(task + 8 + 4 + 4 + kTaskPassedDay + 4 + 4 + 4 + 4 + 8 + 4 + 4 + 3 * kTextRef, 0);
        pool.Write(row, task, kTaskName);
        pool.Write(row, task + 48, "");
        const size_t script = task + 64;
        pool.Write(row, script, module);
        pool.Write(row, script + kTextRef, villager.routine);
        pool.Write(row, script + 2 * kTextRef, "");
    }
    row.insert(row.end(), base.data + base.size - kScheduleTrailer, base.data + base.size);
    return row;
}

size_t BuildScheduleBlob(uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged) {
    Table table;
    if (!ParseTable(blob, kScheduleMinRow, 0, table)) {
        Log("Mod villagers: the schedule table rows do not add up, so no schedules were appended.");
        return 0;
    }
    uint8_t* map = db + retail::kDatabaseMap;
    const bool tableFilled = ReadAt<size_t>(map, retail::kMapSize) > 0;
    TextPool modPool(table.poolBytes);
    std::vector<uint8_t> modRows;
    size_t appended = 0;
    mod_rules::Report report = mod_tables::LogReport();
    for (VillagerDef& villager : g_Villagers) {
        const std::string where = "Mod villager " + villager.fullKey;
        const auto base = table.rows.find(villager.baseId);
        const bool found = base != table.rows.end();
        const blob::Row baseRow = found ? blob::Row{base->second.data, base->second.size} : blob::Row{};
        if (!mod_rules::CheckAgainstSchedule(villager, found ? &baseRow : nullptr, report)) {
            continue;
        }
        if (table.rows.count(villager.id) || (tableFilled && save_overlay::MapFind(map, villager.id))) {
            Log(where + " has no schedule, its id " + std::to_string(villager.id) + " is already in the schedule table.");
            continue;
        }
        const std::string module = villager.module.empty() ? FirstTaskModule(table, base->second) : villager.module;
        AppendRow(modRows, BuildScheduleRow(villager, base->second, module, modPool));
        villager.scheduled = true;
        ++appended;
    }
    if (appended == 0) {
        return 0;
    }
    Merge(blob, table, modRows, appended, modPool.Bytes(), merged);
    return appended;
}

// True when the table name's file part is file, without case.
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

__declspec(noinline) void TryBuild(
    bool chara, uint8_t* db, const uint8_t* blob, std::vector<uint8_t>& merged, size_t& appended
) {
    try {
        appended = chara ? BuildCharaBlob(db, blob, merged) : BuildScheduleBlob(db, blob, merged);
    } catch (...) {
        merged.clear();
        appended = 0;
        for (VillagerDef& villager : g_Villagers) {
            (chara ? villager.appended : villager.scheduled) = false;
        }
        Log(std::string("Mod villagers: building the ") + (chara ? "character" : "schedule") +
            " rows failed, so none were appended.");
    }
}

// The shared detour body: one merged load the first time the table loads.
int64_t LoadTable(
    bool chara, DatabaseLoadFn original, std::atomic<bool>& seen, std::atomic<bool>& done,
    std::atomic<uint8_t*>& database, uint8_t* db, const char* name, const uint8_t* blob
) {
    if (!blob || g_InLoad.load() || !IsTable(name, chara ? kCharaTableFile : kScheduleTableFile)) {
        return original(db, name, blob);
    }
    if (seen.exchange(true)) {
        return original(db, name, blob);
    }
    std::vector<uint8_t> merged;
    size_t appended = 0;
    TryBuild(chara, db, blob, merged, appended);
    g_InLoad = true;
    const int64_t result = original(db, name, merged.empty() ? blob : merged.data());
    g_InLoad = false;
    if (!merged.empty()) {
        Log("Mod villagers: " + Plural(appended, "villager", "villagers") + " appended to the " +
            (chara ? "character table from " + Plural(g_ModCount, "mod", "mods") + "." : std::string("schedule table.")));
    }
    database.store(db, std::memory_order_release);
    done.store(true, std::memory_order_release);
    return result;
}

int64_t __fastcall Detour_CharaLoad(uint8_t* db, const char* name, const uint8_t* blob) {
    return LoadTable(true, g_OriginalCharaLoad, g_CharaSeen, g_CharaDone, g_CharaDb, db, name, blob);
}

int64_t __fastcall Detour_ScheduleLoad(uint8_t* db, const char* name, const uint8_t* blob) {
    return LoadTable(false, g_OriginalScheduleLoad, g_ScheduleSeen, g_ScheduleDone, g_ScheduleDb, db, name, blob);
}

// ---- Script modules --------------------------------------------------------

VillagerDef* FindModule(const char* name) {
    if (!name || _strnicmp(name, kModulePrefix, std::strlen(kModulePrefix)) != 0) {
        return nullptr;
    }
    for (VillagerDef& villager : g_Villagers) {
        if (!villager.module.empty() && _stricmp(villager.module.c_str(), name) == 0) {
            return &villager;
        }
    }
    return nullptr;
}

__declspec(noinline) void LogScriptFailure(VillagerDef& villager, const char* message) {
    try {
        if (villager.loggedScript) {
            return;
        }
        villager.loggedScript = true;
        std::string text = message ? message : "it returned no module table";
        if (text.size() > 300) {
            text.resize(300);
        }
        std::replace(text.begin(), text.end(), '\n', ' ');
        std::replace(text.begin(), text.end(), '\r', ' ');
        Log("[" + villager.modKey + "] Villager " + villager.key + "'s script " + villager.script +
            " failed, so it runs its base villager's script: " + text + ".");
    } catch (...) {
    }
}

// Compiles and runs the villager's module and leaves its table on the
// stack. On failure the base villager's module is loaded instead.
int ServeModule(lua_State* state, VillagerDef& villager) {
    if (g_Lua.loadBufferX(state, villager.source.data(), villager.source.size(), villager.chunkName.c_str(), "t") == 0) {
        g_Lua.pushString(state, villager.module.c_str());
        if (g_Lua.pcallK(state, 1, 1, 0, 0, nullptr) == 0 && g_Lua.type(state, -1) == kTypeTable) {
            return 1;
        }
    }
    LogScriptFailure(
        villager, g_Lua.type(state, -1) == kTypeString ? g_Lua.toLString(state, -1, nullptr) : nullptr
    );
    g_Lua.setTop(state, 0);
    g_Lua.pushString(state, villager.baseModule.empty() ? "ai_chara_template" : villager.baseModule.c_str());
    return g_OriginalLoader(state);
}

int Detour_Loader(lua_State* state) {
    if (g_Lua.type(state, 1) == kTypeString) {
        if (VillagerDef* villager = FindModule(g_Lua.toLString(state, 1, nullptr))) {
            return ServeModule(state, *villager);
        }
    }
    return g_OriginalLoader(state);
}

// ---- Title self-check ----------------------------------------------------

// The id map node's value slot: the record holder, whose first field is the
// record. Null when the id is not in the database.
uint8_t* FindHolder(uint8_t* database, uint64_t id) {
    if (!database) {
        return nullptr;
    }
    uint8_t* node = nullptr;
    GameFunction<MapFindFn>(retail::kInt64MapFind)(database + retail::kDatabaseMap, &node, &id);
    if (!node || node == ReadAt<uint8_t*>(database, retail::kDatabaseMap + retail::kMapListHead)) {
        return nullptr;
    }
    return node + retail::kMapNodeValue;
}

uint8_t* FindRecord(uint8_t* database, uint64_t id) {
    uint8_t* holder = FindHolder(database, id);
    return holder ? ReadAt<uint8_t*>(holder, 0) : nullptr;
}

// The holder the database's record list keeps for record, which the reset
// stores in each CNpcStatus.
uint8_t* RecordListHolder(uint8_t* database, const uint8_t* record) {
    uint8_t** begin = ReadAt<uint8_t**>(database, retail::kDatabaseRecords);
    uint8_t** end = ReadAt<uint8_t**>(database, retail::kDatabaseRecords + 8);
    for (uint8_t** holder = begin; holder && holder != end; ++holder) {
        if (*holder && ReadAt<const uint8_t*>(*holder, 0) == record) {
            return *holder;
        }
    }
    return nullptr;
}

size_t CountModVillagers(uint8_t* saveData) {
    size_t count = 0;
    ForEachVillager(saveData, [&](uint8_t*, uint8_t* status) {
        count += IsModStatus(status) ? 1 : 0;
    });
    return count;
}

void SelfCheck() {
    g_Checked = true;
    if (!g_CharaDone.load(std::memory_order_acquire)) {
        Log("Mod villagers: the character table was never seen loading, so custom villagers are off.");
        return;
    }
    if (!g_ScheduleDone.load(std::memory_order_acquire)) {
        Log("Mod villagers: the schedule table was never seen loading, so mod villagers start disabled.");
    }
    uint8_t* charaDb = g_CharaDb.load(std::memory_order_acquire);
    uint8_t* scheduleDb = g_ScheduleDb.load(std::memory_order_acquire);
    for (VillagerDef& villager : g_Villagers) {
        if (!villager.appended) {
            continue;
        }
        const std::string what = "Mod villager " + villager.fullKey + " is id " + std::to_string(villager.id);
        uint8_t* record = FindRecord(charaDb, villager.id);
        if (!record || ReadAt<uint64_t>(record, retail::kCharaRecordId) != villager.id) {
            Log(what + ", but the character table does not have it. It stays off.");
            continue;
        }
        if (!villager.scheduled || !FindRecord(scheduleDb, villager.id)) {
            // A villager with no schedule is never placed in a new game.
            WriteAt<int32_t>(record, retail::kCharaRecordInitEnabled, 0);
            Log(what + ", but the schedule table does not have it. It starts disabled and stays off.");
            continue;
        }
        villager.enabled = true;
        Log(what + ", found in the character and schedule tables" +
            (villager.module.empty() ? ", with its base villager's script." : ", with script module " + villager.module + "."));
    }
    if (uint8_t* live = LiveSaveData()) {
        Log("Mod villagers: the title's save data holds " +
            Plural(CountModVillagers(live), "mod villager", "mod villagers") + ".");
    }
}

// ---- Creating a villager in a loaded save ----------------------------------

// Builds a CNpcStatus for the record behind holder exactly as the new game
// reset 0x158920 does, places it and links it at the end of the villager
// list. Returns the status, null when the memory could not be had.
uint8_t* CreateVillager(uint8_t* saveData, uint8_t* holder, uint8_t* unitHolder) {
    uint8_t* status = AllocateZeroed(retail::kNpcStatusSize);
    uint8_t* node = AllocateZeroed(retail::kListNodeSize);
    if (!status || !node) {
        return nullptr;
    }
    const uint8_t* record = ReadAt<const uint8_t*>(holder, 0);
    GameFunction<MapObjectStatusCtorFn>(retail::kMapObjectStatusCtor)(status, saveData, &unitHolder);
    WriteAt<uintptr_t>(status, 0, g_ModuleBase + retail::kUnitStatusVtable);
    WriteAt<uintptr_t>(status, retail::kStatusSecondVtable, g_ModuleBase + retail::kUnitStatusSecondVtable);
    WriteAt<uint64_t>(status, retail::kUnitStatusMembers, 0);
    GameFunction<MemberInitFn>(retail::kUnitStatusMemberInit)(status + retail::kUnitStatusMemberData);
    WriteAt<uintptr_t>(status, 0, g_ModuleBase + retail::kNpcStatusVtable);
    WriteAt<uintptr_t>(status, retail::kStatusSecondVtable, g_ModuleBase + retail::kNpcStatusSecondVtable);
    WriteAt<int32_t>(status, retail::kNpcStatusIndex, -1);
    WriteAt<uintptr_t>(status, retail::kNpcStatusLove, g_ModuleBase + retail::kLoveGaugeVtable);
    WriteAt<int32_t>(status, retail::kNpcStatusLoveValue, 0);
    GameFunction<MemberInitFn>(retail::kInt64MapInit)(status + retail::kNpcStatusMap);
    GameFunction<MemberInitFn>(retail::kNpcTalkHistoryInit)(status + retail::kNpcStatusTalkHistory);
    WriteAt<uint64_t>(status, retail::kNpcStatusLast, 0);
    WriteAt<uint8_t*>(status, retail::kNpcStatusChara, holder);
    // The reset's own reference while it places the status.
    AddRef(status, retail::kNpcStatusRefCount);

    alignas(16) float origin[4] = {};
    GameFunction<AreaOriginFn>(retail::kAreaOrigin)(
        origin, unitHolder, ReadAt<uint64_t>(record, retail::kCharaRecordInitArea)
    );
    alignas(16) const float position[4] = {
        origin[0] + static_cast<float>(ReadAt<int32_t>(record, retail::kCharaRecordInitX)),
        origin[1] + static_cast<float>(ReadAt<int32_t>(record, retail::kCharaRecordInitY)), 0.0f, 0.0f
    };
    GameFunction<SetPositionFn>(retail::kUnitSetPosition)(status, position, 1);
    std::memcpy(status + retail::kNpcStatusHome, status + retail::kNpcStatusPosition, 16);
    const auto init = ReadAt<StatusInitFn>(ReadAt<uint8_t*>(status, 0), retail::kNpcStatusInitSlot);
    init(status);
    WriteAt<uint8_t>(status, retail::kNpcStatusEnabled, ReadAt<int32_t>(record, retail::kCharaRecordInitEnabled) != 0);

    WriteAt<uint8_t*>(node, retail::kListNodeValue, status);
    AddRef(status, retail::kNpcStatusRefCount);
    LinkAtEnd(saveData, node);
    Release(status, retail::kNpcStatusRefCount);
    return status;
}

// Creates every enabled villager the save data lacks. Returns how many.
size_t EnsureVillagers(uint8_t* saveData) {
    std::set<uint64_t> present;
    uint8_t* unitHolder = nullptr;
    ForEachVillager(saveData, [&](uint8_t*, uint8_t* status) {
        uint64_t id = 0;
        if (!StatusId(status, id)) {
            return;
        }
        present.insert(id);
        if (!unitHolder && !IsBand(id)) {
            unitHolder = ReadAt<uint8_t*>(status, retail::kNpcStatusUnitHolder);
        }
    });
    uint8_t* charaDb = g_CharaDb.load(std::memory_order_acquire);
    size_t created = 0;
    for (const VillagerDef& villager : g_Villagers) {
        if (!villager.enabled || present.count(villager.id)) {
            continue;
        }
        if (!unitHolder) {
            Log("Mod villagers: the save data has no vanilla villager to take the unit data from, so none were placed.");
            return created;
        }
        uint8_t* record = FindRecord(charaDb, villager.id);
        uint8_t* holder = record ? RecordListHolder(charaDb, record) : nullptr;
        if (!holder) {
            Log("Mod villager " + villager.fullKey + " could not be placed, its record is missing.");
            continue;
        }
        if (!CreateVillager(saveData, holder, unitHolder)) {
            Log("Mod villager " + villager.fullKey + " could not be placed, out of memory.");
            continue;
        }
        ++created;
    }
    return created;
}

// ---- Save splice -----------------------------------------------------------

std::string EntryLine(const uint8_t* status) {
    uint64_t id = 0;
    StatusId(status, id);
    const VillagerDef* villager = FindById(id);
    const std::string key = villager ? villager->fullKey : "?" + std::to_string(id);
    return key + ", " + std::to_string(ReadAt<int32_t>(status, retail::kNpcStatusLoveValue)) + ", " +
           std::to_string(ReadAt<uint8_t>(status, retail::kNpcStatusEnabled) != 0 ? 1 : 0);
}

void Strip(uint8_t* live) {
    const size_t before = g_Stripped.size();
    uint8_t* list = live + retail::kSaveDataNpcList;
    ForEachVillager(live, [&](uint8_t* node, uint8_t* status) {
        if (!IsModStatus(status)) {
            return;
        }
        uint8_t* prev = ReadAt<uint8_t*>(node, retail::kListNodePrev);
        uint8_t* next = ReadAt<uint8_t*>(node, retail::kListNodeNext);
        WriteAt(prev, retail::kListNodeNext, next);
        WriteAt(next, retail::kListNodePrev, prev);
        WriteAt(list, retail::kListSize, ReadAt<size_t>(list, retail::kListSize) - 1);
        g_Stripped.push_back({node, status});
    });
    const size_t moved = g_Stripped.size() - before;
    if (moved > 0) {
        Log("Mod villagers: moved " + Plural(moved, "villager", "villagers") + " out of the save while it is written.");
    }
}

// The file body as of the strip: every held villager, then the carried
// lines of villagers that are not installed.
std::string Snapshot(size_t& count) {
    std::vector<std::string> lines;
    std::set<std::string> held;
    for (const Held& entry : g_Stripped) {
        uint64_t id = 0;
        StatusId(entry.status, id);
        if (const VillagerDef* villager = FindById(id)) {
            held.insert(canopy_ini::Lower(villager->fullKey));
        }
        lines.push_back(EntryLine(entry.status));
    }
    for (const Entry& entry : g_Carried) {
        if (!held.count(canopy_ini::Lower(entry.fullKey))) {
            lines.push_back(entry.raw);
        }
    }
    count = lines.size();
    if (lines.empty()) {
        return {};
    }
    std::string text = "[villagers]\r\n";
    for (const std::string& line : lines) {
        text += "villager = " + line + "\r\n";
    }
    return text;
}

void Restore(uint8_t* live) {
    size_t restored = 0;
    for (const Held& entry : g_Stripped) {
        if (!live || ReadAt<uint8_t*>(entry.status, retail::kNpcStatusSaveData) != live) {
            // The node's reference goes; the node itself is left alone.
            SafeRelease(entry.status);
            continue;
        }
        LinkAtEnd(live, entry.node);
        ++restored;
    }
    if (restored > 0) {
        Log("Mod villagers: " + Plural(restored, "villager is", "villagers are") + " back after the save.");
    }
    g_Stripped.clear();
}

bool ParseEntry(const std::string& value, Entry& entry) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find(',', start);
        if (end == std::string::npos) {
            end = value.size();
        }
        parts.push_back(canopy_ini::Trim(value.substr(start, end - start)));
        start = end + 1;
    }
    entry.raw = value;
    if (parts.size() != 3 || parts[0].empty()) {
        return false;
    }
    entry.fullKey = parts[0];
    return ParseI32(parts[1], entry.love) && ParseI32(parts[2], entry.enabled);
}

void Loaded(int32_t slot, save_overlay::FileState state, const std::string& text) {
    g_Carried.clear();
    g_Loaded.clear();
    g_LoadedSlot = slot;
    if (state != save_overlay::FileState::Read) {
        return;
    }
    size_t start = 0;
    while (start < text.size() && g_Loaded.size() < kMaxSidecarLines) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = canopy_ini::Trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || equals == std::string::npos ||
            canopy_ini::Lower(canopy_ini::Trim(line.substr(0, equals))) != "villager") {
            continue;
        }
        Entry entry;
        if (!ParseEntry(canopy_ini::Trim(line.substr(equals + 1)), entry)) {
            entry.fullKey.clear();
        }
        g_Loaded.push_back(entry);
    }
}

uint8_t* FindStatus(uint8_t* saveData, uint64_t wanted) {
    uint8_t* found = nullptr;
    ForEachVillager(saveData, [&](uint8_t*, uint8_t* status) {
        uint64_t id = 0;
        if (!found && StatusId(status, id) && id == wanted) {
            found = status;
        }
    });
    return found;
}

// Puts the loaded sidecar's love and enabled state on the villagers.
void ApplyLoaded(uint8_t* live) {
    size_t applied = 0;
    size_t kept = 0;
    for (const Entry& entry : g_Loaded) {
        const VillagerDef* villager = entry.fullKey.empty() ? nullptr : FindByKey(entry.fullKey);
        uint8_t* status = villager && villager->enabled ? FindStatus(live, villager->id) : nullptr;
        if (!status) {
            g_Carried.push_back(entry);
            ++kept;
            continue;
        }
        WriteAt<int32_t>(status, retail::kNpcStatusLoveValue, std::clamp(entry.love, 0, kLoveMax));
        WriteAt<uint8_t>(status, retail::kNpcStatusEnabled, entry.enabled != 0);
        ++applied;
    }
    if (applied > 0 || kept > 0) {
        Log("Mod villagers: restored " + Plural(applied, "villager", "villagers") + " from " + SlotName(g_LoadedSlot) +
            (kept > 0 ? ", and kept " + Plural(kept, "line", "lines") + " of villagers that are not installed or are off."
                      : "."));
    }
    g_Loaded.clear();
}

void AfterLoad(uint8_t* live) {
    const size_t created = EnsureVillagers(live);
    if (created > 0) {
        Log("Mod villagers: placed " + Plural(created, "villager", "villagers") + " the loaded save did not have.");
    }
    ApplyLoaded(live);
}

void AfterInPlace(uint8_t* live) {
    const size_t created = EnsureVillagers(live);
    if (created > 0) {
        Log("Mod villagers: placed " + Plural(created, "villager", "villagers") + " after continue.");
    }
}

// The title screen: everything held for the old game goes.
void Clear() {
    for (const Held& entry : g_Stripped) {
        SafeRelease(entry.status);
    }
    g_Stripped.clear();
    g_Carried.clear();
    g_Loaded.clear();
}

void ResolveLuaApi() {
    g_Lua.setTop = GameFunction<LuaSetTopFn>(retail::kLuaSetTop);
    g_Lua.type = GameFunction<LuaTypeFn>(retail::kLuaType);
    g_Lua.toLString = GameFunction<LuaToLStringFn>(retail::kLuaToLString);
    g_Lua.pushString = GameFunction<LuaPushStringFn>(retail::kLuaPushString);
    g_Lua.pcallK = GameFunction<LuaPCallKFn>(retail::kLuaPCallK);
    g_Lua.loadBufferX = GameFunction<LuaLoadBufferXFn>(retail::kLuaLoadBufferX);
}

// Drops every script, so the villagers run their base villager's module.
void DropScripts(const char* why) {
    for (VillagerDef& villager : g_Villagers) {
        villager.module.clear();
        villager.source.clear();
    }
    Log(std::string("Mod villagers run their base villager's script, ") + why + ".");
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData, bool beforeGameInit) {
    std::vector<mod_rules::ModFolder> folders =
        mod_rules::WithVillagers(mod_rules::ScanMods(ModFilePath("mods")));
    if (folders.empty()) {
        Log("No mod villagers, so custom villagers are off.");
        return false;
    }
    if (!ticking || !saveData) {
        Log("Custom villagers need the frame tick and per-save data, so they are off.");
        return false;
    }
    if (!beforeGameInit) {
        Log("Custom villagers need Canopy to start before game init, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModVillagersRequirements)) {
        Log("Custom villagers are off.");
        return false;
    }
    const uintptr_t initSlot = g_ModuleBase + retail::kNpcStatusVtable + retail::kNpcStatusInitSlot;
    if (ReadAt<uintptr_t>(reinterpret_cast<const void*>(initSlot), 0) != g_ModuleBase + retail::kNpcStatusInitCode) {
        Log("Custom villagers are off, the villager init is not where it should be.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    const auto accept = [](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Villagers in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
            Plural(count, "villager", "villagers") + (joined ? "." : ", registered for its villagers."));
        return true;
    };
    for (mod_rules::VillagerSpec& spec :
         mod_rules::ReadVillagers(folders, ModFilePath("mods"), mod_tables::LoaderRefs(), report, accept)) {
        VillagerDef villager;
        static_cast<mod_rules::VillagerSpec&>(villager) = std::move(spec);
        g_Villagers.push_back(std::move(villager));
    }
    if (g_Villagers.empty() || !AssignIds() || g_Villagers.empty()) {
        g_Villagers.clear();
        Log("No usable mod villager, so custom villagers are off.");
        return false;
    }
    for (size_t index = 0; index < g_Villagers.size(); ++index) {
        g_ById.emplace(g_Villagers[index].id, index);
    }
    const bool scripts = std::any_of(g_Villagers.begin(), g_Villagers.end(), [](const VillagerDef& villager) {
        return !villager.module.empty();
    });
    if (scripts && !RequirementsMet(retail::kLuaHostRequirements)) {
        DropScripts("the script loader does not match");
    }

    DetourSet hooks("Custom villagers");
    if (!hooks.Add(
            retail::kCharaTableLoad, reinterpret_cast<void*>(&Detour_CharaLoad),
            reinterpret_cast<void**>(&g_OriginalCharaLoad), "charaTableLoad"
        ) ||
        !hooks.Add(
            retail::kCharaScheduleLoad, reinterpret_cast<void*>(&Detour_ScheduleLoad),
            reinterpret_cast<void**>(&g_OriginalScheduleLoad), "charaScheduleLoad"
        )) {
        hooks.RemoveAll();
        g_Villagers.clear();
        g_ById.clear();
        Log("Custom villagers are off, their hooks could not be created.");
        return false;
    }
    void* loader = reinterpret_cast<void*>(g_ModuleBase + retail::kScriptModuleLoader);
    if (std::any_of(g_Villagers.begin(), g_Villagers.end(), [](const VillagerDef& v) { return !v.module.empty(); })) {
        ResolveLuaApi();
        if (const char* reason = hook_chain::Add(
                loader, kLoaderChainOwner, reinterpret_cast<void*>(&Detour_Loader),
                reinterpret_cast<void**>(&g_OriginalLoader)
            )) {
            DropScripts(("the script loader hook was not created, " + std::string(reason)).c_str());
        } else {
            g_LoaderHooked = true;
        }
    }
    const save_overlay::Adapter adapter = {
        "villagers", "Mod villagers", "villager", "villagers", "Each line: mod.villager, love, enabled.",
        kOverlayOrder, nullptr, &Strip, &Snapshot, &Restore, &Loaded, &AfterLoad, &AfterInPlace, &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        hooks.RemoveAll();
        if (g_LoaderHooked) {
            hook_chain::Remove(loader, kLoaderChainOwner);
            g_LoaderHooked = false;
        }
        g_Villagers.clear();
        g_ById.clear();
        Log("Custom villagers are off, the overlay save could not start.");
        return false;
    }
    g_Active = true;
    std::set<std::string> mods;
    size_t scripted = 0;
    for (const VillagerDef& villager : g_Villagers) {
        mods.insert(villager.modKey);
        scripted += villager.module.empty() ? 0 : 1;
    }
    Log("Custom villagers installed: " + Plural(g_Villagers.size(), "villager", "villagers") + " from " +
        Plural(mods.size(), "mod", "mods") + ", " + Plural(scripted, "with its own script", "with their own scripts") +
        (scripted > 0 ? ". Villager scripts run as game scripts, outside the Lua sandbox." : "."));
    return true;
}

void OnEvent(CanopyEvent event) {
    if (!g_Active) {
        return;
    }
    if (event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

bool OwnsModule(const char* name) {
    return g_LoaderHooked && FindModule(name) != nullptr;
}

uint64_t IdOf(const std::string& fullKey) {
    const VillagerDef* villager = g_Active ? FindByKey(fullKey) : nullptr;
    return villager ? villager->id : 0;
}

}  // namespace mod_villagers
