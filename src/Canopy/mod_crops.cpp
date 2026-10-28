#define NOMINMAX
#include "mod_crops.h"

#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "mod_items.h"
#include "mod_rows.h"
#include "mod_tables.h"
#include "save_overlay.h"
#include "table_engine.h"
#include "table_schema.h"

// =============================================================
// MOD CROPS DESIGN
// =============================================================
// A [crop.<Key>] section is two rows that mod_rows appends with one id: a
// crops.dat row and a cropsharvest.dat row, each a copy of the same vanilla
// crop. The game plants a crop from a seed item, whose action is Plant with
// the crop's id as its value, and finds what a stage drops by the crop's id
// in the harvest table. So once both rows are in, a seed is all a crop needs.
//
// Seeds. Install makes the crop's seed item a seed through mod items, before
// the item table loads, so the item row is built with the Plant action. An
// item with a use of its own is refused and its crop dropped. At the title,
// a crop whose rows are not both in has its seed's action cleared in the item
// record: the plant check reads the crop it looks up without a null check.
//
// Save. A planted crop is a CCropsStatus, the main or sub crop of a cell of a
// map's status, whose data pointer the save writes as its row's id, and a
// load of an id the table lacks leaves it null. Crops are one adapter of the
// overlay save core: the strip points each planted mod crop at its base
// crop's row, so the written save holds only vanilla ids, and the restore
// points it back. The sidecar slot<N>.crops.ini keeps each one's map, cell,
// slot (0 main, 1 sub), base and Mod.Key. Once a load is live, each line
// whose spot still holds its base gets its crop back. A save loaded without
// Canopy, or with the crop off, grows the base crop; the lines of a crop that
// is off are kept while their spot holds the base. Spots are matched by key
// in the restore too, so Canopy never holds a pointer to a status.
//
// Threads. Everything here runs on the startup thread (Install) or the main
// thread.

namespace mod_crops {
namespace {

using namespace core;

using mod_tables::Plural;

constexpr const char* kCropsFile = "crops.dat";
constexpr const char* kHarvestFile = "cropsharvest.dat";
constexpr size_t kMaxSidecarLines = 16384;
// Between flags and objects; any order works, since no other adapter touches
// crop statuses.
constexpr int kOverlayOrder = 70;
constexpr size_t kLineFields = 5;
// Bounds the walk of a map, which a broken save data cannot make endless.
constexpr size_t kMaxMapNodes = 1 << 20;

struct CropDef : mod_rows::Crop {
    // Set at the title.
    bool enabled = false;
    uint8_t** holder = nullptr;
    uint8_t** baseHolder = nullptr;
};

// Where a crop grows: its map, its cell and the cell's slot.
struct Spot {
    uint64_t map = 0;
    uint64_t cell = 0;
    int slot = 0;

    bool operator<(const Spot& other) const {
        return std::tie(map, cell, slot) < std::tie(other.map, other.cell, other.slot);
    }
};

// One line of a sidecar.
struct Entry {
    Spot spot;
    uint64_t base = 0;
    std::string fullKey;
    std::string raw;
};

std::vector<CropDef> g_Crops;
bool g_Active = false;
bool g_Checked = false;

// Save splice, main thread: the spots pointed at their base, by crop.
std::map<Spot, const CropDef*> g_Swapped;
std::vector<std::string> g_Carried;
std::vector<Entry> g_Loaded;
int32_t g_LoadedSlot = 0;

bool IsBand(uint64_t id) {
    return id >= table_schema::kRowBandStart && id < table_schema::kRowBandStart + table_schema::kRowBandSize;
}

const CropDef* FindById(uint64_t id) {
    for (const CropDef& crop : g_Crops) {
        if (crop.id == id) {
            return &crop;
        }
    }
    return nullptr;
}

const CropDef* FindByKey(const std::string& fullKey) {
    for (const CropDef& crop : g_Crops) {
        if (_stricmp(crop.fullKey.c_str(), fullKey.c_str()) == 0) {
            return &crop;
        }
    }
    return nullptr;
}

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

uint64_t HolderId(uint8_t** holder) {
    return holder && *holder ? ReadAt<uint64_t>(*holder, 0) : 0;
}

// ---- Title self-check ----------------------------------------------------

// Clears the action of a seed whose crop is not in, so using it plants
// nothing instead of reading a crop that is not there.
void Unplant(const CropDef& crop) {
    uint8_t* items = mod_tables::ItemDatabase();
    uint8_t* record = crop.seed != 0 && items ? mod_tables::FindRecord(items, crop.seed) : nullptr;
    if (record) {
        *reinterpret_cast<uint64_t*>(record + retail::kItemRecordAction) = 0;
    }
}

void SelfCheck() {
    g_Checked = true;
    if (!table_engine::Complete()) {
        Log("Mod crops: the game tables were never seen loading, so mod crops are off.");
        for (const CropDef& crop : g_Crops) {
            Unplant(crop);
        }
        return;
    }
    uint8_t* crops = table_engine::Database(kCropsFile);
    uint8_t* harvests = table_engine::Database(kHarvestFile);
    for (CropDef& crop : g_Crops) {
        const std::string what = "Mod crop " + crop.fullKey + " is id " + std::to_string(crop.id);
        crop.holder = crops ? mod_tables::FindHolder(crops, crop.id) : nullptr;
        crop.baseHolder = crops ? mod_tables::FindHolder(crops, crop.baseId) : nullptr;
        std::string problem;
        if (!mod_rows::CropAppended(crop.id) || !crop.holder || !*crop.holder) {
            problem = "the crops table does not have it";
        } else if (!harvests || !mod_tables::FindRecord(harvests, crop.id)) {
            problem = "the harvest table does not have it";
        } else if (!crop.baseHolder || !*crop.baseHolder) {
            problem = "its base crop " + std::to_string(crop.baseId) + " is missing";
        }
        if (!problem.empty()) {
            Unplant(crop);
            Log(what + ", but " + problem + ". It stays off" + (crop.seed != 0 ? " and its seed plants nothing." : "."));
            continue;
        }
        crop.enabled = true;
        if (crop.seed == 0) {
            Log(what + ", found in the crops and harvest tables, but no item plants it.");
        } else if (!mod_items::Usable(crop.seed)) {
            Log(what + ", found in the crops and harvest tables, but its seed item is off.");
        } else {
            Log(what + ", found in the crops and harvest tables, planted by item " + std::to_string(crop.seed) + ".");
        }
    }
}

// ---- Save splice -----------------------------------------------------------

// Calls visit(key, value) for each node of the unordered_map at map.
template <typename Visit>
void ForEachNode(uint8_t* map, Visit visit) {
    uint8_t* head = ReadAt<uint8_t*>(map, retail::kMapListHead);
    size_t steps = ReadAt<size_t>(map, retail::kMapSize);
    if (steps > kMaxMapNodes) {
        return;
    }
    for (uint8_t* node = head ? ReadAt<uint8_t*>(head, retail::kMapNodeNext) : nullptr;
         node && node != head && steps-- > 0; node = ReadAt<uint8_t*>(node, retail::kMapNodeNext)) {
        visit(ReadAt<uint64_t>(node, retail::kMapNodeKey), ReadAt<uint8_t*>(node, retail::kMapNodeValue));
    }
}

// Calls visit(spot, holderField) for every crop of every cell of every map.
template <typename Visit>
void ForEachCrop(uint8_t* live, Visit visit) {
    ForEachNode(live + retail::kSaveDataMapStatusMap, [&](uint64_t map, uint8_t* status) {
        if (!status) {
            return;
        }
        ForEachNode(status + retail::kMapStatusCells, [&](uint64_t cell, uint8_t* cellStatus) {
            if (!cellStatus) {
                return;
            }
            for (int slot = 0; slot < 2; ++slot) {
                uint8_t* crop =
                    ReadAt<uint8_t*>(cellStatus, slot == 0 ? retail::kCellStatusCrops : retail::kCellStatusSubCrops);
                if (crop) {
                    visit(Spot{map, cell, slot}, reinterpret_cast<uint8_t***>(crop + retail::kCropsStatusData));
                }
            }
        });
    });
}

void Strip(uint8_t* live) {
    size_t swapped = 0;
    size_t stuck = 0;
    ForEachCrop(live, [&](const Spot& spot, uint8_t*** data) {
        const uint64_t id = HolderId(*data);
        if (!IsBand(id)) {
            return;
        }
        const CropDef* crop = FindById(id);
        if (!crop || !crop->baseHolder || !*crop->baseHolder) {
            ++stuck;
            return;
        }
        *data = crop->baseHolder;
        g_Swapped[spot] = crop;
        ++swapped;
    });
    if (swapped > 0) {
        Log("Mod crops: saving " + Plural(swapped, "planted crop", "planted crops") + " as their base crops.");
    }
    if (stuck > 0) {
        Log("Mod crops: " + Plural(stuck, "planted crop has", "planted crops have") +
            " no base crop to save as, so this save needs Canopy and the mod to load.");
    }
}

std::string SpotText(const Spot& spot) {
    return std::to_string(spot.map) + ", " + std::to_string(spot.cell) + ", " + std::to_string(spot.slot);
}

std::string Snapshot(size_t& count) {
    std::vector<std::string> lines;
    for (const auto& [spot, crop] : g_Swapped) {
        lines.push_back(SpotText(spot) + ", " + std::to_string(crop->baseId) + ", " + crop->fullKey);
    }
    lines.insert(lines.end(), g_Carried.begin(), g_Carried.end());
    count = lines.size();
    if (lines.empty()) {
        return {};
    }
    std::string text = "[crops]\r\n";
    for (const std::string& line : lines) {
        text += "crop = " + line + "\r\n";
    }
    return text;
}

void Restore(uint8_t* live) {
    size_t restored = 0;
    if (live && !g_Swapped.empty()) {
        ForEachCrop(live, [&](const Spot& spot, uint8_t*** data) {
            const auto found = g_Swapped.find(spot);
            if (found != g_Swapped.end() && *data == found->second->baseHolder) {
                *data = found->second->holder;
                ++restored;
            }
        });
    }
    if (restored > 0) {
        Log("Mod crops: " + Plural(restored, "planted crop is", "planted crops are") + " back after the save.");
    }
    g_Swapped.clear();
}

// map, cell, slot, base, Mod.Key.
bool ParseEntry(const std::string& value, Entry& entry) {
    entry.raw = value;
    std::vector<std::string> fields;
    size_t start = 0;
    while (fields.size() + 1 < kLineFields) {
        const size_t comma = value.find(',', start);
        if (comma == std::string::npos) {
            return false;
        }
        fields.push_back(canopy_ini::Trim(value.substr(start, comma - start)));
        start = comma + 1;
    }
    entry.fullKey = canopy_ini::Trim(value.substr(start));
    uint64_t slot = 0;
    return mod_tables::ParseU64(fields[0], entry.spot.map) && mod_tables::ParseU64(fields[1], entry.spot.cell) &&
           mod_tables::ParseU64(fields[2], slot) && slot < 2 && (entry.spot.slot = static_cast<int>(slot), true) &&
           mod_tables::ParseU64(fields[3], entry.base) && !entry.fullKey.empty();
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
            canopy_ini::Lower(canopy_ini::Trim(line.substr(0, equals))) != "crop") {
            continue;
        }
        Entry entry;
        if (ParseEntry(canopy_ini::Trim(line.substr(equals + 1)), entry)) {
            g_Loaded.push_back(entry);
        }
    }
}

// Points each loaded spot still holding its base back at its crop. Lines of
// crops that are not installed or are off are kept while their spot holds
// the base; other lines go.
void AfterLoad(uint8_t* live) {
    if (g_Loaded.empty()) {
        return;
    }
    std::map<Spot, uint8_t***> bySpot;
    ForEachCrop(live, [&](const Spot& spot, uint8_t*** data) { bySpot.emplace(spot, data); });
    size_t restored = 0;
    size_t kept = 0;
    size_t gone = 0;
    for (const Entry& entry : g_Loaded) {
        const auto found = bySpot.find(entry.spot);
        if (found == bySpot.end() || HolderId(*found->second) != entry.base) {
            ++gone;
            continue;
        }
        const CropDef* crop = FindByKey(entry.fullKey);
        if (!crop || !crop->enabled || crop->baseId != entry.base) {
            g_Carried.push_back(entry.raw);
            ++kept;
            continue;
        }
        *found->second = crop->holder;
        ++restored;
    }
    Log("Mod crops: restored " + Plural(restored, "planted crop", "planted crops") + " from " + SlotName(g_LoadedSlot) +
        (kept > 0 ? ", kept " + Plural(kept, "line", "lines") + " of crops that are not installed or are off"
                  : std::string()) +
        (gone > 0 ? ", and dropped " + Plural(gone, "line", "lines") + " of crops no longer planted." : "."));
    g_Loaded.clear();
}

void Clear() {
    g_Swapped.clear();
    g_Carried.clear();
    g_Loaded.clear();
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData) {
    std::vector<mod_rows::Crop> crops = mod_rows::Crops();
    if (crops.empty()) {
        return false;
    }
    if (!ticking || !saveData) {
        Log("Mod crops need the frame tick and per-save data, so they are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModCropsRequirements)) {
        Log("Mod crops are off.");
        return false;
    }
    const save_overlay::Adapter adapter = {
        "crops", "Mod crops", "crop", "crops", "Each line: map, cell, slot, base crop, mod.crop.", kOverlayOrder,
        nullptr, &Strip, &Snapshot, &Restore, &Loaded, &AfterLoad, nullptr, &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        Log("Mod crops are off, the overlay save could not start.");
        return false;
    }
    size_t seeds = 0;
    for (const mod_rows::Crop& crop : crops) {
        std::string why;
        if (crop.seed != 0 && !mod_items::MakeSeed(crop.seed, crop.id, why)) {
            mod_rows::DropCrop(crop.fullKey, why);
            continue;
        }
        seeds += crop.seed != 0 ? 1 : 0;
        CropDef def;
        static_cast<mod_rows::Crop&>(def) = crop;
        g_Crops.push_back(def);
    }
    if (g_Crops.empty()) {
        Log("No usable mod crop, so mod crops are off.");
        return true;
    }
    g_Active = true;
    Log("Mod crops installed: " + Plural(g_Crops.size(), "crop", "crops") + ", " + Plural(seeds, "seed", "seeds") +
        ".");
    return true;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

}  // namespace mod_crops
