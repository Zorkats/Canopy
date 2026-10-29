#pragma once
#include <cstddef>
#include <cstdint>

#include "game_retail.h"
#include "input.h"

// The settings mods show on the Mod Settings page of the game's options menu.
// Each mod keeps its own in mods\<key>.ini, in a section named after it. Mods
// register themselves and their settings while the loader starts them, before
// hooks are enabled; the store is then sealed, so the options menu can read
// it with no lock. Mods reach it only through the loader's API
// (mod_loader.cpp) and the Lua host.
namespace mod_config {

// One UTF-8 string per language index (JA, EN, FR, ES, ZH-TW, KO, ZH-CN).
using Text = const char* [retail::kLanguageCount];

// Returned when a mod or setting could not be registered. A missing setting
// reads as enabled, so a feature keeps its default behavior.
constexpr int kNoMod = -1;
constexpr int kNoSetting = -1;

// Storage limits. The options menu sizes its fake records from them.
constexpr size_t kMaxMods = 64;
constexpr size_t kMaxSettings = 256;
// The most choices one Choice setting can have.
constexpr int32_t kMaxChoices = 8;
// The most values one Slider setting can have: min, max and every step
// between them.
constexpr int32_t kMaxSliderValues = 256;
// Longest mod or setting key.
constexpr size_t kMaxKeyLength = 32;

struct Mod {
    // INI file name and section.
    const char* key;
    const char* const* name;
};

// A Button stores nothing: its value is always 0 and it has no file entry.
enum class Kind { Toggle, Binding, Choice, Button, Slider };

struct Setting {
    Kind kind;
    // The device a binding takes its pad from.
    input::Device device;
    int mod;
    const char* key;
    const char* const* label;
    const char* const* help;
    // A choice's names, choiceCount of them, each one string per language.
    const Text* choices;
    int32_t choiceCount;
    // A slider's values: min, min + step, and so on up to max.
    int32_t min;
    int32_t max;
    int32_t step;
    // 0 or 1 for a toggle, a framework pad id for a binding, the chosen
    // index for a choice, the number itself for a slider.
    int32_t value;
};

// Registers a mod, shown on the page as one dropdown holding its settings.
// The strings must outlive the process. Returns the mod's handle.
int RegisterMod(const char* key, const Text& name);

// Registers an on/off setting of mod, saved under key in the mod's section.
// The strings must outlive the process. Returns the setting's handle.
int AddToggle(int mod, const char* key, const Text& label, const Text& help, bool defaultOn);

// Registers a hotkey binding of mod: one pad of device, input::kNoPad for
// none. The strings must outlive the process. Returns the setting's handle.
int AddBinding(int mod, const char* key, const Text& label, const Text& help, input::Device device, int32_t defaultPad);

// Registers a setting of mod that picks one of count choices (2 to
// kMaxChoices), saved as the chosen index. The strings must outlive the
// process. Returns the setting's handle.
int AddChoice(
    int mod, const char* key, const Text& label, const Text& help, const Text* choices, int32_t count,
    int32_t defaultIndex
);

// Registers a setting of mod that picks a number from min to max in steps
// of step, saved as the number. max - min must be a whole number of steps,
// the range at most kMaxSliderValues values, and defaultValue one of them.
// A stored number is clamped to the range and moved to the nearest step.
// The strings must outlive the process. Returns the setting's handle.
int AddSlider(
    int mod, const char* key, const Text& label, const Text& help, int32_t min, int32_t max, int32_t step,
    int32_t defaultValue
);

// Registers a button row of mod. key names it in the log and must be unique
// like a setting's. The strings must outlive the process. Returns the
// setting's handle.
int AddButton(int mod, const char* key, const Text& label);

bool Enabled(int setting);
// The bound pad, input::kNoPad when unbound or not registered.
int32_t Binding(int setting);
// The chosen index of a choice, fallback when it is not registered.
int32_t Choice(int setting, int32_t fallback);
// Any setting's raw value, fallback when it is not registered.
int32_t Value(int setting, int32_t fallback);

// True when key is 1 to kMaxKeyLength letters, digits, '_' or '-', the rule
// for mod and setting keys. Any thread.
bool KeyValid(const char* key);

// The full path of mod's settings file, or null for a bad handle.
const char* SettingsFile(int mod);

// Ends registration. Every later Register or Add call fails.
void Seal();
bool Sealed();
// Drops the mods and settings registered after the first modCount and
// settingCount, for a mod whose start failed. Before Seal only.
void Truncate(size_t modCount, size_t settingCount);

size_t ModCount();
const Mod& ModAt(size_t index);
size_t Count();
const Setting& At(size_t index);
// Stores and saves a new value. Game thread only.
void SetValue(size_t index, int32_t value);

}  // namespace mod_config
