#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <ctime>
#include <mutex>
#include <string>

#include "core.h"
#include "hook_chain.h"

// The loader's side of core.h: the log file every DLL writes through, and
// detours linked first into the hook chains mods share.

namespace core {
namespace {

constexpr const char* kLogFileName = "Canopy.log";

std::mutex g_LogMutex;
HANDLE g_LogFileHandle = INVALID_HANDLE_VALUE;

}  // namespace

void Log(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_LogMutex);
    if (g_LogFileHandle == INVALID_HANDLE_VALUE) {
        const std::string path = ModFilePath(kLogFileName);
        g_LogFileHandle = CreateFileA(
            path.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (g_LogFileHandle == INVALID_HANDLE_VALUE) {
            return;
        }
    }

    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char timeText[26];
    ctime_s(timeText, sizeof(timeText), &time);
    timeText[24] = '\0';

    const std::string line = std::string("[") + timeText + "] " + message + "\r\n";
    DWORD bytesWritten = 0;
    WriteFile(g_LogFileHandle, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr);
    FlushFileBuffers(g_LogFileHandle);
}

bool DetourSet::Add(uintptr_t rva, void* detour, void** original, const char* name) {
    void* target = reinterpret_cast<void*>(g_ModuleBase + rva);
    if (const char* reason = hook_chain::Add(target, hook_chain::kLoaderOwner, detour, original)) {
        Log(std::string(feature_) + ": the hook for " + name + " was not created, " + reason + ".");
        return false;
    }
    targets_.push_back(target);
    return true;
}

void DetourSet::RemoveAll() {
    for (void* target : targets_) {
        hook_chain::Remove(target, hook_chain::kLoaderOwner);
    }
    targets_.clear();
}

}  // namespace core
