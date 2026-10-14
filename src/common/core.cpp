#define NOMINMAX
#include "core.h"

#include <windows.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#include <cstdio>

namespace core {

uintptr_t g_ModuleBase = 0;

namespace {

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

}  // namespace core
