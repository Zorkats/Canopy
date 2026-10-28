#define NOMINMAX
#include "table_engine.h"

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core.h"

// =============================================================
// TABLE ENGINE DESIGN
// =============================================================
// The game registers every CDatabase table with its database manager, then
// loads them in two batches through CDatabaseManager::load 0x69B6B0: the
// framework tables (texture.dat and icon.dat among them) on the main thread
// inside game init, then the game tables on the boot loader thread. load
// hands each table's files to the job system, whose job reads a file and
// calls the table's own load through vtable slot 1 as (db, path, bytes,
// size). Different tables load on different threads at once; the files of
// one table, framework_X.dat then X.dat, load in order on one thread.
//
// Hook. A MinHook detour on the manager's load walks the pending tables
// before the original runs: the pending map's list at manager+8, each node's
// vector of 304-byte elements, the path at element+8 and the CDatabase at
// element+296. For each table vtable not seen yet it keeps slot 1 and writes
// Detour_AnyLoad there, with VirtualProtect around the write. Every slot is
// swapped before the original submits a job, and the kept originals are
// published through an atomic count, so the detour finds them without a
// lock. A slot keeps pointing here after the batch; nothing else calls it.
// The per-table MinHook detours of items, villagers, whistles and recipes
// sit at the start of the same functions, so the kept original lands on
// them and they run after this detour, unchanged.
//
// Load. Detour_AnyLoad takes the file name from the path and remembers the
// table's CDatabase for it. For a file modules rewrite (the patches of
// mod_patches, then the list appends of mod_appends), it first hands the
// parsed vanilla bytes of framework_X.dat and of X.dat to each module in
// the order they registered, each one getting the blob the one before
// returned. A module may return a whole new blob: the same rows in the same
// order, some fields changed or some lists grown, and its texts in a pool
// after the one it was given. A blob that does not parse is dropped and the
// bytes before it go on to the next module. Everything after that sees the
// rewritten rows as the vanilla ones: the append below, and so a [row.]
// row that copies a base row copies the patched values, and the per-table
// detours of items, villagers, whistles and recipes, which run after this
// one and read the rows it passes on: an item whose base row is patched
// starts from the patched price, icon and so on, and a patched vanilla
// item, villager or recipe is what they see as vanilla.
//
// For a file a module contributes to, it parses
// the bytes, lets the module append rows, and calls the original with the
// vanilla rows, then the new rows, then both string pools, in a buffer it
// frees once the original returns: the game copies the pool and the rows
// out and never keeps the pointer. Rows only go into X.dat, never into its
// framework_ twin. Any other file passes through untouched.
//
// Watch. For a watched file the ids and enum names of framework_X.dat and
// X.dat, the added rows included, are kept under a lock, and the file is
// marked known once X.dat has loaded, so other tables' rows can check their
// references against it.
//
// Late start. When the first batch the detour sees holds no framework_
// table, Canopy started too late for the tables that load inside game init,
// so the engine logs once and leaves every table alone.

namespace table_engine {
namespace {

using namespace core;

using ManagerLoadFn = uint64_t(__fastcall*)(uint8_t* manager, void* queue);
using LoadFn = int64_t(__fastcall*)(uint8_t* db, const char* path, const uint8_t* blob, uint32_t size);

constexpr const char* kFrameworkPrefix = "framework_";
// About 250 table files load in all; these bound a walk of a bad layout.
constexpr size_t kMaxSlots = 512;
constexpr size_t kMaxPending = 1024;
constexpr size_t kMaxNodes = 1024;
constexpr size_t kPathCapacity = 128;
// Every row starts with its u64 id and its enum name.
constexpr size_t kRowEnum = 8;

struct Slot {
    const void* vtable;
    LoadFn original;
};

struct Pending {
    uint8_t* db;
    void* vtable;
    char path[kPathCapacity];
};

struct FileState {
    // Lowercase, without the folder: texture.dat.
    std::string name;
    // Read-only once the hooks are enabled.
    std::vector<AppendFn> appends;
    std::vector<RewriteFn> rewrites;
    std::atomic<bool> known{false};
    // Under g_Mutex.
    std::unordered_set<uint64_t> ids;
    std::unordered_map<std::string, uint64_t> enums;
    std::unordered_set<uint32_t> crcs;
};

ManagerLoadFn g_OriginalManagerLoad = nullptr;
int g_InstallState = 0;
Slot g_Slots[kMaxSlots];
std::atomic<size_t> g_SlotCount{0};
std::atomic<int> g_Batches{0};
std::atomic<bool> g_Missed{false};
// Fixed at startup, before the hooks are enabled.
std::vector<std::unique_ptr<FileState>> g_Files;
// Guards every FileState's sets, g_Databases and g_FileBatches.
std::mutex g_Mutex;
std::unordered_map<std::string, uint8_t*> g_Databases;
std::unordered_map<std::string, int> g_FileBatches;
// The two batches run one after the other, never at once.
Pending g_Pending[kMaxPending];
uintptr_t g_ImageEnd = 0;

std::string Lower(const char* text) {
    std::string lowered = text ? text : "";
    for (char& c : lowered) {
        c = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return lowered;
}

// "data/database/framework_Texture.dat" is texture.dat, a framework file.
std::string FileName(const char* path, bool& framework) {
    const char* part = path ? path : "";
    for (const char* c = part; *c; ++c) {
        if (*c == '/' || *c == '\\') {
            part = c + 1;
        }
    }
    std::string name = Lower(part);
    const size_t prefix = std::strlen(kFrameworkPrefix);
    framework = name.compare(0, prefix, kFrameworkPrefix) == 0;
    return framework ? name.substr(prefix) : name;
}

FileState* FindFile(const std::string& name) {
    for (const std::unique_ptr<FileState>& file : g_Files) {
        if (file->name == name) {
            return file.get();
        }
    }
    return nullptr;
}

FileState* AddFile(const char* file) {
    bool framework = false;
    const std::string name = FileName(file, framework);
    if (FileState* found = FindFile(name)) {
        return found;
    }
    g_Files.push_back(std::make_unique<FileState>());
    g_Files.back()->name = name;
    return g_Files.back().get();
}

bool InImage(const void* address) {
    const uintptr_t value = reinterpret_cast<uintptr_t>(address);
    return value >= g_ModuleBase && value < g_ImageEnd;
}

// ---- Slot swap (the thread that calls the manager's load) ----------------

LoadFn FindOriginal(const void* vtable) {
    const size_t count = g_SlotCount.load(std::memory_order_acquire);
    for (size_t index = 0; index < count; ++index) {
        if (g_Slots[index].vtable == vtable) {
            return g_Slots[index].original;
        }
    }
    return nullptr;
}

int64_t __fastcall Detour_AnyLoad(uint8_t* db, const char* path, const uint8_t* blob, uint32_t size);

// Copies the pending elements of the manager's map into g_Pending. False
// when the walk faults or runs past its bounds, which means the layout is
// not the one this build has.
bool ReadPending(uint8_t* manager, size_t& count) {
    count = 0;
    __try {
        uint8_t* head = *reinterpret_cast<uint8_t**>(manager + retail::kManagerPendingList);
        if (!head) {
            return false;
        }
        uint8_t* node = *reinterpret_cast<uint8_t**>(head);
        for (size_t nodes = 0; node != head; ++nodes) {
            if (!node || nodes >= kMaxNodes) {
                return false;
            }
            uint8_t* element = *reinterpret_cast<uint8_t**>(node + retail::kPendingNodeBegin);
            uint8_t* end = *reinterpret_cast<uint8_t**>(node + retail::kPendingNodeEnd);
            if (end < element || static_cast<size_t>(end - element) % retail::kPendingElementSize != 0) {
                return false;
            }
            for (; element < end; element += retail::kPendingElementSize) {
                if (count == kMaxPending) {
                    return false;
                }
                Pending& pending = g_Pending[count];
                pending.db = *reinterpret_cast<uint8_t**>(element + retail::kPendingElementDatabase);
                pending.vtable = pending.db ? *reinterpret_cast<void**>(pending.db) : nullptr;
                const char* text = *reinterpret_cast<const char**>(element + retail::kPendingElementPath);
                size_t length = 0;
                for (; text && text[length] != '\0' && length + 1 < kPathCapacity; ++length) {
                    pending.path[length] = text[length];
                }
                pending.path[length] = '\0';
                ++count;
            }
            node = *reinterpret_cast<uint8_t**>(node);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        count = 0;
        return false;
    }
}

// Points the vtable's slot 1 here, keeping the original. False when the
// slot does not look like a table's load or cannot be written.
bool SwapSlot(void* vtable) {
    if (!InImage(vtable) || FindOriginal(vtable)) {
        return InImage(vtable);
    }
    void** slot = reinterpret_cast<void**>(static_cast<uint8_t*>(vtable) + retail::kDatabaseLoadSlot);
    void* original = *slot;
    if (original == reinterpret_cast<void*>(&Detour_AnyLoad)) {
        return true;
    }
    const size_t count = g_SlotCount.load(std::memory_order_relaxed);
    if (!InImage(original) || count == kMaxSlots) {
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        return false;
    }
    g_Slots[count] = {vtable, reinterpret_cast<LoadFn>(original)};
    g_SlotCount.store(count + 1, std::memory_order_release);
    *slot = reinterpret_cast<void*>(&Detour_AnyLoad);
    DWORD unused = 0;
    VirtualProtect(slot, sizeof(void*), old, &unused);
    return true;
}

void PrepareBatch(uint8_t* manager) {
    const int batch = g_Batches.fetch_add(1) + 1;
    if (g_Missed.load()) {
        return;
    }
    size_t count = 0;
    if (!ReadPending(manager, count)) {
        Log("Table engine: the pending tables of load " + std::to_string(batch) +
            " could not be read, so no table of that load gets mod rows.");
        return;
    }
    bool framework = false;
    std::vector<std::string> names(count);
    for (size_t index = 0; index < count; ++index) {
        bool isFramework = false;
        names[index] = FileName(g_Pending[index].path, isFramework);
        framework = framework || isFramework;
    }
    if (batch == 1 && !framework) {
        g_Missed = true;
        Log("Table engine: Canopy started after the framework tables loaded, so it stays off and no table gets mod "
            "rows.");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const std::string& name : names) {
            g_FileBatches.emplace(name, batch);
        }
    }
    for (size_t index = 0; index < count; ++index) {
        if (g_Pending[index].vtable && !SwapSlot(g_Pending[index].vtable) && FindFile(names[index])) {
            Log("Table engine: the load of " + names[index] + " could not be hooked, so it gets no mod rows.");
        }
    }
}

__declspec(noinline) void TryPrepareBatch(uint8_t* manager) {
    try {
        PrepareBatch(manager);
    } catch (...) {
        Log("Table engine: preparing a table load failed, so some tables get no mod rows.");
    }
}

uint64_t __fastcall Detour_ManagerLoad(uint8_t* manager, void* queue) {
    if (manager) {
        TryPrepareBatch(manager);
    }
    return g_OriginalManagerLoad(manager, queue);
}

// ---- Table loads (game workers) ------------------------------------------

// The watched file's state for path, or null, and the CDatabase noted.
FileState* Prepare(uint8_t* db, const char* path, bool& framework) {
    const std::string name = FileName(path, framework);
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Databases.emplace(name, db);
    }
    return FindFile(name);
}

// The bytes with every added row, or merged left empty.
void Append(FileState& file, uint8_t* db, const uint8_t* blob, uint32_t size, std::vector<uint8_t>& merged) {
    blob::Table table;
    std::string error;
    if (!blob::Parse(blob, size, table, error)) {
        Log("Table engine: " + file.name + " could not be read (" + error + "), so it gets no mod rows.");
        return;
    }
    blob::Appender appender(table);
    for (const AppendFn append : file.appends) {
        append(file.name.c_str(), db, table, appender);
    }
    if (appender.Count() == 0) {
        return;
    }
    appender.Merge(merged);
    Log("Table engine: " + file.name + " gets " + std::to_string(appender.Count()) +
        (appender.Count() == 1 ? " mod row." : " mod rows."));
}

// The vanilla bytes as the modules rewrite them in turn, or rewritten left
// empty when none did.
void RewriteRows(
    FileState& file, bool framework, const uint8_t* blob, uint32_t size, std::vector<uint8_t>& rewritten
) {
    const std::string name = (framework ? kFrameworkPrefix : "") + file.name;
    blob::Table table;
    std::string error;
    if (!blob::Parse(blob, size, table, error)) {
        Log("Table engine: " + name + " could not be read (" + error + "), so it gets no patches.");
        return;
    }
    for (const RewriteFn rewrite : file.rewrites) {
        std::vector<uint8_t> next;
        rewrite(file.name.c_str(), framework, table, next);
        blob::Table parsed;
        if (next.empty()) {
            continue;
        }
        if (next.size() > UINT32_MAX || !blob::Parse(next.data(), next.size(), parsed, error)) {
            Log("Table engine: the rewritten " + name + " could not be read back, so one module's changes to it are "
                "dropped.");
            continue;
        }
        // table points into rewritten's bytes, so both move together.
        rewritten = std::move(next);
        table = std::move(parsed);
    }
}

// Keeps the ids and enum names the game now has for the file.
void Record(FileState& file, const uint8_t* data, uint32_t size, bool framework) {
    blob::Table table;
    std::string error;
    if (blob::Parse(data, size, table, error)) {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::string name;
        for (const blob::Row& row : table.rows) {
            const uint64_t id = blob::RowId(row);
            file.ids.insert(id);
            if (blob::ReadText(table, row, kRowEnum, name) && !name.empty()) {
                file.crcs.insert(blob::EnumCrc(name.data(), name.size()));
                file.enums.emplace(name, id);
            }
        }
    }
    if (!framework) {
        file.known.store(true, std::memory_order_release);
    }
}

__declspec(noinline) FileState* TryPrepare(uint8_t* db, const char* path, bool& framework) {
    try {
        return Prepare(db, path, framework);
    } catch (...) {
        return nullptr;
    }
}

__declspec(noinline) void TryRewrite(
    FileState& file, bool framework, const uint8_t* blob, uint32_t size, std::vector<uint8_t>& rewritten
) {
    try {
        RewriteRows(file, framework, blob, size, rewritten);
    } catch (...) {
        rewritten.clear();
        Log("Table engine: patching " + file.name + " failed, so it gets no patches.");
    }
}

__declspec(noinline) void TryAppend(
    FileState& file, uint8_t* db, const uint8_t* blob, uint32_t size, std::vector<uint8_t>& merged
) {
    try {
        Append(file, db, blob, size, merged);
    } catch (...) {
        merged.clear();
        Log("Table engine: adding mod rows to " + file.name + " failed, so it gets none.");
    }
}

__declspec(noinline) void TryRecord(FileState& file, const uint8_t* data, uint32_t size, bool framework) {
    try {
        Record(file, data, size, framework);
    } catch (...) {
        file.known.store(!framework, std::memory_order_release);
    }
}

int64_t __fastcall Detour_AnyLoad(uint8_t* db, const char* path, const uint8_t* blob, uint32_t size) {
    // A slot only points here once its original is kept, and db's vtable
    // is the one whose slot was called.
    const LoadFn original = db ? FindOriginal(*reinterpret_cast<void**>(db)) : nullptr;
    if (!original) {
        return 0;
    }
    bool framework = false;
    FileState* file = TryPrepare(db, path, framework);
    if (!file || !blob) {
        return original(db, path, blob, size);
    }
    // Patches first, so appended rows go after the patched vanilla ones.
    std::vector<uint8_t> rewritten;
    if (!file->rewrites.empty()) {
        TryRewrite(*file, framework, blob, size, rewritten);
    }
    const uint8_t* vanilla = rewritten.empty() ? blob : rewritten.data();
    const uint32_t vanillaSize = rewritten.empty() ? size : static_cast<uint32_t>(rewritten.size());
    std::vector<uint8_t> merged;
    if (!file->appends.empty() && !framework) {
        TryAppend(*file, db, vanilla, vanillaSize, merged);
    }
    const uint8_t* data = merged.empty() ? vanilla : merged.data();
    const uint32_t bytes = merged.empty() ? vanillaSize : static_cast<uint32_t>(merged.size());
    const int64_t result = original(db, path, data, bytes);
    TryRecord(*file, data, bytes, framework);
    return result;
}

const FileState* Watched(const char* file) {
    bool framework = false;
    return FindFile(FileName(file, framework));
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool beforeGameInit) {
    if (g_InstallState != 0) {
        return g_InstallState > 0;
    }
    g_InstallState = -1;
    if (!beforeGameInit) {
        Log("The table engine needs Canopy to start before game init, so it is off.");
        return false;
    }
    if (!RequirementsMet(retail::kTableEngineRequirements)) {
        Log("The table engine is off.");
        return false;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_ModuleBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_ModuleBase + static_cast<uintptr_t>(dos->e_lfanew));
    g_ImageEnd = g_ModuleBase + nt->OptionalHeader.SizeOfImage;
    DetourSet hooks("Table engine");
    if (!hooks.Add(
            retail::kDatabaseManagerLoad, reinterpret_cast<void*>(&Detour_ManagerLoad),
            reinterpret_cast<void**>(&g_OriginalManagerLoad), "databaseManagerLoad"
        )) {
        hooks.RemoveAll();
        Log("The table engine is off, its hook could not be created.");
        return false;
    }
    g_InstallState = 1;
    Log("Table engine installed.");
    return true;
}

void Contribute(const char* file, AppendFn append) {
    AddFile(file)->appends.push_back(append);
}

void Rewrite(const char* file, RewriteFn rewrite) {
    AddFile(file)->rewrites.push_back(rewrite);
}

void Watch(const char* file) {
    AddFile(file);
}

bool Known(const char* file) {
    const FileState* state = Watched(file);
    return state && state->known.load(std::memory_order_acquire);
}

bool WaitKnown(const char* file, uint32_t milliseconds) {
    const ULONGLONG start = GetTickCount64();
    while (!Known(file)) {
        if (GetTickCount64() - start >= milliseconds) {
            return false;
        }
        Sleep(1);
    }
    return true;
}

int Batch(const char* file) {
    bool framework = false;
    const std::string name = FileName(file, framework);
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto found = g_FileBatches.find(name);
    return found == g_FileBatches.end() ? 0 : found->second;
}

bool HasId(const char* file, uint64_t id) {
    const FileState* state = Watched(file);
    std::lock_guard<std::mutex> lock(g_Mutex);
    return state && state->ids.count(id) > 0;
}

bool EnumId(const char* file, const std::string& name, uint64_t& id) {
    const FileState* state = Watched(file);
    if (!state) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto found = state->enums.find(name);
    if (found == state->enums.end()) {
        return false;
    }
    id = found->second;
    return true;
}

bool HasEnum(const char* file, const std::string& name) {
    const FileState* state = Watched(file);
    std::lock_guard<std::mutex> lock(g_Mutex);
    return state && (state->enums.count(name) > 0 || state->crcs.count(blob::EnumCrc(name.data(), name.size())) > 0);
}

uint8_t* Database(const char* file) {
    bool framework = false;
    const std::string name = FileName(file, framework);
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto found = g_Databases.find(name);
    return found == g_Databases.end() ? nullptr : found->second;
}

bool Complete() {
    return g_InstallState > 0 && g_Batches.load() > 0 && !g_Missed.load();
}

}  // namespace table_engine
