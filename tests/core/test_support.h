#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

// What the core tests need from the system: a process id for temporary
// folder names and the game folder to read. The game folder is
// CANOPY_GAME_DIR when it is set, else the usual Steam install.
namespace test_support {

inline unsigned long ProcessId() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

inline std::filesystem::path GameDir() {
#ifdef _WIN32
    wchar_t overrideDir[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableW(L"CANOPY_GAME_DIR", overrideDir, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::path(L"D:\\SteamLibrary\\steamapps\\common\\Village in the Shade");
#else
    const char* overrideDir = std::getenv("CANOPY_GAME_DIR");
    if (overrideDir && *overrideDir) {
        return std::filesystem::u8path(overrideDir);
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::u8path(std::string(home ? home : "") +
                                   "/.local/share/Steam/steamapps/common/Village in the Shade");
#endif
}

}  // namespace test_support
