#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "canopy_ini.h"
#include "mod_rules.h"
#include "nltx.h"
#include "rules_art.h"
#include "rules_items.h"
#include "rules_objects.h"
#include "rules_report.h"
#include "rules_rows.h"
#include "rules_whistles.h"
#include "test_support.h"

// Checks of the canopy.ini rules the loader and the desktop tools share:
// the ini reader's lines, the value helpers, references, the whistle,
// art, item and object readers against mod folders written to a temporary folder, the
// item and object rows the loader appends, and the JSON the tools hand out.
namespace {

namespace fs = std::filesystem;
using mod_rules::Diagnostic;
using mod_rules::RefKind;
using mod_rules::Severity;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED rules_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

// A mods folder of its own under %TEMP%, removed when the test ends.
class TempMods {
public:
    TempMods() {
        root_ = fs::temp_directory_path() / ("canopy_rules_tests_" + std::to_string(test_support::ProcessId()));
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

// The diagnostic whose message contains text, or null.
const Diagnostic* Said(const mod_rules::Report& report, const std::string& text) {
    for (const Diagnostic& diagnostic : report.Items()) {
        if (diagnostic.message.find(text) != std::string::npos) {
            return &diagnostic;
        }
    }
    return nullptr;
}

void TestIniLines() {
    std::printf("canopy.ini lines\n");
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(
        "\xEF\xBB\xBF; comment\n"
        "[mod]\n"
        "Key = Test\n"
        "\n"
        "[whistle.A]\r\n"
        "  notes = S L  \r\n"
        "# another comment\n"
        "learn=5\n"
    );
    CHECK(sections.size() == 2);
    if (sections.size() != 2) {
        return;
    }
    CHECK(sections[0].name == "mod" && sections[0].line == 2);
    CHECK(canopy_ini::Value(&sections[0], "key", "") == "Test");
    CHECK(canopy_ini::ValueLine(&sections[0], "key") == 3);
    CHECK(sections[1].name == "whistle.A" && sections[1].line == 5);
    CHECK(canopy_ini::Value(&sections[1], "notes", "") == "S L");
    CHECK(canopy_ini::ValueLine(&sections[1], "notes") == 6);
    CHECK(canopy_ini::ValueLine(&sections[1], "learn") == 8);
    // A missing key points at its section, a null section at nothing.
    CHECK(canopy_ini::ValueLine(&sections[1], "base") == 5);
    CHECK(canopy_ini::ValueLine(nullptr, "base") == 0);
    CHECK(sections[1].values.size() == sections[1].valueLines.size());
}

void TestValues() {
    std::printf("Rule values\n");
    CHECK(mod_rules::KeyValid("Ab_9-z"));
    CHECK(!mod_rules::KeyValid(""));
    CHECK(!mod_rules::KeyValid("a.b"));
    CHECK(!mod_rules::KeyValid(std::string(33, 'a')));
    CHECK(mod_rules::KeyValid(std::string(32, 'a')));
    int32_t number = 0;
    CHECK(mod_rules::ParseI32("-2147483647", number) && number == -2147483647);
    CHECK(!mod_rules::ParseI32("2147483648", number));
    CHECK(!mod_rules::ParseI32("1.5", number));
    uint64_t wide = 0;
    CHECK(!mod_rules::ParseU64("", wide));
    CHECK(!mod_rules::ParseU64("12345678901234567890", wide));
    CHECK(mod_rules::FullKey("Mod", "Key") == "Mod.Key");
    CHECK(mod_rules::FullKey("Mod", "Other.Key") == "Other.Key");
    const std::vector<std::string> parts = mod_rules::SplitList(" a, b ,,c ");
    CHECK(parts.size() == 4 && parts[0] == "a" && parts[1] == "b" && parts[2].empty() && parts[3] == "c");
    CHECK(mod_rules::ScriptPathValid("scripts/a.lua"));
    CHECK(!mod_rules::ScriptPathValid("../a.lua"));
    CHECK(!mod_rules::ScriptPathValid("C:/a.lua"));
    CHECK(!mod_rules::ScriptPathValid("a.txt"));
    CHECK(mod_rules::ModulePart("My-Mod") == "my_mod");
    CHECK(mod_rules::EnumPart("my-mod.1") == "MY_MOD_1");
}

void TestRefs() {
    std::printf("Rule references\n");
    mod_rules::KeyRefs refs;
    CHECK(refs.Add(RefKind::Flag, "Mod.Seen") == 1);
    CHECK(refs.Add(RefKind::Flag, "mod.seen") == 1);
    CHECK(refs.Add(RefKind::Flag, "Mod.Other") == 2);
    CHECK(refs.Add(RefKind::Item, "Mod.Seen") == 1);
    CHECK(refs.Find(RefKind::Flag, "MOD.SEEN") == 1);
    CHECK(refs.Find(RefKind::Skin, "Mod.Seen") == 0);
    // Flags: own key, Mod.Key, or a vanilla id outside the mod band.
    CHECK(mod_rules::FlagRef(refs, "Mod", "Seen") == 1);
    CHECK(mod_rules::FlagRef(refs, "Else", "Mod.Other") == 2);
    CHECK(mod_rules::FlagRef(refs, "Mod", "Missing") == 0);
    CHECK(mod_rules::FlagRef(refs, "Mod", "5") == 5);
    CHECK(mod_rules::FlagRef(refs, "Mod", "0") == 0);
    CHECK(mod_rules::FlagRef(refs, "Mod", "95000") == 0);
    CHECK(mod_rules::FlagRef(refs, "Mod", "99998") == 0);
    CHECK(mod_rules::FlagRef(refs, "Mod", "94999") == 94999);
    CHECK(mod_rules::FlagRef(refs, "Mod", "99999") == 0);
    // Items: vanilla ids stop below the mod item range.
    CHECK(mod_rules::ItemRef(refs, "Mod", "1499999999") == 1499999999);
    CHECK(mod_rules::ItemRef(refs, "Mod", "1500000000") == 0);
    CHECK(mod_rules::ItemRef(refs, "Mod", "Seen") == 1);
    CHECK(mod_rules::KeyRef(refs, RefKind::Item, "Mod", "") == 0);
}

void TestMelodies() {
    std::printf("Whistle melodies\n");
    uint64_t notes = 0;
    int32_t count = 0;
    CHECK(mod_rules::ParseMelody("S S L L", notes, count) && count == 4 && notes == 0x3);
    CHECK(mod_rules::MelodyText(notes, count) == "SSLL");
    CHECK(mod_rules::ParseMelody("1010", notes, count) && mod_rules::MelodyText(notes, count) == "LSLS");
    CHECK(!mod_rules::ParseMelody("S", notes, count));
    CHECK(!mod_rules::ParseMelody("SSSSSSSSSSS", notes, count));
    CHECK(mod_rules::ParseMelody("SSSSSSSSSS", notes, count) && count == 10);
    CHECK(!mod_rules::ParseMelody("SXL", notes, count));
    const mod_rules::Melody low{2, 3};
    const mod_rules::Melody high{3, 0};
    CHECK(low < high && !(high < low));
}

void TestWhistleReader() {
    std::printf("Whistle reader\n");
    TempMods mods;
    mods.Write(
        "Alpha/canopy.ini",
        "[mod]\n"
        "name = Alpha Mod\n"
        "\n"
        "[whistle.Rise]\n"
        "name = Rise\n"
        "notes = SSLL\n"
        "learn = Seen\n"
        "score = 11\n"
        "script = whistles/rise.lua\n"
        "entry = 9bad\n"
        "\n"
        "[whistle.Copy]\n"
        "name = Copy\n"
        "notes = S S L L\n"
        "learn = 7\n"
        "\n"
        "[whistle.NoLearn]\n"
        "name = NoLearn\n"
        "notes = LLS\n"
        "learn = Nope\n"
        "\n"
        "[whistle.rise]\n"
        "name = Again\n"
        "notes = LSL\n"
        "learn = 7\n"
    );
    mods.Write("Alpha/whistles/rise.lua", "function main() end\n");
    mods.Write(
        "Beta/canopy.ini",
        "[mod]\n"
        "key = Alpha\n"
        "\n"
        "[whistle.Other]\n"
        "name = Other\n"
        "notes = LLLSSS\n"
        "learn = 7\n"
    );
    mods.Write("Gamma/canopy.ini", "[mod]\nkey = bad key\n[whistle.X]\nname = X\nnotes = SL\nlearn = 7\n");
    mods.Write("Plain/readme.txt", "no ini here\n");

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    CHECK(folders.size() == 4);
    CHECK(mod_rules::WithSections(folders, {"whistle."}).size() == 3);

    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Flag, "Alpha.Seen");
    mod_rules::Report report;
    std::vector<std::string> accepted;
    const auto accept = [&](const mod_rules::ModFolder& folder, const std::string& modKey, size_t count) {
        accepted.push_back(folder.name + ":" + modKey + ":" + std::to_string(count));
        return true;
    };
    const std::vector<mod_rules::WhistleSpec> whistles =
        mod_rules::ReadWhistles(folders, mods.Dir(), refs, report, accept);

    CHECK(whistles.size() == 1);
    CHECK(accepted.size() == 1 && accepted[0] == "Alpha:Alpha:1");
    if (!whistles.empty()) {
        const mod_rules::WhistleSpec& rise = whistles[0];
        CHECK(rise.fullKey == "Alpha.Rise");
        CHECK(rise.line == 4);
        CHECK(rise.learn == 1);
        CHECK(rise.score == 0);
        CHECK(rise.entry == "main");
        CHECK(rise.module == "whistle_alpha_rise");
        CHECK(rise.chunkName == "=Alpha/whistles/rise.lua");
        CHECK(rise.source == "function main() end\n");
        CHECK(rise.names[0] == "Rise" && rise.names[6] == "Rise");
    }
    const Diagnostic* score = Said(report, "Whistle Rise keeps its base whistle's score, score must be 1 to 10.");
    CHECK(score && score->severity == Severity::Warning && score->folder == "Alpha" && score->line == 8);
    const Diagnostic* entry = Said(report, "Whistle Rise runs entry main");
    CHECK(entry && entry->severity == Severity::Warning && entry->line == 10);
    const Diagnostic* copy = Said(report, "[Alpha] Whistle Copy skipped, its melody SSLL is already Alpha.Rise's.");
    CHECK(copy && copy->severity == Severity::Error && copy->line == 14);
    const Diagnostic* learn = Said(
        report,
        "[Alpha] Whistle NoLearn skipped, learn must name a flag of an installed mod or a vanilla flag id, and "
        "Nope is neither."
    );
    CHECK(learn && learn->line == 20);
    const Diagnostic* again = Said(report, "[Alpha] Whistle rise skipped, the mod already has a whistle with that key.");
    CHECK(again && again->line == 22);
    const Diagnostic* owner =
        Said(report, "Whistles in folder Beta skipped, another folder already has whistles under the key Alpha.");
    CHECK(owner && owner->folder == "Beta" && owner->line == 2);
    const Diagnostic* key = Said(report, "Whistles in folder Gamma skipped, its key bad key must be");
    CHECK(key && key->line == 2);
    CHECK(report.Count(Severity::Error) == 5);
    CHECK(report.Count(Severity::Warning) == 2);

    // Against a whistle table: a missing base, then a vanilla melody.
    std::set<mod_rules::Melody> melodies = {{4, 0x3}};
    mod_rules::Report table;
    CHECK(!whistles.empty() && !mod_rules::CheckAgainstTable(whistles[0], nullptr, melodies, table));
    CHECK(Said(table, "Mod whistle Alpha.Rise skipped, its base 2 is not in the whistle table."));
    std::vector<uint8_t> row(mod_rules::kWhistleRowSize, 0);
    const blob::Row base{row.data(), static_cast<uint32_t>(row.size())};
    CHECK(!whistles.empty() && !mod_rules::CheckAgainstTable(whistles[0], &base, melodies, table));
    CHECK(Said(table, "Mod whistle Alpha.Rise skipped, its melody SSLL is already a vanilla whistle's."));
    melodies.clear();
    CHECK(!whistles.empty() && mod_rules::CheckAgainstTable(whistles[0], &base, melodies, table));
    CHECK(melodies.size() == 1);
}

// What a built row asks of the tables: vanilla texture 899 is missing.
class StubTables : public mod_rules::ArtTables {
public:
    bool EnumId(const char* file, const std::string& name, uint64_t&, std::string& why) const override {
        why = std::string(file) + " has no " + name;
        return false;
    }
    bool RefFine(const table_schema::TableSchema&, const char* file, uint64_t id, bool titleChecks, std::string& why)
        const override {
        if (titleChecks || id != 899) {
            return true;
        }
        why = std::string("it names texture 899, which is not in ") + file;
        return false;
    }
};

void TestArtReader() {
    std::printf("Art reader\n");
    TempMods mods;
    mods.Write(
        "Art/canopy.ini",
        "[mod]\n"
        "key = Art\n"
        "\n"
        "[texture.Tex]\n"
        "file = art/tex.nltx\n"
        "\n"
        "[texture.Missing]\n"
        "file = art/none.png\n"
        "\n"
        "[icon.Whole]\n"
        "texture = Tex\n"
        "\n"
        "[icon.Past]\n"
        "texture = Tex\n"
        "rect = 0, 0, 64, 8\n"
        "\n"
        "[icon.Ghost]\n"
        "texture = Ghost\n"
        "\n"
        "[icon.Vanilla]\n"
        "texture = 899\n"
        "rect = 0, 0, 8, 8\n"
        "\n"
        "[icon.NoRect]\n"
        "texture = TEX_ID_SOMETHING\n"
        "\n"
        "[skin.Twice]\n"
        "replace = a: Tex, A: Tex\n"
        "\n"
        "[skin.Swap]\n"
        "replace = body: 899\n"
        "\n"
        "[row.gimmick.X]\n"
        "x = 1\n"
        "\n"
        "[row.icon.Raw]\n"
        "textureID = Art.Tex\n"
        "drawInfo.w = 4\n"
    );
    std::vector<uint8_t> texture;
    std::string error;
    CHECK(nltx::Encode(16, 8, std::vector<uint8_t>(16 * 8 * 4, 0xFF), texture, error));
    mods.Write("Art/art/tex.nltx", std::string(texture.begin(), texture.end()));
    mods.Write("Beta/canopy.ini", "[mod]\nkey = Art\n[texture.Other]\nfile = other.png\n");

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    mod_rules::Report report;
    std::vector<std::string> accepted;
    mod_rules::ArtHost host;
    host.accept = [&](const mod_rules::ModFolder& folder, const std::string& modKey,
                      const std::vector<mod_rules::ArtSpec>& rows) {
        std::vector<const mod_rules::ArtSpec*> counted;
        for (const mod_rules::ArtSpec& row : rows) {
            counted.push_back(&row);
        }
        accepted.push_back(folder.name + ":" + modKey + ":" + mod_rules::CountArt(counted));
        return true;
    };
    std::vector<mod_rules::ArtSpec> rows = mod_rules::ReadArt(folders, mods.Dir(), report, host);
    CHECK(accepted.size() == 1 && accepted[0] == "Art:Art:1 texture, 6 icons and 1 skin");
    CHECK(rows.size() == 8);
    std::vector<mod_rules::ArtSpec*> pointers;
    for (mod_rules::ArtSpec& row : rows) {
        pointers.push_back(&row);
    }
    mod_rules::ResolveArt(pointers, mod_rules::ArtNumbers(), report);

    const mod_rules::ArtSpec* tex = mod_rules::FindArt(pointers, table_schema::kTexture, "art", "TEX");
    CHECK(tex && tex->id == 1900000001 && tex->width == 16 && tex->height == 8);
    CHECK(tex && tex->gamePath == "data/texture/canopy/art/tex.nltx");
    const mod_rules::ArtSpec* whole = mod_rules::FindArt(pointers, table_schema::kIcon, "Art", "Whole");
    CHECK(whole && whole->id == 1950000001 && whole->texture.row == tex);
    CHECK(whole && whole->rect.size() == 4 && whole->rect[2] == "16" && whole->rect[3] == "8");
    CHECK(whole && whole->size.size() == 2 && whole->size[0] == "16" && whole->offset[0] == "0");
    const mod_rules::ArtSpec* vanilla = mod_rules::FindArt(pointers, table_schema::kIcon, "Art", "Vanilla");
    CHECK(vanilla && vanilla->id == 1950000002 && vanilla->texture.id == 899 && !vanilla->texture.row);
    const mod_rules::ArtSpec* raw = mod_rules::FindArt(pointers, table_schema::kIcon, "Art", "Raw");
    CHECK(raw && raw->id == 1950000003 && raw->fields.size() == 2 && raw->fields[0].ref.row == tex);
    CHECK(!mod_rules::FindArt(pointers, table_schema::kIcon, "Art", "Past"));

    const Diagnostic* missing = Said(report, "[Art] Texture Missing skipped, art/none.png could not be found.");
    CHECK(missing && missing->severity == Severity::Error && missing->folder == "Art" && missing->line == 8);
    const Diagnostic* past = Said(report, "[Art] Icon Past skipped, its rect runs past the 16x8 texture Art.Tex.");
    CHECK(past && past->line == 15);
    const Diagnostic* ghost = Said(
        report,
        "[Art] Icon Ghost skipped, its texture Ghost is not a texture of this mod, a Mod.Key, a vanilla id or an "
        "enum name."
    );
    CHECK(ghost && ghost->line == 18);
    const Diagnostic* noRect =
        Said(report, "[Art] Icon NoRect skipped, it needs rect = x, y, w, h for a vanilla texture.");
    CHECK(noRect && noRect->line == 24);
    const Diagnostic* twice = Said(report, "[Art] Skin Twice skipped, it replaces A twice.");
    CHECK(twice && twice->line == 28);
    // A row of a generated table is rules_rows', which refuses gimmick.
    CHECK(!Said(report, "[Art] Row gimmick.X skipped"));
    const Diagnostic* owner =
        Said(report, "Art in folder Beta skipped, another folder already has art under the key Art.");
    CHECK(owner && owner->folder == "Beta" && owner->line == 2);
    CHECK(report.Count(Severity::Error) == 6);
    CHECK(report.Count(Severity::Warning) == 0);
    mod_rules::Report rowReport;
    CHECK(mod_rules::ReadRows(folders, rowReport, mod_rules::AcceptAll).empty());
    const Diagnostic* table = Said(
        rowReport,
        "[Art] Row gimmick.X skipped, the game's save keeps gimmick ids, so a save made with a mod row would break "
        "once the mod is gone. Use [object.<Key>] sections instead (docs/OBJECTS.md)."
    );
    CHECK(table && table->line == 33 && rowReport.Items().size() == 1);

    // Built as the table load builds them, with no vanilla rows.
    const StubTables tables;
    const blob::Table empty;
    const std::unordered_map<uint64_t, size_t> byId;
    blob::Pool pool(0);
    std::vector<uint8_t> row;
    std::string why;
    CHECK(whole && mod_rules::BuildArtRow(*whole, empty, byId, tables, pool, row, why));
    CHECK(row.size() >= 32 && blob::Get<uint64_t>(row.data(), 24) == 1900000001);
    CHECK(mod_rules::CheckArtRefs(table_schema::kIcon, row, tables, why));
    const mod_rules::ArtSpec* swap =
        mod_rules::FindArt(pointers, table_schema::kAnimeReplaceTexture, "Art", "Swap");
    CHECK(swap && mod_rules::BuildArtRow(*swap, empty, byId, tables, pool, row, why));
    CHECK(!mod_rules::CheckArtRefs(table_schema::kAnimeReplaceTexture, row, tables, why));
    CHECK(why == "entries 0: it names texture 899, which is not in texture.dat");
}

void TestItemReader() {
    std::printf("Item reader\n");
    TempMods mods;
    mods.Write(
        "Alpha/canopy.ini",
        "[mod]\n"
        "key = Alpha\n"
        "\n"
        "[item.Stew]\n"
        "name = Stew\n"
        "use = Consume\n"
        "stamina = 40\n"
        "hp = 10\n"
        "icon = 77\n"
        "\n"
        "[item.Charm]\n"
        "name = Charm\n"
        "use = script\n"
        "consume = on\n"
        "icon = Alpha.Glow\n"
        "\n"
        "[item.Lamp]\n"
        "name = Lamp\n"
        "use = place\n"
        "object = Lamp\n"
        "category = 9\n"
        "\n"
        "[item.Lamp2]\n"
        "name = Lamp2\n"
        "use = place\n"
        "object = Alpha.Lamp\n"
        "\n"
        "[item.Bad]\n"
        "name = Bad\n"
        "use = throw\n"
        "icon = 77\n"
        "\n"
        "[item.NoIcon]\n"
        "name = NoIcon\n"
        "\n"
        "[item.Hungry]\n"
        "name = Hungry\n"
        "use = consume\n"
        "icon = 77\n"
        "\n"
        "[item.Big]\n"
        "name = Big\n"
        "use = consume\n"
        "stamina = 1000\n"
        "icon = 77\n"
        "\n"
        "[item.Ghost]\n"
        "name = Ghost\n"
        "use = place\n"
        "object = Nope\n"
        "\n"
        "[item.Old]\n"
        "name = Old\n"
        "base = 100\n"
        "stamina = 5\n"
        "price = 3\n"
    );
    mods.Write("Alpha/lua/main.lua", "canopy.log('hi')\n");
    mods.Write("Beta/canopy.ini", "[mod]\nkey = Beta\n[item.Charm]\nname = Charm\nuse = script\nicon = 77\n");

    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Icon, "Alpha.Glow");
    refs.Add(RefKind::Object, "Alpha.Lamp");
    mod_rules::Report report;
    const std::vector<mod_rules::ItemSpec> items =
        mod_rules::ReadItems(folders, mods.Dir(), refs, report, mod_rules::AcceptAll);

    CHECK(items.size() == 4);
    if (items.size() == 4) {
        const mod_rules::ItemSpec& stew = items[0];
        CHECK(stew.fullKey == "Alpha.Stew" && stew.baseId == 0);
        CHECK(stew.use == mod_rules::ItemUse::Consume && stew.useSet);
        CHECK(stew.stamina == 40 && stew.hp == 10);
        CHECK(stew.icon == 77 && !stew.modIcon);
        const mod_rules::ItemSpec& charm = items[1];
        CHECK(charm.use == mod_rules::ItemUse::Script && charm.consume);
        CHECK(charm.icon == 1 && charm.modIcon);
        const mod_rules::ItemSpec& lamp = items[2];
        CHECK(lamp.use == mod_rules::ItemUse::Place && lamp.object == 1);
        CHECK(lamp.objectKey == "Alpha.Lamp" && lamp.category == 9 && lamp.icon == 0);
        const mod_rules::ItemSpec& old = items[3];
        CHECK(old.baseId == 100 && !old.useSet && old.use == mod_rules::ItemUse::None);
        CHECK(old.stamina == 0);
    }
    const auto error = [&](const std::string& text, int line) {
        const Diagnostic* said = Said(report, text);
        return said && said->severity == Severity::Error && said->line == line;
    };
    CHECK(error("[Alpha] Item Lamp2 skipped, another item already places the object Alpha.Lamp.", 23));
    CHECK(error("[Alpha] Item Bad skipped, use must be none, consume, script or place.", 30));
    CHECK(error("[Alpha] Item NoIcon skipped, an item without a base needs icon", 33));
    CHECK(error("[Alpha] Item Hungry skipped, use = consume needs stamina or hp.", 38));
    CHECK(error("[Alpha] Item Big skipped, stamina must be a whole number from 1 to 999.", 44));
    CHECK(error("[Alpha] Item Ghost skipped, no mod object is named Nope.", 50));
    CHECK(error("[Beta] Item Charm skipped, use = script needs the mod's lua\\main.lua", 5));
    const Diagnostic* restore = Said(report, "[Alpha] Item Old: stamina and hp are ignored");
    CHECK(restore && restore->severity == Severity::Warning && restore->line == 55);
    const Diagnostic* price = Said(report, "[Alpha] Item Old: price is ignored");
    CHECK(price && price->line == 56);
    CHECK(report.Count(Severity::Error) == 7);
    CHECK(report.Count(Severity::Warning) == 2);
}

// Records each text a row builder writes, by offset.
struct TextLog {
    std::unordered_map<size_t, std::string> texts;
    mod_rules::TextWriter Writer() {
        return [this](std::vector<uint8_t>&, size_t offset, const std::string& text) {
            texts[offset] = text;
        };
    }
};

template <typename T>
T At(const std::vector<uint8_t>& row, size_t offset) {
    return blob::Get<T>(row.data(), offset);
}

blob::Row AsRow(const std::vector<uint8_t>& row) {
    return blob::Row{row.data(), static_cast<uint32_t>(row.size())};
}

void TestItemRows() {
    std::printf("Item rows\n");
    // The action starts after the 6 mod features: 388 + 6 * 8.
    constexpr size_t kAction = 436;
    mod_rules::ItemSpec stew;
    stew.fullKey = "Alpha.Stew";
    stew.use = mod_rules::ItemUse::Consume;
    stew.useSet = true;
    stew.stamina = 40;
    stew.hp = 10;
    for (std::string& name : stew.names) {
        name = "Stew";
    }
    mod_rules::ItemRowParts parts;
    parts.id = 1500000001;
    parts.enumName = "CANOPY_ALPHA_STEW";
    parts.icon = 77;
    parts.outline = 78;
    TextLog log;
    const std::vector<uint8_t> food = mod_rules::BuildItemRow(stew, parts, log.Writer());
    // 2 one-value effects and 2 empty ones, then the tail.
    CHECK(food.size() == kAction + 48 + 20 + 20 + 12 + 12 + 52);
    CHECK(mod_rules::TailOffset(AsRow(food)) == food.size() - 52);
    CHECK(At<uint64_t>(food, 0) == 1500000001);
    CHECK(log.texts[8] == "CANOPY_ALPHA_STEW" && log.texts[32] == "Stew" && log.texts[88].empty());
    CHECK(At<uint64_t>(food, 312) == 0 && At<uint64_t>(food, 320) == 2 && At<int32_t>(food, 340) == 1);
    CHECK(At<uint32_t>(food, 384) == 6 && At<uint64_t>(food, 388) == 5 && At<uint64_t>(food, 428) == 71);
    CHECK(At<uint64_t>(food, kAction) == 1200);
    size_t effect = kAction + 48;
    CHECK(At<uint64_t>(food, effect) == 2 && At<uint32_t>(food, effect + 8) == 1);
    CHECK(At<int64_t>(food, effect + 12) == 10);
    effect += 20;
    CHECK(At<uint64_t>(food, effect) == 3 && At<int64_t>(food, effect + 12) == 40);
    effect += 20;
    CHECK(At<uint64_t>(food, effect) == 8 && At<uint32_t>(food, effect + 8) == 0);
    effect += 12;
    CHECK(At<uint64_t>(food, effect) == 0 && At<uint32_t>(food, effect + 8) == 0);
    const size_t tail = food.size() - 52;
    CHECK(At<uint64_t>(food, tail) == 77 && At<uint64_t>(food, tail + 8) == 78);
    CHECK(At<uint64_t>(food, tail + 24) == 0 && At<int32_t>(food, tail + 32) == 0);

    // A script item names its module and entry, and has no effects.
    mod_rules::ItemSpec charm = stew;
    charm.use = mod_rules::ItemUse::Script;
    charm.stamina = 0;
    charm.hp = 0;
    parts.module = "canopyitem_alpha_charm";
    const std::vector<uint8_t> script = mod_rules::BuildItemRow(charm, parts, log.Writer());
    CHECK(script.size() == kAction + 48 + 4 * 12 + 52);
    CHECK(At<uint64_t>(script, kAction) == 9000);
    CHECK(log.texts[kAction + 24] == "canopyitem_alpha_charm" && log.texts[kAction + 32] == "main");
    CHECK(log.texts[kAction + 40].empty());

    // A placed item is a type 5 Placement of its gimmick, in its object's
    // category unless it sets one.
    mod_rules::ItemSpec lamp = charm;
    lamp.use = mod_rules::ItemUse::Place;
    parts.gimmick = 1650000000;
    parts.placeCategory = 30;
    const std::vector<uint8_t> place = mod_rules::BuildItemRow(lamp, parts, log.Writer());
    CHECK(At<uint64_t>(place, 312) == 5 && At<uint64_t>(place, 320) == 30);
    CHECK(At<uint64_t>(place, kAction) == 1400 && At<uint64_t>(place, kAction + 8) == 1650000000);
    CHECK(log.texts[kAction + 24].empty());
    CHECK(At<uint64_t>(place, place.size() - 52 + 24) == 1650000000);
    lamp.category = 9;
    CHECK(At<uint64_t>(mod_rules::BuildItemRow(lamp, parts, log.Writer()), 320) == 9);
    blob::Table table;
    table.rows = {AsRow(food), AsRow(place)};
    CHECK(mod_rules::FurnitureItem(table, 1650000000) == &table.rows[1]);
    CHECK(mod_rules::FurnitureItem(table, 1650000001) == nullptr);
    CHECK(mod_rules::RowCategory(table.rows[1]) == 30);

    // A base with a use only takes an item that sets its own, and a plain
    // base keeps its action.
    mod_rules::ItemSpec old;
    old.fullKey = "Alpha.Old";
    old.baseId = 100;
    for (std::string& name : old.names) {
        name = "Old";
    }
    mod_rules::Report report;
    const blob::Row used = AsRow(food);
    CHECK(!mod_rules::CheckAgainstTable(old, &used, report));
    CHECK(Said(report, "Mod item Alpha.Old skipped, its base must be a consumable raw good (categories 2 to 15) with no use"));
    old.useSet = true;
    CHECK(mod_rules::CheckAgainstTable(old, &used, report));
    old.useSet = false;
    mod_rules::ItemSpec none = stew;
    none.use = mod_rules::ItemUse::None;
    const std::vector<uint8_t> plain = mod_rules::BuildItemRow(none, parts, log.Writer());
    CHECK(At<uint64_t>(plain, kAction) == 0);
    const blob::Row plainRow = AsRow(plain);
    CHECK(mod_rules::CheckAgainstTable(old, &plainRow, report));
    parts.base = &plainRow;
    parts.id = 1500000002;
    const std::vector<uint8_t> copy = mod_rules::BuildItemRow(old, parts, log.Writer());
    CHECK(copy.size() == plain.size() && At<uint64_t>(copy, 0) == 1500000002);
    CHECK(At<uint64_t>(copy, 320) == 2 && At<uint64_t>(copy, kAction) == 0);
    CHECK(At<uint64_t>(copy, copy.size() - 52 + 24) == 0);
    // An item with no base passes the table check without one.
    CHECK(mod_rules::CheckAgainstTable(stew, nullptr, report));
}

// A gimmick.dat row with one angle, one part, one feature and the given ids,
// laid out as the serializer writes it.
std::vector<uint8_t> GimmickRow(uint64_t id, uint64_t item) {
    std::vector<uint8_t> row;
    const auto put = [&](uint64_t value, size_t size) {
        for (size_t byte = 0; byte < size; ++byte) {
            row.push_back(static_cast<uint8_t>(value >> (8 * byte)));
        }
    };
    put(id, 8);
    // enum, refID, 7 names, animeID.
    put(0, 8);
    put(77, 8);
    put(0, 56);
    put(0, 8);
    for (int art = 0; art < 4; ++art) {
        put(500 + art, 4);
    }
    // shadow, thumbnail, isDebugGenerate, isMirror.
    put(0, 20);
    put(1, 4);
    put(90, 4);
    // silo, inventory, script, 23 script args.
    put(0, 16 + 23 * 16);
    // One part: name, isAnime, isInstancing, seqID, one render step,
    // shaderID, no shader textures, 9 flags and floats, layerId, attackID,
    // power, effect, one of each mask, scale, priority, warpType, 5 warp
    // args, animalPopType, animalPopMax, no pop layers, animalPopRate.
    put(1, 4);
    put(0, 20);
    put(1, 4);
    put(0, 8);
    put(0, 8);
    put(0, 4);
    put(0, 36);
    put(0, 24);
    for (int mask = 0; mask < 3; ++mask) {
        put(1, 4);
        put(0, 4);
    }
    put(0, 12 + 40 + 8);
    put(0, 4);
    put(0, 4);
    // colorRepTexID, one feature, itemID, 3 sounds, the trailer.
    put(600, 4);
    put(1, 4);
    put(3, 8);
    put(item, 8);
    put(0, 24);
    put(110, 4);
    put(0, 12);
    return row;
}

void TestObjects() {
    std::printf("Object reader and rows\n");
    TempMods mods;
    mods.Write(
        "Alpha/canopy.ini",
        "[mod]\n"
        "key = Alpha\n"
        "\n"
        "[object.Lamp]\n"
        "name = Lamp\n"
        "clone = 243000001\n"
        "skin = Glow\n"
        "\n"
        "[object.Chair]\n"
        "name = Chair\n"
        "clone = 243000002\n"
        "skin = Nope\n"
        "\n"
        "[object.NoClone]\n"
        "name = NoClone\n"
        "\n"
        "[object.lamp]\n"
        "name = Again\n"
        "clone = 243000001\n"
        "\n"
        "[item.Lamp]\n"
        "name = Lamp\n"
        "use = place\n"
        "object = Lamp\n"
    );
    const std::vector<mod_rules::ModFolder> folders = mod_rules::ScanMods(mods.Dir());
    CHECK(mod_rules::WithObjects(folders).size() == 1);
    mod_rules::KeyRefs refs;
    refs.Add(RefKind::Skin, "Alpha.Glow");
    mod_rules::Report report;
    const std::vector<mod_rules::ObjectSpec> objects =
        mod_rules::ReadObjects(folders, refs, report, mod_rules::AcceptAll);
    CHECK(objects.size() == 2);
    if (objects.size() == 2) {
        CHECK(objects[0].fullKey == "Alpha.Lamp" && objects[0].cloneId == 243000001 && objects[0].line == 4);
        CHECK(objects[0].skin == 1 && objects[0].modSkin);
        CHECK(objects[1].skin == 0 && !objects[1].modSkin);
    }
    const Diagnostic* skin = Said(report, "[Alpha] Object Chair keeps its clone's skin");
    CHECK(skin && skin->severity == Severity::Warning && skin->line == 12);
    const Diagnostic* clone = Said(report, "[Alpha] Object NoClone skipped, it needs clone = ");
    CHECK(clone && clone->severity == Severity::Error && clone->line == 14);
    CHECK(Said(report, "[Alpha] Object lamp skipped, the mod already has an object with that key."));
    CHECK(report.Count(Severity::Error) == 2);

    // The desktop check registers the objects and warns about one no item
    // places.
    mod_rules::Context context;
    context.folders = folders;
    context.modsDir = mods.Dir();
    mod_rules::CheckObjects(context);
    CHECK(context.refs.Find(RefKind::Object, "alpha.chair") == 2);
    CHECK(Said(context.report, "Mod object Alpha.Chair cannot be placed"));
    CHECK(!Said(context.report, "Mod object Alpha.Lamp cannot be placed"));

    // The row walk, and the furniture rule against an item table.
    const std::vector<uint8_t> gimmick = GimmickRow(243000001, 300);
    const blob::Row gimmickRow = AsRow(gimmick);
    mod_rules::GimmickLayout layout;
    CHECK(mod_rules::WalkGimmick(gimmickRow, layout));
    CHECK(layout.trailer == gimmick.size() - 16 && layout.itemId == layout.trailer - 32);
    CHECK(layout.colorRep == layout.itemId - 16);
    CHECK(mod_rules::GimmickItem(gimmickRow) == 300);
    std::vector<uint8_t> cut = gimmick;
    cut.pop_back();
    CHECK(!mod_rules::WalkGimmick(AsRow(cut), layout));
    CHECK(mod_rules::GimmickItem(AsRow(cut)) == 0);
    CHECK(mod_rules::WalkGimmick(gimmickRow, layout));

    mod_rules::ItemSpec place;
    place.use = mod_rules::ItemUse::Place;
    place.useSet = true;
    mod_rules::ItemRowParts parts;
    parts.id = 300;
    parts.gimmick = 243000001;
    TextLog log;
    const std::vector<uint8_t> furniture = mod_rules::BuildItemRow(place, parts, log.Writer());
    parts.id = 301;
    parts.gimmick = 243000009;
    const std::vector<uint8_t> other = mod_rules::BuildItemRow(place, parts, log.Writer());
    blob::Table items;
    items.rows = {AsRow(other), AsRow(furniture)};
    mod_rules::Report table;
    if (!objects.empty()) {
        CHECK(mod_rules::CheckAgainstTables(objects[0], &gimmickRow, &items, table));
        CHECK(!mod_rules::CheckAgainstTables(objects[0], nullptr, &items, table));
        CHECK(Said(table, "Mod object Alpha.Lamp skipped, its clone 243000001 is not in the gimmick table."));
        const std::vector<uint8_t> loose = GimmickRow(243000001, 301);
        const blob::Row looseRow = AsRow(loose);
        CHECK(!mod_rules::CheckAgainstTables(objects[0], &looseRow, &items, table));
        CHECK(Said(table, "is not furniture"));
        // Without the item table the clone only has to give an item back.
        CHECK(mod_rules::CheckAgainstTables(objects[0], &looseRow, nullptr, table));
        const std::vector<uint8_t> fixed = GimmickRow(243000001, 0);
        const blob::Row fixedRow = AsRow(fixed);
        CHECK(!mod_rules::CheckAgainstTables(objects[0], &fixedRow, nullptr, table));

        mod_rules::ObjectRowParts objectParts;
        objectParts.id = 1650000001;
        objectParts.enumName = "CANOPY_ALPHA_LAMP";
        objectParts.item = 1500000003;
        objectParts.skin = 2000000004;
        const std::vector<uint8_t> built =
            mod_rules::BuildObjectRow(objects[0], gimmickRow, layout, objectParts, log.Writer());
        CHECK(built.size() == gimmick.size());
        CHECK(At<uint64_t>(built, 0) == 1650000001 && At<uint64_t>(built, 16) == 0);
        CHECK(log.texts[8] == "CANOPY_ALPHA_LAMP" && log.texts[24] == "Lamp" && log.texts[72] == "Lamp");
        CHECK(At<int32_t>(built, 88) == 2000000004 && At<int32_t>(built, 100) == 2000000004);
        CHECK(At<int32_t>(built, layout.colorRep) == 2000000004);
        CHECK(At<uint64_t>(built, layout.itemId) == 1500000003);
        CHECK(At<uint32_t>(built, layout.trailer) == 0);
        // The rest is the clone's, its feature included.
        CHECK(At<uint64_t>(built, layout.colorRep + 8) == 3);
        objectParts.skin = 0;
        const std::vector<uint8_t> plain =
            mod_rules::BuildObjectRow(objects[0], gimmickRow, layout, objectParts, log.Writer());
        CHECK(At<int32_t>(plain, 88) == 500 && At<int32_t>(plain, layout.colorRep) == 600);
    }
}

// The furniture rule over the game's own tables, read only: every gimmick
// row walks, and the clone sources are the 1783 T1 counted.
void TestRealGimmicks() {
    std::printf("Gimmicks of the game\n");
    const fs::path folder = test_support::GameDir();
    if (!fs::exists(folder / "data.dat")) {
        std::printf("  note: no data.dat in %s, skipped\n", folder.u8string().c_str());
        return;
    }
    mod_rules::VanillaData vanilla;
    std::string error;
    if (!CHECK(vanilla.Open(folder.wstring(), error))) {
        return;
    }
    const blob::Table* gimmicks = vanilla.Table("gimmick.dat");
    const blob::Table* items = vanilla.Table("item.dat");
    if (!CHECK(gimmicks && items)) {
        return;
    }
    size_t walked = 0;
    size_t furniture = 0;
    mod_rules::Report report;
    for (const blob::Row& row : gimmicks->rows) {
        mod_rules::GimmickLayout layout;
        walked += mod_rules::WalkGimmick(row, layout) ? 1 : 0;
        mod_rules::ObjectSpec object;
        object.cloneId = blob::RowId(row);
        furniture += mod_rules::CheckAgainstTables(object, &row, items, report) ? 1 : 0;
    }
    CHECK(walked == gimmicks->rows.size());
    CHECK(furniture == 1783);
}

void TestReportJson() {
    std::printf("Tool JSON\n");
    TempMods mods;
    mods.Write("Quote/canopy.ini", "[mod]\nkey = Q\"x\n[whistle.A]\nname = A\nnotes = SL\nlearn = 7\n");
    const mod_rules::Validation validation = mod_rules::Validate(L"", fs::path(mods.Dir()).wstring());
    CHECK(validation.ok);
    const std::string json = mod_rules::ValidationJson(validation);
    CHECK(json.find("\"ok\":true") != std::string::npos);
    CHECK(json.find("\"vanilla\":false") != std::string::npos);
    CHECK(json.find("\"folders\":[\"Quote\"]") != std::string::npos);
    CHECK(json.find("its key Q\\\"x must be") != std::string::npos);
    CHECK(json.find("\"line\":2") != std::string::npos);

    const mod_rules::Validation missing = mod_rules::Validate(L"", L"Z:\\no\\such\\folder");
    CHECK(!missing.ok);
    CHECK(mod_rules::ValidationJson(missing).find("\"ok\":false,\"error\":\"The mods folder") != std::string::npos);
    CHECK(!mod_rules::Validate(L"", L"").ok);

    const std::string sections = mod_rules::SectionsJson();
    CHECK(sections.find("\"prefix\":\"whistle.\"") != std::string::npos);
    CHECK(sections.find("\"key\":\"notes\",\"kind\":\"melody\",\"required\":true") != std::string::npos);
    CHECK(sections.find("\"min\":1,\"max\":11") != std::string::npos);
    CHECK(sections.find("],\"tables\":[\"texture.dat\",\"icon.dat\",") != std::string::npos);
    CHECK(sections.find("\"storesales.dat\"") != std::string::npos);
    CHECK(sections.size() >= 2 && sections.compare(sections.size() - 2, 2, "]}") == 0);
    bool printable = true;
    for (const char c : sections) {
        printable = printable && static_cast<unsigned char>(c) >= 0x20;
    }
    CHECK(printable);
    CHECK(mod_rules::SectionOf("whistle.Rise") == &mod_rules::kWhistleSection);
    CHECK(mod_rules::SectionOf("WHISTLE.Rise") == &mod_rules::kWhistleSection);
    CHECK(mod_rules::SectionOf("whistle.") == nullptr);
    CHECK(mod_rules::SectionOf("mod") != nullptr);
    CHECK(mod_rules::SectionOf("modx") == nullptr);
#ifdef _WIN32
    // Not UTF-8, so read as the ANSI code page: checked where that is 1252.
    if (GetACP() == 1252) {
        CHECK(mod_rules::ToUtf8(std::string("\xE9t\xE9")) == "\xC3\xA9t\xC3\xA9");
    }
#else
    // Not UTF-8, and the narrow form is UTF-8 here: each bad byte is U+FFFD.
    CHECK(mod_rules::ToUtf8(std::string("\xE9t\xE9")) == "\xEF\xBF\xBDt\xEF\xBF\xBD");
#endif
    CHECK(mod_rules::ToUtf8(std::string("\xC3\xA9")) == "\xC3\xA9");
}

}  // namespace

void TestRules(int& checks, int& failures) {
    TestIniLines();
    TestValues();
    TestRefs();
    TestMelodies();
    TestWhistleReader();
    TestArtReader();
    TestItemReader();
    TestItemRows();
    TestObjects();
    TestRealGimmicks();
    TestReportJson();
    checks += g_checks;
    failures += g_failures;
}
