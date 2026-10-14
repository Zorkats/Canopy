#pragma once
#include <cstdint>

// Keyboard, mouse and controller presses read through the engine's own pad
// manager, by framework pad id, the ids the game's key config uses.
namespace input {

// The two kinds of binding a hotkey has.
enum class Device { KeyboardMouse, Controller };

constexpr int32_t kNoPad = 0;
constexpr int32_t kPadEscape = 65;
constexpr int32_t kPadDelete = 182;

bool Install();

// Reads this frame's presses. Runs first in the frame tick.
void Tick();

// True on the frame pad goes down, once per press, for every pad on its own.
// kNoPad is never pressed.
bool Pressed(int32_t pad);

// True while pad is held down, read from the devices' own state, so it holds
// through menus and events. False while the game window lacks focus.
bool Held(int32_t pad);

// This frame's mouse wheel: 1 for a notch up, -1 for a notch down, else 0.
int32_t WheelSteps();

// The pad of device pressed this frame, or kNoPad. The keyboard is read
// before the mouse.
int32_t PressedOn(Device device);

// While capturing, the game and its menus see no input, Pressed still sees
// every press, and AnyHeld tells whether any key or button is down.
void SetCapturing(bool capturing);
bool AnyHeld();

// True when pad can be bound on device. Escape, Delete and the left and right
// mouse buttons are kept for the menus.
bool Bindable(Device device, int32_t pad);

// The pad's English name, "None" for kNoPad.
const char* PadName(int32_t pad);

}  // namespace input
