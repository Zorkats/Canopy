#define NOMINMAX
#include "mod_shops.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "canopy_ini.h"
#include "core.h"
#include "mod_items.h"
#include "mod_tables.h"
#include "save_overlay.h"
#include "table_engine.h"
#include "table_schema.h"

// =============================================================
// MOD SHOPS DESIGN
// =============================================================
// A [sale.<Key>] is a storesales row that mod_rows appends; the game lists it
// by itself, since every day it rebuilds each shop's list from the rows of
// its storeID whose seasons and flags hold. The list is part of the save:
// each shop is a CStoreStatus whose saleItems_ holds one CSaleItemInfo per
// entry, by value, with pointers to the item and storesales rows that the
// save writes as their ids. A load of an id a table lacks leaves the pointer
// null, and the list sort reads every entry's storesales pointer, so a save
// holding a mod sale crashes the game without Canopy. The same goes for a
// vanilla sale patched to sell a mod item.
//
// Sales are one adapter of the overlay save core. The strip takes every mod
// entry out of the lists, an entry whose storesales row is in the row band or
// whose item is a mod item, and keeps its bytes with its shop and index; the
// restore puts the bytes back at their indices. The sidecar
// slot<N>.sales.ini keeps each one's shop, index, storesales id and its
// stock, rank, limit day, size and silhouette flag. Once a load is live, each
// line whose row and item are there again is rebuilt and put back, unless
// the list already has that row; the rest are dropped, since the next day's
// rebuild makes the list again anyway. Continue mode reads the stripped save
// data back in, so the last lines are put back after it too.
//
// Memory. Restoring stays within each list's capacity. A rebuilt entry may
// need more, so the list grows through the game's own std::allocator, and
// the entries, which own nothing, are moved by copying their bytes. A list
// whose entries do not have the CSaleItemInfo vtable is left alone.
//
// Threads. Everything here runs on the game thread.

namespace mod_shops {
namespace {

using namespace core;
using mod_tables::Plural;
namespace ts = table_schema;

constexpr const char* kSalesFile = "storesales.dat";
constexpr size_t kMaxSidecarLines = 4096;
// Far more than the game has.
constexpr size_t kMaxShops = 256;
constexpr size_t kMaxEntries = 4096;
// Before objects (90) and items, the highest.
constexpr int kOverlayOrder = 80;
constexpr size_t kLineFields = 8;

using VectorAllocate = void*(__fastcall*)(size_t bytes);
using VectorDeallocate = void(__fastcall*)(void* pointer, size_t bytes);

// One mod entry of a shop list: its shop, its index before the strip, its
// storesales id, and its bytes, which hold the stock and the other numbers.
// The pointers in bytes are live only for an entry the strip took.
struct Entry {
    int32_t store = 0;
    size_t index = 0;
    uint64_t saleId = 0;
    uint8_t bytes[retail::kSaleItemSize] = {};
};

// Taken by the strip, put back by the restore.
std::vector<Entry> g_Stripped;
// Read from the slot's file by a load.
std::vector<Entry> g_Loaded;
int32_t g_LoadedSlot = 0;
// The entries of the last save written or loaded, for continue mode.
std::vector<Entry> g_Last;

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

uintptr_t Vtable() {
    return g_ModuleBase + retail::kSaleItemInfoVtable;
}

// The id of the row a pointer field of an entry points at, 0 for none.
uint64_t PointedId(const uint8_t* entry, size_t field) {
    uint8_t** holder = ReadAt<uint8_t**>(entry, field);
    return holder && *holder ? ReadAt<uint64_t>(*holder, 0) : 0;
}

bool IsModEntry(const uint8_t* entry) {
    const uint64_t sale = PointedId(entry, retail::kSaleItemSalesData);
    return (sale >= ts::kRowBandStart && sale < ts::kRowBandStart + ts::kRowBandSize) ||
           mod_items::IsModItem(PointedId(entry, retail::kSaleItemData));
}

// Calls visit(status) for every shop of the save data.
template <typename Visit>
void ForEachShop(uint8_t* live, Visit visit) {
    uint8_t* list = live + retail::kSaveDataStoreList;
    uint8_t** begin = ReadAt<uint8_t**>(list, 0);
    uint8_t** end = ReadAt<uint8_t**>(list, 8);
    if (!begin || end < begin || static_cast<size_t>(end - begin) > kMaxShops) {
        return;
    }
    for (uint8_t** at = begin; at != end; ++at) {
        if (*at) {
            visit(*at);
        }
    }
}

// The entries of a shop list: begin, count and capacity. False for a list
// that does not look like one.
bool ListOf(uint8_t* status, uint8_t*& begin, size_t& count, size_t& capacity) {
    uint8_t* list = status + retail::kStoreStatusSales;
    begin = ReadAt<uint8_t*>(list, 0);
    uint8_t* end = ReadAt<uint8_t*>(list, 8);
    uint8_t* last = ReadAt<uint8_t*>(list, 16);
    if (!begin) {
        count = capacity = 0;
        return !end && !last;
    }
    if (end < begin || last < end || (end - begin) % retail::kSaleItemSize != 0 ||
        (last - begin) % retail::kSaleItemSize != 0) {
        return false;
    }
    count = static_cast<size_t>(end - begin) / retail::kSaleItemSize;
    capacity = static_cast<size_t>(last - begin) / retail::kSaleItemSize;
    return count <= kMaxEntries;
}

// True when every entry of every list has the CSaleItemInfo vtable.
bool ListsLookRight(uint8_t* live) {
    bool right = true;
    ForEachShop(live, [&](uint8_t* status) {
        uint8_t* begin = nullptr;
        size_t count = 0;
        size_t capacity = 0;
        if (!ListOf(status, begin, count, capacity)) {
            right = false;
            return;
        }
        for (size_t index = 0; index < count; ++index) {
            if (ReadAt<uintptr_t>(begin + index * retail::kSaleItemSize, 0) != Vtable()) {
                right = false;
            }
        }
    });
    return right;
}

// Puts entries, lowest index first, into the shop list at their indices, or
// at its end when it is shorter, growing it when it is full. False, with
// nothing changed, when it cannot grow.
bool Insert(uint8_t* status, std::vector<const Entry*> entries) {
    uint8_t* begin = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    if (entries.empty() || !ListOf(status, begin, count, capacity)) {
        return entries.empty();
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry* left, const Entry* right) {
        return left->index < right->index;
    });
    constexpr size_t kSize = retail::kSaleItemSize;
    uint8_t* list = status + retail::kStoreStatusSales;
    const size_t needed = count + entries.size();
    if (needed > capacity) {
        auto allocate = GameFunction<VectorAllocate>(retail::kVectorAllocate);
        auto deallocate = GameFunction<VectorDeallocate>(retail::kVectorDeallocate);
        uint8_t* grown = static_cast<uint8_t*>(allocate(needed * kSize));
        if (!grown) {
            return false;
        }
        if (count > 0) {
            std::memcpy(grown, begin, count * kSize);
        }
        if (begin) {
            deallocate(begin, capacity * kSize);
        }
        begin = grown;
        capacity = needed;
        WriteAt<uint8_t*>(list, 0, begin);
        WriteAt<uint8_t*>(list, 16, begin + capacity * kSize);
    }
    for (const Entry* entry : entries) {
        const size_t at = std::min(entry->index, count);
        std::memmove(begin + (at + 1) * kSize, begin + at * kSize, (count - at) * kSize);
        std::memcpy(begin + at * kSize, entry->bytes, kSize);
        ++count;
    }
    WriteAt<uint8_t*>(list, 8, begin + count * kSize);
    return true;
}

// The storesales ids a shop list holds.
std::vector<uint64_t> SaleIds(uint8_t* status) {
    std::vector<uint64_t> ids;
    uint8_t* begin = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    if (ListOf(status, begin, count, capacity)) {
        for (size_t index = 0; index < count; ++index) {
            ids.push_back(PointedId(begin + index * retail::kSaleItemSize, retail::kSaleItemSalesData));
        }
    }
    return ids;
}

// ---- Save splice ---------------------------------------------------------

void Strip(uint8_t* live) {
    size_t taken = 0;
    size_t odd = 0;
    ForEachShop(live, [&](uint8_t* status) {
        uint8_t* begin = nullptr;
        size_t count = 0;
        size_t capacity = 0;
        if (!ListOf(status, begin, count, capacity)) {
            ++odd;
            return;
        }
        constexpr size_t kSize = retail::kSaleItemSize;
        size_t kept = 0;
        for (size_t index = 0; index < count; ++index) {
            uint8_t* entry = begin + index * kSize;
            if (!IsModEntry(entry)) {
                if (kept != index) {
                    std::memmove(begin + kept * kSize, entry, kSize);
                }
                ++kept;
                continue;
            }
            Entry stripped;
            stripped.store = ReadAt<int32_t>(status, retail::kStoreStatusId);
            stripped.index = index;
            stripped.saleId = PointedId(entry, retail::kSaleItemSalesData);
            std::memcpy(stripped.bytes, entry, kSize);
            g_Stripped.push_back(stripped);
            ++taken;
        }
        WriteAt<uint8_t*>(status + retail::kStoreStatusSales, 8, begin + kept * kSize);
    });
    if (taken > 0) {
        Log("Mod shops: moved " + Plural(taken, "sale", "sales") + " out of the save while it is written.");
    }
    if (odd > 0) {
        Log("Mod shops: " + Plural(odd, "shop list does", "shop lists do") +
            " not look as expected and stayed as they are, so this save may need Canopy and the mods to load.");
    }
}

std::string Snapshot(size_t& count) {
    count = g_Stripped.size();
    if (g_Stripped.empty()) {
        return {};
    }
    std::string text = "[sales]\r\n";
    for (const Entry& entry : g_Stripped) {
        text += "sale = " + std::to_string(entry.store) + ", " + std::to_string(entry.index) + ", " +
                std::to_string(entry.saleId) + ", " +
                std::to_string(ReadAt<int32_t>(entry.bytes, retail::kSaleItemStock)) + ", " +
                std::to_string(ReadAt<int32_t>(entry.bytes, retail::kSaleItemRank)) + ", " +
                std::to_string(ReadAt<int32_t>(entry.bytes, retail::kSaleItemLimitDay)) + ", " +
                std::to_string(ReadAt<int32_t>(entry.bytes, retail::kSaleItemSizeValue)) + ", " +
                std::to_string(ReadAt<uint8_t>(entry.bytes, retail::kSaleItemSilhouette)) + "\r\n";
    }
    return text;
}

void Restore(uint8_t* live) {
    size_t restored = 0;
    if (live && !g_Stripped.empty()) {
        std::map<int32_t, std::vector<const Entry*>> byShop;
        for (const Entry& entry : g_Stripped) {
            byShop[entry.store].push_back(&entry);
        }
        ForEachShop(live, [&](uint8_t* status) {
            const auto found = byShop.find(ReadAt<int32_t>(status, retail::kStoreStatusId));
            if (found != byShop.end() && Insert(status, found->second)) {
                restored += found->second.size();
                byShop.erase(found);
            }
        });
    }
    if (restored > 0) {
        Log("Mod shops: " + Plural(restored, "sale is", "sales are") + " back after the save.");
    }
    if (restored < g_Stripped.size()) {
        Log("Mod shops: " + Plural(g_Stripped.size() - restored, "sale", "sales") +
            " could not go back after the save, and the next day lists them again.");
    }
    g_Last = std::move(g_Stripped);
    g_Stripped.clear();
}

bool ParseLine(const std::string& value, Entry& entry) {
    const std::vector<std::string> parts = mod_rules::SplitList(value);
    if (parts.size() != kLineFields) {
        return false;
    }
    uint64_t index = 0;
    int32_t numbers[6] = {};
    const size_t fields[] = {retail::kSaleItemStock, retail::kSaleItemRank, retail::kSaleItemLimitDay,
                             retail::kSaleItemSizeValue};
    if (!mod_rules::ParseI32(parts[0], numbers[0]) || !mod_tables::ParseU64(parts[1], index) ||
        index > kMaxEntries || !mod_tables::ParseU64(parts[2], entry.saleId) || entry.saleId == 0) {
        return false;
    }
    for (size_t field = 0; field < 5; ++field) {
        if (!mod_rules::ParseI32(parts[3 + field], numbers[1 + field])) {
            return false;
        }
    }
    entry.store = numbers[0];
    entry.index = static_cast<size_t>(index);
    for (size_t field = 0; field < std::size(fields); ++field) {
        WriteAt<int32_t>(entry.bytes, fields[field], numbers[1 + field]);
    }
    WriteAt<uint8_t>(entry.bytes, retail::kSaleItemSilhouette, numbers[5] != 0 ? 1 : 0);
    return true;
}

void Loaded(int32_t slot, save_overlay::FileState state, const std::string& text) {
    g_Loaded.clear();
    g_Last.clear();
    g_LoadedSlot = slot;
    if (state != save_overlay::FileState::Read) {
        return;
    }
    size_t start = 0;
    size_t bad = 0;
    while (start < text.size() && g_Loaded.size() < kMaxSidecarLines) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = canopy_ini::Trim(text.substr(start, end - start));
        start = end + 1;
        const size_t equals = line.find('=');
        if (line.empty() || line[0] == ';' || line[0] == '[' || equals == std::string::npos ||
            canopy_ini::Lower(canopy_ini::Trim(line.substr(0, equals))) != "sale") {
            continue;
        }
        Entry entry;
        if (ParseLine(canopy_ini::Trim(line.substr(equals + 1)), entry)) {
            g_Loaded.push_back(entry);
        } else {
            ++bad;
        }
    }
    if (bad > 0) {
        Log("Mod shops: " + Plural(bad, "line", "lines") + " of " + SlotName(slot) + "'s sales file could not be read.");
    }
}

// Rebuilds each entry whose storesales row and item are there, and puts it
// back in its shop's list unless the list has its row already. Returns how
// many went back; dropped counts the rest.
size_t Apply(uint8_t* live, const std::vector<Entry>& entries, size_t& dropped) {
    dropped = 0;
    if (entries.empty()) {
        return 0;
    }
    uint8_t* sales = table_engine::Database(kSalesFile);
    uint8_t* items = mod_tables::ItemDatabase();
    if (!sales || !items || !ListsLookRight(live)) {
        dropped = entries.size();
        return 0;
    }
    std::vector<Entry> built;
    for (const Entry& entry : entries) {
        uint8_t** sale = mod_tables::FindHolder(sales, entry.saleId);
        const uint64_t itemId = sale && *sale ? ReadAt<uint64_t>(*sale, retail::kStoreSalesItem) : 0;
        uint8_t** item = itemId != 0 ? mod_tables::FindHolder(items, itemId) : nullptr;
        if (!item || !*item || !mod_items::Usable(itemId)) {
            ++dropped;
            continue;
        }
        Entry rebuilt = entry;
        WriteAt<uintptr_t>(rebuilt.bytes, 0, Vtable());
        WriteAt<uint64_t>(rebuilt.bytes, 8, 0);
        WriteAt<uint8_t**>(rebuilt.bytes, retail::kSaleItemData, item);
        WriteAt<uint8_t**>(rebuilt.bytes, retail::kSaleItemSalesData, sale);
        built.push_back(rebuilt);
    }
    std::map<int32_t, std::vector<const Entry*>> byShop;
    for (const Entry& entry : built) {
        byShop[entry.store].push_back(&entry);
    }
    size_t applied = 0;
    ForEachShop(live, [&](uint8_t* status) {
        const auto found = byShop.find(ReadAt<int32_t>(status, retail::kStoreStatusId));
        if (found == byShop.end()) {
            return;
        }
        const std::vector<uint64_t> listed = SaleIds(status);
        std::vector<const Entry*> missing;
        for (const Entry* entry : found->second) {
            if (std::find(listed.begin(), listed.end(), entry->saleId) == listed.end()) {
                missing.push_back(entry);
            }
        }
        if (Insert(status, missing)) {
            applied += missing.size();
        }
        byShop.erase(found);
    });
    dropped = entries.size() - applied;
    return applied;
}

void AfterLoad(uint8_t* live) {
    if (g_Loaded.empty()) {
        return;
    }
    size_t dropped = 0;
    const size_t applied = Apply(live, g_Loaded, dropped);
    Log("Mod shops: put back " + Plural(applied, "sale", "sales") + " from " + SlotName(g_LoadedSlot) +
        (dropped > 0 ? ", and left out " + Plural(dropped, "sale", "sales") +
                           " whose row or item is gone or already listed; the next day lists the shops again."
                     : "."));
    g_Last = std::move(g_Loaded);
    g_Loaded.clear();
}

void AfterInPlace(uint8_t* live) {
    size_t dropped = 0;
    const size_t applied = Apply(live, g_Last, dropped);
    if (applied > 0) {
        Log("Mod shops: put back " + Plural(applied, "sale", "sales") + " after continue.");
    }
}

void Clear() {
    g_Stripped.clear();
    g_Loaded.clear();
    g_Last.clear();
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool saveData, bool needed) {
    if (!needed) {
        return false;
    }
    if (!ticking || !saveData) {
        Log("Mod shops need the frame tick and per-save data, so mod sales are off.");
        return false;
    }
    if (!RequirementsMet(retail::kModShopsRequirements)) {
        Log("Mod shops: the game's list allocator does not match, so mod sales are off.");
        return false;
    }
    const save_overlay::Adapter adapter = {
        "sales",       "Mod shops", "sale",       "sales",
        "Each line: shop, index, storesales id, stock, rank, limit day, size, silhouette.",
        kOverlayOrder, nullptr,     &Strip,       &Snapshot,
        &Restore,      &Loaded,     &AfterLoad,   &AfterInPlace,
        &Clear,
    };
    if (!save_overlay::Register(adapter)) {
        Log("Mod shops are off, the overlay save could not start, so mod sales are off.");
        return false;
    }
    Log("Mod shops installed: shop lists keep mod sales out of the save.");
    return true;
}

}  // namespace mod_shops
