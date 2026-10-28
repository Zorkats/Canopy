#include "rules_objects.h"

#include <algorithm>
#include <iterator>
#include <limits>

#include "platform_files.h"

// =============================================================
// OBJECT RULES
// =============================================================
// What the loader's mod objects accept from canopy.ini. An object is a copy
// of a vanilla furniture gimmick with its own id, names and skin, placed by
// the mod item that names it. Every message is the loader's log line.

namespace mod_rules {
namespace {

constexpr const char* kPrefix = "object.";
constexpr size_t kPrefixLength = 7;
constexpr size_t kMaxNameLength = 200;

// The gimmick.dat row layout, in the serializer's field order. Texts are
// 8-byte refs and a vector is a u32 count, then its elements.
constexpr size_t kRowId = 0;
constexpr size_t kRowEnum = 8;
constexpr size_t kRowRefId = 16;
constexpr size_t kRowNames = 24;
constexpr size_t kTextRef = 8;
// After the art: the shadow texture, the thumbnail, isDebugGenerate and
// isMirror.
constexpr size_t kRowAngles = 124;
constexpr size_t kScriptArgs = 23;
// The trailer: version, region, product and platform.
constexpr size_t kTrailerSize = 16;
// The most elements each vector holds in the vanilla table, with room.
constexpr uint32_t kMaxAngles = 8;
constexpr uint32_t kMaxParts = 64;
constexpr uint32_t kMaxFeatures = 8;
constexpr uint32_t kMaxSmall = 2;
constexpr uint32_t kMaxMasks = 4;
constexpr int64_t kMaxArtId = std::numeric_limits<int32_t>::max();

const FieldInfo kFields[] = {
    {"name", ValueKind::LangText, true,
     "The name the game shows, one line of up to 200 bytes. name.<lang> sets it for one language."},
    {"clone", ValueKind::VanillaId, true,
     "The vanilla furniture object to copy, with its model, size and behavior: one its own item places.", nullptr,
     "gimmick.dat"},
    {"skin", ValueKind::SkinRef, false,
     "A skin for the model: a vanilla AnimeReplaceTexture id, or a mod skin by key or Mod.Key. Leave it out to keep "
     "the clone's."},
};

bool IsObjectSection(const canopy_ini::IniSection& section) {
    return platform::CompareNoCase(section.name.c_str(), kPrefix, kPrefixLength) == 0;
}

bool HasObjects(const ModFolder& folder) {
    return std::any_of(folder.sections.begin(), folder.sections.end(), IsObjectSection);
}

// Reads one [object.X] section. Reports and returns false when it is unusable.
bool ReadObject(
    const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section, const Refs& refs,
    Report& report, ObjectSpec& object
) {
    object.modKey = modKey;
    object.folder = folder.name;
    object.key = section.name.substr(kPrefixLength);
    object.fullKey = modKey + "." + object.key;
    object.line = section.line;
    const std::string where = "[" + modKey + "] Object " + object.key;
    const auto at = [&](const char* key) {
        return canopy_ini::ValueLine(&section, key);
    };
    if (!KeyValid(object.key)) {
        report.Error(
            folder.name, section.line, where + " skipped, its key must be 1 to 32 letters, digits, '_' or '-'."
        );
        return false;
    }
    if (!ParseU64(canopy_ini::Value(&section, "clone", ""), object.cloneId) || object.cloneId == 0) {
        object.cloneId = 0;
        report.Error(folder.name, at("clone"), where + " skipped, it needs clone = <vanilla furniture object id>.");
        return false;
    }
    ReadTexts(section, "name", object.names);
    for (const std::string& name : object.names) {
        if (name.empty()) {
            report.Error(folder.name, at("name"), where + " skipped, it needs name = <text>.");
            return false;
        }
        if (!TextValid(name, kMaxNameLength)) {
            report.Error(folder.name, at("name"), where + " skipped, a name must be one line of up to 200 bytes.");
            return false;
        }
    }
    // A vanilla skin id, else a mod skin by key or Mod.Key.
    const std::string skin = canopy_ini::Value(&section, "skin", "");
    if (!skin.empty() && (!ParseU64(skin, object.skin) || object.skin == 0 || object.skin > kMaxArtId)) {
        object.skin = KeyRef(refs, RefKind::Skin, modKey, skin);
        object.modSkin = object.skin != 0;
        if (!object.modSkin) {
            report.Warning(
                folder.name, at("skin"),
                where + " keeps its clone's skin, skin must be a vanilla AnimeReplaceTexture id or a mod skin."
            );
        }
    }
    return true;
}

// Reads a gimmick.dat row in order, failing once a length runs past the end.
class Cursor {
public:
    explicit Cursor(const blob::Row& row) : row_(row) {}
    bool Ok() const {
        return ok_;
    }
    size_t At() const {
        return at_;
    }
    void Skip(size_t bytes) {
        if (!ok_ || bytes > row_.size - at_) {
            ok_ = false;
            return;
        }
        at_ += bytes;
    }
    // A vector: its count, at most cap, then count elements of size bytes.
    uint32_t Vector(size_t size, uint32_t cap) {
        if (!ok_ || row_.size - at_ < 4) {
            ok_ = false;
            return 0;
        }
        const uint32_t count = blob::Get<uint32_t>(row_.data, at_);
        at_ += 4;
        if (count > cap) {
            ok_ = false;
            return 0;
        }
        Skip(size * count);
        return count;
    }

private:
    const blob::Row& row_;
    size_t at_ = 0;
    bool ok_ = true;
};

// One PartsInfo.
void SkipPart(Cursor& cursor) {
    // name, isAnime, isInstancing, seqID.
    cursor.Skip(kTextRef + 12);
    cursor.Vector(8, kMaxSmall);
    cursor.Skip(8);
    cursor.Vector(8, kMaxSmall);
    // isIsometric, isCollision, isSensor, isSE, isAngle, decayStart,
    // decayEnd, isFixCollision, hitKind.
    cursor.Skip(36);
    // layerId, attackID, power, effect.
    cursor.Skip(kTextRef + 16);
    for (int mask = 0; mask < 3; ++mask) {
        cursor.Vector(4, kMaxMasks);
    }
    // scale, priority, warpType, 5 warp args, animalPopType, animalPopMax.
    cursor.Skip(12 + 5 * kTextRef + 8);
    cursor.Vector(kTextRef, kMaxMasks);
    // animalPopRate.
    cursor.Skip(4);
}

// The object an item section places, as Mod.Key, or empty.
std::string PlacedObject(const std::string& modKey, const canopy_ini::IniSection& section) {
    if (platform::CompareNoCase(section.name.c_str(), "item.", 5) != 0 ||
        canopy_ini::Lower(canopy_ini::Trim(canopy_ini::Value(&section, "use", ""))) != "place") {
        return std::string();
    }
    const std::string object = canopy_ini::Trim(canopy_ini::Value(&section, "object", ""));
    return object.empty() ? std::string() : FullKey(modKey, object);
}

}  // namespace

const SectionInfo kObjectSection = {kPrefix, "Object", "docs/OBJECTS.md", kFields, std::size(kFields)};

std::vector<ModFolder> WithObjects(const std::vector<ModFolder>& folders) {
    std::vector<ModFolder> wanted;
    for (const ModFolder& folder : folders) {
        if (HasObjects(folder)) {
            wanted.push_back(folder);
        }
    }
    return wanted;
}

std::vector<ObjectSpec> ReadObjects(
    const std::vector<ModFolder>& folders, const Refs& refs, Report& report, const FolderAccept& accept
) {
    std::vector<ObjectSpec> all;
    std::vector<std::string> owners;
    for (const ModFolder& folder : folders) {
        if (!HasObjects(folder)) {
            continue;
        }
        const canopy_ini::IniSection* info = canopy_ini::FindSection(folder.sections, "mod");
        const int infoLine = canopy_ini::ValueLine(info, "key");
        const std::string modKey = ModKey(folder);
        if (!KeyValid(modKey)) {
            report.Error(
                folder.name, infoLine,
                "Objects in folder " + folder.name + " skipped, its key " + modKey +
                    " must be 1 to 32 letters, digits, '_' or '-'."
            );
            continue;
        }
        if (std::any_of(owners.begin(), owners.end(), [&](const std::string& owner) {
                return platform::CompareNoCase(owner.c_str(), modKey.c_str()) == 0;
            })) {
            report.Error(
                folder.name, infoLine,
                "Objects in folder " + folder.name + " skipped, another folder already has objects under the key " +
                    modKey + "."
            );
            continue;
        }
        std::vector<ObjectSpec> objects;
        for (const canopy_ini::IniSection& section : folder.sections) {
            if (!IsObjectSection(section)) {
                continue;
            }
            ObjectSpec object;
            if (!ReadObject(folder, modKey, section, refs, report, object)) {
                continue;
            }
            const auto same = [&](const ObjectSpec& other) {
                return platform::CompareNoCase(other.key.c_str(), object.key.c_str()) == 0;
            };
            if (std::any_of(objects.begin(), objects.end(), same)) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Object " + object.key + " skipped, the mod already has an object with that key."
                );
                continue;
            }
            if (all.size() + objects.size() >= kMaxObjects) {
                report.Error(
                    folder.name, section.line,
                    "[" + modKey + "] Object " + object.key + " skipped, Canopy takes at most 500 mod objects."
                );
                break;
            }
            objects.push_back(std::move(object));
        }
        if (objects.empty() || !accept(folder, modKey, objects.size())) {
            continue;
        }
        owners.push_back(modKey);
        for (ObjectSpec& object : objects) {
            all.push_back(std::move(object));
        }
    }
    return all;
}

bool WalkGimmick(const blob::Row& row, GimmickLayout& layout) {
    Cursor cursor(row);
    cursor.Skip(kRowAngles);
    cursor.Vector(4, kMaxAngles);
    // siloCapacity, inventoryCapacity, the script, its args.
    cursor.Skip(8 + kTextRef + 2 * kTextRef * kScriptArgs);
    const uint32_t parts = cursor.Vector(0, kMaxParts);
    for (uint32_t part = 0; part < parts; ++part) {
        SkipPart(cursor);
    }
    layout.colorRep = cursor.At();
    cursor.Skip(4);
    cursor.Vector(8, kMaxFeatures);
    layout.itemId = cursor.At();
    // itemID, putSEID, outSEID, footSEType.
    cursor.Skip(32);
    layout.trailer = cursor.At();
    cursor.Skip(kTrailerSize);
    return cursor.Ok() && cursor.At() == row.size;
}

uint64_t GimmickItem(const blob::Row& row) {
    GimmickLayout layout;
    return WalkGimmick(row, layout) ? blob::Get<uint64_t>(row.data, layout.itemId) : 0;
}

bool CheckAgainstTables(const ObjectSpec& object, const blob::Row* clone, const blob::Table* items, Report& report) {
    const std::string where = "Mod object " + object.fullKey;
    if (!clone) {
        report.Error(
            object.folder, object.line,
            where + " skipped, its clone " + std::to_string(object.cloneId) + " is not in the gimmick table."
        );
        return false;
    }
    GimmickLayout layout;
    if (!WalkGimmick(*clone, layout)) {
        report.Error(
            object.folder, object.line,
            where + " skipped, its clone " + std::to_string(object.cloneId) + " has a layout Canopy does not know."
        );
        return false;
    }
    const uint64_t gives = blob::Get<uint64_t>(clone->data, layout.itemId);
    const auto placesClone = [&] {
        const auto item = std::find_if(items->rows.begin(), items->rows.end(), [&](const blob::Row& row) {
            return blob::RowId(row) == gives;
        });
        return item != items->rows.end() && PlacesGimmick(*item, object.cloneId);
    };
    if (gives == 0 || (items && !placesClone())) {
        report.Error(
            object.folder, object.line,
            where + " skipped, its clone " + std::to_string(object.cloneId) +
                " is not furniture: no item places it and gives it back."
        );
        return false;
    }
    return true;
}

std::vector<uint8_t> BuildObjectRow(
    const ObjectSpec& object, const blob::Row& clone, const GimmickLayout& layout, const ObjectRowParts& parts,
    const TextWriter& write
) {
    std::vector<uint8_t> row(clone.data, clone.data + clone.size);
    blob::Put<uint64_t>(row.data(), kRowId, parts.id);
    write(row, kRowEnum, parts.enumName);
    blob::Put<uint64_t>(row.data(), kRowRefId, 0);
    for (size_t language = 0; language < static_cast<size_t>(kLanguageCount); ++language) {
        write(row, kRowNames + kTextRef * language, object.names[language]);
    }
    if (parts.skin != 0) {
        const int32_t skin = static_cast<int32_t>(parts.skin);
        for (size_t art = 0; art < kGimmickArtCount; ++art) {
            blob::Put<int32_t>(row.data(), kGimmickArt + 4 * art, skin);
        }
        blob::Put<int32_t>(row.data(), layout.colorRep, skin);
    }
    blob::Put<uint64_t>(row.data(), layout.itemId, parts.item);
    std::fill(row.begin() + static_cast<std::ptrdiff_t>(layout.trailer), row.end(), uint8_t{0});
    return row;
}

void CheckObjects(Context& context) {
    const std::vector<ObjectSpec> objects = ReadObjects(context.folders, context.refs, context.report, AcceptAll);
    for (const ObjectSpec& object : objects) {
        context.refs.Add(RefKind::Object, object.fullKey);
    }
    // An object no item places can never be put down.
    std::vector<std::string> placed;
    for (const ModFolder& folder : context.folders) {
        const std::string modKey = ModKey(folder);
        for (const canopy_ini::IniSection& section : folder.sections) {
            const std::string object = PlacedObject(modKey, section);
            if (!object.empty()) {
                placed.push_back(object);
            }
        }
    }
    for (const ObjectSpec& object : objects) {
        const bool isPlaced = std::any_of(placed.begin(), placed.end(), [&](const std::string& key) {
            return platform::CompareNoCase(key.c_str(), object.fullKey.c_str()) == 0;
        });
        if (!isPlaced) {
            context.report.Warning(
                object.folder, object.line,
                "Mod object " + object.fullKey + " cannot be placed, no item has use = place and object = " +
                    object.key + "."
            );
        }
    }
    const blob::Table* gimmicks = context.vanilla ? context.vanilla->Table("gimmick.dat") : nullptr;
    const blob::Table* items = context.vanilla ? context.vanilla->Table("item.dat") : nullptr;
    if (!gimmicks || !items) {
        return;
    }
    const bool haveSkins = context.vanilla->Table("animereplacetexture.dat") != nullptr;
    for (const ObjectSpec& object : objects) {
        const auto clone = std::find_if(gimmicks->rows.begin(), gimmicks->rows.end(), [&](const blob::Row& row) {
            return blob::RowId(row) == object.cloneId;
        });
        const bool found = clone != gimmicks->rows.end();
        if (!CheckAgainstTables(object, found ? &*clone : nullptr, items, context.report)) {
            continue;
        }
        if (!object.modSkin && object.skin != 0 && haveSkins &&
            !context.vanilla->HasRow("animereplacetexture.dat", object.skin)) {
            context.report.Warning(
                object.folder, object.line,
                "Mod object " + object.fullKey + " keeps its clone's skin, no AnimeReplaceTexture row has the id " +
                    std::to_string(object.skin) + "."
            );
        }
    }
}

}  // namespace mod_rules
