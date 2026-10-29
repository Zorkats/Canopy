#define NOMINMAX
#include "save_overlay.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

#include "core.h"
#include "save_data.h"

// =============================================================
// OVERLAY SAVE DESIGN
// =============================================================
// Mod state such as custom items and mod flags lives in the live save data
// while a game runs, so the game's own code sees it, but the game's save
// files never hold it. Each kind is an adapter; the core here runs their
// shared lifecycle. With no adapter registered, nothing is hooked.
//
// Save. The save task's prepare step 0x144F00 runs on the game thread
// before the save data is written. Its post-hook strips every adapter in
// ascending order and takes each one's file text right after its strip, so
// an adapter that strips later can still be seen by an earlier one's text.
// The write 0x4752B0 runs on a worker; its post-hook only raises a flag, and
// the next frame restores every adapter in descending order. The save
// utility's acknowledge (save_data.cpp) is the second trigger: a finished
// save restores at once if the frame has not yet. The last fallback is a
// save that never wrote: after 30 seconds, once the player has the field
// again, the state goes back.
//
// Files. When the acknowledge reports a good save of a known slot, each
// adapter's text as of the strip goes to mods\CanopyData\slot<N>.<name>.ini
// through a temporary file; empty text deletes the old file. A failed save
// writes nothing. A deleted slot deletes every adapter's file.
//
// Load. A good load is acknowledged before the loaded save data is
// installed: the save utility's callback runs the acknowledge first, then
// the success callback that installs the save data and calls the post-load
// fixup 0x46DB80. The acknowledge only queues; the frame tick, or the
// fixup's post-hook when it runs on the game thread, reads every adapter's
// file and hands it over.
// Once both the files are read and the save data is installed, in either
// order, each adapter's afterLoad runs once. The fixup's rcx is not the save
// data, so the live one is read. If the frame's save loaded event comes
// with only one half seen, the other is taken as done, so a load is never
// left half applied.
//
// Reset 0x158920 fills a fresh save data with the new game state, at boot
// and at the title; its post-hook gives each adapter the same save data.
// Continue mode 0x2B8A40 reads a snapshot of the live save data back in
// place; the snapshot was taken unstripped, so adapters resync from it.
// The title screen clears every adapter's state.
//
// Threads. Everything runs on the game thread except the write flag, the
// fixup flag and the acknowledge queue, which are atomic or locked.

namespace save_overlay {
namespace {

using namespace core;

using ResetSaveDataFn = void*(__fastcall*)(uint8_t* saveData);
using PrepareForSaveFn = uint64_t(__fastcall*)(uint8_t* saveData);
using SerializeSaveFn = uint8_t(__fastcall*)(void* function);
using AfterLoadFixupFn = int64_t(__fastcall*)(void* unused);
using LoadContinueFn = int64_t(__fastcall*)(void* continueData);

// About 30 seconds: a save whose write never came is given up on, once the
// player has the field again.
constexpr uint64_t kSaveTimeoutMs = 30000;
// Adapter names that may have files, registered or not, so a deleted slot
// leaves none behind.
constexpr const char* kKnownNames[] = {"items", "flags", "villagers", "recipes", "objects", "sales", "crops"};

struct SlotEvent {
    int32_t operation;
    int32_t slot;
    bool ok;
};

struct Snapshot {
    std::string text;
    size_t count = 0;
};

std::vector<Adapter> g_Adapters;
DetourSet g_Hooks("Overlay save");
bool g_Hooked = false;

ResetSaveDataFn g_OriginalReset = nullptr;
PrepareForSaveFn g_OriginalPrepare = nullptr;
SerializeSaveFn g_OriginalSerialize = nullptr;
AfterLoadFixupFn g_OriginalAfterLoad = nullptr;
LoadContinueFn g_OriginalContinue = nullptr;

std::atomic<DWORD> g_GameThread{0};

// Save, game thread.
bool g_Saving = false;
uint64_t g_SaveStarted = 0;
bool g_LoggedBusy = false;
std::atomic<bool> g_Serialized{false};
std::vector<Snapshot> g_Snapshots;
bool g_SnapshotReady = false;

// Load, game thread.
bool g_LoadRead = false;
bool g_LoadInstalled = false;
std::atomic<bool> g_InstalledSignal{false};

std::mutex g_QueueMutex;
std::deque<SlotEvent> g_Queue;

// ---- Small helpers -------------------------------------------------------

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

std::string Count(const Adapter& adapter, size_t count) {
    return std::to_string(count) + " " + (count == 1 ? adapter.one : adapter.many);
}

// The label inside a sentence: "mod items".
std::string Title(const Adapter& adapter) {
    std::string title = adapter.label;
    if (!title.empty()) {
        title[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(title[0])));
    }
    return title;
}

std::string FilePath(const char* name, int32_t slot) {
    return ModFilePath(("mods\\CanopyData\\slot" + std::to_string(slot) + "." + name + ".ini").c_str());
}

uint64_t HashKey(uint64_t key) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (int byte = 0; byte < 8; ++byte) {
        hash = (hash ^ ((key >> (8 * byte)) & 0xFF)) * 0x100000001B3ull;
    }
    return hash;
}

// The bucket of key: the first and last node of its run in the list.
uint8_t* Bucket(uint8_t* map, uint64_t key) {
    uint8_t* buckets = ReadAt<uint8_t*>(map, retail::kMapBuckets);
    if (!buckets) {
        return nullptr;
    }
    return buckets + 16 * (HashKey(key) & ReadAt<uint64_t>(map, retail::kMapMask));
}

// ---- Save ----------------------------------------------------------------

__declspec(noinline) void OnPrepareForSave(uint8_t* saveData) {
    try {
        g_Serialized = false;
        g_Saving = true;
        g_SaveStarted = GetTickCount64();
        g_LoggedBusy = false;
        g_SnapshotReady = false;
        const bool live = saveData && saveData == LiveSaveData();
        g_Snapshots.assign(g_Adapters.size(), Snapshot{});
        for (size_t index = 0; index < g_Adapters.size(); ++index) {
            const Adapter& adapter = g_Adapters[index];
            if (live && adapter.strip) {
                adapter.strip(saveData);
            }
            if (adapter.snapshot) {
                g_Snapshots[index].text = adapter.snapshot(g_Snapshots[index].count);
            }
        }
        g_SnapshotReady = true;
    } catch (...) {
        Log("Overlay save: preparing the save failed.");
    }
}

uint64_t __fastcall Detour_PrepareForSave(uint8_t* saveData) {
    const uint64_t result = g_OriginalPrepare(saveData);
    OnPrepareForSave(saveData);
    return result;
}

uint8_t __fastcall Detour_SerializeSave(void* function) {
    const uint8_t result = g_OriginalSerialize(function);
    g_Serialized = true;
    return result;
}

void RestoreAll() {
    uint8_t* live = LiveSaveData();
    for (auto adapter = g_Adapters.rbegin(); adapter != g_Adapters.rend(); ++adapter) {
        if (adapter->restore) {
            adapter->restore(live);
        }
    }
    g_Saving = false;
    g_Serialized = false;
}

// Writes or deletes one adapter's file for a good save of slot.
void Commit(const Adapter& adapter, const Snapshot& snapshot, int32_t slot) {
    const std::string path = FilePath(adapter.name, slot);
    if (snapshot.text.empty()) {
        if (DeleteFileA(path.c_str())) {
            Log(
                std::string(adapter.label) + ": " + SlotName(slot) + " saved with none, its old " + adapter.one +
                " file was deleted."
            );
        }
        return;
    }
    const std::string text = "; Canopy " + Title(adapter) + " for save slot " + std::to_string(slot) + ". " + adapter.format +
                             "\r\n\r\n" + snapshot.text;
    CreateDirectoryA(ModFilePath("mods\\CanopyData").c_str(), nullptr);
    if (const uint32_t error = save_data::WriteFileAtomically(path, text)) {
        Log(
            std::string(adapter.label) + ": " + SlotName(slot) + " " + adapter.one + " file could not be written, error " +
            std::to_string(error) + "."
        );
        return;
    }
    Log(std::string(adapter.label) + ": wrote " + SlotName(slot) + ", " + Count(adapter, snapshot.count) + ".");
}

// ---- Load ----------------------------------------------------------------

// Runs each adapter's afterLoad once the files are read and the loaded save
// data is live.
void TryAfterLoad() {
    if (!g_LoadRead || !g_LoadInstalled) {
        return;
    }
    g_LoadRead = false;
    g_LoadInstalled = false;
    uint8_t* live = LiveSaveData();
    if (!live) {
        return;
    }
    for (const Adapter& adapter : g_Adapters) {
        if (adapter.afterLoad) {
            adapter.afterLoad(live);
        }
    }
}

// Hands every adapter its file for a good load. slot 0 reads none.
void ReadFiles(int32_t slot) {
    for (const Adapter& adapter : g_Adapters) {
        if (!adapter.loaded) {
            continue;
        }
        FileState state = FileState::Missing;
        std::string text;
        const std::string path = FilePath(adapter.name, slot);
        if (slot > 0 && GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (save_data::ReadFileText(path, text)) {
                state = FileState::Read;
            } else {
                state = FileState::Unreadable;
                text.clear();
                Log(
                    std::string(adapter.label) + ": " + SlotName(slot) + " " + adapter.one +
                    " file could not be read, its " + adapter.many + " are not restored."
                );
            }
        }
        adapter.loaded(slot, state, text);
    }
    g_LoadRead = true;
}

void HandleSlot(const SlotEvent& event) {
    const bool validSlot = event.slot >= 1 && event.slot <= save_data::kMaxSlot;
    if (event.operation == retail::kSaveUtilOperationSave) {
        if (g_Saving) {
            RestoreAll();
        }
        for (size_t index = 0; g_SnapshotReady && index < g_Adapters.size() && index < g_Snapshots.size(); ++index) {
            const Adapter& adapter = g_Adapters[index];
            if (!adapter.snapshot) {
                continue;
            }
            if (!event.ok) {
                Log(std::string(adapter.label) + ": the save failed, so no " + adapter.one + " file was written.");
            } else if (!validSlot) {
                Log(
                    std::string(adapter.label) + ": a save finished with an unknown slot, so no " + adapter.one +
                    " file was written."
                );
            } else {
                Commit(adapter, g_Snapshots[index], event.slot);
            }
        }
        g_SnapshotReady = false;
        g_Snapshots.clear();
    } else if (event.operation == retail::kSaveUtilOperationLoad && event.ok) {
        if (!validSlot) {
            Log("Overlay save: a load finished with an unknown slot, so no mod file was read.");
        }
        ReadFiles(validSlot ? event.slot : 0);
        TryAfterLoad();
    } else if (event.operation == retail::kSaveUtilOperationDelete && event.ok && validSlot) {
        std::vector<const char*> names(std::begin(kKnownNames), std::end(kKnownNames));
        for (const Adapter& adapter : g_Adapters) {
            if (std::none_of(names.begin(), names.end(), [&](const char* name) {
                    return std::strcmp(name, adapter.name) == 0;
                })) {
                names.push_back(adapter.name);
            }
        }
        for (const char* name : names) {
            if (DeleteFileA(FilePath(name, event.slot).c_str())) {
                const auto adapter = std::find_if(g_Adapters.begin(), g_Adapters.end(), [&](const Adapter& entry) {
                    return std::strcmp(entry.name, name) == 0;
                });
                Log(
                    adapter != g_Adapters.end()
                        ? std::string(adapter->label) + ": deleted the " + adapter->one + " file of " +
                              SlotName(event.slot) + "."
                        : "Overlay save: deleted the " + std::string(name) + " file of " + SlotName(event.slot) + "."
                );
            }
        }
    }
}

void OnSlot(int32_t operation, int32_t slot, bool ok) {
    std::lock_guard<std::mutex> lock(g_QueueMutex);
    g_Queue.push_back({operation, slot, ok});
}

void Drain() {
    std::deque<SlotEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_QueueMutex);
        events.swap(g_Queue);
    }
    for (const SlotEvent& event : events) {
        HandleSlot(event);
    }
    if (g_InstalledSignal.exchange(false)) {
        g_LoadInstalled = true;
        TryAfterLoad();
    }
}

__declspec(noinline) void OnInstalled() {
    try {
        Drain();
    } catch (...) {
        Log("Overlay save: applying the loaded save failed.");
    }
}

int64_t __fastcall Detour_AfterLoadFixup(void* unused) {
    const int64_t result = g_OriginalAfterLoad(unused);
    g_InstalledSignal = true;
    if (GetCurrentThreadId() == g_GameThread.load()) {
        OnInstalled();
    }
    return result;
}

// ---- Reset and continue mode ---------------------------------------------

__declspec(noinline) void OnReset(uint8_t* saveData) {
    try {
        for (const Adapter& adapter : g_Adapters) {
            if (adapter.reset) {
                adapter.reset(saveData);
            }
        }
    } catch (...) {
        Log("Overlay save: resetting the new game state failed.");
    }
}

void* __fastcall Detour_ResetSaveData(uint8_t* saveData) {
    void* result = g_OriginalReset(saveData);
    if (saveData) {
        OnReset(saveData);
    }
    return result;
}

__declspec(noinline) void OnContinue() {
    try {
        uint8_t* live = LiveSaveData();
        if (!live) {
            return;
        }
        for (const Adapter& adapter : g_Adapters) {
            if (adapter.afterInPlace) {
                adapter.afterInPlace(live);
            }
        }
    } catch (...) {
        Log("Overlay save: resyncing after continue mode failed.");
    }
}

int64_t __fastcall Detour_LoadContinue(void* continueData) {
    const int64_t result = g_OriginalContinue(continueData);
    OnContinue();
    return result;
}

__declspec(noinline) void TickSafely() {
    try {
        Drain();
        if (g_Saving && g_Serialized.load()) {
            RestoreAll();
        }
        if (g_Saving && !g_Serialized.load() && GetTickCount64() - g_SaveStarted > kSaveTimeoutMs && PlayerInField()) {
            if (!g_LoggedBusy) {
                g_LoggedBusy = true;
                Log("Overlay save: a save started but was never written, so the mod state goes back now.");
            }
            RestoreAll();
            g_SnapshotReady = false;
        }
    } catch (...) {
        Log("Overlay save: the frame step failed.");
    }
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Register(const Adapter& adapter) {
    if (!g_Hooked) {
        if (!RequirementsMet(retail::kSaveOverlayRequirements)) {
            Log("Overlay save is off.");
            return false;
        }
        const auto add = [](uintptr_t rva, auto detour, auto original, const char* name) {
            return g_Hooks.Add(rva, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(original), name);
        };
        if (!add(retail::kResetSaveData, &Detour_ResetSaveData, &g_OriginalReset, "resetSaveData") ||
            !add(retail::kPrepareForSave, &Detour_PrepareForSave, &g_OriginalPrepare, "prepareForSave") ||
            !add(retail::kSerializeSave, &Detour_SerializeSave, &g_OriginalSerialize, "serializeSave") ||
            !add(retail::kAfterLoadFixup, &Detour_AfterLoadFixup, &g_OriginalAfterLoad, "afterLoadFixup") ||
            !add(retail::kLoadContinueInPlace, &Detour_LoadContinue, &g_OriginalContinue, "loadContinueInPlace")) {
            g_Hooks.RemoveAll();
            Log("Overlay save is off, its hooks could not be created.");
            return false;
        }
        save_data::SetSlotCallback(&OnSlot);
        g_Hooked = true;
        Log("Overlay save hooks installed.");
    }
    const auto at = std::upper_bound(g_Adapters.begin(), g_Adapters.end(), adapter, [](const Adapter& left, const Adapter& right) {
        return left.order < right.order;
    });
    g_Adapters.insert(at, adapter);
    Log("Overlay save: " + Title(adapter) + " take part, strip order " + std::to_string(adapter.order) + ".");
    return true;
}

bool Stripped() {
    return g_Saving;
}

void Tick() {
    if (!g_Hooked) {
        return;
    }
    g_GameThread = GetCurrentThreadId();
    TickSafely();
}

void OnEvent(CanopyEvent event) {
    if (!g_Hooked) {
        return;
    }
    if (event == CANOPY_EVENT_TITLE_SCREEN) {
        g_Saving = false;
        g_Serialized = false;
        g_SnapshotReady = false;
        g_Snapshots.clear();
        g_LoadRead = false;
        g_LoadInstalled = false;
        g_InstalledSignal = false;
        for (const Adapter& adapter : g_Adapters) {
            if (adapter.clear) {
                adapter.clear();
            }
        }
    } else if (event == CANOPY_EVENT_SAVE_LOADED && (g_LoadRead || g_LoadInstalled)) {
        if (!g_LoadRead) {
            Log("Overlay save: a save loaded with no finished load reported, so no mod file was read.");
            ReadFiles(0);
        } else if (!g_LoadInstalled) {
            Log("Overlay save: a save loaded without the post-load step, so the mod state is applied now.");
            g_LoadInstalled = true;
        }
        TryAfterLoad();
    }
}

uint8_t* LiveSaveData() {
    uint8_t* manager = ReadGlobal<uint8_t*>(retail::kGameManager);
    return manager ? ReadAt<uint8_t*>(manager, retail::kManagerSaveData) : nullptr;
}

// ---- Shared primitives ---------------------------------------------------

// A bucket's run is walked back from its last node.
uint8_t* MapFind(uint8_t* map, uint64_t key) {
    uint8_t* end = ReadAt<uint8_t*>(map, retail::kMapListHead);
    uint8_t* bucket = Bucket(map, key);
    if (!bucket || !end) {
        return nullptr;
    }
    uint8_t* first = ReadAt<uint8_t*>(bucket, 0);
    uint8_t* node = ReadAt<uint8_t*>(bucket, 8);
    if (node == end) {
        return nullptr;
    }
    size_t steps = ReadAt<size_t>(map, retail::kMapSize) + 1;
    while (node && steps-- > 0) {
        if (ReadAt<uint64_t>(node, retail::kMapNodeKey) == key) {
            return node;
        }
        if (node == first) {
            return nullptr;
        }
        node = ReadAt<uint8_t*>(node, retail::kMapNodePrev);
    }
    return nullptr;
}

// Fixes the bucket, unlinks the node and counts it out, as the game's erase
// does before it frees the node.
bool MapPark(uint8_t* map, uint8_t* node) {
    uint8_t* end = ReadAt<uint8_t*>(map, retail::kMapListHead);
    uint8_t* bucket = Bucket(map, ReadAt<uint64_t>(node, retail::kMapNodeKey));
    if (!bucket || !end) {
        return false;
    }
    uint8_t* first = ReadAt<uint8_t*>(bucket, 0);
    uint8_t* last = ReadAt<uint8_t*>(bucket, 8);
    uint8_t* next = ReadAt<uint8_t*>(node, retail::kMapNodeNext);
    uint8_t* prev = ReadAt<uint8_t*>(node, retail::kMapNodePrev);
    if (first == node && last == node) {
        WriteAt(bucket, 0, end);
        WriteAt(bucket, 8, end);
    } else if (first == node) {
        WriteAt(bucket, 0, next);
    } else if (last == node) {
        WriteAt(bucket, 8, prev);
    }
    WriteAt(prev, retail::kMapNodeNext, next);
    WriteAt(next, retail::kMapNodePrev, prev);
    WriteAt(map, retail::kMapSize, ReadAt<size_t>(map, retail::kMapSize) - 1);
    return true;
}

// Splices the node in front of its bucket's first node. An empty bucket
// holds the list head as its first, so the node then goes to the list end.
bool MapUnpark(uint8_t* map, uint8_t* node) {
    uint8_t* end = ReadAt<uint8_t*>(map, retail::kMapListHead);
    const uint64_t key = ReadAt<uint64_t>(node, retail::kMapNodeKey);
    uint8_t* bucket = Bucket(map, key);
    if (!bucket || !end || MapFind(map, key)) {
        return false;
    }
    uint8_t* first = ReadAt<uint8_t*>(bucket, 0);
    uint8_t* prev = ReadAt<uint8_t*>(first, retail::kMapNodePrev);
    WriteAt(node, retail::kMapNodeNext, first);
    WriteAt(node, retail::kMapNodePrev, prev);
    WriteAt(prev, retail::kMapNodeNext, node);
    WriteAt(first, retail::kMapNodePrev, node);
    WriteAt(bucket, 0, node);
    if (first == end) {
        WriteAt(bucket, 8, node);
    }
    WriteAt(map, retail::kMapSize, ReadAt<size_t>(map, retail::kMapSize) + 1);
    return true;
}

size_t VectorRemove(uint8_t* vector, bool (*match)(const void*), std::vector<VectorEntry>& removed) {
    void** first = ReadAt<void**>(vector, 0);
    void** last = ReadAt<void**>(vector, 8);
    if (!first || last <= first) {
        return 0;
    }
    size_t taken = 0;
    size_t index = 0;
    for (void** element = first; element < last; ++index) {
        if (match(*element)) {
            removed.push_back({index, *element});
            std::memmove(element, element + 1, sizeof(void*) * static_cast<size_t>(last - element - 1));
            --last;
            *last = nullptr;
            ++taken;
        } else {
            ++element;
        }
    }
    WriteAt(vector, 8, last);
    return taken;
}

bool VectorReinsert(uint8_t* vector, std::vector<VectorEntry> entries) {
    if (entries.empty()) {
        return true;
    }
    void** first = ReadAt<void**>(vector, 0);
    void** last = ReadAt<void**>(vector, 8);
    void** capacity = ReadAt<void**>(vector, 16);
    if (!first || last < first || capacity < last || static_cast<size_t>(capacity - last) < entries.size()) {
        return false;
    }
    std::stable_sort(entries.begin(), entries.end(), [](const VectorEntry& left, const VectorEntry& right) {
        return left.index < right.index;
    });
    for (const VectorEntry& entry : entries) {
        const size_t size = static_cast<size_t>(last - first);
        const size_t at = std::min(entry.index, size);
        std::memmove(first + at + 1, first + at, sizeof(void*) * (size - at));
        first[at] = entry.value;
        ++last;
    }
    WriteAt(vector, 8, last);
    return true;
}

}  // namespace save_overlay
