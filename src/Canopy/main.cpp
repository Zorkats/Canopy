#define NOMINMAX
#include <windows.h>

#include <MinHook.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

#include "core.h"
#include "file_overrides.h"
#include "game_events.h"
#include "game_ui.h"
#include "input.h"
#include "lua_host.h"
#include "mod_appends.h"
#include "mod_art.h"
#include "mod_config.h"
#include "mod_debug.h"
#include "mod_flags.h"
#include "mod_items.h"
#include "mod_objects.h"
#include "mod_patches.h"
#include "mod_recipes.h"
#include "mod_crops.h"
#include "mod_rows.h"
#include "mod_shops.h"
#include "mod_villagers.h"
#include "mod_whistles.h"
#include "mod_loader.h"
#include "options_menu.h"
#include "save_data.h"
#include "title_badge.h"
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
//
// All of that runs on the game's main thread, inside a detour of game init.
// DllMain only checks game init's code and creates and enables that one
// detour: it runs under the loader lock, where loading mods, reading files
// or even logging could deadlock. WinMain calls game init once, and game
// init creates the file system, reads every database table and mounts the
// archives. Running the startup first and the original after it means every
// hook is live before the game reads its first file, which file overrides
// need, and before the code any mod hooks can run. The old way, a thread
// started from DllMain, raced game init. It is still the fallback when game
// init does not match this build or cannot be hooked, with file overrides
// off, since a file read before the hooks are enabled would skip its
// override.

namespace {

using namespace core;

using FrameUpdateFn = uint64_t(__fastcall*)(void* app);
using GameDestroyFn = void*(__fastcall*)(void* game, uint32_t flags);
using GameInitFn = uint8_t(__fastcall*)(void* app, void* config);

FrameUpdateFn g_OriginalFrameUpdate = nullptr;
GameDestroyFn g_OriginalGameDestroy = nullptr;
GameInitFn g_OriginalGameInit = nullptr;
// Why DllMain could not hook game init. Logged by the fallback thread,
// since DllMain itself must not write the log.
const char* g_EarlyFailure = nullptr;

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

// Wine's ntdll, and so Proton's, exports wine_get_version; Windows' does
// not. Canopy runs the same on both, so this only helps a bug report.
void LogWine() {
    using WineVersionFn = const char*(__cdecl*)();
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto version = ntdll ? reinterpret_cast<WineVersionFn>(GetProcAddress(ntdll, "wine_get_version")) : nullptr;
    if (version) {
        const char* text = version();
        Log(std::string("Running under Wine or Proton, version ") + (text ? text : "unknown") + ".");
    }
}

// beforeGameInit: called from the game init detour, before the original.
void InitializeHooks(bool beforeGameInit) {
    g_ModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    Log("Canopy starting, API version " + std::to_string(CANOPY_API_VERSION) + ".");
    LogWine();
    // Already done when game init was hooked; this covers a late start.
    ResolveAddresses();
    Log(AddressReport());
    if (beforeGameInit) {
        Log("Starting on the main thread, before game init.");
    } else {
        Log(std::string("Starting on a loader thread, which races game init: ") + g_EarlyFailure + ".");
    }

    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MH_Initialize failed with status " + std::to_string(status) + ".");
        return;
    }

    DetourSet shared("Shared");
    const bool ticking = RequirementsMet(retail::kSharedRequirements) &&
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
    const bool exitHooked = RequirementsMet(retail::kExitRequirements) &&
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
    // The title event clears the per-save data, so it needs the events. It
    // and every feature that writes through it only run on a checked build:
    // a searched address that is wrong could damage a save.
    const bool checked = CheckedBuild();
    const bool saveData = events && checked && save_data::Install();
    // Only the game init detour enables the hooks before the first read.
    const bool overrides = beforeGameInit && file_overrides::Install();
    // Canopy's version and the mod count under the game's version.
    const bool badge = title_badge::Install();
    LogFeature("Frame tick", ticking);
    LogFeature("Exit callbacks", exitHooked);
    LogFeature("Options menu", optionsMenu);
    LogFeature("Toasts", toasts);
    LogFeature("Yes/No popups", popups);
    LogFeature("Title and load events", events);
    LogFeature("Per-save data", saveData);
    if (!checked) {
        Log("Per-save data needs a checked game build, so features that write saves stay off.");
    }
    LogFeature("File overrides", overrides);
    LogFeature("Title badge", badge);

    mod_loader::LoadAll(ticking, exitHooked);
    // Lua mods register their settings too, so this comes before the seal.
    const bool luaHost = lua_host::Install(ticking);
    // Art folders register as mods too. Before items and whistles, which
    // name mod icons, and after file overrides, which serve the textures.
    // With no art it hooks nothing.
    const bool art = mod_art::Install(ticking, beforeGameInit);
    // Object folders register as mods too. After art, whose skins objects
    // name, and before items, whose place items name objects. With no
    // objects it hooks nothing.
    const bool objects = mod_objects::Install(ticking, saveData, beforeGameInit);
    // Item folders register as mods too. With no items it hooks nothing.
    const bool items = mod_items::Install(ticking, saveData, beforeGameInit);
    if (objects) {
        mod_objects::LinkItems();
    }
    // Flag folders register as mods too. With no flags it hooks nothing.
    const bool flags = mod_flags::Install(ticking, saveData, beforeGameInit);
    // Villager folders register as mods too. With no villagers it hooks
    // nothing. After the Lua host, so its module loader detour runs first.
    const bool villagers = mod_villagers::Install(ticking, saveData, beforeGameInit);
    // Whistle and recipe folders register as mods too, and with none they
    // hook nothing. After items and flags, which their keys name, and after
    // the villagers, so the loader chain runs the Lua host, the villagers,
    // then the whistles.
    const bool whistles = mod_whistles::Install(ticking, beforeGameInit);
    const bool recipes = mod_recipes::Install(ticking, saveData, beforeGameInit);
    // Generic rows of the generated tables register as mods too. After every
    // module whose parts a row may name, and before patches, so a table's
    // rows are patched first and then appended to. With no rows it hooks
    // nothing.
    const bool rows = mod_rows::Install(ticking, beforeGameInit);
    // Shop lists keep mod sales, and mod items a patched sale sells, out of
    // the save. After rows and items. A sale the save cannot be kept clean
    // of is not appended at all.
    if (!mod_shops::Install(ticking, saveData, mod_rows::HasSales() || items) && mod_rows::HasSales()) {
        mod_rows::DisableSales();
    }
    // Planted mod crops are saved as their base crops, and each crop's seed
    // item becomes its seed. After rows and items, before tables load. A crop
    // the save cannot be kept clean of is not appended at all.
    if (!mod_crops::Install(ticking, saveData) && mod_rows::HasCrops()) {
        mod_rows::DisableCrops();
    }
    // Patches rewrite vanilla rows as their tables load. Beside mod art on
    // the table engine, after items, flags and art, whose keys a patch may
    // name. With no patches it hooks nothing.
    mod_patches::Install(ticking, beforeGameInit);
    // List additions grow lists inside vanilla rows as their tables load.
    // After patches, so a table's vanilla elements are patched first and its
    // lists grown after, and after items and flags, whose keys an addition
    // may name. With none it hooks nothing.
    mod_appends::Install(ticking, beforeGameInit);
    // The debug commands' settings form an entry of their own, after the
    // mods' entries. With an unsupported build it hooks nothing.
    const bool debug = mod_debug::Install();
    mod_config::Seal();
    if (!ticking && !exitHooked && !optionsMenu && !overrides && !badge && !luaHost && !art && !objects && !items && !flags &&
        !villagers && !whistles && !recipes && !rows && !debug && mod_loader::HookCount() == 0) {
        Log("Nothing to enable.");
        return;
    }

    mod_loader::Started();
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("MH_EnableHook(MH_ALL_HOOKS) failed, backing out every mod.");
        MH_DisableHook(MH_ALL_HOOKS);
        mod_loader::ShutdownAll();
        // Game init's detour still returns through its trampoline, which
        // MinHook frees when it shuts down.
        if (!beforeGameInit) {
            MH_Uninitialize();
        }
        return;
    }
    Log(beforeGameInit ? "All hooks enabled, before game init." : "All hooks enabled.");
}

// WinMain calls game init once, on the main thread.
uint8_t __fastcall Detour_GameInit(void* app, void* config) {
    static bool started = false;
    if (!started) {
        started = true;
        InitializeHooks(true);
    }
    return g_OriginalGameInit(app, config);
}

// Creates and enables the game init detour. Runs under the loader lock, so
// it only reads code and calls MinHook, and never logs. Null when the
// detour is enabled, otherwise why it is not.
const char* HookGameInit() {
    g_ModuleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    ResolveAddresses();
    // RequirementsMet logs, so the check is done here without it.
    for (const retail::Requirement* requirement : retail::kStartupRequirements) {
        if (!requirement->Met()) {
            return "game init was not found in this game build";
        }
    }
    // Game init creates the file system, so a loader loaded this late can
    // no longer start before it.
    if (ReadGlobal<void*>(retail::kFileSystem)) {
        return "the game had already initialized";
    }
    const MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        return "MH_Initialize failed";
    }
    void* target = reinterpret_cast<void*>(g_ModuleBase + retail::kGameInit);
    if (MH_CreateHook(
            target, reinterpret_cast<void*>(&Detour_GameInit), reinterpret_cast<void**>(&g_OriginalGameInit)
        ) != MH_OK) {
        return "game init could not be hooked";
    }
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        return "game init could not be hooked";
    }
    return nullptr;
}

void InitializeOnThread() {
    InitializeHooks(false);
}

}  // namespace

// =============================================================
// DLL ENTRY POINT
// =============================================================
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        DetectProxyType(module);
        g_EarlyFailure = HookGameInit();
        if (g_EarlyFailure) {
            std::thread(InitializeOnThread).detach();
        }
    }
    return TRUE;
}
