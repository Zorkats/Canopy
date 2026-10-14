#define NOMINMAX
#include <windows.h>

#include <MinHook.h>

#include <cstdint>
#include <string>
#include <thread>

#include "core.h"
#include "game_events.h"
#include "game_ui.h"
#include "input.h"
#include "mod_config.h"
#include "mod_loader.h"
#include "options_menu.h"
#include "universal_proxy_x64.h"
#include "canopy_api.h"

// =============================================================
// CANOPY
// =============================================================
// Installs as dinput8.dll, which the game imports directly, and forwards the
// real dinput8 exports. It owns the services every mod shares: the frame
// tick, input, the settings store and its Mod Settings page in the options
// menu. Then it loads the mods in the mods folder and hands each one the API
// in sdk/canopy_api.h. Every hook, the loader's and the mods', is enabled
// together once all of them are created.

namespace {

using namespace core;

using FrameUpdateFn = uint64_t(__fastcall*)(void* app);
using GameDestroyFn = void*(__fastcall*)(void* game, uint32_t flags);

FrameUpdateFn g_OriginalFrameUpdate = nullptr;
GameDestroyFn g_OriginalGameDestroy = nullptr;

uint64_t __fastcall Detour_FrameUpdate(void* app) {
    const uint64_t result = g_OriginalFrameUpdate(app);
    input::Tick();
    options_menu::Tick();
    mod_loader::Tick();
    return result;
}

// Mods save their state while every game system is still alive.
void* __fastcall Detour_GameDestroy(void* game, uint32_t flags) {
    mod_loader::Exit();
    return g_OriginalGameDestroy(game, flags);
}

void LogFeature(const char* name, bool installed) {
    Log(std::string(name) + (installed ? " installed." : " disabled."));
}

void InitializeHooks() {
    g_ModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Log("Canopy starting, API version " + std::to_string(CANOPY_API_VERSION) + ".");

    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize failed with status " + std::to_string(status) + ".");
        return;
    }

    DetourSet shared("Shared");
    const bool ticking = MatchesSignatures(retail::kSharedSignatures) &&
                         shared.Add(
                             retail::kFrameUpdate,
                             reinterpret_cast<void*>(&Detour_FrameUpdate),
                             reinterpret_cast<void**>(&g_OriginalFrameUpdate),
                             "frameUpdate"
                         );
    // Without it no hotkey fires, but every other feature still works.
    if (!input::Install()) {
        Log("Input disabled, hotkeys will not respond.");
    }
    DetourSet exitHook("Exit");
    const bool exitHooked = MatchesSignatures(retail::kExitSignatures) &&
                            exitHook.Add(
                                retail::kGameDestroy,
                                reinterpret_cast<void*>(&Detour_GameDestroy),
                                reinterpret_cast<void**>(&g_OriginalGameDestroy),
                                "gameDestroy"
                            );
    // The page runs its prompts and title returns from the tick.
    const bool optionsMenu = ticking && options_menu::Install();
    // Mods use these from the tick, so they need it too.
    const bool toasts = ticking && game_ui::InstallToasts();
    const bool popups = ticking && game_ui::InstallPopups();
    const bool events = ticking && game_events::Install();
    LogFeature("Frame tick", ticking);
    LogFeature("Exit callbacks", exitHooked);
    LogFeature("Options menu", optionsMenu);
    LogFeature("Toasts", toasts);
    LogFeature("Yes/No popups", popups);
    LogFeature("Title and load events", events);

    mod_loader::LoadAll(ticking, exitHooked);
    mod_config::Seal();
    if (!ticking && !exitHooked && !optionsMenu && mod_loader::HookCount() == 0) {
        Log("Nothing to enable.");
        return;
    }

    mod_loader::Started();
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("MH_EnableHook(MH_ALL_HOOKS) failed, backing out every mod.");
        MH_DisableHook(MH_ALL_HOOKS);
        mod_loader::ShutdownAll();
        MH_Uninitialize();
        return;
    }
    Log("All hooks enabled.");
}

}  // namespace

// =============================================================
// DLL ENTRY POINT
// =============================================================
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        DetectProxyType(module);
        std::thread(InitializeHooks).detach();
    }
    return TRUE;
}
