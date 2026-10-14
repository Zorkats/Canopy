#pragma once
#include <cstddef>
#include <cstdint>

// Lets any number of owners, the loader and each mod, detour the same game
// function. See the design notes in hook_chain.cpp.
namespace hook_chain {

// The owner of the loader's own hooks. Mods own theirs by mod handle.
constexpr int64_t kLoaderOwner = -1;

// Links detour at the end of target's chain, so it runs after every detour
// created before it. Stores in *original the address that continues the
// chain, before the detour can be reached. Returns null on success, else why
// it failed. Once Started, a new chain is enabled at once.
const char* Add(void* target, int64_t owner, void* detour, void** original);

// Unlinks owner's detour from target's chain, leaving the others in place.
// False when owner does not hook target.
bool Remove(void* target, int64_t owner);

// Marks every chain as enabled: from now on new chains are enabled at once,
// and an emptied chain stays in place, passing calls straight to the game.
void Started();

}  // namespace hook_chain
