#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "game_retail.h"

// Shared by the loader and the mods built in this repository: module base,
// log, memory access, engine object helpers and detour bookkeeping. Each DLL
// links its own copy. Everything here runs on the game thread unless it says
// otherwise.
namespace core {

// village.exe's load address. Each DLL sets it before using anything here.
extern uintptr_t g_ModuleBase;

// Appends a line to Canopy.log. Thread safe. Each DLL provides its
// own: the loader writes the file, a mod goes through the API.
void Log(const std::string& message);
std::string Hex(uintptr_t value);

// Full path of a file next to the DLL that calls it.
std::string ModFilePath(const char* fileName);

template <typename T>
T ReadAt(const void* base, size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteAt(void* base, size_t offset, T value) {
    std::memcpy(static_cast<uint8_t*>(base) + offset, &value, sizeof(T));
}

template <typename T>
T ReadGlobal(uintptr_t rva) {
    return *reinterpret_cast<T*>(g_ModuleBase + rva);
}

template <typename Fn>
Fn GameFunction(uintptr_t rva) {
    return reinterpret_cast<Fn>(g_ModuleBase + rva);
}

// Finds the game addresses of the running village.exe, once per DLL, after
// g_ModuleBase is set: from game_builds.txt for a checked build, otherwise
// by searching the exe file, or the loaded image when the file cannot be
// read. Never logs, so the loader runs it under the loader lock.
void ResolveAddresses();
bool AddressesResolved();
// True when the running build is listed in game_builds.txt. Features that
// write saves only run on a checked build.
bool CheckedBuild();
// How the addresses were found, one line for the log.
std::string AddressReport();
// The resolved RVA of the address called name, 0 when it was not found or
// nothing is resolved yet.
uintptr_t FindAddress(const char* name);
// The resolved address or field value called name, false when it was not
// found or nothing is resolved yet.
bool FindValue(const char* name, uintptr_t& value);

// True when the running build meets every requirement in the list; logs the
// first one it does not.
template <size_t N>
bool RequirementsMet(const retail::Requirement* const (&requirements)[N]) {
    for (const retail::Requirement* requirement : requirements) {
        if (!requirement->Met()) {
            Log(requirement->Problem());
            return false;
        }
    }
    return true;
}

// UI language index, English when it is out of range.
int32_t CurrentLanguage();

// True while the field player has input: no menu, dialog, event or loading
// screen owns the pad.
bool PlayerInField();

// True while the game window has keyboard focus. Thread safe.
bool GameWindowFocused();

// Engine objects are intrusively refcounted: a 32-bit count at refOffset and
// a deleting destructor in vtable slot 0.
void AddRef(uint8_t* object, size_t refOffset);
void Release(uint8_t* object, size_t refOffset);

// Zeroed memory from the engine heap, which engine objects are freed to.
uint8_t* AllocateZeroed(size_t size);

// Replaces an Ngf::CString's text.
void AssignText(void* text, const char* data);

// UTF-8 logtext row in the current language, or null.
const char* LogText(uint64_t logId);

// Attaches a task to a parent task. Holds a temporary reference across the
// call like the engine's own call sites.
void AttachTask(void* parent, uint8_t* task, int32_t mode);

// The engine and this DLL are both MSVC x64 builds, so std::function has the
// engine's layout: 64 bytes with the target pointer in the last slot.
using PromptCallback = std::function<void()>;
static_assert(sizeof(PromptCallback) == 64, "std::function layout differs from the engine's");

// Builds the engine's Yes/No popup with the game's own button labels and one
// message, which may use the engine's <br> and <c> tags. The caller attaches
// it. The callbacks run inside the popup's update. Null on failure.
uint8_t* CreateYesNoPopup(const char* message, PromptCallback onYes, PromptCallback onNo);

// Fades every sound player out, as the engine's own title option does, then
// returns to the title screen, which destroys every field and menu layer.
// Call it from the frame tick, never from inside a task's update.
void ReturnToTitle();

// The detours one feature created. A feature that fails part way removes
// only its own, so the others keep working. Each DLL provides Add and
// RemoveAll: the loader through MinHook, a mod through the API.
class DetourSet {
public:
    explicit DetourSet(const char* feature) : feature_(feature) {}

    bool Add(uintptr_t rva, void* detour, void** original, const char* name);
    void RemoveAll();

private:
    const char* feature_;
    std::vector<void*> targets_;
};

}  // namespace core
