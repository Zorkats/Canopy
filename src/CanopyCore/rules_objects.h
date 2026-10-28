#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "blob.h"
#include "mod_rules.h"
#include "rules_items.h"

// The rules of [object.<Key>] sections. See docs/OBJECTS.md.
namespace mod_rules {

// What an [object.<Key>] section says. The loader adds the id the object is
// given.
struct ObjectSpec {
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as the registry and the sidecars write it.
    std::string fullKey;
    int line = 0;
    // The vanilla furniture gimmick the object copies.
    uint64_t cloneId = 0;
    std::string names[kLanguageCount];
    // The model's skin, 0 to keep the clone's. A mod skin when modSkin is
    // set, else a vanilla AnimeReplaceTexture id.
    uint64_t skin = 0;
    bool modSkin = false;
};

// The most mod objects Canopy takes in all.
constexpr size_t kMaxObjects = 500;

extern const SectionInfo kObjectSection;

// Reads every [object.X] section of folders, in folder and file order, as the
// loader installs them: per mod key the first folder owns it, keys are unique
// per mod, and at most kMaxObjects load. accept sees each folder with usable
// objects.
std::vector<ObjectSpec> ReadObjects(
    const std::vector<ModFolder>& folders, const Refs& refs, Report& report, const FolderAccept& accept
);
// The folders with an [object.X] section, a bare [object.] included.
std::vector<ModFolder> WithObjects(const std::vector<ModFolder>& folders);

// ---- The gimmick table ---------------------------------------------------

// Where a gimmick.dat row's fields after its variable parts sit. The fields
// before them are fixed: id, enum, refID, 7 names, the anime id, then the 4
// AnimeReplaceTexture ids.
struct GimmickLayout {
    size_t colorRep = 0;
    size_t itemId = 0;
    size_t trailer = 0;
};

constexpr size_t kGimmickArt = 88;
constexpr size_t kGimmickArtCount = 4;

// Walks a gimmick.dat row field by field. False when its lengths do not add up
// to the row's size.
bool WalkGimmick(const blob::Row& row, GimmickLayout& layout);
// The item a gimmick row gives back when picked up, 0 when the row does not
// walk.
uint64_t GimmickItem(const blob::Row& row);

// The checks against the vanilla tables: the clone row is there, walks, and
// is furniture, a gimmick its own item places. clone is null when the table
// has no row cloneId. items is null when the item table cannot be read, as on
// the loader's gimmick worker; the clone must then only give an item back.
// False, reported, when the object is skipped.
bool CheckAgainstTables(const ObjectSpec& object, const blob::Row* clone, const blob::Table* items, Report& report);

// What a row needs besides the spec.
struct ObjectRowParts {
    uint64_t id = 0;
    std::string enumName;
    // The mod item that places it and that picking it up gives, 0 for none.
    uint64_t item = 0;
    // The skin id, 0 to keep the clone's.
    uint64_t skin = 0;
};

// The gimmick.dat row of object: a copy of the clone with the object's id,
// enum, names, item and skin, no refID and a zero trailer. The clone must
// walk to layout.
std::vector<uint8_t> BuildObjectRow(
    const ObjectSpec& object, const blob::Row& clone, const GimmickLayout& layout, const ObjectRowParts& parts,
    const TextWriter& write
);

// The desktop check: reads the objects, registers their keys, then checks
// them against the vanilla tables when there are any. Runs before the items,
// which name objects.
void CheckObjects(Context& context);

}  // namespace mod_rules
