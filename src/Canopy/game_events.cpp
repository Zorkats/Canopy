#define NOMINMAX
#include "game_events.h"

#include <atomic>
#include <cstdint>

#include "core.h"

// =============================================================
// GAME EVENTS DESIGN
// =============================================================
// The field events are edges of PlayerInField, read once per frame. The
// other two come from hooks that only raise a flag: the title screen's
// constructor and the save load's field setup both run inside task updates,
// so the events are reported from the next frame tick instead, outside them.
//
// The title screen also rebuilds itself after its options change the
// language. That is not a new arrival, so a title event is reported only
// when the game was somewhere else since the last one.
//
// A game runs from a save load, or from the field first gaining the player
// after a new game, until the next title screen. The game shows its popup
// log only then.

namespace game_events {
namespace {

using namespace core;

using TitleCtorFn = uint8_t*(__fastcall*)(uint8_t* task);
using StartLoadGameFn = void(__fastcall*)();

TitleCtorFn g_OriginalTitleCtor = nullptr;
StartLoadGameFn g_OriginalStartLoadGame = nullptr;

std::atomic<bool> g_TitlePending{false};
std::atomic<bool> g_LoadPending{false};
bool g_AtTitle = false;
bool g_InField = false;
bool g_InGame = false;

uint8_t* __fastcall Detour_TitleCtor(uint8_t* task) {
    g_TitlePending = true;
    return g_OriginalTitleCtor(task);
}

void __fastcall Detour_StartLoadGame() {
    g_OriginalStartLoadGame();
    g_LoadPending = true;
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kEventRequirements)) {
        return false;
    }
    DetourSet detours("Game events");
    const bool created =
        detours.Add(
            retail::kTitleCtor,
            reinterpret_cast<void*>(&Detour_TitleCtor),
            reinterpret_cast<void**>(&g_OriginalTitleCtor),
            "titleCtor"
        ) &&
        detours.Add(
            retail::kStartLoadGame,
            reinterpret_cast<void*>(&Detour_StartLoadGame),
            reinterpret_cast<void**>(&g_OriginalStartLoadGame),
            "startLoadGame"
        );
    if (!created) {
        detours.RemoveAll();
        return false;
    }
    return true;
}

int Poll(CanopyEvent (&events)[kMaxPerFrame]) {
    int count = 0;
    if (g_TitlePending.exchange(false) && !g_AtTitle) {
        g_AtTitle = true;
        g_InGame = false;
        events[count++] = CANOPY_EVENT_TITLE_SCREEN;
        Log("Reached the title screen.");
    }
    if (g_LoadPending.exchange(false)) {
        g_AtTitle = false;
        g_InGame = true;
        events[count++] = CANOPY_EVENT_SAVE_LOADED;
        Log("A save finished loading.");
    }
    const bool inField = PlayerInField();
    if (inField != g_InField) {
        g_InField = inField;
        // A new game reaches the field without a load.
        if (inField) {
            g_AtTitle = false;
            g_InGame = true;
        }
        events[count++] = inField ? CANOPY_EVENT_FIELD_ENTER : CANOPY_EVENT_FIELD_LEAVE;
    }
    return count;
}

bool InGame() {
    return g_InGame;
}

}  // namespace game_events
