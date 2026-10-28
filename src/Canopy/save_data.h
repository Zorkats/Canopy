#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// Per-save mod data: values each mod keeps with the save the player is
// playing, in one section per mod key. They live in memory while a game runs.
// A successful save to slot N writes them to mods\CanopyData\slot<N>.ini, a
// successful load of slot N reads that file back, and deleting slot N deletes
// it. The title screen clears them. Every function is thread safe. The API in
// mod_loader.cpp reaches the store through this interface only, and so will
// any later bridge.
namespace save_data {

// Longest value, in bytes.
constexpr size_t kMaxValueLength = 1024;
// The most values one section may hold.
constexpr size_t kMaxValuesPerSection = 1024;
// Slots count from 1. Retail uses 1 to 16; the cap keeps file names sane.
constexpr int32_t kMaxSlot = 99;

// Hooks the save utility's acknowledge, which every finished save, load and
// delete goes through. False on an unsupported game build, where every Set
// is refused.
bool Install();

// Drops every value. Called when the title screen is reached.
void Reset();

// Stores value, one line of UTF-8 text, under key in section. key follows
// the setting key rules. Returns why it cannot, or null.
const char* Set(const std::string& section, const char* key, const char* value);
// The value under key in section, false when there is none.
bool Get(const std::string& section, const char* key, std::string& value);
bool Has(const std::string& section, const char* key);
// Removes the value under key, false when there was none.
bool Remove(const std::string& section, const char* key);

// Set and Get for a number, stored as its decimal text. GetInt is false when
// the value is missing or is not a whole number.
const char* SetInt(const std::string& section, const char* key, int32_t value);
bool GetInt(const std::string& section, const char* key, int32_t& value);

// Told about every finished game save, game load and slot delete, good or
// failed, with the save utility's operation (retail::kSaveUtilOperation*)
// and the slot, 0 when unknown. Runs on whatever thread acknowledges the
// request, so it must only queue. The overlay save core (save_overlay.cpp)
// takes it and hands each event to its adapters. Startup thread, once.
using SlotCallback = void (*)(int32_t operation, int32_t slot, bool ok);
void SetSlotCallback(SlotCallback callback);

// The whole file at path, false when it cannot be read or is over 16 MB.
bool ReadFileText(const std::string& path, std::string& text);
// Writes text to a temporary file and moves it over path, so path holds
// either its old text or the new one. 0, or the Windows error.
uint32_t WriteFileAtomically(const std::string& path, const std::string& text);

}  // namespace save_data
