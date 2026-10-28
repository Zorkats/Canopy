#define NOMINMAX
#include "core.h"

#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <cstdio>
#include <iterator>

#include "game_addresses.h"
#include "platform_files.h"

namespace core {

uintptr_t g_ModuleBase = 0;

namespace {

game_addresses::Resolution g_Addresses;
// Set once g_Addresses holds this DLL's resolution.
std::atomic<bool> g_AddressesResolved{false};
// Where a searched resolution came from, for the report.
const char* g_SearchSource = "";
// Set when force_search.txt made a checked build search, with how the search
// differed from the checked addresses, one "name checked searched" per line.
bool g_SearchForced = false;
std::string g_ForcedDifferences;

// What mods\CanopyData\force_search.txt asks for: a search even on a checked
// build, of village.exe or, with the first word "loaded", of the loaded game.
// It tries on a known build the path an unknown one takes.
enum class ForcedSearch { None, File, Loaded };

ForcedSearch ReadForcedSearch(const std::wstring& gameFolder) {
    std::vector<uint8_t> text;
    if (!platform::ReadWholeFile(gameFolder + L"\\mods\\CanopyData\\force_search.txt", 4096, text)) {
        return ForcedSearch::None;
    }
    const std::string content(text.begin(), text.end());
    const size_t first = content.find_first_not_of(" \t\r\n");
    return first != std::string::npos && content.compare(first, 6, "loaded") == 0 ? ForcedSearch::Loaded
                                                                                   : ForcedSearch::File;
}

using AllocateFn = uint8_t*(__fastcall*)(void* heap, uint64_t alignment, uint64_t size);
using CStringAssignFn = void(__fastcall*)(void* text, const char* data, size_t length, int32_t mode);
using ReleaseFn = void(__fastcall*)(void* object, uint64_t flags);
using GetLogTextByIdFn = const char*(__fastcall*)(uint64_t logId);
using AttachTaskFn = uint64_t(__fastcall*)(void* parent, uint8_t** task, int32_t mode);
using PopupSelectCtorFn = uint8_t*(__fastcall*)(
    uint8_t* task, const void* messages, const char* yesText, const char* noText, PromptCallback* onYes,
    PromptCallback* onNo, int64_t a7, int64_t a8
);
using ReturnToTitleFn = void(__fastcall*)();
using SoundFadeFn = void(__fastcall*)(void* player, float volume, double frames, void* onDone);
using IsReceiverActiveFn = bool(__fastcall*)(const void* receiver);

// Same value the engine passes to CString::assign when it fills text.
constexpr int32_t kCStringAssignMode = 2;

std::string GetModDirectory() {
    char path[MAX_PATH];
    HMODULE module = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetModDirectory),
        &module
    );
    GetModuleFileNameA(module, path, MAX_PATH);
    PathRemoveFileSpecA(path);
    return std::string(path);
}

// Mirrors the gate the player component itself checks before acting
// (0x20E5D0). The chain is only valid in the field, so the reads are guarded.
bool PlayerHasControl(const uint8_t* saveData) {
    __try {
        const uint8_t* status = ReadAt<const uint8_t*>(saveData, retail::kSaveDataPlayerStatus);
        if (!status) {
            return false;
        }
        const uint8_t* component = ReadAt<const uint8_t*>(status, retail::kPlayerStatusComponent);
        if (!component ||
            ReadAt<uintptr_t>(component, 0) != g_ModuleBase + retail::kPlayerComponentVtable) {
            return false;
        }
        const uint8_t* receiver = ReadAt<const uint8_t*>(component, retail::kPlayerComponentReceiver);
        return receiver && GameFunction<IsReceiverActiveFn>(retail::kIsReceiverActive)(receiver) &&
               ReadAt<uint64_t>(saveData, retail::kSaveDataFieldBlocker) == 0 &&
               (ReadAt<uint32_t>(saveData, retail::kSaveDataStateFlags) & retail::kStateFlagFieldLocked) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string Hex(uintptr_t value) {
    char text[24];
    snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

std::string ModFilePath(const char* fileName) {
    return GetModDirectory() + "\\" + fileName;
}

int32_t CurrentLanguage() {
    const int32_t language = ReadGlobal<int32_t>(retail::kLanguage);
    return language >= 0 && language < retail::kLanguageCount ? language : retail::kLanguageEnglish;
}

bool PlayerInField() {
    const uint8_t* manager = ReadGlobal<uint8_t*>(retail::kGameManager);
    if (!manager) {
        return false;
    }
    const uint8_t* saveData = ReadAt<const uint8_t*>(manager, retail::kManagerSaveData);
    return saveData && PlayerHasControl(saveData);
}

bool GameWindowFocused() {
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId();
}

void AddRef(uint8_t* object, size_t refOffset) {
    if (object) {
        _InterlockedIncrement(reinterpret_cast<volatile long*>(object + refOffset));
    }
}

void Release(uint8_t* object, size_t refOffset) {
    if (!object) {
        return;
    }
    auto* count = reinterpret_cast<volatile long*>(object + refOffset);
    if (*count != 0 && _InterlockedExchangeAdd(count, -1) == 1) {
        (*reinterpret_cast<ReleaseFn*>(*reinterpret_cast<void**>(object)))(object, 1);
    }
}

uint8_t* AllocateZeroed(size_t size) {
    const auto allocate = GameFunction<AllocateFn>(retail::kAllocate);
    uint8_t* memory = allocate(ReadGlobal<void*>(retail::kEngineHeap), retail::kAllocationAlignment, size);
    if (memory) {
        std::memset(memory, 0, size);
    }
    return memory;
}

void AssignText(void* text, const char* data) {
    const auto assign = (*static_cast<CStringAssignFn**>(text))[1];
    assign(text, data, std::strlen(data), kCStringAssignMode);
}

const char* LogText(uint64_t logId) {
    return GameFunction<GetLogTextByIdFn>(retail::kGetLogTextById)(logId);
}

void AttachTask(void* parent, uint8_t* task, int32_t mode) {
    AddRef(task, retail::kTaskRefCount);
    uint8_t* taskRef = task;
    GameFunction<AttachTaskFn>(retail::kAttachTask)(parent, &taskRef, mode);
    Release(task, retail::kTaskRefCount);
}

uint8_t* CreateYesNoPopup(const char* message, PromptCallback onYes, PromptCallback onNo) {
    const char* yesText = LogText(retail::kPopupYesLogId);
    const char* noText = LogText(retail::kPopupNoLogId);
    if (!yesText || !noText) {
        Log("Popup failed, the yes/no button text is missing.");
        return nullptr;
    }

    // A one-message std::vector<Ngf::CString>. The ctor copies the strings,
    // so both can live on the stack.
    alignas(8) uint8_t text[retail::kCStringSize] = {};
    WriteAt<uintptr_t>(text, 0, g_ModuleBase + retail::kCStringVtable);
    WriteAt<const char*>(text, retail::kCStringData, message);
    WriteAt<size_t>(text, retail::kCStringLength, std::strlen(message));
    WriteAt<int32_t>(text, retail::kCStringMode, kCStringAssignMode);
    const uint8_t* messages[3] = {text, text + retail::kCStringSize, text + retail::kCStringSize};

    uint8_t* task = AllocateZeroed(retail::kPopupSelectTaskSize);
    if (!task) {
        return nullptr;
    }
    // The ctor moves both targets into the popup and leaves these empty.
    return GameFunction<PopupSelectCtorFn>(retail::kPopupSelectCtor)(
        task, messages, yesText, noText, &onYes, &onNo, retail::kPopupSelectDefaultArg,
        retail::kPopupSelectDefaultArg
    );
}

namespace {

// Fades every player of a sound player list out and stops it once silent.
void FadeOutPlayers(const uint8_t* head) {
    if (!head) {
        return;
    }
    // A std::function<void(CSoundPlayer*)> holding the engine's stopWithFade
    // lambda inline: its vtable in the first slot, the target pointer in the
    // last. Each player copies it, and the lambda needs no destruction.
    alignas(16) uintptr_t onDone[8] = {};
    onDone[0] = g_ModuleBase + retail::kStopAfterFadeVtable;
    onDone[7] = reinterpret_cast<uintptr_t>(&onDone[0]);
    const auto fade = GameFunction<SoundFadeFn>(retail::kSoundFade);
    for (const uint8_t* node = ReadAt<const uint8_t*>(head, retail::kSoundNodeNext); node && node != head;
         node = ReadAt<const uint8_t*>(node, retail::kSoundNodeNext)) {
        if (void* player = ReadAt<void*>(node, retail::kSoundNodePlayer)) {
            fade(player, 0.0f, retail::kTitleSoundFadeFrames, onDone);
        }
    }
}

}  // namespace

void ReturnToTitle() {
    // The title routine leaves every sound playing, so the sound effects get
    // the fade the engine's settings title option (0x458F20) gives them, and
    // the BGM the fade BgmPlaywithCrossFade (0x6FA9C0) gives the outgoing
    // song. The BGM players sit in a list of their own.
    const uint8_t* sound = ReadGlobal<uint8_t*>(retail::kSoundManager);
    FadeOutPlayers(sound ? ReadAt<const uint8_t*>(sound, retail::kSoundManagerPlayers) : nullptr);
    const uint8_t* system = ReadGlobal<uint8_t*>(retail::kSoundSystem);
    const uint8_t* bgm = system ? ReadAt<const uint8_t*>(system, retail::kSoundSystemBgmManager) : nullptr;
    FadeOutPlayers(bgm ? ReadAt<const uint8_t*>(bgm, retail::kBgmManagerPlayers) : nullptr);
    GameFunction<ReturnToTitleFn>(retail::kReturnToTitle)();
}

// =============================================================
// GAME ADDRESSES
// =============================================================
void ResolveAddresses() {
    if (g_AddressesResolved.load()) {
        return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_ModuleBase);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(g_ModuleBase + dos->e_lfanew);
    const game_addresses::Image live{
        reinterpret_cast<const uint8_t*>(g_ModuleBase), nt->OptionalHeader.SizeOfImage, g_ModuleBase
    };
    game_addresses::Header header;
    game_addresses::ReadHeader(live, header);
    wchar_t path[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
    const bool hasPath = length > 0 && length < std::size(path);
    const std::wstring exePath = hasPath ? std::wstring(path, length) : std::wstring();
    const size_t slash = exePath.find_last_of(L"\\/");
    const ForcedSearch forced =
        slash == std::wstring::npos ? ForcedSearch::None : ReadForcedSearch(exePath.substr(0, slash));

    game_addresses::Resolution checked;
    const bool isChecked = game_addresses::LookUp(header, checked);
    if (isChecked && forced == ForcedSearch::None) {
        g_Addresses = std::move(checked);
        g_AddressesResolved.store(true);
        return;
    }
    // The file has the code as built: the loaded image may already hold
    // hooks, and its pointers are relocated. It must be the running build.
    std::vector<uint8_t> file;
    std::vector<uint8_t> laid;
    std::string error;
    game_addresses::Header fileHeader;
    const bool fromFile = forced != ForcedSearch::Loaded && hasPath &&
                          platform::ReadWholeFile(exePath, 1ull << 30, file) &&
                          game_addresses::LayOut(file, laid, error) &&
                          game_addresses::ReadHeader({laid.data(), laid.size(), 0}, fileHeader) &&
                          fileHeader.timestamp == header.timestamp && fileHeader.imageSize == header.imageSize;
    file.clear();
    file.shrink_to_fit();
    if (fromFile) {
        g_Addresses = game_addresses::Search({laid.data(), laid.size(), fileHeader.imageBase});
        g_SearchSource = "village.exe";
    } else {
        g_Addresses = game_addresses::Search(live);
        g_SearchSource = "the loaded game";
    }
    if (isChecked) {
        g_SearchForced = true;
        g_ForcedDifferences = game_addresses::Compare(checked, g_Addresses);
    }
    g_AddressesResolved.store(true);
}

bool AddressesResolved() {
    return g_AddressesResolved.load();
}

bool CheckedBuild() {
    return g_AddressesResolved.load() && g_Addresses.Verified();
}

std::string AddressReport() {
    if (!g_AddressesResolved.load()) {
        return "Game addresses were not resolved.";
    }
    const game_addresses::Resolution& resolution = g_Addresses;
    char build[48];
    snprintf(build, sizeof(build), "0x%08X", resolution.header.timestamp);
    if (resolution.Verified()) {
        return std::string("Game build ") + build + " is checked: " + resolution.build + ".";
    }
    using game_addresses::Status;
    const std::string searched = std::string("Searched ") + g_SearchSource + ": " +
                                 std::to_string(resolution.Count(Status::Found)) + " of " +
                                 std::to_string(resolution.entries.size()) + " addresses and fields found";
    if (!g_SearchForced) {
        return std::string("Game build ") + build + " is not checked. " + searched +
               ". Features that write saves stay off.";
    }
    std::string differences;
    for (size_t begin = 0; begin < g_ForcedDifferences.size();) {
        size_t end = g_ForcedDifferences.find('\n', begin);
        if (end == std::string::npos) {
            end = g_ForcedDifferences.size();
        }
        differences += (differences.empty() ? "" : "; ") + g_ForcedDifferences.substr(begin, end - begin);
        begin = end + 1;
    }
    return std::string("Game build ") + build + " is checked, but mods\\CanopyData\\force_search.txt asks for a search. " +
           searched + (differences.empty() ? ", and every one agrees with the checked addresses."
                                           : ". It differs from the checked addresses (name, checked, searched): " +
                                                 differences + ".") +
           " Features that write saves stay off.";
}

uintptr_t FindAddress(const char* name) {
    uintptr_t rva = 0;
    return FindValue(name, rva) ? rva : 0;
}

bool FindValue(const char* name, uintptr_t& value) {
    if (!g_AddressesResolved.load()) {
        return false;
    }
    for (const game_addresses::Entry& entry : g_Addresses.entries) {
        if (entry.name == name) {
            value = entry.rva;
            return entry.status == game_addresses::Status::Found;
        }
    }
    return false;
}

}  // namespace core

namespace retail {

Address::operator uintptr_t() const {
    constexpr uintptr_t kNotFound = ~uintptr_t{0};
    uintptr_t rva = rva_.load(std::memory_order_relaxed);
    if (rva == 0) {
        // Nothing is stored before resolution, so an early read retries.
        rva = core::FindAddress(name_);
        if (rva != 0) {
            rva_.store(rva, std::memory_order_relaxed);
        } else if (core::AddressesResolved()) {
            rva_.store(kNotFound, std::memory_order_relaxed);
        }
    }
    return rva == kNotFound ? 0 : rva;
}

std::string Address::Problem() const {
    return std::string("Game address ") + name_ + " was not found in this game build.";
}

bool Field::Met() const {
    return Problem().empty();
}

std::string Field::Problem() const {
    static const game_addresses::Resolution kUnresolved;
    return game_addresses::FieldProblem(
        core::AddressesResolved() ? core::g_Addresses : kUnresolved, name_, value_
    );
}

}  // namespace retail
