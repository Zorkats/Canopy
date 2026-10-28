#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "blob.h"
#include "canopy_ini.h"
#include "mod_rules.h"
#include "rules_patch.h"
#include "table_schema.h"
#include "test_support.h"

// Checks of the [patch.<table>.<id>] rules: reading sections and every
// error, merging several mod folders, applying patches to a table's bytes,
// and the desktop check. Mods are written to a temporary folder; the game
// is read, never written, and only when it is installed. Only ids, counts,
// sizes and offsets are printed, never game text.
namespace {

namespace fs = std::filesystem;
namespace ts = table_schema;
using mod_rules::Diagnostic;
using mod_rules::PatchField;
using mod_rules::PatchSection;
using mod_rules::RowPatch;
using mod_rules::Severity;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED patch_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

// A mods folder of its own under the temporary folder, removed when the
// test ends.
class TempMods {
public:
    explicit TempMods(const char* name) {
        root_ = fs::temp_directory_path() /
                ("canopy_patch_tests_" + std::to_string(test_support::ProcessId()) + "_" + name);
        std::error_code error;
        fs::remove_all(root_, error);
        fs::create_directories(root_, error);
    }
    ~TempMods() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    void Write(const std::string& relative, const std::string& text) const {
        const fs::path path = root_ / fs::u8path(relative);
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << text;
    }
    std::string Dir() const {
        return root_.string();
    }

private:
    fs::path root_;
};

// The lines joined into a file: line n of the file is lines[n - 1].
std::string Lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const std::string& line : lines) {
        text += line + "\n";
    }
    return text;
}

bool Said(const mod_rules::Report& report, Severity severity, int line, const std::string& part) {
    for (const Diagnostic& item : report.Items()) {
        if (item.severity == severity && item.line == line && item.message.find(part) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool SaidExactly(const mod_rules::Report& report, Severity severity, const std::string& folder, int line,
                 const std::string& message) {
    for (const Diagnostic& item : report.Items()) {
        if (item.severity == severity && item.folder == folder && item.line == line && item.message == message) {
            return true;
        }
    }
    return false;
}

const PatchField* FieldOf(const RowPatch& patch, const std::string& path) {
    for (const PatchField& field : patch.fields) {
        if (field.path == path) {
            return &field;
        }
    }
    return nullptr;
}

const PatchSection* SectionOf(const std::vector<PatchSection>& sections, const ts::TableSchema& schema, uint64_t id,
                              const std::string& enumName = std::string()) {
    for (const PatchSection& section : sections) {
        if (section.schema == &schema && section.id == id && section.enumName == enumName) {
            return &section;
        }
    }
    return nullptr;
}

// ---- Reading ---------------------------------------------------------------

void TestReading() {
    std::printf("Patch sections\n");
    CHECK(mod_rules::kPatchSection.prefix == std::string("patch."));
    CHECK(mod_rules::kPatchSection.title == std::string("Patch"));
    bool registered = false;
    for (const mod_rules::SectionInfo* info : mod_rules::Sections()) {
        registered = registered || info == &mod_rules::kPatchSection;
    }
    CHECK(registered);

    TempMods mods("read");
    mods.Write(
        "Alpha/canopy.ini",
        Lines({
            "[mod]",                              // 1
            "key = Alpha",                        // 2
            "[patch.icon.1000]",                  // 3
            "DrawInfo.0.X = 12.5",                // 4  fine, any case
            "comment = A note",                   // 5  fine, a text
            "drawInfo.y = 3",                     // 6  fine, every element
            "drawInfo.2.y = 4",                   // 7  set on line 6
            "drawInfo.0.x = 13",                  // 8  set on line 4
            "id = 5",                             // 9  an id
            "enumName = ICON_X",                  // 10 an enum name
            "trailer = 1",                        // 11 not editable
            "nosuch = 1",                         // 12 not in the schema
            "drawInfo.0.w = wide",                // 13 not a number
            "drawInfo.9.w = 1",                   // 14 past the array
            "textureID = Other.Tex",              // 15 not a number
            "[patch.icon.dat.1000]",              // 16 the row again
            "drawInfo.1.x = 1",                   // 17
            "[patch.nosuch.5]",                   // 18 no such table
            "x = 1",                              // 19
            "[patch.icon.12x]",                   // 20 malformed row
            "x = 1",                              // 21
            "[patch.icon]",                       // 22 no row
            "[patch.ICON.ICON_ID_ITEM_TEST]",     // 23 by enum name
            "drawInfo.3.h = 2",                   // 24
            "[patch.AnimeReplaceTexture.Dat.7]",  // 25
            "entries.name = a",                   // 26 every element
            "entries.1.texture = 9",              // 27 fine
            "entries.99999.name = a",             // 28 past any row
            "[patch.texture.8]",                  // 29 nothing set
            "[patch.icon.bad-name]",              // 30 malformed row
            "x = 1",                              // 31
            "[patch.texture.99999999999999999999]",  // 32 too large
            "x = 1",                              // 33
        })
    );
    mods.Write("Bad/canopy.ini", Lines({"[mod]", "key = bad key", "[patch.icon.1000]", "drawInfo.0.x = 1"}));
    mods.Write("Other/canopy.ini", Lines({"[item.Thing]", "base = 1"}));

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    CHECK(folders.size() == 3);
    mod_rules::KeyRefs refs;
    mod_rules::Report report;
    const std::vector<PatchSection> sections = mod_rules::ReadPatches(folders, refs, report);
    CHECK(sections.size() == 3);

    const PatchSection* icon = SectionOf(sections, ts::kIcon, 1000);
    if (CHECK(icon != nullptr)) {
        CHECK(icon->folder == "Alpha" && icon->modKey == "Alpha" && icon->line == 3);
        // drawInfo.0.x, comment and the seven drawInfo.N.y.
        CHECK(icon->fields.size() == 9);
        CHECK(!icon->fields.empty() && icon->fields[0].path == "drawInfo.0.x" && icon->fields[0].value == "12.5" &&
              icon->fields[0].line == 4);
        CHECK(icon->fields.size() > 1 && icon->fields[1].path == "comment" && icon->fields[1].value == "A note");
        CHECK(icon->fields.size() == 9 && icon->fields[8].path == "drawInfo.6.y" && icon->fields[8].line == 6);
    }
    const PatchSection* named = SectionOf(sections, ts::kIcon, 0, "ICON_ID_ITEM_TEST");
    CHECK(named && named->fields.size() == 1 && named->fields[0].path == "drawInfo.3.h");
    const PatchSection* skin = SectionOf(sections, ts::kAnimeReplaceTexture, 7);
    CHECK(skin && skin->fields.size() == 1 && skin->fields[0].path == "entries.1.texture" &&
          skin->fields[0].value == "9");

    const std::string at = "[Alpha] Patch icon.dat 1000, ";
    CHECK(Said(report, Severity::Error, 7, at + "drawinfo.2.y skipped, the section already sets drawInfo.2.y on line 6."));
    CHECK(Said(report, Severity::Error, 8, at + "drawinfo.0.x skipped, the section already sets drawInfo.0.x on line 4."));
    CHECK(Said(report, Severity::Error, 9, at + "id skipped, id cannot be patched, a patch never changes a row's id"));
    CHECK(Said(report, Severity::Error, 10, at + "enumname skipped, enumName cannot be patched"));
    CHECK(Said(report, Severity::Error, 11, at + "trailer skipped, "));
    CHECK(Said(report, Severity::Error, 12, at + "nosuch skipped, "));
    CHECK(Said(report, Severity::Error, 13, at + "drawinfo.0.w skipped, "));
    CHECK(Said(report, Severity::Error, 14, at + "drawinfo.9.w skipped, "));
    CHECK(Said(report, Severity::Error, 15, at + "textureid skipped, "));
    CHECK(SaidExactly(report, Severity::Error, "Alpha", 16,
                      "[Alpha] Patch icon.dat 1000 skipped, the mod already patches that row on line 3."));
    CHECK(SaidExactly(report, Severity::Error, "Alpha", 18,
                      "[Alpha] Patch nosuch.5 skipped, Canopy has no schema for the table nosuch, so it cannot be "
                      "patched. PATCHES.md lists the 133 tables that take patches."));
    CHECK(SaidExactly(report, Severity::Error, "Alpha", 20,
                      "[Alpha] Patch icon.12x skipped, its row 12x must be a decimal id or an enum name."));
    CHECK(SaidExactly(report, Severity::Error, "Alpha", 22,
                      "[Alpha] Section patch.icon skipped, a patch is [patch.<table>.<id>]."));
    CHECK(Said(report, Severity::Error, 26,
               "[Alpha] Patch animereplacetexture.dat 7, entries.name skipped, entries.name names every element of "
               "entries, and a path names one, such as entries.0.name."));
    CHECK(Said(report, Severity::Error, 28, "entries.99999.name skipped, entries has no element 99999"));
    CHECK(SaidExactly(report, Severity::Warning, "Alpha", 29,
                      "[Alpha] Patch texture.dat 8 skipped, it sets no field."));
    CHECK(Said(report, Severity::Error, 30, "its row bad-name must be a decimal id or an enum name."));
    CHECK(Said(report, Severity::Error, 32, "its id 99999999999999999999 is too large."));
    CHECK(SaidExactly(report, Severity::Error, "Bad", 2,
                      "Patches in folder Bad skipped, its key bad key must be 1 to 32 letters, digits, '_' or '-'."));
    CHECK(report.Count(Severity::Error) == 18);
    CHECK(report.Count(Severity::Warning) == 1);

    // A reference written as Mod.Key, for the kinds the loader knows.
    static const ts::FieldSpec kItemRef = {"item", 0, 8, ts::FieldType::Ref64, "item.dat", nullptr, nullptr,
                                           0,      0, nullptr, 0, true, -1};
    static const ts::FieldSpec kTextureRef = {"tex", 0, 8, ts::FieldType::Ref64, "texture.dat", nullptr, nullptr,
                                              0,     0, nullptr, 0, true, -1};
    ts::Location item;
    item.field = &kItemRef;
    item.type = ts::FieldType::Ref64;
    item.size = 8;
    item.path = "item";
    ts::Location texture = item;
    texture.field = &kTextureRef;
    texture.path = "tex";
    mod_rules::KeyRefs keys;
    const uint64_t thing = keys.Add(mod_rules::RefKind::Item, "Other.Thing");
    std::string resolved;
    std::string why;
    CHECK(mod_rules::ResolvePatchValue(item, "other.thing", keys, resolved, why) && resolved == std::to_string(thing));
    CHECK(!mod_rules::ResolvePatchValue(item, "Other.Gone", keys, resolved, why) &&
          why == "item names Other.Gone, which is not an item of an installed mod");
    CHECK(mod_rules::ResolvePatchValue(item, "1234", keys, resolved, why) && resolved == "1234");
    CHECK(mod_rules::ResolvePatchValue(texture, "Other.Thing", keys, resolved, why) && resolved == "Other.Thing");
    CHECK(mod_rules::ResolvePatchValue(item, "12.5", keys, resolved, why) && resolved == "12.5");
    // A 64-bit flag of a generated table takes a mod flag too.
    std::vector<ts::Location> flags;
    if (CHECK(ts::ResolvePath(*ts::FindPatchable("storesales"), "condOnFlag.0", flags, why) && flags.size() == 1)) {
        const uint64_t seen = keys.Add(mod_rules::RefKind::Flag, "Other.Seen");
        CHECK(flags[0].type == ts::FieldType::Ref64);
        CHECK(mod_rules::ResolvePatchValue(flags[0], "Other.Seen", keys, resolved, why) &&
              resolved == std::to_string(seen));
        CHECK(!mod_rules::ResolvePatchValue(flags[0], "Other.Thing", keys, resolved, why) &&
              why == "condOnFlag.0 names Other.Thing, which is not a flag of an installed mod");
    }

    // Where the game's record keeps a field.
    ts::Location location;
    CHECK(mod_rules::PatchRecordOffset(ts::kIcon, "drawInfo.2.w", location) == 96 + 2 * 32 + 8);
    CHECK(location.path == "drawInfo.2.w" && location.type == ts::FieldType::F32 && location.size == 4);
    CHECK(mod_rules::PatchRecordOffset(ts::kIcon, "textureID", location) == 88);
    CHECK(mod_rules::PatchRecordOffset(ts::kIcon, "comment", location) == -1);
    CHECK(mod_rules::PatchRecordOffset(ts::kIcon, "drawInfo.x", location) == -1);
    CHECK(mod_rules::PatchRecordOffset(ts::kTexture, "isResident", location) == 144);
    CHECK(mod_rules::PatchRecordOffset(ts::kTexture, "height", location) == 160);
    CHECK(mod_rules::PatchRecordOffset(ts::kTexture, "fileName.en", location) == -1);
    CHECK(mod_rules::PatchRecordOffset(ts::kAnimeReplaceTexture, "entries.0.texture", location) == -1);
}

// ---- Merging ---------------------------------------------------------------

void TestMerging() {
    std::printf("Patch merging\n");
    TempMods mods("merge");
    // Load order without case: Alpha, beta, Gamma; Delta is turned off.
    mods.Write("Alpha/canopy.ini", Lines({"[patch.icon.1000]", "drawInfo.0.x = 1", "drawInfo.0.y = 2"}));
    mods.Write(
        "beta/canopy.ini",
        Lines({"[mod]", "key = Beta", "[patch.icon.1000]", "drawInfo.0.x = 5", "comment = b", "[patch.texture.9]",
               "width = 64"})
    );
    mods.Write(
        "Gamma/canopy.ini",
        Lines({"[patch.icon.dat.1000]", "drawInfo.0.x = 9", "drawInfo.0.y = 8", "drawInfo.1.w = 3"})
    );
    mods.Write("Delta/canopy.ini", Lines({"[patch.icon.1000]", "drawInfo.0.x = 100"}));
    mods.Write("CanopyData/disabled.txt", "Delta\n");

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    std::vector<std::string> names;
    for (const mod_rules::ModFolder& folder : folders) {
        if (folder.hasIni) {
            names.push_back(folder.name);
        }
    }
    CHECK((names == std::vector<std::string>{"Alpha", "beta", "Gamma"}));
    mod_rules::KeyRefs refs;
    mod_rules::Report readReport;
    const std::vector<PatchSection> sections = mod_rules::ReadPatches(folders, refs, readReport);
    CHECK(sections.size() == 4 && readReport.Items().empty());

    mod_rules::Report report;
    const std::vector<RowPatch> rows = mod_rules::MergePatches(sections, mod_rules::PatchEnumLookup(), report);
    CHECK(rows.size() == 2);
    if (rows.size() == 2) {
        const RowPatch& icon = rows[0];
        CHECK(icon.schema == &ts::kIcon && icon.id == 1000 && icon.origins.size() == 3);
        CHECK(icon.fields.size() == 4);
        const PatchField* x = FieldOf(icon, "drawInfo.0.x");
        const PatchField* y = FieldOf(icon, "drawInfo.0.y");
        const PatchField* comment = FieldOf(icon, "comment");
        const PatchField* w = FieldOf(icon, "drawInfo.1.w");
        CHECK(x && x->value == "9" && x->folder == "Gamma");
        CHECK(y && y->value == "8" && y->folder == "Gamma");
        CHECK(comment && comment->value == "b" && comment->folder == "beta" && comment->modKey == "Beta");
        CHECK(w && w->value == "3" && w->folder == "Gamma");
        CHECK(rows[1].schema == &ts::kTexture && rows[1].id == 9 && rows[1].fields.size() == 1);
        CHECK(mod_rules::CountPatches(rows) == "5 fields on 2 rows of 2 tables, from 3 mods");
    }
    CHECK(report.Count(Severity::Warning) == 3 && report.Count(Severity::Error) == 0);
    CHECK(SaidExactly(report, Severity::Warning, "beta", 4,
                      "Patch conflict: Alpha and beta both set icon.dat 1000 drawInfo.0.x. beta wins, it comes later "
                      "in load order."));
    CHECK(SaidExactly(report, Severity::Warning, "Gamma", 2,
                      "Patch conflict: beta and Gamma both set icon.dat 1000 drawInfo.0.x. Gamma wins, it comes later "
                      "in load order."));
    CHECK(SaidExactly(report, Severity::Warning, "Gamma", 3,
                      "Patch conflict: Alpha and Gamma both set icon.dat 1000 drawInfo.0.y. Gamma wins, it comes later "
                      "in load order."));

    // Pure: the same sections merge the same way again.
    mod_rules::Report again;
    const std::vector<RowPatch> second = mod_rules::MergePatches(sections, mod_rules::PatchEnumLookup(), again);
    CHECK(second.size() == rows.size() && again.Items().size() == report.Items().size());

    // Several places at once, named in one line.
    TempMods many("merge_many");
    many.Write("A/canopy.ini", Lines({"[patch.icon.5]", "drawInfo.x = 1"}));
    many.Write("B/canopy.ini", Lines({"[patch.icon.5]", "drawInfo.0.x = 2", "drawInfo.1.x = 2", "drawInfo.2.x = 2"}));
    mod_rules::Report manyReport;
    const std::vector<RowPatch> manyRows = mod_rules::MergePatches(
        mod_rules::ReadPatches(mod_rules::ScanMods(many.Dir()), refs, manyReport), mod_rules::PatchEnumLookup(),
        manyReport
    );
    CHECK(manyRows.size() == 1 && manyRows[0].fields.size() == 7);
    CHECK(SaidExactly(manyReport, Severity::Warning, "B", 2,
                      "Patch conflict: A and B both set icon.dat 5 drawInfo.0.x, drawInfo.1.x and drawInfo.2.x. B "
                      "wins, it comes later in load order."));

    // An enum name and an id of one row are one row once looked up; the
    // folder that patches it both ways keeps its first section.
    TempMods named("merge_named");
    named.Write("A/canopy.ini", Lines({"[patch.icon.ICON_ID_ITEM_TEST]", "drawInfo.0.h = 1"}));
    named.Write("B/canopy.ini", Lines({"[patch.icon.1000]", "drawInfo.0.h = 2", "[patch.icon.ICON_ID_ITEM_TEST]",
                                       "drawInfo.0.w = 2"}));
    const std::vector<PatchSection> namedSections =
        mod_rules::ReadPatches(mod_rules::ScanMods(named.Dir()), refs, manyReport);
    mod_rules::Report apart;
    CHECK(mod_rules::MergePatches(namedSections, mod_rules::PatchEnumLookup(), apart).size() == 2);
    CHECK(apart.Items().empty());
    const mod_rules::PatchEnumLookup lookup = [](const ts::TableSchema& schema, const std::string& name,
                                                 uint64_t& id) {
        id = 1000;
        return &schema == &ts::kIcon && name == "ICON_ID_ITEM_TEST";
    };
    mod_rules::Report joined;
    const std::vector<RowPatch> one = mod_rules::MergePatches(namedSections, lookup, joined);
    CHECK(one.size() == 1 && one[0].id == 1000 && one[0].enumName.empty() && one[0].fields.size() == 1);
    CHECK(!one.empty() && FieldOf(one[0], "drawInfo.0.h") && FieldOf(one[0], "drawInfo.0.h")->value == "2");
    CHECK(SaidExactly(joined, Severity::Warning, "B", 2,
                      "Patch conflict: A and B both set icon.dat 1000 drawInfo.0.h. B wins, it comes later in load "
                      "order."));
    CHECK(SaidExactly(joined, Severity::Error, "B", 3,
                      "[B] Patch icon.dat ICON_ID_ITEM_TEST skipped, the mod already patches that row on line 1."));
}

// ---- Applying --------------------------------------------------------------

// A blob of rows over pool, its texts already in pool.
std::vector<uint8_t> BuildBlob(const std::vector<std::vector<uint8_t>>& rows, const blob::Pool& pool) {
    std::vector<uint8_t> bytes(blob::kHeaderSize);
    uint32_t rowBytes = 0;
    for (const std::vector<uint8_t>& row : rows) {
        const uint32_t size = static_cast<uint32_t>(row.size());
        bytes.resize(bytes.size() + 4);
        blob::Put<uint32_t>(bytes.data(), bytes.size() - 4, size);
        bytes.insert(bytes.end(), row.begin(), row.end());
        rowBytes += size;
    }
    bytes.insert(bytes.end(), pool.Bytes().begin(), pool.Bytes().end());
    blob::Put<uint32_t>(bytes.data(), 0, static_cast<uint32_t>(rows.size()));
    blob::Put<uint32_t>(bytes.data(), 4, rowBytes);
    blob::Put<uint32_t>(bytes.data(), 8, static_cast<uint32_t>(pool.Bytes().size()));
    return bytes;
}

std::vector<uint8_t> IconRow(uint64_t id, const std::string& name, blob::Pool& pool) {
    std::vector<uint8_t> row = ts::NewRow(ts::kIcon, 0, pool);
    blob::Put<uint64_t>(row.data(), 0, id);
    pool.Write(row, 8, name);
    for (size_t at = 32; at < 32 + 224; at += 4) {
        blob::Put<float>(row.data(), at, static_cast<float>(at));
    }
    return row;
}

RowPatch MakePatch(const ts::TableSchema& schema, uint64_t id, const std::string& enumName,
                   const std::vector<std::pair<std::string, std::string>>& fields, size_t order = 0) {
    RowPatch patch;
    patch.schema = &schema;
    patch.id = id;
    patch.enumName = enumName;
    for (const auto& [path, value] : fields) {
        PatchField field;
        field.path = path;
        field.value = value;
        field.folder = "Test";
        field.modKey = "Test";
        field.order = order;
        patch.fields.push_back(field);
    }
    patch.origins.push_back({"Test", "Test", 1});
    return patch;
}

// The absolute byte offsets in a blob that applied fields of result wrote.
std::set<size_t> PatchedBytes(const blob::Table& table, const mod_rules::PatchResult& result) {
    std::set<size_t> bytes;
    for (const mod_rules::PatchedRow& row : result.rows) {
        const blob::Row& source = table.rows[row.row];
        for (const PatchField* field : row.applied) {
            std::vector<ts::Location> locations;
            std::string why;
            const uint32_t count = ts::VectorCount(*row.patch->schema, source.data, source.size);
            if (!ts::Resolve(*row.patch->schema, field->path, count, source.size, locations, why)) {
                continue;
            }
            const size_t start = blob::kHeaderSize + static_cast<size_t>(source.data - table.rowsStart);
            for (const ts::Location& location : locations) {
                for (size_t at = 0; at < location.size; ++at) {
                    bytes.insert(start + location.offset + at);
                }
            }
        }
    }
    return bytes;
}

// True when out is in, every row in its place and of its size, the bytes
// outside patched ones the same, and the pool kept with any texts after it.
bool SameOutside(const std::vector<uint8_t>& in, const blob::Table& table, const std::vector<uint8_t>& out,
                 const mod_rules::PatchResult& result, size_t& changed) {
    blob::Table parsed;
    std::string error;
    if (!blob::Parse(out.data(), out.size(), parsed, error) || parsed.rows.size() != table.rows.size() ||
        parsed.rowBytes != table.rowBytes || parsed.poolBytes < table.poolBytes) {
        return false;
    }
    for (size_t index = 0; index < table.rows.size(); ++index) {
        if (parsed.rows[index].size != table.rows[index].size ||
            parsed.rows[index].data - parsed.rowsStart != table.rows[index].data - table.rowsStart) {
            return false;
        }
    }
    const std::set<size_t> patched = PatchedBytes(table, result);
    const size_t declared = blob::kHeaderSize + table.rowsSize + table.poolBytes;
    if (out.size() != declared + (parsed.poolBytes - table.poolBytes) || in.size() < declared) {
        return false;
    }
    changed = 0;
    for (size_t at = 0; at < declared; ++at) {
        if (at >= 8 && at < 12) {
            continue;
        }
        if (out[at] != in[at]) {
            if (!patched.count(at)) {
                return false;
            }
            ++changed;
        }
    }
    return true;
}

void TestApplying() {
    std::printf("Patch applying\n");
    blob::Pool pool(0);
    std::vector<std::vector<uint8_t>> rows = {
        IconRow(1000, "ICON_A", pool), IconRow(1001, "ICON_B", pool), IconRow(1002, "ICON_C", pool)};
    const std::vector<uint8_t> bytes = BuildBlob(rows, pool);
    blob::Table table;
    std::string error;
    CHECK(blob::Parse(bytes.data(), bytes.size(), table, error) && table.rows.size() == 3);

    const RowPatch first = MakePatch(ts::kIcon, 1000, "", {{"drawInfo.0.x", "12.5"}, {"comment", "patched note"}});
    const RowPatch byName = MakePatch(ts::kIcon, 0, "ICON_C", {{"drawInfo.6.offsetY", "-3"}});
    const RowPatch missing = MakePatch(ts::kIcon, 4242, "", {{"drawInfo.0.x", "1"}});
    const RowPatch missingName = MakePatch(ts::kIcon, 0, "ICON_GONE", {{"drawInfo.0.x", "1"}});
    std::vector<uint8_t> out;
    mod_rules::PatchResult result;
    CHECK(mod_rules::ApplyPatches(table, {&first, &byName, &missing, &missingName}, out, result));
    CHECK(result.fields == 3 && result.rows.size() == 2 && result.missing.size() == 2);
    CHECK(result.missing.size() == 2 && result.missing[0] == &missing && result.missing[1] == &missingName);
    CHECK(result.rows.size() == 2 && result.rows[0].id == 1000 && result.rows[0].row == 0 &&
          result.rows[1].id == 1002 && result.rows[1].row == 2 && result.rows[1].applied.size() == 1);
    size_t changed = 0;
    CHECK(SameOutside(bytes, table, out, result, changed) && changed > 0);
    blob::Table patched;
    if (CHECK(blob::Parse(out.data(), out.size(), patched, error))) {
        CHECK(patched.poolBytes == table.poolBytes + std::strlen("patched note") + 2);
        std::string text;
        CHECK(blob::ReadText(patched, patched.rows[0], 16, text) && text == "patched note");
        CHECK(blob::ReadText(patched, patched.rows[0], 8, text) && text == "ICON_A");
        CHECK(blob::Get<float>(patched.rows[0].data, 32) == 12.5f);
        CHECK(blob::Get<float>(patched.rows[2].data, 32 + 6 * 32 + 28) == -3.0f);
        CHECK(blob::Get<float>(patched.rows[1].data, 32) == 32.0f);
    }

    // Numbers only: the pool and the size stay as they were.
    const RowPatch numbers = MakePatch(ts::kIcon, 1001, "", {{"textureID", "77"}, {"drawInfo.3.w", "8"}});
    CHECK(mod_rules::ApplyPatches(table, {&numbers}, out, result));
    CHECK(out.size() == bytes.size() && result.fields == 2);
    CHECK(SameOutside(bytes, table, out, result, changed) && changed > 0);
    CHECK(blob::Get<uint64_t>(out.data(), blob::kHeaderSize + 2 * 4 + rows[0].size() + 24) == 77);

    // Nothing to write: false, out empty.
    CHECK(!mod_rules::ApplyPatches(table, {&missing}, out, result) && out.empty() && result.missing.size() == 1);

    // Two patches of one place: the later folder writes it, once.
    const RowPatch early = MakePatch(ts::kIcon, 1000, "", {{"drawInfo.0.y", "1"}, {"drawInfo.0.h", "4"}}, 0);
    const RowPatch late = MakePatch(ts::kIcon, 0, "ICON_A", {{"drawInfo.0.y", "2"}}, 1);
    CHECK(mod_rules::ApplyPatches(table, {&late, &early}, out, result));
    CHECK(result.fields == 2 && result.rows.size() == 2);
    CHECK(result.rows.size() == 2 && result.rows[0].applied.size() == 1 && result.rows[1].applied.size() == 1 &&
          result.rows[1].applied[0]->path == "drawInfo.0.h");
    CHECK(blob::Get<float>(out.data(), blob::kHeaderSize + 4 + 32 + 4) == 2.0f);

    // A vector element only within the row's own count.
    blob::Pool skinPool(0);
    std::vector<uint8_t> skin = ts::NewRow(ts::kAnimeReplaceTexture, 2, skinPool);
    blob::Put<uint64_t>(skin.data(), 0, 7);
    std::vector<uint8_t> small = ts::NewRow(ts::kAnimeReplaceTexture, 0, skinPool);
    blob::Put<uint64_t>(small.data(), 0, 8);
    const std::vector<uint8_t> skinBytes = BuildBlob({small, skin}, skinPool);
    blob::Table skins;
    CHECK(blob::Parse(skinBytes.data(), skinBytes.size(), skins, error));
    const RowPatch swap = MakePatch(
        ts::kAnimeReplaceTexture, 7, "", {{"entries.1.texture", "55"}, {"entries.1.name", "body"}, {"entries.3.name", "x"}}
    );
    const RowPatch none = MakePatch(ts::kAnimeReplaceTexture, 8, "", {{"entries.0.texture", "1"}});
    CHECK(mod_rules::ApplyPatches(skins, {&swap, &none}, out, result));
    CHECK(result.fields == 2 && result.rows.size() == 2);
    if (result.rows.size() == 2) {
        CHECK(result.rows[0].failed.size() == 1 && result.rows[0].failed[0].first->path == "entries.3.name");
        CHECK(result.rows[1].applied.empty() && result.rows[1].failed.size() == 1);
    }
    CHECK(SameOutside(skinBytes, skins, out, result, changed) && changed > 0);
    blob::Table swapped;
    if (CHECK(blob::Parse(out.data(), out.size(), swapped, error) && swapped.rows.size() == 2)) {
        std::string text;
        CHECK(blob::ReadText(swapped, swapped.rows[1], 28 + 12, text) && text == "body");
        CHECK(blob::Get<uint32_t>(swapped.rows[1].data, 28 + 12 + 8) == 55);
    }
}

// ---- The real game, read only ----------------------------------------------

bool OpenGame(mod_rules::VanillaData& vanilla) {
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return false;
    }
    std::string error;
    if (!CHECK(vanilla.Open(folder.wstring(), error))) {
        std::printf("  %s\n", error.c_str());
        return false;
    }
    return true;
}

// The table's blob bytes as parsed: the header, rows and pool it declares.
std::vector<uint8_t> TableBytes(const blob::Table& table) {
    const uint8_t* header = table.rowsStart - blob::kHeaderSize;
    return std::vector<uint8_t>(header, header + blob::kHeaderSize + table.rowsSize + table.poolBytes);
}

void TestRealTables() {
    std::printf("Patches on the game's tables\n");
    mod_rules::VanillaData vanilla;
    if (!OpenGame(vanilla)) {
        return;
    }
    const blob::Table* icons = vanilla.Table("icon.dat");
    const blob::Table* textures = vanilla.Table("texture.dat");
    if (!CHECK(icons && textures && icons->rows.size() > 2 && textures->rows.size() > 2)) {
        return;
    }
    for (const auto& [table, patches] :
         std::vector<std::pair<const blob::Table*, std::vector<RowPatch>>>{
             {icons,
              {MakePatch(ts::kIcon, blob::RowId(icons->rows.front()), "",
                         {{"drawInfo.0.x", "12.5"}, {"drawInfo.6.h", "7"}, {"comment", "Canopy patch check"}}),
               MakePatch(ts::kIcon, blob::RowId(icons->rows.back()), "", {{"drawInfo.1.offsetX", "-2"}}),
               MakePatch(ts::kIcon, 0xFFFFFFFFFFFFull, "", {{"drawInfo.0.x", "1"}})}},
             {textures,
              {MakePatch(ts::kTexture, blob::RowId(textures->rows[1]), "", {{"width", "64"}, {"isNoCompress", "1"}}),
               MakePatch(ts::kTexture, blob::RowId(textures->rows.back()), "", {{"comment", "Canopy patch check"}})}}}) {
        std::vector<const RowPatch*> pointers;
        for (const RowPatch& patch : patches) {
            pointers.push_back(&patch);
        }
        const std::vector<uint8_t> before = TableBytes(*table);
        std::vector<uint8_t> out;
        mod_rules::PatchResult result;
        CHECK(mod_rules::ApplyPatches(*table, pointers, out, result));
        size_t changed = 0;
        CHECK(SameOutside(before, *table, out, result, changed));
        CHECK(result.missing.size() == (table == icons ? 1u : 0u));
        size_t failed = 0;
        for (const mod_rules::PatchedRow& row : result.rows) {
            failed += row.failed.size();
        }
        CHECK(failed == 0);
        blob::Table patched;
        std::string error;
        std::string text;
        if (CHECK(blob::Parse(out.data(), out.size(), patched, error))) {
            const blob::Row& row = table == icons ? patched.rows.front() : patched.rows.back();
            CHECK(blob::ReadText(patched, row, 16, text) && text == "Canopy patch check");
        }
        std::printf("  %s: %zu rows, %zu fields on %zu rows, %zu bytes changed, pool %u to %u bytes\n",
                    table == icons ? "icon.dat" : "texture.dat", table->rows.size(), result.fields,
                    result.rows.size(), changed, table->poolBytes, patched.poolBytes);
    }
}

// ---- The desktop check -----------------------------------------------------

void TestCheck() {
    std::printf("Patch check\n");
    // Without the game: the sections are read and merged, rows not looked up.
    TempMods bare("check_bare");
    bare.Write("A/canopy.ini", Lines({"[patch.icon.123456789]", "drawInfo.0.x = 1", "nosuch = 1"}));
    mod_rules::Context offline;
    offline.modsDir = bare.Dir();
    offline.folders = mod_rules::ScanMods(bare.Dir());
    mod_rules::CheckAll(offline);
    CHECK(offline.report.Count(Severity::Error) == 1 && Said(offline.report, Severity::Error, 3, "nosuch skipped"));

    mod_rules::VanillaData vanilla;
    if (!OpenGame(vanilla)) {
        return;
    }
    const blob::Table* icons = vanilla.Table("icon.dat");
    const blob::Table* framework = vanilla.Table("framework_icon.dat");
    const blob::Table* skins = vanilla.Table("animereplacetexture.dat");
    if (!CHECK(icons && skins && !icons->rows.empty() && !skins->rows.empty())) {
        return;
    }
    const uint64_t icon = blob::RowId(icons->rows.front());
    // A row of the framework twin, or the same row again without one.
    const uint64_t twin = framework && !framework->rows.empty() ? blob::RowId(framework->rows.front()) : icon;
    uint64_t gone = 4000000000ull;
    while (vanilla.HasRow("icon.dat", gone) || vanilla.HasRow("framework_icon.dat", gone)) {
        ++gone;
    }
    const blob::Row& skinRow = skins->rows.front();
    const uint32_t count = ts::VectorCount(ts::kAnimeReplaceTexture, skinRow.data, skinRow.size);
    std::printf("  icon rows %zu, framework icon rows %zu, first skin row %u entries\n", icons->rows.size(),
                framework ? framework->rows.size() : size_t(0), count);

    TempMods mods("check");
    mods.Write(
        "Real/canopy.ini",
        Lines({
            "[patch.icon." + std::to_string(icon) + "]",                    // 1
            "drawInfo.0.x = 1",                                             // 2
            "[patch.icon." + std::to_string(gone) + "]",                    // 3
            "drawInfo.0.x = 1",                                             // 4
            "[patch.animereplacetexture." + std::to_string(blob::RowId(skinRow)) + "]",  // 5
            "entries." + std::to_string(count) + ".texture = 1",            // 6
            "comment = fine",                                               // 7
        })
    );
    mods.Write("Twin/canopy.ini", Lines({"[patch.icon." + std::to_string(twin) + "]", "drawInfo.0.y = 2"}));
    mod_rules::Context context;
    context.modsDir = mods.Dir();
    context.folders = mod_rules::ScanMods(mods.Dir());
    context.vanilla = &vanilla;
    mod_rules::CheckAll(context);
    CHECK(SaidExactly(context.report, Severity::Error, "Real", 3,
                      "Patch icon.dat " + std::to_string(gone) + " skipped, the game's table has no such row."));
    CHECK(Said(context.report, Severity::Error, 6,
               "[Real] Patch animereplacetexture.dat " + std::to_string(blob::RowId(skinRow)) + ", entries." +
                   std::to_string(count) + ".texture skipped, entries has no element " + std::to_string(count)));
    CHECK(context.report.Count(Severity::Error) == 2);
    CHECK(context.report.Count(Severity::Warning) == 0);

    // By enum name, looked up in the tables.
    std::string enumName;
    CHECK(blob::ReadText(*icons, icons->rows.front(), 8, enumName) && !enumName.empty());
    TempMods named("check_named");
    named.Write("A/canopy.ini", Lines({"[patch.icon." + enumName + "]", "drawInfo.0.x = 1"}));
    named.Write("B/canopy.ini", Lines({"[patch.icon." + std::to_string(icon) + "]", "drawInfo.0.x = 2"}));
    named.Write("C/canopy.ini", Lines({"[patch.icon.NO_SUCH_ICON_NAME_AT_ALL]", "drawInfo.0.x = 2"}));
    mod_rules::Context byName;
    byName.modsDir = named.Dir();
    byName.folders = mod_rules::ScanMods(named.Dir());
    byName.vanilla = &vanilla;
    mod_rules::CheckAll(byName);
    CHECK(byName.report.Count(Severity::Warning) == 1 &&
          Said(byName.report, Severity::Warning, 2,
               "Patch conflict: A and B both set icon.dat " + std::to_string(icon) + " drawInfo.0.x."));
    CHECK(byName.report.Count(Severity::Error) == 1 &&
          SaidExactly(byName.report, Severity::Error, "C", 1,
                      "Patch icon.dat NO_SUCH_ICON_NAME_AT_ALL skipped, the game's table has no such row."));
}

// ---- Tables with no enum name, and the loader's merge ------------------------

void TestLimits() {
    std::printf("Patch limits\n");
    // A text read at an offset that would wrap is refused, not read.
    blob::Pool pool(0);
    const std::vector<uint8_t> bytes = BuildBlob({IconRow(7, "SEVEN", pool)}, pool);
    blob::Table table;
    std::string error;
    std::string text;
    if (CHECK(blob::Parse(bytes.data(), bytes.size(), table, error))) {
        const blob::Row& row = table.rows.front();
        CHECK(!blob::ReadText(table, row, static_cast<size_t>(-1), text) && text.empty());
        CHECK(!blob::ReadText(table, row, row.size - 4, text));
        CHECK(blob::ReadText(table, row, 8, text) && text == "SEVEN");
    }

    // blacksmith.dat has no enum name: a row is named by id only.
    TempMods bare("limits_bare");
    bare.Write("A/canopy.ini", Lines({"[patch.blacksmith.SOME_FORGE]", "createTime = 1"}));
    mod_rules::Context offline;
    offline.modsDir = bare.Dir();
    offline.folders = mod_rules::ScanMods(bare.Dir());
    mod_rules::CheckAll(offline);
    CHECK(offline.report.Count(Severity::Error) == 1 &&
          SaidExactly(offline.report, Severity::Error, "A", 1,
                      "[A] Patch blacksmith.SOME_FORGE skipped, blacksmith.dat rows have no enum name, so name the "
                      "row by its id."));

    mod_rules::VanillaData vanilla;
    if (!OpenGame(vanilla)) {
        return;
    }
    const blob::Table* smiths = vanilla.Table("blacksmith.dat");
    const blob::Table* icons = vanilla.Table("icon.dat");
    if (!CHECK(smiths && icons && !smiths->rows.empty() && !icons->rows.empty())) {
        return;
    }
    uint64_t found = 0;
    CHECK(!mod_rules::VanillaEnumLookup(vanilla)(*ts::FindPatchable("blacksmith"), "ANY_NAME", found) && found == 0);

    // A blacksmith row by id is checked and applied, its names never read.
    const uint64_t smith = blob::RowId(smiths->rows.front());
    TempMods real("limits_real");
    real.Write("A/canopy.ini", Lines({"[patch.blacksmith." + std::to_string(smith) + "]", "createTime = 5"}));
    mod_rules::Context context;
    context.modsDir = real.Dir();
    context.folders = mod_rules::ScanMods(real.Dir());
    context.vanilla = &vanilla;
    mod_rules::CheckAll(context);
    CHECK(context.report.Count(Severity::Error) == 0 && context.report.Count(Severity::Warning) == 0);
    const RowPatch patch = MakePatch(*ts::FindPatchable("blacksmith"), smith, "", {{"createTime", "5"}});
    std::vector<uint8_t> out;
    mod_rules::PatchResult result;
    CHECK(mod_rules::ApplyPatches(*smiths, {&patch}, out, result));
    CHECK(result.rows.size() == 1 && result.missing.empty() && result.fields == 1);

    // The loader's merge: one mod names an icon by enum name, the next by id.
    // With the archives' lookup they are one row and one conflict.
    std::string enumName;
    CHECK(blob::ReadText(*icons, icons->rows.front(), 8, enumName) && !enumName.empty());
    const uint64_t icon = blob::RowId(icons->rows.front());
    TempMods named("limits_named");
    named.Write("A/canopy.ini", Lines({"[patch.icon." + enumName + "]", "drawInfo.0.x = 1"}));
    named.Write("B/canopy.ini", Lines({"[patch.icon." + std::to_string(icon) + "]", "drawInfo.0.x = 2"}));
    mod_rules::KeyRefs refs;
    mod_rules::Report report;
    const std::vector<PatchSection> sections = mod_rules::ReadPatches(mod_rules::ScanMods(named.Dir()), refs, report);
    const std::vector<RowPatch> rows = mod_rules::MergePatches(sections, mod_rules::VanillaEnumLookup(vanilla), report);
    if (CHECK(rows.size() == 1)) {
        const PatchField* field = FieldOf(rows[0], "drawInfo.0.x");
        CHECK(rows[0].id == icon && rows[0].enumName.empty());
        CHECK(field && field->value == "2" && field->folder == "B");
    }
    CHECK(report.Count(Severity::Warning) == 1 &&
          Said(report, Severity::Warning, 2,
               "Patch conflict: A and B both set icon.dat " + std::to_string(icon) + " drawInfo.0.x."));
    // Without it, as a loader that cannot read the archives: two rows.
    mod_rules::Report apart;
    CHECK(mod_rules::MergePatches(sections, mod_rules::PatchEnumLookup(), apart).size() == 2 &&
          apart.Count(Severity::Warning) == 0);
}

}  // namespace

void TestPatches(int& checks, int& failures) {
    TestReading();
    TestMerging();
    TestApplying();
    TestRealTables();
    TestCheck();
    TestLimits();
    checks += g_checks;
    failures += g_failures;
}
