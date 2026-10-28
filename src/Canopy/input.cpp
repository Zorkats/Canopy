#include "input.h"

#include <algorithm>
#include <cstdint>

#include "core.h"

// =============================================================
// INPUT DESIGN
// =============================================================
// The game gives every keyboard key, mouse button and controller button one
// framework pad id. Its key config waits for a key by asking the pad manager
// for the first pad of each device type that was pressed this frame, and the
// tick asks the same once per frame, so a binding is one pad id of any device.
// The query ignores the byte that marks a press consumed by a menu, so a
// hotkey works whatever the field did with the press.
//
// That query returns one pad per device type, so two keys going down on the
// same frame would lose one. Pressed also reads every pad's down state each
// frame: a pad reports a press when either source sees it go down, and is
// then latched until neither sees it down, so each press reports once.
//
// While a binding waits for its key, the pad manager's lockout count is kept
// above zero, which the game decrements each frame and which blocks every
// button check, so Escape or B never reach the options menu. The tick clears
// it for its own queries only.

namespace input {
namespace {

using namespace core;

using PadCaptureFn = void*(__fastcall*)(
    uint8_t* padManager, void* const** outRef, int32_t deviceType, int32_t mode, uint8_t* query
);

// Frames the menus stay blocked after each capturing tick: the pad manager
// counts one down before the menus read input and one after.
constexpr int32_t kCaptureLockoutFrames = 2;
constexpr int32_t kPadTypeCount = 3;

constexpr int32_t kFirstKeyboardPad = 65;
constexpr int32_t kFirstMousePad = 191;
constexpr int32_t kMouseLeft = 191;
constexpr int32_t kMouseRight = 192;

// Controller pads 1-20, Xbox names. 5-8 are stick directions the capture
// never returns.
constexpr const char* kControllerNames[] = {
    "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right", nullptr, nullptr, nullptr, nullptr,
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back", "Start",
};

// Keyboard pads 65-187, in DirectInput key order.
constexpr const char* kKeyboardNames[] = {
    "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Left Ctrl",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "Left Shift", "\\",
    "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "Right Shift", "Num *", "Left Alt", "Space", "Caps Lock",
    "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Num Lock", "Scroll Lock",
    "Num 7", "Num 8", "Num 9", "Num -", "Num 4", "Num 5", "Num 6", "Num +", "Num 1", "Num 2", "Num 3", "Num 0",
    "Num .",
    "F11", "F12", "F13", "F14", "F15", "Kana", "Convert", "NoConvert", "Yen", "Num =", "^", "@", ":", "_",
    "Kanji", "Stop", "AX", "Unlabeled", "Num Enter", "Right Ctrl", "Num ,", "Num /", "SysRq", "Right Alt",
    "Pause",
    "Home", "Up", "Page Up", "Left", "Right", "End", "Down", "Page Down", "Insert", "Delete", "Left Win",
    "Right Win", "Menu", "Power", "Sleep",
};
static_assert(sizeof(kKeyboardNames) / sizeof(kKeyboardNames[0]) == 187 - kFirstKeyboardPad + 1, "keyboard names");

// Mouse pads 191-195.
constexpr const char* kMouseNames[] = {"Mouse Left", "Mouse Right", "Mouse Middle", "Mouse Back", "Mouse Forward"};

// Held reads, as the pad manager's hold check reads them. Keyboard pads 65-147
// are DirectInput key codes 1-83; the rest are listed.
constexpr int32_t kLastPlainKeyPad = 147;
constexpr int32_t kKeyPadOffset = 64;
constexpr uint8_t kExtendedKeyCodes[] = {
    87, 88, 100, 101, 102, 112, 121, 123, 125, 141, 144, 145, 146, 147, 148, 149, 150, 151, 156, 157,
    179, 181, 183, 184, 197, 199, 200, 201, 203, 205, 207, 208, 209, 210, 211, 219, 220, 221, 222, 223,
};
static_assert(sizeof(kExtendedKeyCodes) == 187 - kLastPlainKeyPad, "extended key codes");
constexpr uint8_t kKeyDown = 0x80;
// Controller pads 1-20, as bits of the held mask. The stick directions
// include the stick's own bit.
constexpr uint64_t kControllerMasks[] = {
    0x1, 0x2, 0x4, 0x8, 0x10001, 0x40002, 0x80004, 0x20008, 0x1000, 0x2000,
    0x4000, 0x8000, 0x100, 0x200, 0x400, 0x800, 0x40, 0x80, 0x20, 0x10,
};
// The mouse manager's button record of pads 191-195.
constexpr size_t kMouseButtonIndex[] = {0, 1, 2, 4, 3};
// One past the highest pad id.
constexpr int32_t kPadIdCount = kFirstMousePad + static_cast<int32_t>(sizeof(kMouseNames) / sizeof(kMouseNames[0]));

PadCaptureFn g_PadCapture = nullptr;
bool g_Installed = false;
bool g_Capturing = false;
bool g_AnyHeld = false;
bool g_LoggedTextLeak = false;
bool g_Focused = false;
int32_t g_WheelSteps = 0;
// This frame's pressed pad per device type.
int32_t g_Pressed[kPadTypeCount] = {};
// Per pad id: reported as pressed this frame, and down last frame.
bool g_PressedPads[kPadIdCount] = {};
bool g_Latched[kPadIdCount] = {};
// False until a focused frame has read every pad's state, so pads already
// down when focus returns do not report.
bool g_LatchesReady = false;

template <size_t N>
const char* NameIn(const char* const (&names)[N], int32_t pad, int32_t first) {
    const int32_t index = pad - first;
    return index >= 0 && index < static_cast<int32_t>(N) ? names[index] : nullptr;
}

int32_t TypeOf(int32_t pad) {
    if (pad >= kFirstMousePad) {
        return retail::kPadTypeMouse;
    }
    return pad >= kFirstKeyboardPad ? retail::kPadTypeKeyboard : retail::kPadTypeController;
}

int32_t Query(uint8_t* padManager, int32_t deviceType, int32_t mode) {
    // No remap: a binding is the physical pad, and the remap tables are not
    // ready in the first frames, where remapping reads a bad node.
    alignas(16) uint8_t query[retail::kPadQuerySize] = {};
    WriteAt<int32_t>(query, retail::kPadQueryDeviceMask, 0xFF);
    query[retail::kPadQueryWaitFlag] = 1;
    query[retail::kPadQueryIgnoreExclusive] = 1;
    WriteAt<uintptr_t>(query, retail::kPadQueryText, g_ModuleBase + retail::kCStringVtable);
    WriteAt<uintptr_t>(query, retail::kPadQueryText + retail::kCStringData, g_ModuleBase + retail::kEmptyStringData);

    void* const* ref = nullptr;
    g_PadCapture(padManager, &ref, deviceType, mode, query);
    if (ReadAt<int32_t>(query, retail::kPadQueryText + retail::kCStringMode) == 1 && !g_LoggedTextLeak) {
        Log("Pad query text was assigned and is not freed.");
        g_LoggedTextLeak = true;
    }
    const void* record = ref ? *ref : nullptr;
    return record ? static_cast<int32_t>(ReadAt<int64_t>(record, 0)) : kNoPad;
}

// The mouse manager, when it takes mouse input at all.
const uint8_t* ActiveMouse() {
    const uint8_t* mouse = ReadGlobal<const uint8_t*>(retail::kMouseManager);
    if (!mouse || mouse[retail::kMouseGateOff] != 0 || mouse[retail::kMouseGateActive] == 0 ||
        mouse[retail::kMouseGateFocused] == 0) {
        return nullptr;
    }
    return mouse;
}

bool KeyHeld(int32_t pad) {
    const int32_t code = pad <= kLastPlainKeyPad ? pad - kKeyPadOffset : kExtendedKeyCodes[pad - kLastPlainKeyPad - 1];
    const uint8_t* keyboard = reinterpret_cast<const uint8_t*>(g_ModuleBase + retail::kKeyboardState);
    return (keyboard[retail::kKeyboardDown + code] & kKeyDown) != 0;
}

bool ControllerHeld(int32_t pad) {
    const uint64_t mask = kControllerMasks[pad - 1];
    const uint8_t* states = reinterpret_cast<const uint8_t*>(g_ModuleBase + retail::kControllerStates);
    for (int controller = 0; controller < retail::kControllerCount; ++controller) {
        if ((ReadAt<uint64_t>(states, controller * retail::kControllerStateSize) & mask) == mask) {
            return true;
        }
    }
    return false;
}

bool MouseHeld(int32_t pad) {
    const uint8_t* mouse = ActiveMouse();
    if (!mouse) {
        return false;
    }
    const size_t record = retail::kMouseButtons + retail::kMouseButtonSize * kMouseButtonIndex[pad - kFirstMousePad];
    return (ReadAt<uint32_t>(mouse, record) & retail::kMouseButtonHeld) != 0;
}

bool HeldNow(int32_t pad) {
    switch (TypeOf(pad)) {
        case retail::kPadTypeController:
            return ControllerHeld(pad);
        case retail::kPadTypeKeyboard:
            return KeyHeld(pad);
        default:
            return MouseHeld(pad);
    }
}

void UpdatePresses() {
    for (int32_t pad = 1; pad < kPadIdCount; ++pad) {
        if (!PadName(pad)) {
            continue;
        }
        const bool down = g_Pressed[TypeOf(pad)] == pad || HeldNow(pad);
        g_PressedPads[pad] = g_LatchesReady && down && !g_Latched[pad];
        g_Latched[pad] = down;
    }
    g_LatchesReady = true;
}

int32_t ReadWheel() {
    const uint8_t* mouse = ActiveMouse();
    if (!mouse) {
        return 0;
    }
    const uint32_t wheel = ReadAt<uint32_t>(mouse, retail::kMouseWheel);
    if (wheel & retail::kMouseWheelUp) {
        return 1;
    }
    return (wheel & retail::kMouseWheelDown) ? -1 : 0;
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kInputRequirements)) {
        return false;
    }
    g_PadCapture = GameFunction<PadCaptureFn>(retail::kPadCapture);
    g_Installed = true;
    return true;
}

void Tick() {
    std::fill(std::begin(g_Pressed), std::end(g_Pressed), kNoPad);
    std::fill(std::begin(g_PressedPads), std::end(g_PressedPads), false);
    g_AnyHeld = false;
    g_Focused = false;
    g_WheelSteps = 0;
    // The capture looks pads up in the framework_pad table of the registry.
    uint8_t* padManager = g_Installed ? ReadGlobal<uint8_t*>(retail::kPadManager) : nullptr;
    if (!padManager || !ReadGlobal<void*>(retail::kDatabaseRegistry) || !GameWindowFocused()) {
        g_LatchesReady = false;
        return;
    }
    g_Focused = true;
    g_WheelSteps = ReadWheel();
    const int32_t lockout = ReadAt<int32_t>(padManager, retail::kPadManagerLockout);
    if (g_Capturing) {
        WriteAt<int32_t>(padManager, retail::kPadManagerLockout, 0);
    }
    for (int32_t type = 0; type < kPadTypeCount; ++type) {
        g_Pressed[type] = Query(padManager, type, retail::kPadModeTrigger);
        if (g_Capturing && !g_AnyHeld) {
            g_AnyHeld = Query(padManager, type, retail::kPadModeHold) != kNoPad;
        }
    }
    if (g_Capturing) {
        WriteAt<int32_t>(padManager, retail::kPadManagerLockout, std::max(lockout, kCaptureLockoutFrames));
    }
    UpdatePresses();
}

bool Pressed(int32_t pad) {
    return pad > kNoPad && pad < kPadIdCount && g_PressedPads[pad];
}

bool Held(int32_t pad) {
    return g_Focused && pad != kNoPad && PadName(pad) && HeldNow(pad);
}

int32_t WheelSteps() {
    return g_WheelSteps;
}

int32_t PressedOn(Device device) {
    if (device == Device::Controller) {
        return g_Pressed[retail::kPadTypeController];
    }
    const int32_t key = g_Pressed[retail::kPadTypeKeyboard];
    return key != kNoPad ? key : g_Pressed[retail::kPadTypeMouse];
}

void SetCapturing(bool capturing) {
    g_Capturing = capturing;
    if (!capturing) {
        g_AnyHeld = false;
    }
}

bool AnyHeld() {
    return g_AnyHeld;
}

bool Bindable(Device device, int32_t pad) {
    if (!PadName(pad) || pad == kNoPad) {
        return false;
    }
    const int32_t type = TypeOf(pad);
    if (device == Device::Controller) {
        return type == retail::kPadTypeController;
    }
    return type != retail::kPadTypeController && pad != kPadEscape && pad != kPadDelete && pad != kMouseLeft &&
           pad != kMouseRight;
}

const char* PadName(int32_t pad) {
    if (pad == kNoPad) {
        return "None";
    }
    switch (TypeOf(pad)) {
        case retail::kPadTypeController:
            return NameIn(kControllerNames, pad, 1);
        case retail::kPadTypeKeyboard:
            return NameIn(kKeyboardNames, pad, kFirstKeyboardPad);
        default:
            return NameIn(kMouseNames, pad, kFirstMousePad);
    }
}

}  // namespace input
