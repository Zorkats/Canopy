#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "archive.h"
#include "blob.h"
#include "canopy_ini.h"

// The rules a mod folder's canopy.ini is read by, shared by the loader and
// the desktop tools so the two can never disagree. Each kind of section has
// its own rules_<kind> file built on these pieces: a reader the loader calls
// at startup, and a check the tools call, which runs the same reader.
//
// A reader reports through a Report. Each message is the exact line the
// loader writes to Canopy.log, so the loader's sink logs it as it comes, and
// a tool shows it next to the line of the file it is about. A reader never
// logs, writes files or touches game memory itself.
namespace mod_rules {

constexpr int kLanguageCount = 7;
// The language order of every per-language value: name.ja, name.en, ...
extern const char* const kLanguageKeys[kLanguageCount];

// Keys of mods, sections and settings: 1 to 32 letters, digits, '_' or '-'.
constexpr size_t kMaxKeyLength = 32;

// The lowest id Canopy gives out, the mod item range's first. Vanilla ids
// are below it.
constexpr uint64_t kFirstModId = 1500000000;
// Game flags are 1 to kFlagCount - 1. Canopy gives mod flags the band from
// kModFlagFirst to kModFlagLast, which no vanilla data uses.
constexpr int32_t kFlagCount = 99999;
constexpr int32_t kModFlagFirst = 95000;
constexpr int32_t kModFlagLast = 99998;

// ---- Values --------------------------------------------------------------

bool KeyValid(const std::string& key);
std::string Plural(size_t count, const char* one, const char* many);
// One line of printable text of at most limit bytes.
bool TextValid(const std::string& text, size_t limit);
// Decimal digits only, at most 19 of them.
bool ParseU64(const std::string& text, uint64_t& value);
// An optional '-' and decimal digits, within the i32 range.
bool ParseI32(const std::string& text, int32_t& value);
// Uppercase letters and digits, everything else as '_', for enum names.
std::string EnumPart(const std::string& text);
// Lowercase, with '-' as '_', for script module names.
std::string ModulePart(const std::string& text);
// A Lua identifier of at most 64 bytes.
bool IdentifierValid(const std::string& text);
// A path inside the mod folder: relative, no "..", ending in .lua.
bool ScriptPathValid(const std::string& path);
// Splits text at commas and trims each part.
std::vector<std::string> SplitList(const std::string& text);
// The per-language text: key.<lang> when present, else key.
void ReadTexts(const canopy_ini::IniSection& section, const char* key, std::string (&texts)[kLanguageCount]);
// A key of another part of a mod: Mod.Key as written, or Key of modKey.
std::string FullKey(const std::string& modKey, const std::string& ref);
// The part of a section name after prefix, which ends in '.', or empty when
// the name does not start with it. Without case.
std::string SectionKey(const canopy_ini::IniSection& section, const char* prefix);
// Reads a whole file, false when it cannot or it is over 64 MB.
bool ReadFileText(const std::string& path, std::string& text);

// ---- Reports -------------------------------------------------------------

enum class Severity : uint8_t {
    // The section, or the value, is refused: the loader skips it.
    Error,
    // The value is refused and a default used, or the value is ignored.
    Warning,
    // Advice from a desktop check only; the loader never says it.
    Note,
};

struct Diagnostic {
    Severity severity = Severity::Error;
    // The mod folder's name, empty for a message about no one folder.
    std::string folder;
    // The 1-based line of canopy.ini, 0 when it is not about one line.
    int line = 0;
    std::string message;
};

class Report {
public:
    using Sink = std::function<void(const Diagnostic&)>;

    Report() = default;
    // sink sees each diagnostic as it is added; the loader logs it there.
    explicit Report(Sink sink) : sink_(std::move(sink)) {}

    void Add(Severity severity, const std::string& folder, int line, const std::string& message);
    void Error(const std::string& folder, int line, const std::string& message) {
        Add(Severity::Error, folder, line, message);
    }
    void Warning(const std::string& folder, int line, const std::string& message) {
        Add(Severity::Warning, folder, line, message);
    }
    void Note(const std::string& folder, int line, const std::string& message) {
        Add(Severity::Note, folder, line, message);
    }

    const std::vector<Diagnostic>& Items() const {
        return items_;
    }
    size_t Count(Severity severity) const;

private:
    Sink sink_;
    std::vector<Diagnostic> items_;
};

// ---- Mod folders ---------------------------------------------------------

struct ModFolder {
    std::string name;
    // canopy.ini's sections, empty when the folder has none.
    std::vector<canopy_ini::IniSection> sections;
    bool hasIni = false;
    // The folder's own directory, in the ANSI code page. Empty means
    // modsDir\name: ScanMods leaves it so. A desktop check of a mod project
    // kept outside the mods folder sets it.
    std::string dir;
};

// Every folder in modsDir, the game's mods folder, with its canopy.ini read,
// in name order without case. Empty when modsDir cannot be listed.
std::vector<ModFolder> ScanMods(const std::string& modsDir);
// True when folder has a section starting with one of prefixes.
bool HasSection(const ModFolder& folder, const std::vector<const char*>& prefixes);
// The folders of ScanMods with a section starting with one of prefixes.
std::vector<ModFolder> WithSections(const std::vector<ModFolder>& folders, const std::vector<const char*>& prefixes);
// The folder's mod key: key in [mod], or the folder name.
std::string ModKey(const ModFolder& folder);
// The folder's display name: name in [mod], or modKey.
std::string ModName(const ModFolder& folder, const std::string& modKey);
// The directory a reader opens the folder's files in: folder.dir, or
// modsDir\name when that is empty. Every file path a reader builds starts
// here.
std::string FolderDir(const std::string& modsDir, const ModFolder& folder);

// Called by a reader once per folder whose sections it read, with the count
// of usable ones. False drops them: the loader does so when it cannot
// register the mod. Desktop checks accept every folder.
using FolderAccept = std::function<bool(const ModFolder& folder, const std::string& modKey, size_t count)>;
bool AcceptAll(const ModFolder& folder, const std::string& modKey, size_t count);

// ---- References ----------------------------------------------------------

enum class RefKind : uint8_t { Flag, Item, Icon, Texture, Skin, Villager, Whistle, Recipe, Object };

// The ids of other mods' parts, by Mod.Key. The loader answers from what
// it installed; a desktop check from the keys it read, with stand-in ids.
class Refs {
public:
    virtual ~Refs() = default;
    // The id of the part fullKey (Mod.Key, without case), 0 when there is
    // none.
    virtual uint64_t Find(RefKind kind, const std::string& fullKey) const = 0;
};

// The keys a desktop check has read, each given a stand-in id from 1 up
// per kind. Ids from Add are never real game ids.
class KeyRefs : public Refs {
public:
    uint64_t Add(RefKind kind, const std::string& fullKey);
    uint64_t Find(RefKind kind, const std::string& fullKey) const override;

private:
    std::map<RefKind, std::map<std::string, uint64_t>> keys_;
};

// A flag: a flag of modKey, Mod.Flag, or a vanilla flag id outside the mod
// flag band. 0 when there is no such flag.
int32_t FlagRef(const Refs& refs, const std::string& modKey, const std::string& ref);
// An item: an item of modKey, Mod.Item, or a vanilla item id below the mod
// id ranges. 0 when there is no such mod item. A vanilla id is only checked
// against the item table later.
uint64_t ItemRef(const Refs& refs, const std::string& modKey, const std::string& ref);
// A part of kind by Key of modKey or Mod.Key, 0 when there is none.
uint64_t KeyRef(const Refs& refs, RefKind kind, const std::string& modKey, const std::string& ref);

// ---- Fields --------------------------------------------------------------
// What each section kind accepts, as data, so a tool can build a form and
// say what a key means. The reader stays the authority on what is valid.

enum class ValueKind : uint8_t {
    Text,
    // A text with optional per-language keys: name, name.en, ...
    LangText,
    Integer,
    Number,
    // on or off.
    Switch,
    // One of FieldInfo::choices.
    Choice,
    // A comma-separated list.
    List,
    // A file inside the mod folder.
    Path,
    // An id of a row of FieldInfo::table.
    VanillaId,
    FlagRef,
    ItemRef,
    IconRef,
    TextureRef,
    SkinRef,
    // A mod object by key or Mod.Key.
    ObjectRef,
    // S and L notes.
    Melody,
    // Anything else, explained by help.
    Other,
};

struct FieldInfo {
    const char* key;
    ValueKind kind;
    bool required;
    // One sentence for a tool to show.
    const char* help;
    // The value used when the key is left out, null for none.
    const char* defaultValue = nullptr;
    // VanillaId: the table, such as "item.dat".
    const char* table = nullptr;
    // Integer: the range, when min < max.
    int64_t min = 0;
    int64_t max = 0;
    // Choice: the accepted values, separated by '|'.
    const char* choices = nullptr;
};

struct SectionInfo {
    // The section prefix, such as "whistle.", or "mod" for [mod].
    const char* prefix;
    // The kind's name in a tool, such as "Whistle".
    const char* title;
    // The docs page, such as "docs/WHISTLES.md".
    const char* doc;
    const FieldInfo* fields;
    size_t fieldCount;
};

// Every section kind, in the order a tool lists them.
const std::vector<const SectionInfo*>& Sections();
// The kind of a section name, null for one no kind takes.
const SectionInfo* SectionOf(const std::string& sectionName);

// ---- Vanilla data --------------------------------------------------------

// The game's own tables, read from its archives, for desktop checks.
class VanillaData {
public:
    // Opens the game folder, the one with village.exe and data.dat.
    bool Open(const std::wstring& gameFolder, std::string& error);
    bool IsOpen() const;
    // The parsed table data/database/<file>, such as "item.dat". Null when
    // the archives do not have it or it does not parse.
    const blob::Table* Table(const std::string& file) const;
    // True when the table has a row whose u64 id at +0 is id.
    bool HasRow(const std::string& file, uint64_t id) const;
    const archive::GameData& Data() const {
        return data_;
    }

private:
    struct Loaded {
        std::vector<uint8_t> bytes;
        blob::Table table;
        bool ok = false;
    };
    archive::GameData data_;
    mutable std::map<std::string, std::unique_ptr<Loaded>> tables_;
};

// ---- Desktop checks ------------------------------------------------------

// What every kind's check reads and fills. The checks run in the loader's
// install order, so a later kind finds the keys an earlier one added.
struct Context {
    std::string modsDir;
    std::vector<ModFolder> folders;
    KeyRefs refs;
    Report report;
    // Null when no game folder was given: vanilla ids are then not checked.
    const VanillaData* vanilla = nullptr;
};

// Runs every kind's check over context.folders.
void CheckAll(Context& context);

}  // namespace mod_rules
