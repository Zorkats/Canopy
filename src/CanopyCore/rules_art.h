#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "table_schema.h"

// The rules of [texture.<Key>], [icon.<Key>], [skin.<Key>] and
// [row.<table>.<Key>] sections. See docs/ART.md.
namespace mod_rules {

enum class ArtKind : uint8_t { Texture, Icon, Skin, Raw };

struct ArtSpec;

// A reference to a row of another table.
struct ArtRef {
    // As written, for the log.
    std::string text;
    // The line of canopy.ini it is written on.
    int line = 0;
    // A mod row, a vanilla id, or a vanilla enum name. Set by ResolveArt.
    const ArtSpec* row = nullptr;
    uint64_t id = 0;
    std::string enumName;
};

// One replace of a skin: a model texture name and the texture it shows.
struct ArtSwap {
    std::string name;
    ArtRef texture;
};

// One key = value of a raw row.
struct ArtField {
    std::string path;
    std::string value;
    int line = 0;
    // Ref fields name a row of refTable.
    const char* refTable = nullptr;
    ArtRef ref;
};

// What an art section says. The loader adds what its table loads set.
struct ArtSpec {
    const table_schema::TableSchema* schema = nullptr;
    ArtKind kind = ArtKind::Texture;
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as the registry writes it.
    std::string fullKey;
    int line = 0;
    // "[Mod] Icon Key" while reading, "Mod icon Mod.Key" after.
    std::string where;
    std::string label;
    // Given by ResolveArt, 0 for a row that has none.
    uint64_t id = 0;
    uint64_t baseId = 0;

    // Texture: the file as written and on disk, the path the game asks for,
    // and the file served there, with its name for the log.
    std::string file;
    std::wstring source;
    bool nltx = false;
    std::string gamePath;
    std::wstring diskPath;
    std::string diskLabel;
    uint32_t width = 0;
    uint32_t height = 0;

    // Icon.
    ArtRef texture;
    std::vector<std::string> rect;
    int rectLine = 0;
    std::vector<std::string> size;
    std::vector<std::string> offset;

    // Skin.
    std::vector<ArtSwap> swaps;

    // Raw.
    std::vector<ArtField> fields;
    uint32_t elements = 0;
};

// The most mod art rows Canopy takes in all.
constexpr size_t kMaxArtRows = 10000;

extern const SectionInfo kTextureSection;
extern const SectionInfo kIconSection;
extern const SectionInfo kSkinSection;
extern const SectionInfo kRowSection;

// What reading art asks of its host.
struct ArtHost {
    // Reads a texture's file: sets its width and height, and the disk file
    // the game is served. False, with why, when the texture is skipped.
    // Empty: LoadTexture, which only reads the size.
    std::function<bool(ArtSpec& row, std::string& why)> load;
    // Serves a texture's disk file at its gamePath. False when it cannot.
    // Empty on the desktop, which serves nothing.
    std::function<bool(const ArtSpec& row)> serve;
    // Called once per folder with usable art, with its rows. False drops
    // them. Empty accepts every folder.
    std::function<bool(const ModFolder& folder, const std::string& modKey, const std::vector<ArtSpec>& rows)> accept;
};

// Reads every art section of folders, in folder and file order, as the
// loader installs them: per mod key the first folder owns it, keys are
// unique per mod and table, and at most kMaxArtRows load. modsDir is the
// game's mods folder, which texture files are read from (FolderDir: from a
// folder's own dir when it has one).
std::vector<ArtSpec> ReadArt(
    const std::vector<ModFolder>& folders, const std::string& modsDir, Report& report, const ArtHost& host
);

// Gives keys their numbers in schema's id band: numbers[i] is 0 for a key
// that gets none. False when the numbers cannot be kept, so no key gets one.
using ArtNumbers = std::function<bool(
    const table_schema::TableSchema& schema, const std::vector<std::string>& keys, std::vector<uint64_t>& numbers
)>;
// Resolves every row's references and gives the rows that resolve their
// ids, table by table in table_schema::All() order: textures first, since
// icons and skins name them, and an icon's default rect is its texture's
// size. A row that fails, or gets no number, keeps id 0. An empty number
// numbers each table's rows 1, 2, and so on.
void ResolveArt(const std::vector<ArtSpec*>& rows, const ArtNumbers& number, Report& report);
// The row of schema's table with modKey and key that has an id, or null.
const ArtSpec* FindArt(
    const std::vector<ArtSpec*>& rows, const table_schema::TableSchema& schema, const std::string& modKey,
    const std::string& key
);
// "1 texture, 2 icons and 1 skin".
std::string CountArt(const std::vector<const ArtSpec*>& rows);
// True when id is in the band of mod ids of schema's table.
bool InBand(const table_schema::TableSchema& schema, uint64_t id);

// ---- Texture files -------------------------------------------------------

// Reads a whole file of up to 512 MB. False when it cannot.
bool ReadArtFile(const std::wstring& path, std::vector<uint8_t>& bytes);
// A .nltx texture: reads row.source and sets the size from its header.
bool LoadNltx(ArtSpec& row, std::string& why);
// Decodes an image texture's row.source. False, with why, when it does not
// decode or is over 8192x8192.
bool DecodeImage(
    const ArtSpec& row, uint32_t& width, uint32_t& height, std::vector<uint8_t>& pixels, std::string& why
);
// The desktop load: the size of row.source, a .nltx or an image.
bool LoadTexture(ArtSpec& row, std::string& why);

// ---- Rows ----------------------------------------------------------------

// What building a row asks of the game's tables.
class ArtTables {
public:
    virtual ~ArtTables() = default;
    // The id of the row named name in file. False, with why, when there is
    // none.
    virtual bool EnumId(const char* file, const std::string& name, uint64_t& id, std::string& why) const = 0;
    // True when id is in file, or cannot be known yet and the title checks
    // it (titleChecks). False, with why, when the row must be skipped.
    virtual bool RefFine(
        const table_schema::TableSchema& schema, const char* file, uint64_t id, bool titleChecks, std::string& why
    ) const = 0;
};

// Builds mod's row of table: a copy of its base, or a new row, with its
// values written. byId maps the table's ids to its rows. False, with why,
// when it cannot.
bool BuildArtRow(
    const ArtSpec& mod, const blob::Table& table, const std::unordered_map<uint64_t, size_t>& byId,
    const ArtTables& tables, blob::Pool& pool, std::vector<uint8_t>& row, std::string& why
);
// Checks every reference a built row holds, its base's included.
bool CheckArtRefs(
    const table_schema::TableSchema& schema, const std::vector<uint8_t>& row, const ArtTables& tables, std::string& why
);

// The desktop check: reads and resolves the art, registers the keys that
// get ids, then builds each row against the vanilla tables when there are
// some.
void CheckArt(Context& context);

}  // namespace mod_rules
