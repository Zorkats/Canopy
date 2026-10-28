#define NOMINMAX
#include "mod_objects.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "core.h"
#include "mod_art.h"
#include "mod_items.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "rules_objects.h"
#include "save_overlay.h"
#include "table_engine.h"

// =============================================================
// MOD OBJECTS DESIGN
// =============================================================
// A mod defines objects in its canopy.ini, one [object.Key] section each, as
// a copy of a vanilla furniture gimmick (clone = <id>) with its own names
// and, optionally, its own skin. The mod item with use = place and
// object = Key puts it down, and picking it up gives that item back. A
// folder with objects and no Lua or DLL is still registered as a mod. With
// no objects anywhere, nothing below is hooked.
//
// Rules. What an [object.Key] section accepts is CanopyCore's rules_objects,
// shared with the desktop tools; this file installs what they read.
//
// Ids. Each object gets 1650000000 + n, where n comes from
// mods\CanopyData\objects.ini, given out once and never again like the item
// ids. Vanilla gimmick ids are either below 1000000000 or in the
// 2060000000s, and the villager band below keeps under 1650000000.
//
// Table. The table engine hands the gimmick table to AppendObjects on a game
// worker. Each row is the clone's row with the object's id, enum name
// (GIMMICK_ID_CANOPY_<MOD>_<KEY>), names, skin in the four art slots and
// the color replace slot, no refID, a zero trailer, and as the item it
// gives back the mod item that places it. The item table loads at the same
// time on another worker, so neither side waits on the other: each builds
// from what Install fixed. Here the clone must only walk and give an item
// back; the desktop check holds it to the full furniture rule.
//
// Title. Mod items check first that the gimmick table has the object their
// place item names. Then each object is looked up through the game's own
// find, and is on when its row and its clone's are there and its item, if
// any, is on.
//
// Save. A placed object is a CGimmickStatus in the save data's gimmick list,
// whose data pointer the save writes as its row's id. A load of an id the
// table lacks leaves the pointer null, which the status loader then reads,
// so a save holding a mod id crashes the game without Canopy. Objects are
// one adapter of the overlay save core: the strip points each placed mod
// object at its clone's row, so the written save holds only vanilla ids,
// and the restore points it back. The sidecar slot<N>.objects.ini keeps
// each one's uid and Mod.Key. Once a load is live, every status of the list
// is matched by uid and, still holding the clone, gets its object back. A
// save loaded without Canopy, or with the object off, shows the clone, and
// its lines are kept and written back. Statuses are matched by uid in the
// restore too, so Canopy never holds a pointer to one.
//
// Threads. AppendObjects runs on a game worker and only reads the object
// definitions, which never change after Install, and sets each row's
// appended flag. Everything else runs on the main thread.

namespace mod_objects {
namespace {

using namespace core;

using mod_tables::EnumPart;
using mod_tables::Plural;

constexpr const char* kTableFile = "gimmick.dat";
constexpr uint64_t kBandBase = 1650000000;
constexpr uint64_t kBandSize = 50000000;
constexpr size_t kMaxSidecarLines = 4096;
// Before items, which have the highest order.
constexpr int kOverlayOrder = 90;
constexpr const char* kRegistryName = "objects.ini";
constexpr const char* kRegistryHeader =
    "; Canopy mod object ids. Each mod object keeps its number for good, and a number is never given out\r\n"
    "; twice. Do not edit or delete this file: placed mod objects refer to these numbers.\r\n";

// What the rules read, and what installing it adds.
struct ObjectDef : mod_rules::ObjectSpec {
    uint64_t id = 0;
    // The place item, from mod items' Install.
    uint64_t item = 0;
    // Set by the table load, on a worker.
    std::atomic<bool> appended{false};
    // Set at the title.
    bool enabled = false;
    uint8_t** holder = nullptr;
    uint8_t** cloneHolder = nullptr;
};

// One line of a sidecar.
struct Entry {
    uint64_t uid = 0;
    std::string fullKey;
    std::string raw;
};

std::vector<std::unique_ptr<ObjectDef>> g_Objects;
bool g_Active = false;
bool g_Checked = false;

// Save splice, main thread: the uids pointed at their clones, by object.
std::unordered_map<uint64_t, const ObjectDef*> g_Swapped;
std::vector<std::string> g_Carried;
std::vector<Entry> g_Loaded;
int32_t g_LoadedSlot = 0;

bool IsBand(uint64_t id) {
    return id >= kBandBase && id < kBandBase + kBandSize;
}

const ObjectDef* FindById(uint64_t id) {
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        if (object->id == id) {
            return object.get();
        }
    }
    return nullptr;
}

const ObjectDef* FindByKey(const std::string& fullKey) {
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        if (_stricmp(object->fullKey.c_str(), fullKey.c_str()) == 0) {
            return object.get();
        }
    }
    return nullptr;
}

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

// ---- Table append (game worker) ------------------------------------------

void AppendObjects(const char*, uint8_t* db, const blob::Table& table, blob::Appender& appender) {
    std::unordered_map<uint64_t, size_t> byId;
    for (size_t index = 0; index < table.rows.size(); ++index) {
        byId.emplace(blob::RowId(table.rows[index]), index);
    }
    blob::EnumNames names(table, 8);
    mod_rules::Report report = mod_tables::LogReport();
    const mod_rules::TextWriter write = [&](std::vector<uint8_t>& row, size_t offset, const std::string& text) {
        appender.Texts().Write(row, offset, text);
    };
    size_t added = 0;
    std::set<std::string> mods;
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        if (byId.count(object->id) || mod_tables::DatabaseHas(db, object->id)) {
            Log("Mod object " + object->fullKey + " skipped, its id " + std::to_string(object->id) +
                " is already in the gimmick table.");
            continue;
        }
        const auto clone = byId.find(object->cloneId);
        const blob::Row* cloneRow = clone == byId.end() ? nullptr : &table.rows[clone->second];
        mod_rules::GimmickLayout layout;
        if (!mod_rules::CheckAgainstTables(*object, cloneRow, nullptr, report) ||
            !mod_rules::WalkGimmick(*cloneRow, layout)) {
            continue;
        }
        mod_rules::ObjectRowParts parts;
        parts.id = object->id;
        parts.enumName = names.Unique("GIMMICK_ID_CANOPY_" + EnumPart(object->modKey) + "_" + EnumPart(object->key));
        parts.item = object->item;
        parts.skin = object->skin;
        if (object->modSkin && !mod_art::SkinAdded(object->skin)) {
            report.Warning(
                object->folder, object->line,
                "Mod object " + object->fullKey + " keeps its clone's skin, its mod skin " +
                    std::to_string(object->skin) + " is not in the AnimeReplaceTexture table."
            );
            parts.skin = 0;
        }
        appender.Add(mod_rules::BuildObjectRow(*object, *cloneRow, layout, parts, write));
        object->appended.store(true, std::memory_order_release);
        mods.insert(object->modKey);
        ++added;
    }
    if (added > 0) {
        Log("Mod objects: " + Plural(added, "object", "objects") + " from " + Plural(mods.size(), "mod", "mods") +
            " appended to the gimmick table.");
    }
}

// ---- Title self-check ----------------------------------------------------

void SelfCheck() {
    g_Checked = true;
    if (!table_engine::Complete()) {
        Log("Mod objects: the game tables were never seen loading, so mod objects are off.");
        return;
    }
    uint8_t* gimmicks = table_engine::Database(kTableFile);
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        if (!object->appended.load(std::memory_order_acquire)) {
            continue;
        }
        const std::string what = "Mod object " + object->fullKey + " is id " + std::to_string(object->id);
        object->holder = mod_tables::FindHolder(gimmicks, object->id);
        object->cloneHolder = mod_tables::FindHolder(gimmicks, object->cloneId);
        if (!object->holder || !*object->holder) {
            Log(what + ", but the gimmick table does not have it. It stays off.");
            continue;
        }
        if (!object->cloneHolder || !*object->cloneHolder) {
            Log(what + ", but its clone " + std::to_string(object->cloneId) + " is missing. It stays off.");
            continue;
        }
        if (object->item != 0 && !mod_items::Usable(object->item)) {
            Log(what + ", but the item that places it is off. It stays off.");
            continue;
        }
        object->enabled = true;
        Log(what + ", found in the gimmick table" + (object->item == 0 ? ", but no item places it." : "."));
    }
}

// ---- Save splice -----------------------------------------------------------

// Calls visit(status, holderField) for every status of the gimmick list.
template <typename Visit>
void ForEachGimmick(uint8_t* live, Visit visit) {
    uint8_t* list = live + retail::kSaveDataGimmickList;
    uint8_t* head = ReadAt<uint8_t*>(list, retail::kListHead);
    size_t steps = ReadAt<size_t>(list, retail::kListSize);
    for (uint8_t* node = head ? ReadAt<uint8_t*>(head, retail::kListNodeNext) : nullptr;
         node && node != head && steps-- > 0; node = ReadAt<uint8_t*>(node, retail::kListNodeNext)) {
        uint8_t* status = ReadAt<uint8_t*>(node, retail::kListNodeValue);
        if (status) {
            visit(status, reinterpret_cast<uint8_t***>(status + retail::kGimmickStatusData));
        }
    }
}

uint64_t HolderId(uint8_t** holder) {
    return holder && *holder ? ReadAt<uint64_t>(*holder, 0) : 0;
}

void Strip(uint8_t* live) {
    size_t swapped = 0;
    size_t stuck = 0;
    ForEachGimmick(live, [&](uint8_t* status, uint8_t*** data) {
        const uint64_t id = HolderId(*data);
        if (!IsBand(id)) {
            return;
        }
        const ObjectDef* object = FindById(id);
        if (!object || !object->cloneHolder) {
            ++stuck;
            return;
        }
        *data = object->cloneHolder;
        g_Swapped[ReadAt<uint64_t>(status, retail::kGimmickStatusUid)] = object;
        ++swapped;
    });
    if (swapped > 0) {
        Log("Mod objects: saving " + Plural(swapped, "placed object", "placed objects") + " as vanilla furniture.");
    }
    if (stuck > 0) {
        Log("Mod objects: " + Plural(stuck, "placed object has", "placed objects have") +
            " no clone to save as, so this save needs Canopy and the mod to load.");
    }
}

std::string Snapshot(size_t& count) {
    std::vector<std::string> lines;
    for (const auto& [uid, object] : g_Swapped) {
        lines.push_back(std::to_string(uid) + ", " + object->fullKey);
    }
    lines.insert(lines.end(), g_Carried.begin(), g_Carried.end());
    count = lines.size();
    if (lines.empty()) {
        return {};
    }
    std::string text = "[objects]\r\n";
    for (const std::string& line : lines) {
        text += "object = " + line + "\r\n";
    }
    return text;
}

void Restore(uint8_t* live) {
    size_t restored = 0;
    if (live && !g_Swapped.empty()) {
        ForEachGimmick(live, [&](uint8_t* status, uint8_t*** data) {
            const auto found = g_Swapped.find(ReadAt<uint64_t>(status, retail::kGimmickStatusUid));
            if (found != g_Swapped.end() && *data == found->second->cloneHolder) {
                *data = found->second->holder;
                ++restored;
            }
        });
    }
    if (restored > 0) {
        Log("Mod objects: " + Plural(restored, "placed object is", "placed objects are") + " back after the save.");
    }
    g_Swapped.clear();
}

bool ParseEntry(const std::string& value, Entry& entry) {
    entry.raw = value;
    const size_t comma = value.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    entry.fullKey = canopy_ini::Trim(value.substr(comma + 1));
    return mod_tables::ParseU64(canopy_ini::Trim(value.substr(0, comma)), entry.uid) && !entry.fullKey.empty();
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
            canopy_ini::Lower(canopy_ini::Trim(line.substr(0, equals))) != "object") {
            continue;
        }
        Entry entry;
        ParseEntry(canopy_ini::Trim(line.substr(equals + 1)), entry);
        g_Loaded.push_back(entry);
    }
}

// Points each loaded status still holding its clone back at its object.
// Lines of objects that are not installed or are off are kept; lines whose
// status is gone go.
void AfterLoad(uint8_t* live) {
    if (g_Loaded.empty()) {
        return;
    }
    std::unordered_map<uint64_t, uint8_t***> byUid;
    ForEachGimmick(live, [&](uint8_t* status, uint8_t*** data) {
        byUid.emplace(ReadAt<uint64_t>(status, retail::kGimmickStatusUid), data);
    });
    size_t restored = 0;
    size_t kept = 0;
    size_t gone = 0;
    for (const Entry& entry : g_Loaded) {
        const ObjectDef* object = entry.fullKey.empty() ? nullptr : FindByKey(entry.fullKey);
        if (!object || !object->enabled) {
            g_Carried.push_back(entry.raw);
            ++kept;
            continue;
        }
        const auto found = byUid.find(entry.uid);
        if (found == byUid.end() || HolderId(*found->second) != object->cloneId) {
            ++gone;
            continue;
        }
        *found->second = object->holder;
        ++restored;
    }
    if (restored > 0 || kept > 0 || gone > 0) {
        Log("Mod objects: restored " + Plural(restored, "placed object", "placed objects") + " from " +
            SlotName(g_LoadedSlot) +
            (kept > 0 ? ", kept " + Plural(kept, "line", "lines") + " of objects that are not installed or are off"
                      : std::string()) +
            (gone > 0 ? ", and dropped " + Plural(gone, "line", "lines") + " of objects no longer placed." : "."));
    }
    g_Loaded.clear();
}

void Clear() {
    g_Swapped.clear();
    g_Carried.clear();
    g_Loaded.clear();
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData, bool beforeGameInit) {
    std::vector<mod_rules::ModFolder> folders = mod_rules::WithObjects(mod_rules::ScanMods(ModFilePath("mods")));
    if (folders.empty()) {
        Log("No mod objects, so mod objects are off.");
        return false;
    }
    if (!ticking || !saveData) {
        Log("Mod objects need the frame tick and per-save data, so they are off.");
        return false;
    }
    if (!table_engine::Install(beforeGameInit)) {
        Log("Mod objects need the table engine, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModObjectsRequirements)) {
        Log("Mod objects are off.");
        return false;
    }

    mod_rules::Report report = mod_tables::LogReport();
    const auto accept = [](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Objects in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " +
            Plural(count, "object", "objects") + (joined ? "." : ", registered for its objects."));
        return true;
    };
    for (mod_rules::ObjectSpec& spec : mod_rules::ReadObjects(folders, mod_tables::LoaderRefs(), report, accept)) {
        auto object = std::make_unique<ObjectDef>();
        static_cast<mod_rules::ObjectSpec&>(*object) = std::move(spec);
        g_Objects.push_back(std::move(object));
    }
    std::vector<std::string> keys;
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        keys.push_back(object->fullKey);
    }
    std::vector<uint64_t> numbers;
    if (g_Objects.empty() ||
        !mod_tables::AssignNumbers(kRegistryName, kRegistryHeader, "Mod objects", kBandSize, keys, numbers)) {
        g_Objects.clear();
        Log("No usable mod object, so mod objects are off.");
        return false;
    }
    for (size_t index = 0; index < g_Objects.size(); ++index) {
        g_Objects[index]->id = numbers[index] ? kBandBase + numbers[index] : 0;
    }
    g_Objects.erase(
        std::remove_if(
            g_Objects.begin(), g_Objects.end(), [](const std::unique_ptr<ObjectDef>& object) { return object->id == 0; }
        ),
        g_Objects.end()
    );
    if (g_Objects.empty()) {
        Log("No usable mod object, so mod objects are off.");
        return false;
    }

    const save_overlay::Adapter adapter = {
        "objects", "Mod objects", "object", "objects", "Each line: uid, mod.object.", kOverlayOrder,
        nullptr, &Strip, &Snapshot, &Restore, &Loaded, &AfterLoad, nullptr, &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        g_Objects.clear();
        Log("Mod objects are off, the overlay save could not start.");
        return false;
    }
    table_engine::Contribute(kTableFile, &AppendObjects);
    g_Active = true;
    std::set<std::string> mods;
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        mods.insert(object->modKey);
    }
    Log("Mod objects installed: " + Plural(g_Objects.size(), "object", "objects") + " from " +
        Plural(mods.size(), "mod", "mods") + ".");
    return true;
}

void LinkItems() {
    for (const std::unique_ptr<ObjectDef>& object : g_Objects) {
        object->item = mod_items::PlaceItemFor(object->fullKey);
    }
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

uint64_t IdOf(const std::string& fullKey) {
    const ObjectDef* object = FindByKey(fullKey);
    return object ? object->id : 0;
}

uint64_t CloneOf(uint64_t id) {
    const ObjectDef* object = IsBand(id) ? FindById(id) : nullptr;
    return object ? object->cloneId : 0;
}

}  // namespace mod_objects
