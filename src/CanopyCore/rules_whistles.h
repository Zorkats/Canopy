#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "mod_rules.h"

// The rules of [whistle.<Key>] sections. See docs/WHISTLES.md.
namespace mod_rules {

struct WhistleSpec {
    std::string modKey;
    std::string folder;
    std::string key;
    // Mod.Key, as the registry writes it.
    std::string fullKey;
    int line = 0;
    uint64_t baseId = 2;
    std::string names[kLanguageCount];
    uint64_t notes = 0;
    int32_t noteCount = 0;
    int32_t learn = 0;
    // A vanilla score icon id, 0 to keep the base whistle's.
    uint64_t score = 0;
    // A mod icon that takes the score icon's place, 0 for none.
    uint64_t icon = 0;
    // The script's path in the mod folder and its text; empty for none.
    std::string script;
    std::string source;
    std::string entry = "main";
    // whistle_<mod>_<key> when the whistle has a script.
    std::string module;
    std::string chunkName;
};

// A melody as the matcher compares it: note count and note bits, the first
// note the highest bit.
struct Melody {
    int32_t count = 0;
    uint64_t notes = 0;
    bool operator<(const Melody& other) const {
        return count != other.count ? count < other.count : notes < other.notes;
    }
};

constexpr size_t kMaxWhistles = 100;

extern const SectionInfo kWhistleSection;

// 0 and S are a short note, 1 and L a long one; spaces are skipped. False
// unless there are 2 to 10 notes.
bool ParseMelody(const std::string& text, uint64_t& notes, int32_t& count);
// The melody as S and L letters.
std::string MelodyText(uint64_t notes, int32_t count);

// Reads every [whistle.X] section of folders, in folder and file order, as
// the loader installs them: per mod key, the first folder owns it; per
// mod, keys are unique; across all mods, melodies are unique and at most
// kMaxWhistles load. modsDir is the game's mods folder, where scripts are
// read from. accept sees each folder with usable whistles.
std::vector<WhistleSpec> ReadWhistles(
    const std::vector<ModFolder>& folders, const std::string& modsDir, const Refs& refs, Report& report,
    const FolderAccept& accept
);

// ---- The whistle table ---------------------------------------------------

// whistle.dat rows: 300 bytes, the melody's bits at +88 and count at +96.
constexpr size_t kWhistleRowSize = 300;
// The melody of a whistle.dat row of kWhistleRowSize bytes.
Melody RowMelody(const uint8_t* row);
// The checks against the vanilla table the loader runs when it appends the
// rows: the base row is there with the expected layout, and the melody is
// no vanilla whistle's. base is null when the table has no row baseId. A
// free melody is added to melodies. False, reported, when the whistle is
// skipped.
bool CheckAgainstTable(const WhistleSpec& whistle, const blob::Row* base, std::set<Melody>& melodies, Report& report);

// The desktop check: reads the whistles, then checks them against the
// vanilla table when there is one.
void CheckWhistles(Context& context);

}  // namespace mod_rules
