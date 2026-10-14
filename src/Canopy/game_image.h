#pragma once
#include <cstdint>

// Facts about the running village.exe image. Any thread.
namespace game_image {

// The executable's PE header TimeDateStamp, which differs between builds.
uint32_t BuildId();

// The address of the first match of pattern in the executable's code
// sections, or 0. pattern is hex bytes separated by spaces, with ? or ?? for
// any byte. reason is set when the pattern itself is not valid.
uintptr_t FindPattern(const char* pattern, const char*& reason);

}  // namespace game_image
