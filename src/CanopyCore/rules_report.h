#pragma once
#include <memory>
#include <string>

#include "mod_rules.h"

// One desktop check of a mods folder, and the JSON the tools hand out. The
// canopy CLI and canopy_core.dll both go through here, so they agree.
namespace mod_rules {

struct Validation {
    // False when the game folder or the mods folder could not be opened;
    // error says why and nothing was checked.
    bool ok = false;
    std::string error;
    std::wstring gameFolder;
    std::wstring modsFolder;
    // Null when no game folder was given: vanilla ids were not checked.
    std::unique_ptr<VanillaData> vanilla;
    Context context;
};

// Checks every folder of modsFolder, or of gameFolder\mods when modsFolder
// is empty. gameFolder may be empty when modsFolder is not; vanilla ids are
// then not checked.
Validation Validate(const std::wstring& gameFolder, const std::wstring& modsFolder);

// {"abi", "ok", "error", "game", "mods", "vanilla", "folders", "errors",
// "warnings", "notes", "diagnostics": [{"severity", "folder", "file",
// "line", "message"}]}.
std::string ValidationJson(const Validation& validation);
// {"abi", "sections": [{"prefix", "title", "doc", "fields": [{"key",
// "kind", "required", "help", "default", "table", "min", "max",
// "choices"}]}], "tables": [file]}: tables lists the files a [patch.]
// section can name, in table_schema::Patchable() order.
std::string SectionsJson();

// The JSON layout's version. Bump it when a field changes meaning or goes.
constexpr int kJsonAbi = 1;

// "error", "warning" or "note".
const char* SeverityName(Severity severity);
// text as UTF-8: unchanged when it already is, else read as the ANSI code
// page, which is what the loader's file APIs hand out.
std::string ToUtf8(const std::string& text);
std::string ToUtf8(const std::wstring& text);
// A UTF-16 path in the ANSI code page, the form the rules open files with.
// Empty when a character has no ANSI form.
std::string ToAnsi(const std::wstring& text);

}  // namespace mod_rules
