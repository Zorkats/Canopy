#define NOMINMAX
#include "mod_art.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "core.h"
#include "file_overrides.h"
#include "mod_loader.h"
#include "mod_tables.h"
#include "nltx.h"
#include "rules_art.h"
#include "save_data.h"
#include "table_engine.h"
#include "table_schema.h"

// =============================================================
// MOD ART DESIGN
// =============================================================
// A mod adds art in its canopy.ini: [texture.Key] for an image, [icon.Key]
// for a part of a texture the UI draws, [skin.Key] for a set of model
// texture swaps (an AnimeReplaceTexture row), and [row.<table>.Key] for a
// raw row of any table with a schema, written field by field. A folder with
// art and no Lua or DLL is still registered as a mod. With no art anywhere,
// nothing below is hooked. CanopyCore's rules_art reads the sections,
// resolves their references and builds each row; this file converts and
// serves the textures, numbers the rows and adds them to the game's tables.
//
// Ids. Each table has its own band and registry in mods\CanopyData:
// textures 1900000000 + n (textures.ini), icons 1950000000 + n (icons.ini),
// skins 2000000000 + n (skins.ini). A raw row shares its table's band and
// key space. Every id is given out here, before any table loads, so other
// modules can name a mod icon at startup. Enum names are
// TEX_ID_CANOPY_<MOD>_<KEY> and the like, made unique against the table.
//
// Textures. A PNG, or any image WIC reads, is converted to the game's
// texture format into mods\CanopyData\textures\<Mod>\<Key>.nltx, again only
// when the source's size or time changes; a .nltx is checked and used as it
// is. The row names data/texture/canopy/<mod>/<key>.nltx in every language,
// and file overrides serve that path from the file. Width and height stay
// 0 like every vanilla row; the game reads the size from the file.
//
// References. A texture reference is a texture of the same mod by key,
// Mod.Key, a vanilla texture id, or a TEX_ID_ enum name. Mod references are
// checked here, at startup. A vanilla id is checked when its row is built
// if its table loaded in an earlier batch (the skins' textures), otherwise
// at the title (the icons' textures, which load beside them). At the title
// a missing texture is replaced with a vanilla one that exists, so no row
// is left pointing at nothing, and the row is reported off.
//
// Loads. The table engine calls the append functions on the game's job
// workers: textures and icons inside game init, skins on the boot loader.
// They only read the definitions, which never change after Install, and
// set each row's appended flag. An icon that names a TEX_ID_ enum waits up
// to five seconds for the texture table on its worker.
//
// Title. The tick looks each row up through the game's own find and checks
// what it can: a texture's file paths, an icon's texture, and that every
// row is there.

namespace mod_art {
namespace {

using namespace core;
using mod_tables::Plural;
namespace ts = table_schema;

using FT = ts::FieldType;
using Kind = mod_rules::ArtKind;
using mod_rules::InBand;

constexpr uint32_t kEnumWaitMs = 5000;
constexpr const char* kCacheFolder = "mods\\CanopyData\\textures";

// A row as the rules read it, and what its table load sets.
struct ModRow : mod_rules::ArtSpec {
    // Set by the table load, on a worker.
    std::atomic<bool> appended{false};
    std::atomic<uint32_t> entries{0};
};

std::vector<std::unique_ptr<ModRow>> g_Rows;
bool g_Active = false;
bool g_Checked = false;

std::wstring Wide(const std::string& text, UINT codePage) {
    if (text.empty()) {
        return std::wstring();
    }
    const int size = MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(size > 0 ? size : 0), L'\0');
    if (size > 0) {
        MultiByteToWideChar(codePage, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    }
    return result;
}

std::vector<mod_rules::ArtSpec*> Specs() {
    std::vector<mod_rules::ArtSpec*> specs;
    for (const std::unique_ptr<ModRow>& row : g_Rows) {
        specs.push_back(row.get());
    }
    return specs;
}

std::vector<const mod_rules::ArtSpec*> Counted(const std::vector<std::unique_ptr<ModRow>>& rows) {
    std::vector<const mod_rules::ArtSpec*> counted;
    for (const std::unique_ptr<ModRow>& row : rows) {
        counted.push_back(row.get());
    }
    return counted;
}

bool HasRows(const ts::TableSchema& schema) {
    return std::any_of(g_Rows.begin(), g_Rows.end(), [&](const std::unique_ptr<ModRow>& row) {
        return row->schema == &schema;
    });
}

// ---- Files ---------------------------------------------------------------

// The folders of the converted textures, made as needed.
bool MakeCacheFolder(const std::string& modKey) {
    for (const std::string& part : {std::string("mods\\CanopyData"), std::string(kCacheFolder),
                                    std::string(kCacheFolder) + "\\" + modKey}) {
        const std::string path = ModFilePath(part.c_str());
        if (!CreateDirectoryA(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    return true;
}

// The texture of row from a .nltx in the mod folder, used as it is.
bool UseNltx(mod_rules::ArtSpec& row, std::string& why) {
    if (!mod_rules::LoadNltx(row, why)) {
        return false;
    }
    row.diskPath = row.source;
    row.diskLabel = "mods\\" + row.folder + "\\" + row.file;
    return true;
}

// The texture of row from an image, converted once into the cache and
// again when the image's size or time changes.
bool ConvertImage(mod_rules::ArtSpec& row, std::string& why) {
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(row.source.c_str(), GetFileExInfoStandard, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        why = row.file + " could not be found";
        return false;
    }
    const uint64_t bytes = (static_cast<uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const uint64_t time = (static_cast<uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
                          info.ftLastWriteTime.dwLowDateTime;
    const std::string stamp = row.file + "\r\n" + std::to_string(bytes) + "\r\n" + std::to_string(time) + "\r\n";
    const std::string relative = std::string(kCacheFolder) + "\\" + row.modKey + "\\" + row.key;
    const std::string target = ModFilePath((relative + ".nltx").c_str());
    const std::string stampPath = ModFilePath((relative + ".source").c_str());
    row.diskPath = Wide(target, CP_ACP);
    row.diskLabel = relative + ".nltx";

    std::string old;
    std::vector<uint8_t> cached;
    nltx::Header header;
    std::string error;
    if (save_data::ReadFileText(stampPath, old) && old == stamp && mod_rules::ReadArtFile(row.diskPath, cached) &&
        nltx::ParseHeader(cached.data(), cached.size(), header, error)) {
        row.width = header.width;
        row.height = header.height;
        return true;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
    if (!mod_rules::DecodeImage(row, width, height, pixels, why)) {
        return false;
    }
    std::vector<uint8_t> texture;
    if (!nltx::Encode(width, height, pixels, texture, error)) {
        why = row.file + " could not be converted (" + error + ")";
        return false;
    }
    if (!MakeCacheFolder(row.modKey) ||
        save_data::WriteFileAtomically(target, std::string(texture.begin(), texture.end())) != 0) {
        why = row.diskLabel + " could not be written";
        return false;
    }
    // Without the stamp the next start converts again, which is harmless.
    save_data::WriteFileAtomically(stampPath, stamp);
    row.width = width;
    row.height = height;
    Log(row.where + ": converted " + row.file + " to " + row.diskLabel + ", " + std::to_string(width) + "x" +
        std::to_string(height) + ".");
    return true;
}

std::string RegistryHeader(const ts::TableSchema& schema) {
    return std::string("; Canopy mod ") + schema.section + " ids. Each mod row keeps its number for good, and a "
           "number is never given\r\n; out twice. Do not edit or delete this file.\r\n";
}

// The numbers of one table's rows, kept in its registry.
bool Number(const ts::TableSchema& schema, const std::vector<std::string>& keys, std::vector<uint64_t>& numbers) {
    const std::string label = std::string("Mod ") + schema.section + " rows";
    return mod_tables::AssignNumbers(
        schema.registryFile, RegistryHeader(schema).c_str(), label.c_str(), schema.idBandSize, keys, numbers
    );
}

// ---- Rows (game workers) -------------------------------------------------

// The game's tables as the table engine sees them while they load.
class LoadingTables : public mod_rules::ArtTables {
public:
    bool EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const override {
        if (!table_engine::WaitKnown(file, kEnumWaitMs)) {
            why = std::string(file) + " did not load in time to look up " + name;
            return false;
        }
        if (!table_engine::EnumId(file, name, id)) {
            why = std::string(file) + " has no " + name;
            return false;
        }
        return true;
    }

    // A table that loaded in an earlier batch is known in full.
    bool RefFine(const ts::TableSchema& schema, const char* file, uint64_t id, bool titleChecks, std::string& why)
        const override {
        const int own = table_engine::Batch(schema.file);
        const int other = table_engine::Batch(file);
        if (other != 0 && other < own && table_engine::Known(file)) {
            if (!table_engine::HasId(file, id)) {
                why = std::string("it names ") + ts::Find(file)->section + " " + std::to_string(id) +
                      ", which is not in " + file;
                return false;
            }
            return true;
        }
        if (!titleChecks) {
            why = std::string("its ") + ts::Find(file)->section + " " + std::to_string(id) + " cannot be checked, " +
                  file + " is not loaded yet";
            return false;
        }
        return true;
    }
};

void AppendRows(const ts::TableSchema& schema, const blob::Table& table, blob::Appender& appender) {
    std::unordered_map<uint64_t, size_t> byId;
    for (size_t index = 0; index < table.rows.size(); ++index) {
        byId.emplace(blob::RowId(table.rows[index]), index);
    }
    blob::EnumNames names(table, static_cast<size_t>(schema.enumOffset));
    const LoadingTables tables;
    for (const std::unique_ptr<ModRow>& mod : g_Rows) {
        if (mod->schema != &schema) {
            continue;
        }
        if (byId.count(mod->id) || table_engine::HasId(schema.file, mod->id)) {
            Log(mod->label + " skipped, its id " + std::to_string(mod->id) + " is already in " + schema.file + ".");
            continue;
        }
        std::vector<uint8_t> row;
        std::string why;
        if (!mod_rules::BuildArtRow(*mod, table, byId, tables, appender.Texts(), row, why) ||
            !mod_rules::CheckArtRefs(schema, row, tables, why)) {
            Log(mod->label + " skipped, " + why + ".");
            continue;
        }
        const std::string stem =
            std::string(schema.enumStem) + mod_tables::EnumPart(mod->modKey) + "_" + mod_tables::EnumPart(mod->key);
        std::string name = names.Unique(stem);
        while (table_engine::HasEnum(schema.file, name)) {
            name = names.Unique(stem);
        }
        blob::Put<uint64_t>(row.data(), 0, mod->id);
        appender.Texts().Write(row, static_cast<size_t>(schema.enumOffset), name);
        appender.Add(row);
        mod->entries.store(ts::VectorCount(schema, row.data(), row.size()), std::memory_order_relaxed);
        mod->appended.store(true, std::memory_order_release);
    }
}

void AppendTextures(const char*, uint8_t*, const blob::Table& table, blob::Appender& appender) {
    AppendRows(ts::kTexture, table, appender);
}

void AppendIcons(const char*, uint8_t*, const blob::Table& table, blob::Appender& appender) {
    AppendRows(ts::kIcon, table, appender);
}

void AppendSkins(const char*, uint8_t*, const blob::Table& table, blob::Appender& appender) {
    AppendRows(ts::kAnimeReplaceTexture, table, appender);
}

// ---- Title self-check ----------------------------------------------------

// A vanilla texture with a file: the test icon's, or else the first one.
uint64_t FallbackTexture() {
    uint8_t* textures = table_engine::Database(ts::kTexture.file);
    const uint8_t* icon = mod_tables::FindRecord(table_engine::Database(ts::kIcon.file), retail::kFallbackIcon);
    if (icon) {
        const uint64_t id = ReadAt<uint64_t>(icon, retail::kIconRecordTexture);
        if (mod_tables::FindRecord(textures, id)) {
            return id;
        }
    }
    if (!textures) {
        return 0;
    }
    void** first = ReadAt<void**>(textures, retail::kDatabaseRecords);
    void** last = ReadAt<void**>(textures, retail::kDatabaseRecords + 8);
    for (void** holder = first; first && holder < last; ++holder) {
        const uint8_t* record = *holder ? ReadAt<const uint8_t*>(*holder, 0) : nullptr;
        const char* file = record ? ReadAt<const char*>(record, retail::kTextureRecordFile) : nullptr;
        const uint64_t id = record ? ReadAt<uint64_t>(record, 0) : 0;
        if (file && *file && !InBand(ts::kTexture, id)) {
            return id;
        }
    }
    return 0;
}

// Checks the record's references; a missing texture is replaced. False
// when one was missing.
bool CheckRecordRefs(const ModRow& mod, uint8_t* record, const std::string& what) {
    for (size_t index = 0; index < mod.schema->fieldCount; ++index) {
        const ts::FieldSpec& field = mod.schema->fields[index];
        if ((field.type != FT::Ref64 && field.type != FT::Ref32) || !field.refTable || field.recordOffset < 0) {
            continue;
        }
        const size_t at = static_cast<size_t>(field.recordOffset);
        const uint64_t id = field.type == FT::Ref64 ? ReadAt<uint64_t>(record, at) : ReadAt<uint32_t>(record, at);
        if (mod_tables::FindRecord(table_engine::Database(field.refTable), id)) {
            continue;
        }
        const char* noun = ts::Find(field.refTable)->section;
        const uint64_t fallback = std::strcmp(field.refTable, ts::kTexture.file) == 0 ? FallbackTexture() : 0;
        if (fallback != 0 && field.type == FT::Ref64) {
            WriteAt<uint64_t>(record, at, fallback);
        } else if (fallback != 0) {
            WriteAt<uint32_t>(record, at, static_cast<uint32_t>(fallback));
        }
        Log(what + ", but its " + noun + " " + std::to_string(id) + " is not in the " + noun + " table, so it shows " +
            (fallback != 0 ? std::string(noun) + " " + std::to_string(fallback) : std::string("nothing")) +
            " instead. It stays off.");
        return false;
    }
    return true;
}

void CheckRow(const ModRow& mod) {
    if (!mod.appended.load(std::memory_order_acquire)) {
        return;
    }
    const std::string what = mod.label + " is id " + std::to_string(mod.id);
    uint8_t* record = mod_tables::FindRecord(table_engine::Database(mod.schema->file), mod.id);
    if (!record) {
        Log(what + ", but the " + mod.schema->section + " table does not have it. It stays off.");
        return;
    }
    if (!CheckRecordRefs(mod, record, what)) {
        return;
    }
    switch (mod.kind) {
    case Kind::Texture: {
        for (int language = 0; language < table_schema::kLanguageCount; ++language) {
            const char* file = ReadAt<const char*>(record, retail::kTextureRecordFile + 8 * static_cast<size_t>(language));
            if (!file || mod.gamePath != file) {
                Log(what + ", but the texture table does not have its file as written. It stays off.");
                return;
            }
        }
        Log(what + ", " + std::to_string(mod.width) + "x" + std::to_string(mod.height) + ", served from " +
            mod.diskLabel + ".");
        return;
    }
    case Kind::Icon:
        Log(what + ", texture " + std::to_string(ReadAt<uint64_t>(record, retail::kIconRecordTexture)) + ", rect " +
            mod.rect[0] + ", " + mod.rect[1] + ", " + mod.rect[2] + ", " + mod.rect[3] + ".");
        return;
    case Kind::Skin:
        Log(what + ", " + Plural(mod.swaps.size(), "texture swap", "texture swaps") + " over " +
            Plural(mod.entries.load(std::memory_order_relaxed), "entry", "entries") + ".");
        return;
    default:
        Log(what + ".");
        return;
    }
}

void SelfCheck() {
    g_Checked = true;
    if (!table_engine::Complete()) {
        Log("Mod art: the game tables were never seen loading, so mod art is off.");
        return;
    }
    for (const std::unique_ptr<ModRow>& mod : g_Rows) {
        CheckRow(*mod);
    }
}

}  // namespace

// ---- Public --------------------------------------------------------------

bool Install(bool ticking, bool beforeGameInit) {
    std::vector<mod_tables::Folder> folders = mod_tables::FindFolders({"texture.", "icon.", "skin.", "row."});
    if (folders.empty()) {
        Log("No mod art, so mod art is off.");
        return false;
    }
    if (!ticking) {
        Log("Mod art needs the frame tick, so it is off.");
        return false;
    }
    if (!table_engine::Install(beforeGameInit)) {
        Log("Mod art needs the table engine, so it is off.");
        return false;
    }
    if (!RequirementsMet(retail::kModArtRequirements)) {
        Log("Mod art is off.");
        return false;
    }

    mod_rules::ArtHost host;
    host.load = [](mod_rules::ArtSpec& row, std::string& why) {
        return row.nltx ? UseNltx(row, why) : ConvertImage(row, why);
    };
    host.serve = [](const mod_rules::ArtSpec& row) {
        return file_overrides::AddGeneratedFile(row.gamePath, row.diskPath, row.folder);
    };
    host.accept = [](const mod_tables::Folder& folder, const std::string& modKey,
                     const std::vector<mod_rules::ArtSpec>& rows) {
        bool joined = false;
        const std::string name = mod_rules::ModName(folder, modKey);
        if (mod_loader::AttachLuaMod(modKey.c_str(), name.c_str(), ("mods\\" + folder.name).c_str(), joined) ==
            CANOPY_NO_MOD) {
            Log("Art in folder " + folder.name + " skipped, the mod could not be registered.");
            return false;
        }
        std::vector<const mod_rules::ArtSpec*> counted;
        for (const mod_rules::ArtSpec& row : rows) {
            counted.push_back(&row);
        }
        Log("Mod " + name + " (" + modKey + ", folder " + folder.name + ") defines " + mod_rules::CountArt(counted) +
            (joined ? "." : ", registered for its art."));
        return true;
    };
    mod_rules::Report report = mod_tables::LogReport();
    for (mod_rules::ArtSpec& spec : mod_rules::ReadArt(folders, ModFilePath("mods"), report, host)) {
        auto row = std::make_unique<ModRow>();
        static_cast<mod_rules::ArtSpec&>(*row) = std::move(spec);
        g_Rows.push_back(std::move(row));
    }
    mod_rules::ResolveArt(Specs(), &Number, report);
    g_Rows.erase(
        std::remove_if(g_Rows.begin(), g_Rows.end(), [](const std::unique_ptr<ModRow>& row) { return row->id == 0; }),
        g_Rows.end()
    );
    if (g_Rows.empty()) {
        Log("No usable mod art, so mod art is off.");
        return false;
    }
    for (const std::unique_ptr<ModRow>& row : g_Rows) {
        row->where = row->label;
    }
    if (HasRows(ts::kTexture)) {
        table_engine::Contribute(ts::kTexture.file, &AppendTextures);
    }
    if (HasRows(ts::kIcon)) {
        table_engine::Contribute(ts::kIcon.file, &AppendIcons);
    }
    if (HasRows(ts::kAnimeReplaceTexture)) {
        table_engine::Contribute(ts::kAnimeReplaceTexture.file, &AppendSkins);
    }
    // Every reference to a texture is checked against it.
    table_engine::Watch(ts::kTexture.file);
    g_Active = true;
    std::set<std::string> mods;
    for (const std::unique_ptr<ModRow>& row : g_Rows) {
        mods.insert(row->modKey);
    }
    Log("Mod art installed: " + mod_rules::CountArt(Counted(g_Rows)) + " from " + Plural(mods.size(), "mod", "mods") +
        ".");
    return true;
}

void OnEvent(CanopyEvent event) {
    if (g_Active && event == CANOPY_EVENT_TITLE_SCREEN && !g_Checked) {
        SelfCheck();
    }
}

namespace {

uint64_t RowId(const ts::TableSchema& schema, const std::string& modKey, const std::string& ref) {
    if (!g_Active || ref.empty()) {
        return 0;
    }
    const size_t dot = ref.find('.');
    const std::vector<mod_rules::ArtSpec*> specs = Specs();
    const mod_rules::ArtSpec* row = dot == std::string::npos
                                        ? mod_rules::FindArt(specs, schema, modKey, ref)
                                        : mod_rules::FindArt(specs, schema, ref.substr(0, dot), ref.substr(dot + 1));
    return row ? row->id : 0;
}

bool RowAdded(const ts::TableSchema& schema, uint64_t id) {
    for (const std::unique_ptr<ModRow>& row : g_Rows) {
        if (row->schema == &schema && row->id == id) {
            return row->appended.load(std::memory_order_acquire);
        }
    }
    return false;
}

}  // namespace

uint64_t TextureId(const std::string& modKey, const std::string& ref) {
    return RowId(ts::kTexture, modKey, ref);
}

uint64_t IconId(const std::string& modKey, const std::string& ref) {
    return RowId(ts::kIcon, modKey, ref);
}

bool IconAdded(uint64_t id) {
    return RowAdded(ts::kIcon, id);
}

uint64_t SkinId(const std::string& modKey, const std::string& ref) {
    return RowId(ts::kAnimeReplaceTexture, modKey, ref);
}

bool SkinAdded(uint64_t id) {
    return RowAdded(ts::kAnimeReplaceTexture, id);
}

}  // namespace mod_art
