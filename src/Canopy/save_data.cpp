#define NOMINMAX
#include "save_data.h"

#include <windows.h>

#include <cerrno>
#include <cstdlib>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "core.h"
#include "mod_config.h"

// =============================================================
// PER-SAVE DATA DESIGN
// =============================================================
// Every save, load and delete the game asks of its save utility ends in one
// place: once the request is done, its poll (or the next request's start)
// calls the utility's acknowledge, which clears it. The acknowledge's detour
// reads the finished request first: its operation, slot, status and data
// size. That covers the game's own saves, the load menu, and VillageQoL's
// quick save and quick load, which use the same utility, without hooking
// each of their polls. VillageQoL hooks the acknowledge too; the chain runs
// ours first, then its detour.
//
// The system data (options and key config) goes through the same utility
// with a smaller buffer, so only requests with a game save's buffer size
// count as saves and loads. A load from the load menu leaves the request's
// slot at 0 and loads the selected entry of the utility's save list instead.
//
// The values go to mods\CanopyData\slot<N>.ini next to the loader, one
// section per mod key, never into the game's own save folder or save data.
// A successful save writes the whole file to a temporary file that then
// replaces it, so a failed write leaves the old file. A successful load
// replaces every value with the file's, and a successful delete removes the
// file. The title screen clears the values (mod_loader.cpp), so a new game,
// which starts from the title, starts with none. Sections of mods that are
// not installed are kept and written back.

namespace save_data {
namespace {

using namespace core;

using AcknowledgeFn = uint8_t(__fastcall*)(uint8_t* util);

constexpr const char* kFolder = "mods\\CanopyData";
// A larger file is not read.
constexpr LONGLONG kMaxFileSize = 16 * 1024 * 1024;

// Sections and keys compare without case, like mod and setting keys.
struct NoCase {
    bool operator()(const std::string& left, const std::string& right) const {
        return _stricmp(left.c_str(), right.c_str()) < 0;
    }
};

using Values = std::map<std::string, std::string, NoCase>;
using Store = std::map<std::string, Values, NoCase>;

AcknowledgeFn g_OriginalAcknowledge = nullptr;
// Set before the hooks are enabled, never changed after.
SlotCallback g_SlotCallback = nullptr;
// Set before any mod starts, never changed after.
bool g_Installed = false;
std::mutex g_Mutex;
// Guarded by g_Mutex. Never holds an empty section.
Store g_Store;

// Why value cannot be stored, or null when it can: one line of text.
const char* RejectValue(const std::string& value) {
    if (value.size() > kMaxValueLength) {
        return "its value is longer than 1024 bytes";
    }
    for (const char c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return "its value has a line break or another control character";
        }
    }
    return nullptr;
}

std::string SlotPath(int32_t slot) {
    return ModFilePath((std::string(kFolder) + "\\slot" + std::to_string(slot) + ".ini").c_str());
}

// The file's text: a comment, then each section with its values.
std::string Serialize(const Store& store, int32_t slot) {
    std::string text = "; Canopy per-save mod data for save slot " + std::to_string(slot) + ".\r\n";
    for (const auto& [section, values] : store) {
        text += "\r\n[" + section + "]\r\n";
        for (const auto& [key, value] : values) {
            text += key + "=" + value + "\r\n";
        }
    }
    return text;
}

// The sections of a file's text. Lines it cannot use are skipped.
Store Parse(const std::string& text) {
    Store store;
    Values* values = nullptr;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == ';') {
            continue;
        }
        if (line[0] == '[') {
            const std::string section = line.size() > 2 && line.back() == ']' ? line.substr(1, line.size() - 2) : "";
            values = mod_config::KeyValid(section.c_str()) ? &store[section] : nullptr;
            continue;
        }
        // A value may hold '=', a key never does.
        const size_t equals = line.find('=');
        if (!values || equals == std::string::npos || values->size() >= kMaxValuesPerSection) {
            continue;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (mod_config::KeyValid(key.c_str()) && !RejectValue(value)) {
            (*values)[key] = value;
        }
    }
    for (auto section = store.begin(); section != store.end();) {
        section = section->second.empty() ? store.erase(section) : std::next(section);
    }
    return store;
}

// The whole file, false when it cannot be read.
bool ReadText(const std::string& path, std::string& text) {
    HANDLE file =
        CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    LARGE_INTEGER size = {};
    bool read = GetFileSizeEx(file, &size) && size.QuadPart <= kMaxFileSize;
    if (read && size.QuadPart > 0) {
        text.resize(static_cast<size_t>(size.QuadPart));
        DWORD count = 0;
        read = ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &count, nullptr) && count == text.size();
    }
    CloseHandle(file);
    return read;
}

// Writes text to a temporary file, then moves it over path, so path holds
// either its old text or the new one. Returns 0, or the Windows error.
DWORD WriteAtomically(const std::string& path, const std::string& text) {
    const std::string temporary = path + ".tmp";
    HANDLE file =
        CreateFileA(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    DWORD written = 0;
    bool done = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                written == text.size() && FlushFileBuffers(file);
    DWORD error = done ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (done && !MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        done = false;
        error = GetLastError();
    }
    if (!done) {
        DeleteFileA(temporary.c_str());
        // A short write sets no error code, and still failed.
        return error != ERROR_SUCCESS ? error : ERROR_WRITE_FAULT;
    }
    return ERROR_SUCCESS;
}

std::string SlotName(int32_t slot) {
    return "slot " + std::to_string(slot);
}

std::string SectionCount(size_t count) {
    return std::to_string(count) + (count == 1 ? " mod" : " mods");
}

// A save to slot finished: its file gets the current values. With none, an
// older file would come back on the next load, so it is deleted.
void WriteSlot(int32_t slot) {
    const std::string path = SlotPath(slot);
    std::string text;
    size_t sections = 0;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        sections = g_Store.size();
        if (sections > 0) {
            text = Serialize(g_Store, slot);
        }
    }
    if (sections == 0) {
        if (DeleteFileA(path.c_str())) {
            Log("Per-save data: " + SlotName(slot) + " saved with no values, its old file was deleted.");
        }
        return;
    }
    CreateDirectoryA(ModFilePath(kFolder).c_str(), nullptr);
    if (const DWORD error = WriteAtomically(path, text)) {
        Log("Per-save data: " + SlotName(slot) + " could not be written, error " + std::to_string(error) + ".");
        return;
    }
    Log("Per-save data: wrote " + SlotName(slot) + ", " + SectionCount(sections) + ".");
}

// A load of slot finished: the values become the file's, or none.
void ReadSlot(int32_t slot) {
    const std::string path = SlotPath(slot);
    std::string text;
    Store loaded;
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log("Per-save data: " + SlotName(slot) + " has none.");
    } else if (!ReadText(path, text)) {
        Log("Per-save data: " + SlotName(slot) + " could not be read, starting with none.");
    } else {
        loaded = Parse(text);
        Log("Per-save data: loaded " + SlotName(slot) + ", " + SectionCount(loaded.size()) + ".");
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Store = std::move(loaded);
}

// A delete of slot finished. The values in memory stay: a quick load deletes
// the slot it just loaded.
void DeleteSlot(int32_t slot) {
    if (DeleteFileA(SlotPath(slot).c_str())) {
        Log("Per-save data: deleted " + SlotName(slot) + ".");
    }
}

// The slot a finished request used, 0 when unknown. A load from the load
// menu leaves the request's slot at 0 and loads the selected entry of the
// utility's save list.
int32_t RequestSlot(const uint8_t* util, int32_t operation) {
    const int32_t slot = ReadAt<int32_t>(util, retail::kSaveUtilSlot);
    if (slot != 0 || operation != retail::kSaveUtilOperationLoad) {
        return slot;
    }
    const uint8_t* list = util + retail::kSaveUtilList;
    const uint8_t* begin = ReadAt<const uint8_t*>(list, retail::kSaveListBegin);
    const uint8_t* end = ReadAt<const uint8_t*>(list, retail::kSaveListEnd);
    const int32_t selected = ReadAt<int32_t>(list, retail::kSaveListSelected);
    const size_t count = begin && end > begin ? static_cast<size_t>(end - begin) / retail::kSaveEntrySize : 0;
    if (selected < 0 || static_cast<size_t>(selected) >= count) {
        return 0;
    }
    return ReadAt<int32_t>(begin + retail::kSaveEntrySize * selected, retail::kSaveEntrySlot);
}

void Finish(const uint8_t* util) {
    const int32_t operation = ReadAt<int32_t>(util, retail::kSaveUtilOperation);
    const int32_t slot = RequestSlot(util, operation);
    if (slot < 1 || slot > kMaxSlot) {
        return;
    }
    // The system data is saved and loaded with a smaller buffer.
    const bool gameSave = ReadAt<uint32_t>(util, retail::kSaveUtilDataSize) == retail::kGameSaveDataSize;
    if (operation == retail::kSaveUtilOperationSave && gameSave) {
        WriteSlot(slot);
    } else if (operation == retail::kSaveUtilOperationLoad && gameSave) {
        ReadSlot(slot);
    } else if (operation == retail::kSaveUtilOperationDelete) {
        DeleteSlot(slot);
    }
}

// Tells the slot callback about a finished game save, game load or delete.
void Notify(const uint8_t* util, bool ok) {
    if (!g_SlotCallback) {
        return;
    }
    const int32_t operation = ReadAt<int32_t>(util, retail::kSaveUtilOperation);
    const bool gameSave = ReadAt<uint32_t>(util, retail::kSaveUtilDataSize) == retail::kGameSaveDataSize;
    if (operation == retail::kSaveUtilOperationDelete ||
        ((operation == retail::kSaveUtilOperationSave || operation == retail::kSaveUtilOperationLoad) && gameSave)) {
        g_SlotCallback(operation, RequestSlot(util, operation), ok);
    }
}

// Reads the request before the original clears it. Failed requests change
// no file, but the slot callback hears of them.
uint8_t __fastcall Detour_SaveUtilAcknowledge(uint8_t* util) {
    if (ReadAt<int32_t>(util, retail::kSaveUtilState) == retail::kSaveUtilStateDone) {
        const bool ok = ReadAt<int32_t>(util, retail::kSaveUtilStatus) == retail::kSaveUtilStatusOk;
        if (ok) {
            Finish(util);
        }
        Notify(util, ok);
    }
    return g_OriginalAcknowledge(util);
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kSaveDataRequirements)) {
        return false;
    }
    DetourSet detours("Per-save data");
    if (!detours.Add(
            retail::kSaveUtilAcknowledge,
            reinterpret_cast<void*>(&Detour_SaveUtilAcknowledge),
            reinterpret_cast<void**>(&g_OriginalAcknowledge),
            "saveUtilAcknowledge"
        )) {
        detours.RemoveAll();
        return false;
    }
    g_Installed = true;
    return true;
}

void Reset() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Store.clear();
}

void SetSlotCallback(SlotCallback callback) {
    g_SlotCallback = callback;
}

bool ReadFileText(const std::string& path, std::string& text) {
    return ReadText(path, text);
}

uint32_t WriteFileAtomically(const std::string& path, const std::string& text) {
    return WriteAtomically(path, text);
}

const char* Set(const std::string& section, const char* key, const char* value) {
    if (!g_Installed) {
        return "per-save data is not available on this game build";
    }
    if (!mod_config::KeyValid(key)) {
        return "its key must be 1 to 32 letters, digits, '_' or '-'";
    }
    if (!value) {
        return "it needs a value";
    }
    const std::string text(value);
    if (const char* reason = RejectValue(text)) {
        return reason;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    Values& values = g_Store[section];
    if (values.size() >= kMaxValuesPerSection && values.find(key) == values.end()) {
        return "the mod already keeps 1024 values";
    }
    values[key] = text;
    return nullptr;
}

bool Get(const std::string& section, const char* key, std::string& value) {
    if (!key) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto values = g_Store.find(section);
    if (values == g_Store.end()) {
        return false;
    }
    const auto found = values->second.find(key);
    if (found == values->second.end()) {
        return false;
    }
    value = found->second;
    return true;
}

bool Has(const std::string& section, const char* key) {
    std::string value;
    return Get(section, key, value);
}

bool Remove(const std::string& section, const char* key) {
    if (!key) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_Mutex);
    const auto values = g_Store.find(section);
    if (values == g_Store.end() || values->second.erase(key) == 0) {
        return false;
    }
    if (values->second.empty()) {
        g_Store.erase(values);
    }
    return true;
}

const char* SetInt(const std::string& section, const char* key, int32_t value) {
    return Set(section, key, std::to_string(value).c_str());
}

bool GetInt(const std::string& section, const char* key, int32_t& value) {
    std::string text;
    if (!Get(section, key, text) || text.empty()) {
        return false;
    }
    char* end = nullptr;
    errno = 0;
    // long is 32 bits on Windows, so ERANGE covers int32_t's range.
    const long number = std::strtol(text.c_str(), &end, 10);
    if (*end != '\0' || errno == ERANGE) {
        return false;
    }
    value = static_cast<int32_t>(number);
    return true;
}

}  // namespace save_data
