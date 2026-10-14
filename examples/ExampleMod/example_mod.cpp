// ExampleMod: a complete Canopy mod in one file.
//
// It shows one of each setting kind on the Mod Settings page and writes what
// it sees to Canopy.log:
//   - "Field Messages" (toggle): log when the player gains or loses control.
//   - "Message Style" (choice): short or detailed lines.
//   - "Ping Key" / "Ping Button" (bindings): log a line when pressed.
//   - "Show a Toast" (button): asks with a Yes/No popup, then shows a toast.
// It also toasts when a save finishes loading, logs reaching the title
// screen, checks the game build, and finds a function by byte pattern.
// For other mods it provides the ExampleMod.Greeter interface, declared in
// example_mod_api.h, and it declares a manifest with no dependencies.
//
// It uses sdk/canopy_api.h and example_mod_api.h. docs/MODDING.md walks through
// it.
//
// Copyright (c) Zorkats.

#include <cstdio>

#include "canopy_api.h"
#include "example_mod_api.h"

namespace {

// F9 on the keyboard and Y on a controller, as framework pad ids.
constexpr int32_t kPadF9 = 131;
constexpr int32_t kPadY = 12;

// The village.exe build this mod was written against: its PE TimeDateStamp.
constexpr uint32_t kTestedBuild = 0x6ABD400D;

// The start of the title screen's constructor. Call targets and addresses
// change between builds, so their four bytes are wildcards. Many task
// constructors begin the same way; the pattern runs on to the field stores
// only this one has, so its first match is the right function.
constexpr const char* kTitleCtorPattern =
    "48 89 4C 24 08 53 48 83 EC 40 48 8B D9 E8 ?? ?? ?? ?? 90 48 8D 05 ?? ?? ?? ?? 48 89 03 "
    "48 8D 05 ?? ?? ?? ?? 48 89 43 10 48 8D 05 ?? ?? ?? ?? 48 89 43 20 48 8D 05 ?? ?? ?? ?? "
    "48 89 83 00 02 00 00 33 C0 48 89 83 18 02 00 00";

const CanopyApi* g_Api = nullptr;
CanopyMod g_Mod = CANOPY_NO_MOD;
CanopySetting g_FieldMessages = CANOPY_NO_SETTING;
CanopySetting g_MessageStyle = CANOPY_NO_SETTING;
CanopySetting g_PingKey = CANOPY_NO_SETTING;
CanopySetting g_PingButton = CANOPY_NO_SETTING;

bool Detailed() {
    return g_Api->GetSetting(g_MessageStyle, 0) == 1;
}

// Runs on the game thread for each game event, before that frame's OnFrame.
void OnEvent(CanopyEvent event, void*) {
    const bool detailed = Detailed();
    switch (event) {
        case CANOPY_EVENT_FIELD_ENTER:
        case CANOPY_EVENT_FIELD_LEAVE:
            if (g_Api->GetSetting(g_FieldMessages, 1) == 0) {
                break;
            }
            if (event == CANOPY_EVENT_FIELD_ENTER) {
                g_Api->Log(g_Mod, detailed ? "The player has control again: no menu, event or loading screen is open."
                                           : "In the field.");
            } else {
                g_Api->Log(g_Mod, detailed ? "The player lost control: a menu, event or loading screen opened."
                                           : "Left the field.");
            }
            break;
        case CANOPY_EVENT_SAVE_LOADED:
            g_Api->ShowToast(g_Mod, "Example Mod: save loaded.");
            break;
        case CANOPY_EVENT_TITLE_SCREEN:
            g_Api->Log(g_Mod, "At the title screen.");
            break;
        default:
            // A newer loader may report events this mod does not know.
            break;
    }
}

// Runs once per frame on the game thread.
void OnFrame(void*) {
    // Read the bindings every frame: the player can rebind them at any time.
    const int32_t key = g_Api->GetSetting(g_PingKey, CANOPY_NO_PAD);
    const int32_t button = g_Api->GetSetting(g_PingButton, CANOPY_NO_PAD);
    const bool keyPressed = g_Api->Pressed(key);
    if (keyPressed || g_Api->Pressed(button)) {
        char line[128];
        std::snprintf(
            line, sizeof(line), Detailed() ? "Ping from %s. Player in field: %s." : "Ping from %s.",
            g_Api->PadName(keyPressed ? key : button), g_Api->PlayerInField() ? "yes" : "no"
        );
        g_Api->Log(g_Mod, line);
    }
}

// Gets the popup's answer once it has closed.
void OnAnswer(bool yes, void*) {
    if (yes) {
        g_Api->ShowToast(g_Mod, "Hello from Example Mod.");
    } else {
        g_Api->Log(g_Mod, "The player declined the toast.");
    }
}

// Runs when the player presses the "Show a Toast" row.
void OnShowToastPressed(void*) {
    g_Api->ShowYesNo(g_Mod, "Show a toast from Example Mod?", &OnAnswer, nullptr);
}

// The ExampleMod.Greeter table's function: logs a greeting for who.
void Greet(const char* who) {
    char line[128];
    std::snprintf(line, sizeof(line), "Hello, %s.", who ? who : "someone");
    g_Api->Log(g_Mod, line);
}

// The table other mods get with GetInterface. It lives as long as the DLL.
const ExampleModGreeter kGreeter = {&Greet};

void CheckGame() {
    char line[160];
    const uint32_t build = g_Api->GameBuild();
    if (build == kTestedBuild) {
        g_Api->Log(g_Mod, "Running the game build this mod was tested on.");
    } else {
        std::snprintf(line, sizeof(line), "Game build %08X is not the tested %08X.", build, kTestedBuild);
        g_Api->Log(g_Mod, line);
    }

    const uintptr_t address = g_Api->FindPattern(kTitleCtorPattern);
    if (address != 0) {
        std::snprintf(
            line, sizeof(line), "Found the title screen constructor at village.exe+0x%llX.",
            static_cast<unsigned long long>(address - g_Api->GameBase())
        );
        g_Api->Log(g_Mod, line);
    } else {
        g_Api->Log(g_Mod, "The title screen constructor was not found.");
    }
}

}  // namespace

CANOPY_MOD_MANIFEST("ExampleMod", "Example Mod", "Zorkats", CANOPY_VERSION(1, 0, 0), 3, NULL, 0);

CANOPY_MOD_EXPORT bool CanopyModInit(const CanopyApi* api) {
    // Refuse a loader older than the API this mod was built for. Functions
    // are only ever appended, so having the last one this mod uses means the
    // loader has them all.
    if (!api || api->version < CANOPY_API_VERSION || !CANOPY_HAS(api, ProvideInterface)) {
        return false;
    }
    g_Api = api;

    // Every language left out shows the English text.
    const CanopyText name = {"サンプルMOD", "Example Mod", "Mod d'exemple", "Mod de ejemplo"};
    g_Mod = api->RegisterMod("ExampleMod", &name);
    if (g_Mod == CANOPY_NO_MOD) {
        return false;
    }

    const CanopyText fieldLabel = {nullptr, "Field Messages"};
    const CanopyText fieldHelp = {nullptr, "Log a line when the player gains or loses control."};
    g_FieldMessages = api->AddToggle(g_Mod, "FieldMessages", &fieldLabel, &fieldHelp, true);

    const CanopyText styleLabel = {nullptr, "Message Style"};
    const CanopyText styleHelp = {nullptr, "How much each log line says."};
    const CanopyText styles[] = {{nullptr, "Short"}, {nullptr, "Detailed"}};
    g_MessageStyle = api->AddChoice(g_Mod, "MessageStyle", &styleLabel, &styleHelp, styles, 2, 0);

    const CanopyText pingHelp = {nullptr, "Press it to write a line to Canopy.log."};
    const CanopyText keyLabel = {nullptr, "Ping Key"};
    g_PingKey = api->AddBinding(g_Mod, "PingKey", &keyLabel, &pingHelp, CANOPY_DEVICE_KEYBOARD_MOUSE, kPadF9);
    const CanopyText buttonLabel = {nullptr, "Ping Button"};
    g_PingButton = api->AddBinding(g_Mod, "PingButton", &buttonLabel, &pingHelp, CANOPY_DEVICE_CONTROLLER, kPadY);

    const CanopyText toastLabel = {nullptr, "Show a Toast"};
    api->AddButton(g_Mod, "ShowToast", &toastLabel, &OnShowToastPressed, nullptr);

    if (!api->OnFrame(g_Mod, &OnFrame, nullptr) || !api->OnEvent(g_Mod, &OnEvent, nullptr)) {
        return false;
    }
    if (!api->ProvideInterface(g_Mod, EXAMPLE_MOD_GREETER_NAME, EXAMPLE_MOD_GREETER_VERSION, &kGreeter)) {
        return false;
    }
    // Reading the executable is safe from CanopyModInit; calling game functions
    // is not.
    CheckGame();
    api->Log(g_Mod, "Started.");
    return true;
}
