#include "mod_rules.h"

#include <algorithm>
#include <cstring>

#include "disabled_mods.h"
#include "platform_files.h"

// =============================================================
// MOD RULES
// =============================================================
// The pieces every rules_<kind> file reads canopy.ini with. They moved here
// from the loader's mod_tables so the loader and the desktop tools run the
// same code; the loader's copies now forward here.

namespace mod_rules {

const char* const kLanguageKeys[kLanguageCount] = {"ja", "en", "fr", "es", "zh-tw", "ko", "zh-cn"};

namespace {

constexpr size_t kMaxIdentifier = 64;
// Larger files are not read at all.
constexpr size_t kMaxFileBytes = 64 * 1024 * 1024;

}  // namespace

// ---- Values --------------------------------------------------------------

bool KeyValid(const std::string& key) {
    if (key.empty() || key.size() > kMaxKeyLength) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::string Plural(size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

bool TextValid(const std::string& text, size_t limit) {
    if (text.size() > limit) {
        return false;
    }
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7F) {
            return false;
        }
    }
    return true;
}

bool ParseU64(const std::string& text, uint64_t& value) {
    if (text.empty() || text.size() > 19) {
        return false;
    }
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    return true;
}

bool ParseI32(const std::string& text, int32_t& value) {
    const bool negative = !text.empty() && text[0] == '-';
    uint64_t magnitude = 0;
    if (!ParseU64(negative ? text.substr(1) : text, magnitude) || magnitude > 2147483647ull) {
        return false;
    }
    value = negative ? -static_cast<int32_t>(magnitude) : static_cast<int32_t>(magnitude);
    return true;
}

std::string EnumPart(const std::string& text) {
    std::string part;
    for (const char c : text) {
        if (c >= 'a' && c <= 'z') {
            part += static_cast<char>(c - 'a' + 'A');
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            part += c;
        } else {
            part += '_';
        }
    }
    return part;
}

std::string ModulePart(const std::string& text) {
    std::string part = canopy_ini::Lower(text);
    std::replace(part.begin(), part.end(), '-', '_');
    return part;
}

bool IdentifierValid(const std::string& text) {
    if (text.empty() || text.size() > kMaxIdentifier || (text[0] >= '0' && text[0] <= '9')) {
        return false;
    }
    return std::all_of(text.begin(), text.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool ScriptPathValid(const std::string& path) {
    if (path.size() < 5 || path.size() > 200 || path[0] == '\\' || path[0] == '/' ||
        path.find(':') != std::string::npos || path.find("..") != std::string::npos) {
        return false;
    }
    return platform::CompareNoCase(path.c_str() + path.size() - 4, ".lua") == 0;
}

std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(',', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        parts.push_back(canopy_ini::Trim(text.substr(start, end - start)));
        start = end + 1;
    }
    return parts;
}

void ReadTexts(const canopy_ini::IniSection& section, const char* key, std::string (&texts)[kLanguageCount]) {
    const std::string fallback = canopy_ini::Value(&section, key, "");
    for (int language = 0; language < kLanguageCount; ++language) {
        const std::string name = std::string(key) + "." + kLanguageKeys[language];
        texts[language] = canopy_ini::Value(&section, name.c_str(), fallback);
    }
}

std::string FullKey(const std::string& modKey, const std::string& ref) {
    return ref.find('.') == std::string::npos ? modKey + "." + ref : ref;
}

std::string SectionKey(const canopy_ini::IniSection& section, const char* prefix) {
    const size_t length = std::strlen(prefix);
    if (section.name.size() <= length || platform::CompareNoCase(section.name.c_str(), prefix, length) != 0) {
        return {};
    }
    return section.name.substr(length);
}

bool ReadFileText(const std::string& path, std::string& text) {
    return platform::ReadWholeFile(path, kMaxFileBytes, text);
}

// ---- Reports -------------------------------------------------------------

void Report::Add(Severity severity, const std::string& folder, int line, const std::string& message) {
    items_.push_back({severity, folder, line, message});
    if (sink_) {
        sink_(items_.back());
    }
}

size_t Report::Count(Severity severity) const {
    return static_cast<size_t>(std::count_if(items_.begin(), items_.end(), [&](const Diagnostic& item) {
        return item.severity == severity;
    }));
}

// ---- Mod folders ---------------------------------------------------------

std::vector<ModFolder> ScanMods(const std::string& modsDir) {
    std::vector<ModFolder> folders;
    // Folders turned off in mods\CanopyData\disabled.txt are not mods here.
    const std::vector<std::string> turnedOff = disabled_mods::Cached(modsDir);
    std::vector<platform::FolderEntry> entries;
    if (!platform::ListFolder(modsDir, entries)) {
        return folders;
    }
    // Each canopy.ini as it is spelled on disk, Canopy.INI and all.
    platform::PathLookup lookup;
    for (const platform::FolderEntry& entry : entries) {
        const std::string& name = entry.name;
        if (!entry.folder) {
            continue;
        }
        if (disabled_mods::IsDisabled(turnedOff, name)) {
            continue;
        }
        ModFolder folder;
        folder.name = name;
        const std::string ini = lookup.Find(platform::Join(platform::Join(modsDir, name), "canopy.ini"));
        folder.hasIni = platform::Exists(ini);
        if (folder.hasIni) {
            folder.sections = canopy_ini::ReadIni(ini);
        }
        folders.push_back(std::move(folder));
    }
    std::sort(folders.begin(), folders.end(), [](const ModFolder& left, const ModFolder& right) {
        return platform::CompareNoCase(left.name.c_str(), right.name.c_str()) < 0;
    });
    return folders;
}

bool HasSection(const ModFolder& folder, const std::vector<const char*>& prefixes) {
    return std::any_of(folder.sections.begin(), folder.sections.end(), [&](const canopy_ini::IniSection& section) {
        return std::any_of(prefixes.begin(), prefixes.end(), [&](const char* prefix) {
            return !SectionKey(section, prefix).empty();
        });
    });
}

std::vector<ModFolder> WithSections(const std::vector<ModFolder>& folders, const std::vector<const char*>& prefixes) {
    std::vector<ModFolder> wanted;
    for (const ModFolder& folder : folders) {
        if (HasSection(folder, prefixes)) {
            wanted.push_back(folder);
        }
    }
    return wanted;
}

std::string ModKey(const ModFolder& folder) {
    return canopy_ini::Value(canopy_ini::FindSection(folder.sections, "mod"), "key", folder.name);
}

std::string ModName(const ModFolder& folder, const std::string& modKey) {
    return canopy_ini::Value(canopy_ini::FindSection(folder.sections, "mod"), "name", modKey);
}

std::string FolderDir(const std::string& modsDir, const ModFolder& folder) {
    return folder.dir.empty() ? platform::Join(modsDir, folder.name) : folder.dir;
}

bool AcceptAll(const ModFolder&, const std::string&, size_t) {
    return true;
}

// ---- References ----------------------------------------------------------

uint64_t KeyRefs::Add(RefKind kind, const std::string& fullKey) {
    std::map<std::string, uint64_t>& keys = keys_[kind];
    const std::string folded = canopy_ini::Lower(fullKey);
    const auto found = keys.find(folded);
    if (found != keys.end()) {
        return found->second;
    }
    const uint64_t id = keys.size() + 1;
    keys.emplace(folded, id);
    return id;
}

uint64_t KeyRefs::Find(RefKind kind, const std::string& fullKey) const {
    const auto keys = keys_.find(kind);
    if (keys == keys_.end()) {
        return 0;
    }
    const auto found = keys->second.find(canopy_ini::Lower(fullKey));
    return found == keys->second.end() ? 0 : found->second;
}

int32_t FlagRef(const Refs& refs, const std::string& modKey, const std::string& ref) {
    uint64_t number = 0;
    if (ParseU64(ref, number)) {
        const bool modBand =
            number >= static_cast<uint64_t>(kModFlagFirst) && number <= static_cast<uint64_t>(kModFlagLast);
        return number > 0 && number < static_cast<uint64_t>(kFlagCount) && !modBand ? static_cast<int32_t>(number)
                                                                                   : 0;
    }
    return ref.empty() ? 0 : static_cast<int32_t>(refs.Find(RefKind::Flag, FullKey(modKey, ref)));
}

uint64_t ItemRef(const Refs& refs, const std::string& modKey, const std::string& ref) {
    uint64_t number = 0;
    if (ParseU64(ref, number)) {
        return number > 0 && number < kFirstModId ? number : 0;
    }
    return ref.empty() ? 0 : refs.Find(RefKind::Item, FullKey(modKey, ref));
}

uint64_t KeyRef(const Refs& refs, RefKind kind, const std::string& modKey, const std::string& ref) {
    return ref.empty() ? 0 : refs.Find(kind, FullKey(modKey, ref));
}

// ---- Fields --------------------------------------------------------------

const SectionInfo* SectionOf(const std::string& sectionName) {
    for (const SectionInfo* info : Sections()) {
        const size_t length = std::strlen(info->prefix);
        const bool dotted = length > 0 && info->prefix[length - 1] == '.';
        if (dotted ? (sectionName.size() > length && platform::CompareNoCase(sectionName.c_str(), info->prefix, length) == 0)
                   : platform::CompareNoCase(sectionName.c_str(), info->prefix) == 0) {
            return info;
        }
    }
    return nullptr;
}

// ---- Vanilla data --------------------------------------------------------

bool VanillaData::Open(const std::wstring& gameFolder, std::string& error) {
    tables_.clear();
    return data_.Open(gameFolder, error);
}

bool VanillaData::IsOpen() const {
    return data_.IsOpen();
}

const blob::Table* VanillaData::Table(const std::string& file) const {
    const std::string key = canopy_ini::Lower(file);
    auto found = tables_.find(key);
    if (found == tables_.end()) {
        auto loaded = std::make_unique<Loaded>();
        std::string error;
        if (data_.IsOpen() && data_.Read("data/database/" + key, loaded->bytes, error)) {
            loaded->ok = blob::Parse(loaded->bytes.data(), loaded->bytes.size(), loaded->table, error);
        }
        found = tables_.emplace(key, std::move(loaded)).first;
    }
    return found->second->ok ? &found->second->table : nullptr;
}

bool VanillaData::HasRow(const std::string& file, uint64_t id) const {
    const blob::Table* table = Table(file);
    if (!table) {
        return false;
    }
    return std::any_of(table->rows.begin(), table->rows.end(), [&](const blob::Row& row) {
        return blob::RowId(row) == id;
    });
}

}  // namespace mod_rules
