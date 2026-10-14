#define NOMINMAX
#include "mod_config.h"

#include <windows.h>

#include <cstring>
#include <string>

#include "core.h"

namespace mod_config {
namespace {

// Marks a key the file does not have yet.
constexpr int kMissing = -1;

// The options menu reads both lists with no lock, so the storage never moves.
Mod g_Mods[kMaxMods] = {};
std::string g_Files[kMaxMods];
size_t g_ModCount = 0;
Setting g_Settings[kMaxSettings] = {};
size_t g_Count = 0;
bool g_Sealed = false;

bool ValidMod(int mod) {
    return mod >= 0 && static_cast<size_t>(mod) < g_ModCount;
}

bool ValidSetting(int setting) {
    return setting >= 0 && static_cast<size_t>(setting) < g_Count;
}

// A key is a file name and an INI key at once, so it keeps to characters
// that are safe in both.
bool ValidKey(const char* key) {
    const size_t length = key ? std::strlen(key) : 0;
    if (length == 0 || length > kMaxKeyLength) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        const char c = key[index];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        if (!letter && !digit && c != '_' && c != '-') {
            return false;
        }
    }
    return true;
}

// A setting as the log names it: mod key, then setting key.
std::string SettingName(int mod, const char* key) {
    return std::string("Setting ") + (ValidMod(mod) ? std::string(g_Mods[mod].key) + "." : "") + (key ? key : "(null)");
}

void Save(const Setting& setting) {
    WritePrivateProfileStringA(
        g_Mods[setting.mod].key, setting.key, std::to_string(setting.value).c_str(), g_Files[setting.mod].c_str()
    );
}

// Why a setting cannot be added, or null when it can.
const char* RejectSetting(int mod, const char* key) {
    if (g_Sealed) {
        return "settings can only be added while the mod starts";
    }
    if (!ValidMod(mod)) {
        return "its mod is not registered";
    }
    if (!ValidKey(key)) {
        return "its key must be 1 to 32 letters, digits, '_' or '-'";
    }
    if (g_Count == kMaxSettings) {
        return "the settings list is full";
    }
    for (size_t index = 0; index < g_Count; ++index) {
        if (g_Settings[index].mod == mod && _stricmp(g_Settings[index].key, key) == 0) {
            return "the mod already has a setting with this key";
        }
    }
    return nullptr;
}

// Stores a new setting with its file value, or defaultValue when the file has
// none. read checks a stored value. Returns its handle.
template <typename Read>
int AddSetting(int mod, const char* key, const Text& label, const Text& help, Kind kind, input::Device device,
               int32_t defaultValue, Read read, const Text* choices = nullptr, int32_t choiceCount = 0) {
    if (const char* reason = RejectSetting(mod, key)) {
        core::Log(SettingName(mod, key) + " not registered, " + reason + ".");
        return kNoSetting;
    }
    Setting& setting = g_Settings[g_Count];
    setting.kind = kind;
    setting.device = device;
    setting.mod = mod;
    setting.key = key;
    setting.label = label;
    setting.help = help;
    setting.choices = choices;
    setting.choiceCount = choiceCount;
    const int stored = static_cast<int>(GetPrivateProfileIntA(g_Mods[mod].key, key, kMissing, g_Files[mod].c_str()));
    setting.value = stored == kMissing ? defaultValue : read(stored, defaultValue);
    // A new key is written right away, so the file lists every setting.
    if (stored == kMissing) {
        Save(setting);
    }
    return static_cast<int>(g_Count++);
}

}  // namespace

int RegisterMod(const char* key, const Text& name) {
    const char* reason = nullptr;
    if (g_Sealed) {
        reason = "mods can only register while the loader starts them";
    } else if (!ValidKey(key)) {
        reason = "its key must be 1 to 32 letters, digits, '_' or '-'";
    } else if (g_ModCount == kMaxMods) {
        reason = "the mod list is full";
    } else {
        for (size_t index = 0; index < g_ModCount; ++index) {
            if (_stricmp(g_Mods[index].key, key) == 0) {
                reason = "another mod already uses this key";
            }
        }
    }
    if (reason) {
        core::Log(std::string("Mod ") + (key ? key : "(null)") + " not registered, " + reason + ".");
        return kNoMod;
    }
    g_Mods[g_ModCount] = {key, name};
    g_Files[g_ModCount] = core::ModFilePath((std::string("mods\\") + key + ".ini").c_str());
    return static_cast<int>(g_ModCount++);
}

int AddToggle(int mod, const char* key, const Text& label, const Text& help, bool defaultOn) {
    return AddSetting(
        mod, key, label, help, Kind::Toggle, input::Device::KeyboardMouse, defaultOn ? 1 : 0,
        [](int stored, int32_t) { return stored != 0 ? 1 : 0; }
    );
}

int AddBinding(int mod, const char* key, const Text& label, const Text& help, input::Device device, int32_t defaultPad) {
    if (defaultPad != input::kNoPad && !input::Bindable(device, defaultPad)) {
        core::Log(SettingName(mod, key) + " not registered, its default pad cannot be bound.");
        return kNoSetting;
    }
    return AddSetting(mod, key, label, help, Kind::Binding, device, defaultPad, [device](int stored, int32_t fallback) {
        return stored == input::kNoPad || input::Bindable(device, stored) ? stored : fallback;
    });
}

int AddChoice(
    int mod, const char* key, const Text& label, const Text& help, const Text* choices, int32_t count,
    int32_t defaultIndex
) {
    if (!choices || count < 2 || count > kMaxChoices || defaultIndex < 0 || defaultIndex >= count) {
        core::Log(SettingName(mod, key) + " not registered, its choices are not valid.");
        return kNoSetting;
    }
    return AddSetting(
        mod, key, label, help, Kind::Choice, input::Device::KeyboardMouse, defaultIndex,
        [count](int stored, int32_t fallback) { return stored >= 0 && stored < count ? stored : fallback; },
        choices, count
    );
}

int AddButton(int mod, const char* key, const Text& label) {
    if (const char* reason = RejectSetting(mod, key)) {
        core::Log(SettingName(mod, key) + " not registered, " + reason + ".");
        return kNoSetting;
    }
    Setting& setting = g_Settings[g_Count];
    setting = {};
    setting.kind = Kind::Button;
    setting.device = input::Device::KeyboardMouse;
    setting.mod = mod;
    setting.key = key;
    setting.label = label;
    return static_cast<int>(g_Count++);
}

bool Enabled(int setting) {
    return !ValidSetting(setting) || g_Settings[setting].value != 0;
}

int32_t Binding(int setting) {
    if (!ValidSetting(setting) || g_Settings[setting].kind != Kind::Binding) {
        return input::kNoPad;
    }
    return g_Settings[setting].value;
}

int32_t Choice(int setting, int32_t fallback) {
    if (!ValidSetting(setting) || g_Settings[setting].kind != Kind::Choice) {
        return fallback;
    }
    return g_Settings[setting].value;
}

int32_t Value(int setting, int32_t fallback) {
    return ValidSetting(setting) ? g_Settings[setting].value : fallback;
}

const char* SettingsFile(int mod) {
    return ValidMod(mod) ? g_Files[mod].c_str() : nullptr;
}

void Seal() {
    g_Sealed = true;
}

bool Sealed() {
    return g_Sealed;
}

void Truncate(size_t modCount, size_t settingCount) {
    if (g_Sealed || modCount > g_ModCount || settingCount > g_Count) {
        return;
    }
    for (size_t index = modCount; index < g_ModCount; ++index) {
        g_Mods[index] = {};
        g_Files[index].clear();
    }
    for (size_t index = settingCount; index < g_Count; ++index) {
        g_Settings[index] = {};
    }
    g_ModCount = modCount;
    g_Count = settingCount;
}

size_t ModCount() {
    return g_ModCount;
}

const Mod& ModAt(size_t index) {
    return g_Mods[index];
}

size_t Count() {
    return g_Count;
}

const Setting& At(size_t index) {
    return g_Settings[index];
}

void SetValue(size_t index, int32_t value) {
    if (index >= g_Count || g_Settings[index].kind == Kind::Button || g_Settings[index].value == value) {
        return;
    }
    const Setting& setting = g_Settings[index];
    g_Settings[index].value = value;
    Save(setting);
    std::string shown = std::to_string(value);
    if (setting.kind == Kind::Binding) {
        shown = input::PadName(value);
    } else if (setting.kind == Kind::Choice && value >= 0 && value < setting.choiceCount) {
        shown = setting.choices[value][retail::kLanguageEnglish];
    }
    core::Log(std::string("Setting ") + g_Mods[setting.mod].key + "." + setting.key + " set to " + shown + ".");
}

}  // namespace mod_config
