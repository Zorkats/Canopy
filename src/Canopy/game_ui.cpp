#define NOMINMAX
#include "game_ui.h"

#include <cstdint>
#include <cstring>
#include <string>

#include "core.h"
#include "game_events.h"
#include "options_menu.h"

// =============================================================
// TOAST DESIGN
// =============================================================
// A toast goes through the game's own popup log, so it gets the native font,
// window art and stacking. Each one is queued with a carrier logtext row and
// a tag that marks it as the loader's and carries a slot id; the log's text
// getter is detoured to swap the carrier's text for the toast's. A queued
// toast may wait behind others before it is drawn, so the text of the last
// kToastSlots toasts is kept.
//
// The tag is stored in CGameLog and only read by the engine when the log has
// a source object, which a toast never has.
//
// The game draws its popup log only while a game runs, not on the title
// screen. A toast queued there would never show, or show late, so toasts are
// refused until a game runs.

// =============================================================
// YES/NO POPUP DESIGN
// =============================================================
// The engine's own Yes/No popup, with the game's button labels. Over the Mod
// Settings page it is attached to the page the way Exit Game attaches its
// popup to the options menu. In the field it goes on a new layer of the UI
// root, like the engine's save tasks, drawn on the field popup layer like the
// Lua popup command's. A reference of our own tells when the engine let go of
// it; the answer is only reported then, outside every task update.

namespace game_ui {
namespace {

using namespace core;

using PushLogFn = uint64_t(__fastcall*)(
    void* logManager, void* source, int32_t tag, uint32_t count, uint64_t logId, void** parentTask
);
using GetLogTextFn = uint64_t(__fastcall*)(void* gameLog, void* outText);
using MakeLayerKeyFn = void*(__fastcall*)(void* out, int64_t layer, int64_t unused, const char* name);

// High half of a toast's tag marks it as the loader's, low half holds its id.
constexpr int32_t kToastMarker = 0x564D0000;
constexpr int32_t kToastMarkerMask = static_cast<int32_t>(0xFFFF0000);
constexpr int32_t kToastIdMask = 0x0000FFFF;
// Divides the id range, so an id always maps to the same slot.
constexpr size_t kToastSlots = 256;
static_assert((kToastIdMask + 1) % kToastSlots == 0, "toast ids must map to fixed slots");
// Longest toast text, in bytes of UTF-8.
constexpr size_t kMaxToastBytes = 512;
// A popup that never lets go after its answer is given up on after this many
// frames.
constexpr int kPopupTimeoutFrames = 600;

struct Toast {
    int32_t id = -1;
    std::string text;
};

GetLogTextFn g_OriginalGetLogText = nullptr;
PushLogFn g_PushLog = nullptr;
MakeLayerKeyFn g_MakeLayerKey = nullptr;
bool g_ToastsInstalled = false;
bool g_PopupsInstalled = false;

Toast g_Toasts[kToastSlots];
int32_t g_NextToastId = 0;

enum class Answer { None, Yes, No };
// Our own reference to the open popup, and what the player picked.
uint8_t* g_Popup = nullptr;
Answer g_Answer = Answer::None;
int g_PopupFrames = 0;

// text cut to kMaxToastBytes without splitting a UTF-8 sequence.
std::string Clip(const char* text) {
    size_t length = strnlen(text, kMaxToastBytes + 1);
    if (length > kMaxToastBytes) {
        length = kMaxToastBytes;
        while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80) {
            --length;
        }
    }
    return std::string(text, length);
}

uint64_t __fastcall Detour_GetLogText(void* gameLog, void* outText) {
    const int32_t tag = ReadAt<int32_t>(gameLog, retail::kGameLogTag);
    if (ReadAt<uint64_t>(gameLog, retail::kGameLogId) != retail::kToastCarrierLogId ||
        ReadAt<const void*>(gameLog, retail::kGameLogSource) || (tag & kToastMarkerMask) != kToastMarker) {
        return g_OriginalGetLogText(gameLog, outText);
    }
    // A slot reused before its toast was drawn shows empty rather than
    // another toast's text.
    const int32_t id = tag & kToastIdMask;
    const Toast& toast = g_Toasts[static_cast<size_t>(id) % kToastSlots];
    AssignText(outText, toast.id == id ? toast.text.c_str() : "");
    return 0;
}

}  // namespace

bool InstallToasts() {
    if (!RequirementsMet(retail::kToastRequirements)) {
        return false;
    }
    g_PushLog = GameFunction<PushLogFn>(retail::kPushLog);
    DetourSet detours("Toasts");
    if (!detours.Add(
            retail::kGetLogText,
            reinterpret_cast<void*>(&Detour_GetLogText),
            reinterpret_cast<void**>(&g_OriginalGetLogText),
            "getLogText"
        )) {
        return false;
    }
    g_ToastsInstalled = true;
    return true;
}

bool InstallPopups() {
    if (!RequirementsMet(retail::kPopupRequirements)) {
        return false;
    }
    g_MakeLayerKey = GameFunction<MakeLayerKeyFn>(retail::kMakeLayerKey);
    g_PopupsInstalled = true;
    return true;
}

const char* ShowToast(const char* text) {
    if (!g_ToastsInstalled) {
        return "toasts are not available on this game build";
    }
    void* logManager = ReadGlobal<void*>(retail::kGameManager);
    if (!logManager || !ReadGlobal<void*>(retail::kUiTaskRoot) || !game_events::InGame()) {
        return "toasts only show while a game runs, not at the title screen";
    }
    const int32_t id = g_NextToastId;
    g_NextToastId = (id + 1) & kToastIdMask;
    Toast& toast = g_Toasts[static_cast<size_t>(id) % kToastSlots];
    toast.id = id;
    toast.text = Clip(text);
    void* parentTask = nullptr;
    g_PushLog(logManager, nullptr, kToastMarker | id, UINT32_MAX, retail::kToastCarrierLogId, &parentTask);
    return nullptr;
}

const char* OpenYesNo(const char* text) {
    if (!g_PopupsInstalled) {
        return "the popup is not available on this game build";
    }
    if (g_Popup) {
        return "another Yes/No popup is open";
    }
    uint8_t* page = options_menu::ActivePage();
    void* root = ReadGlobal<void*>(retail::kUiTaskRoot);
    if (!page && !(root && PlayerInField())) {
        return "it opens only while the player is in the field or from the Mod Settings page";
    }
    g_Answer = Answer::None;
    uint8_t* task =
        CreateYesNoPopup(text, [] { g_Answer = Answer::Yes; }, [] { g_Answer = Answer::No; });
    if (!task) {
        return "the popup could not be created";
    }
    AddRef(task, retail::kTaskRefCount);
    if (page) {
        AttachTask(page, task, retail::kAttachModeMenuPopup);
    } else {
        // Without a layer key the window renders as a dark box with
        // unreadable text.
        alignas(8) uint8_t layerKey[retail::kLayerKeySize] = {};
        g_MakeLayerKey(layerKey, retail::kFieldPopupLayer, 0, "");
        WriteAt<uint64_t>(task, retail::kTaskLayerKeyCount, 1);
        std::memcpy(task + retail::kTaskLayerKeys, layerKey, sizeof(layerKey));
        AttachTask(root, task, retail::kAttachModePushLayer);
    }
    g_Popup = task;
    g_PopupFrames = 0;
    return nullptr;
}

bool PopupClosed(bool& yes) {
    if (!g_Popup) {
        return false;
    }
    const bool released = ReadAt<int32_t>(g_Popup, retail::kTaskRefCount) <= 1;
    const bool answered = g_Answer != Answer::None;
    if (!released && !(answered && ++g_PopupFrames > kPopupTimeoutFrames)) {
        return false;
    }
    yes = g_Answer == Answer::Yes;
    Release(g_Popup, retail::kTaskRefCount);
    g_Popup = nullptr;
    g_Answer = Answer::None;
    return true;
}

}  // namespace game_ui
