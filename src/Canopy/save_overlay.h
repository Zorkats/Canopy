#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "canopy_api.h"

// Overlay save: mod state that lives in the live save data while a game runs
// but never reaches the game's save file. Each kind of state is an adapter.
// Before the save data is written the core strips every adapter's state out
// of it, and right after the write it puts it back; each adapter keeps its
// own file, mods\CanopyData\slot<N>.<name>.ini, written only when the save
// succeeds. See the design notes in save_overlay.cpp.
namespace save_overlay {

// How a finished load found an adapter's file.
enum class FileState { Missing, Read, Unreadable };

// One kind of mod state. Every callback may be null, and every one runs on
// the game thread.
struct Adapter {
    // The file name part: slot<N>.<name>.ini. Lowercase letters.
    const char* name;
    // The log prefix, such as "Mod items".
    const char* label;
    // One entry and several, for the log: "item" and "items".
    const char* one;
    const char* many;
    // The file's line format, for its header comment: "Each line: ...".
    const char* format;
    // Lower strips first and restores last.
    int order;
    // A fresh save data was just reset to the new game state.
    void (*reset)(uint8_t* saveData);
    // The save data is about to be written: take the mod state out of it.
    // May run twice before one restore.
    void (*strip)(uint8_t* saveData);
    // The file body as of the strip, from its [section] line on, empty for
    // none, and how many entries it holds. Runs right after strip. The core
    // adds the header comment.
    std::string (*snapshot)(size_t& count);
    // The write is done: put the state back. saveData is the live save
    // data, null when there is none.
    void (*restore)(uint8_t* saveData);
    // A load of slot finished well; slot is 0 when unknown. text is the
    // file's text when state is Read.
    void (*loaded)(int32_t slot, FileState state, const std::string& text);
    // The loaded save data is live, and loaded has run for it.
    void (*afterLoad)(uint8_t* saveData);
    // Continue mode read its snapshot back into the live save data.
    void (*afterInPlace)(uint8_t* saveData);
    // The title screen was reached: drop everything held for the old game.
    // The save data is not touched.
    void (*clear)();
};

// Adds an adapter. The first one checks the signatures and creates the
// core's hooks, and takes save_data's slot callback; with no adapter
// nothing is hooked. False, with the adapter left out, when the hooks cannot
// be made. Startup thread, before hooks are enabled. The caller must have
// the frame tick and the per-save data.
bool Register(const Adapter& adapter);

// True between a strip and its restore. APIs that change overlay state
// refuse while it is. Game thread.
bool Stripped();

// Handles finished saves, loads and deletes, and restores after a write.
// Game thread, once per frame, before the frame's events.
void Tick();

// The frame's game events. Game thread, before the mods hear of them.
void OnEvent(CanopyEvent event);

// The live save data, null when there is none.
uint8_t* LiveSaveData();

// ---- Shared primitives ---------------------------------------------------
// For MSVC unordered_map<u64, T> and vectors of pointers in the save data.

// The node of key in map, null when there is none.
uint8_t* MapFind(uint8_t* map, uint64_t key);
// Unlinks node from map and keeps it: no free, no reference change. False,
// with nothing changed, when the map has no buckets.
bool MapPark(uint8_t* map, uint8_t* node);
// Links a parked node back, in the bucket of the map's current mask. False,
// with nothing changed, when its key is in the map again.
bool MapUnpark(uint8_t* map, uint8_t* node);

// A pointer taken out of a vector and the index it had.
struct VectorEntry {
    size_t index;
    void* value;
};

// Compacts every element match accepts out of a vector of pointers (begin,
// end and capacity at +0, +8 and +16), appending each with its index before
// the compaction to removed. Returns how many.
size_t VectorRemove(uint8_t* vector, bool (*match)(const void*), std::vector<VectorEntry>& removed);
// Puts entries back at their indices, lowest first, within the vector's
// capacity. False, with nothing changed, when the capacity is short.
bool VectorReinsert(uint8_t* vector, std::vector<VectorEntry> entries);

}  // namespace save_overlay
