#include "rules_append.h"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>

#include "platform_files.h"

// =============================================================
// APPEND RULES
// =============================================================
// Offering pools, fishing spots, traps, forage spots and wild crop areas are
// lists inside vanilla rows. A section adds elements to them instead of
// adding rows, so no id is given out and nothing reaches the save: the save
// keeps what a draw gave, never the lists drawn from.
//
// The fields each element gets, by the table's own names (docs/CATCHES.md):
//   offeringitem itemInfo     itemID, itemName, dropRate, count
//   fishing captureInfo       id (a fish's item id), rate, condSeason (one
//                             season), condTime (H:MM-H:MM); the game lists
//                             a fish once per season and time window
//   capture captureInfo       type (0 creature, 1 item), id, rate, and the
//                             trap's bait status it needs (checkStatusName
//                             "bait", checkStatusValue 0 or 1); the game
//                             lists a catch once per bait state
//   placementitem tableInfo.<i>.itemInfo, for each table whose tableID is a
//                             named spot object: weight, itemID, itemName,
//                             popWeather, condOnFlag, condOffFlag, popMax -1
//   naturaloccurrence cropsInfo  cropsName, cropsID, popSeason (0 to 3, or 4
//                             for every season), dayPopMin, dayPopMax,
//                             objectMaxCount, condOnFlag
// Every other field of a new element is 0 or empty, as unset fields of
// vanilla elements are. itemName and cropsName are labels no vanilla element
// leaves empty; a mod element gets its Mod.Key.
//
// A spec goes into a row all at once or not at all, so a list that is full
// (most are k_vectors the record holds inline, see MaxElements) leaves that
// row as it was, and the line says so.

namespace mod_rules {
namespace {

namespace ts = table_schema;

const char* const kFrameworkPrefix = "framework_";
// The time windows vanilla fish are listed under.
const char* const kDayWindows[] = {"0:00-6:00", "7:00-12:00", "12:00-18:00", "18:00-0:00"};
const char* const kSeasons[] = {"spring", "summer", "autumn", "winter"};
// naturaloccurrence popSeason for a crop that grows in every season.
constexpr int kEverySeason = 4;
constexpr int kMaxRate = 1000;
// The ids a failure line names before it says how many more.
constexpr size_t kListedIds = 8;

const FieldInfo kOfferingFields[] = {
    {"pools", ValueKind::List, true, "The offering pools that get the item: offeringitem.dat ids or enum names, or all."},
    {"item", ValueKind::ItemRef, true, "The item: a mod item by key or Mod.Key, or a vanilla item id or enum name."},
    {"rate", ValueKind::Integer, false, "The entry's dropRate, 1 to 1000. Every vanilla entry has 1.", "1", nullptr, 1,
     kMaxRate},
    {"count", ValueKind::Integer, false, "How many of the item the entry gives, 1 to 99.", "1", nullptr, 1, 99},
};

const FieldInfo kCatchFields[] = {
    {"fish", ValueKind::VanillaId, false, "A fish for fishing spots: a vanilla fish by its item id or item enum name.",
     nullptr, "fish.dat"},
    {"item", ValueKind::ItemRef, false,
     "An item for traps: a mod item by key or Mod.Key, or a vanilla item id or enum name."},
    {"creature", ValueKind::VanillaId, false, "A creature for traps: a vanilla creature id or enum name.", nullptr,
     "creature.dat"},
    {"spots", ValueKind::List, false, "With fish: the fishing spots, fishing.dat ids or enum names, or all."},
    {"traps", ValueKind::List, false, "With item or creature: the traps, capture.dat ids or enum names, or all."},
    {"rate", ValueKind::Number, false,
     "The catch's weight against the others. With fish, a number over 0 up to 1000 (vanilla 0.5 to 6); with item or "
     "creature, a whole number from 1 to 1000 (vanilla 1 to 60).",
     "1"},
    {"seasons", ValueKind::List, false, "With fish: spring, summer, autumn and winter, or all.", "all"},
    {"times", ValueKind::List, false, "With fish: time windows as H:MM-H:MM, or all for the four vanilla fish use.",
     "all"},
    {"bait", ValueKind::Choice, false, "With item or creature: whether the trap must be baited, not baited, or either.",
     "both", nullptr, 0, 0, "yes|no|both"},
};

const FieldInfo kForageFields[] = {
    {"item", ValueKind::ItemRef, false,
     "An item for forage spots: a mod item by key or Mod.Key, or a vanilla item id or enum name."},
    {"crop", ValueKind::VanillaId, false, "A wild crop for areas: a vanilla crop id or enum name.", nullptr,
     "crops.dat"},
    {"spots", ValueKind::List, false, "With item: the forage spot objects, gimmick.dat ids or enum names, or all."},
    {"areas", ValueKind::List, false, "With crop: the areas, naturaloccurrence.dat ids or enum names, or all."},
    {"weight", ValueKind::Integer, false, "With item: its weight against the spot's other items, 1 to 1000.", "10",
     nullptr, 1, kMaxRate},
    {"weather", ValueKind::List, false, "With item: the weathers it shows in, weather.dat ids or enum names, or all.",
     "all"},
    {"flag", ValueKind::FlagRef, false, "A flag that must be on: a mod flag by key or Mod.Key, or a vanilla flag id."},
    {"unless", ValueKind::FlagRef, false, "With item: a flag that must be off."},
    {"seasons", ValueKind::List, false, "With crop: spring, summer, autumn and winter, or all.", "all"},
    {"daily", ValueKind::Other, false, "With crop: how many grow each day, as min-max from 0 to 100.", "1-2"},
    {"most", ValueKind::Integer, false, "With crop: the most the area holds at once, 1 to 100.", "5", nullptr, 1, 100},
};

// One section kind and the nouns its messages use.
struct Kind {
    const SectionInfo* section;
    const char* one;
    const char* many;
};

int LineOf(const canopy_ini::IniSection& section, size_t index) {
    return index < section.valueLines.size() ? section.valueLines[index] : section.line;
}

bool Digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// An enum name as the game writes them: capitals, digits and '_', with one
// '_' at least.
bool LooksLikeEnum(const std::string& text) {
    return text.size() <= 128 && text.find('_') != std::string::npos &&
           std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
           });
}

bool SameText(const std::string& left, const char* right) {
    return platform::CompareNoCase(left.c_str(), right) == 0;
}

std::string Stem(const char* file) {
    const std::string name = file;
    return name.size() > 4 && name.compare(name.size() - 4, 4, ".dat") == 0 ? name.substr(0, name.size() - 4) : name;
}

std::string Capitalized(const char* text) {
    std::string out = text;
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    }
    return out;
}

// "a, b and c".
std::string Joined(const std::vector<std::string>& parts) {
    std::string text;
    for (size_t index = 0; index < parts.size(); ++index) {
        text += (index == 0 ? "" : index + 1 == parts.size() ? " and " : ", ") + parts[index];
    }
    return text;
}

// ---- Reading -------------------------------------------------------------

// What one section is being read into, with the helpers its keys share.
struct Reader {
    const ModFolder& folder;
    const canopy_ini::IniSection& section;
    const Refs& refs;
    Report& report;
    AppendSpec& spec;

    std::string Value(const char* key) const {
        return canopy_ini::Value(&section, key, "");
    }
    bool Fail(const char* key, const std::string& why) const {
        report.Error(folder.name, key ? canopy_ini::ValueLine(&section, key) : section.line,
                     spec.where + " skipped, " + why + ".");
        return false;
    }
    void Ignored(const char* key, const char* goesWith) const {
        if (!Value(key).empty()) {
            report.Warning(folder.name, canopy_ini::ValueLine(&section, key),
                           spec.where + ": " + key + " is ignored, it goes with " + goesWith + ".");
        }
    }

    // text as a row of table. modItems also takes a mod item by key or
    // Mod.Key.
    bool Ref(const char* table, const char* key, const std::string& text, bool modItems, AppendRef& out) const {
        out = AppendRef();
        out.table = table;
        out.key = key;
        out.text = text;
        const std::string noun = Stem(table);
        const std::string named = std::string(key) + " " + text;
        if (Digits(text)) {
            if (!ParseU64(text, out.id) || out.id == 0) {
                return Fail(key, named + " is not a " + noun + " id");
            }
            if (out.id >= ts::kRowBandStart && out.id < ts::kRowBandStart + ts::kRowBandSize) {
                return Fail(key, named + " is a mod row's id, which " + key + " does not take");
            }
            if (modItems && ItemRef(refs, spec.modKey, text) == 0) {
                return Fail(key, named + " is a mod item's id; name a mod item by its key");
            }
            out.vanilla = true;
            return true;
        }
        if (modItems) {
            out.id = ItemRef(refs, spec.modKey, text);
            if (out.id != 0) {
                return true;
            }
            if (text.find('.') != std::string::npos) {
                return Fail(key, named + " is not an item of an installed mod");
            }
        }
        const ts::TableSchema* schema = ts::FindPatchable(table);
        if (LooksLikeEnum(text) && schema && schema->enumOffset >= 0) {
            if (text.rfind("CANOPY_", 0) == 0) {
                return Fail(key, named + " is a mod row's enum name, which " + key + " does not take");
            }
            out.enumName = text;
            out.vanilla = true;
            return true;
        }
        return Fail(key, named + " is not " + (modItems ? "an item of this mod, a Mod.Key, " : "") + "a " + noun +
                             " id or enum name");
    }

    // The required reference key, added to spec.refs; its index in ref.
    bool AddRef(const char* table, const char* key, bool modItems, int& ref) const {
        const std::string text = Value(key);
        if (text.empty()) {
            return Fail(key, std::string("it needs ") + key);
        }
        AppendRef out;
        if (!Ref(table, key, text, modItems, out)) {
            return false;
        }
        ref = static_cast<int>(spec.refs.size());
        spec.refs.push_back(std::move(out));
        return true;
    }

    // The required list of rows of table, or all.
    bool Rows(const char* table, const char* key, bool& all, std::vector<AppendRef>& rows) const {
        const std::string text = Value(key);
        if (text.empty()) {
            return Fail(nullptr, std::string("it needs ") + key + " = all, or " + Stem(table) + " ids or enum names");
        }
        all = SameText(text, "all");
        if (all) {
            return true;
        }
        for (const std::string& part : SplitList(text)) {
            if (part.empty()) {
                return Fail(key, std::string(key) + " has an empty entry");
            }
            AppendRef row;
            if (!Ref(table, key, part, false, row)) {
                return false;
            }
            const bool twice = std::any_of(rows.begin(), rows.end(), [&](const AppendRef& other) {
                return other.id == row.id && other.enumName == row.enumName;
            });
            if (!twice) {
                rows.push_back(std::move(row));
            }
        }
        return true;
    }

    // An integer from low to high, fallback when the key is not given.
    bool Integer(const char* key, int32_t low, int32_t high, int32_t fallback, int32_t& out) const {
        const std::string text = Value(key);
        out = fallback;
        if (text.empty()) {
            return true;
        }
        if (!ParseI32(text, out) || out < low || out > high) {
            return Fail(key, std::string(key) + " must be a whole number from " + std::to_string(low) + " to " +
                                 std::to_string(high));
        }
        return true;
    }

    // A flag that must be on or off, 0 for none.
    bool Flag(const char* key, int32_t& out) const {
        const std::string text = Value(key);
        out = 0;
        if (text.empty()) {
            return true;
        }
        out = FlagRef(refs, spec.modKey, text);
        if (out == 0) {
            return Fail(key, std::string(key) + " " + text + " is not a flag of an installed mod or a vanilla flag id");
        }
        return true;
    }

    // Season numbers 0 to 3; all of them, and all set, for all or none.
    bool Seasons(std::vector<int>& seasons, bool& all) const {
        const std::string text = Value("seasons");
        all = text.empty() || SameText(text, "all");
        seasons.clear();
        if (all) {
            seasons = {0, 1, 2, 3};
            return true;
        }
        for (const std::string& part : SplitList(text)) {
            const auto found = std::find_if(std::begin(kSeasons), std::end(kSeasons), [&](const char* name) {
                return SameText(part, name);
            });
            if (found == std::end(kSeasons)) {
                return Fail("seasons", "seasons must be spring, summer, autumn and winter, or all, not " + part);
            }
            const int season = static_cast<int>(found - std::begin(kSeasons));
            if (std::find(seasons.begin(), seasons.end(), season) == seasons.end()) {
                seasons.push_back(season);
            }
        }
        return true;
    }
};

// H:MM, hours 0 to 23 and minutes 00 to 59.
bool ParseClock(const std::string& text, int& hour, int& minute) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 2 || text.size() != colon + 3 ||
        !Digits(text.substr(0, colon)) || !Digits(text.substr(colon + 1))) {
        return false;
    }
    hour = std::stoi(text.substr(0, colon));
    minute = std::stoi(text.substr(colon + 1));
    return hour <= 23 && minute <= 59;
}

// H:MM-H:MM, written back the way the game writes it: hours without a
// leading zero.
bool ParseWindow(const std::string& text, std::string& window) {
    const size_t dash = text.find('-');
    int from[2] = {};
    int to[2] = {};
    if (dash == std::string::npos || !ParseClock(text.substr(0, dash), from[0], from[1]) ||
        !ParseClock(text.substr(dash + 1), to[0], to[1])) {
        return false;
    }
    auto clock = [](const int (&time)[2]) {
        return std::to_string(time[0]) + (time[1] < 10 ? ":0" : ":") + std::to_string(time[1]);
    };
    window = clock(from) + "-" + clock(to);
    return true;
}

// A number over 0 and up to kMaxRate, as digits with one '.' at most.
bool RateValid(const std::string& text) {
    const size_t dot = text.find('.');
    const std::string whole = text.substr(0, dot);
    const std::string part = dot == std::string::npos ? std::string() : text.substr(dot + 1);
    if ((whole.empty() && part.empty()) || (!whole.empty() && !Digits(whole)) ||
        (dot != std::string::npos && !Digits(part)) || whole.size() > 4 || part.size() > 6) {
        return false;
    }
    const double value = std::stod((whole.empty() ? "0" : whole) + (part.empty() ? "" : "." + part));
    return value > 0 && value <= kMaxRate;
}

bool ReadOffering(const Reader& in, AppendSpec& spec) {
    spec.schema = ts::FindPatchable("offeringitem.dat");
    spec.list = "itemInfo";
    int item = -1;
    int32_t rate = 1;
    int32_t count = 1;
    if (!in.Rows("offeringitem.dat", "pools", spec.allRows, spec.rows) || !in.AddRef("item.dat", "item", true, item) ||
        !in.Integer("rate", 1, kMaxRate, 1, rate) || !in.Integer("count", 1, 99, 1, count)) {
        return false;
    }
    spec.elements.push_back({
        {"itemID", "", item},
        {"itemName", spec.fullKey},
        {"dropRate", std::to_string(rate)},
        {"count", std::to_string(count)},
    });
    return true;
}

bool ReadFishCatch(const Reader& in, AppendSpec& spec) {
    spec.schema = ts::FindPatchable("fishing.dat");
    spec.list = "captureInfo";
    in.Ignored("traps", "item or creature");
    in.Ignored("bait", "item or creature");
    int fish = -1;
    // A fish is named by its item: the id fish.dat shares, or the item's
    // enum name, since fish.dat has none.
    if (!in.Rows("fishing.dat", "spots", spec.allRows, spec.rows) || !in.AddRef("item.dat", "fish", false, fish)) {
        return false;
    }
    spec.refs[static_cast<size_t>(fish)].alsoIn = "fish.dat";
    std::string rate = in.Value("rate");
    if (rate.empty()) {
        rate = "1";
    }
    if (!RateValid(rate)) {
        return in.Fail("rate", "rate must be a number over 0 and up to 1000, such as 0.5 or 3");
    }
    std::vector<int> seasons;
    bool allSeasons = false;
    if (!in.Seasons(seasons, allSeasons)) {
        return false;
    }
    std::vector<std::string> windows;
    const std::string times = in.Value("times");
    if (times.empty() || SameText(times, "all")) {
        windows.assign(std::begin(kDayWindows), std::end(kDayWindows));
    } else {
        for (const std::string& part : SplitList(times)) {
            std::string window;
            if (!ParseWindow(part, window)) {
                return in.Fail("times", "times must be windows such as 7:00-12:00, or all, not " + part);
            }
            if (std::find(windows.begin(), windows.end(), window) == windows.end()) {
                windows.push_back(window);
            }
        }
    }
    for (const int season : seasons) {
        for (const std::string& window : windows) {
            spec.elements.push_back({
                {"id", "", fish},
                {"rate", rate},
                {"condSeason.count", "1"},
                {"condSeason.0", std::to_string(season)},
                {"condTime", window},
            });
        }
    }
    return true;
}

bool ReadTrapCatch(const Reader& in, AppendSpec& spec, bool creature) {
    spec.schema = ts::FindPatchable("capture.dat");
    spec.list = "captureInfo";
    in.Ignored("spots", "fish");
    in.Ignored("seasons", "fish");
    in.Ignored("times", "fish");
    int what = -1;
    int32_t rate = 1;
    if (!in.Rows("capture.dat", "traps", spec.allRows, spec.rows) ||
        !(creature ? in.AddRef("creature.dat", "creature", false, what) : in.AddRef("item.dat", "item", true, what)) ||
        !in.Integer("rate", 1, kMaxRate, 1, rate)) {
        return false;
    }
    const std::string bait = in.Value("bait");
    std::vector<int> states;
    if (bait.empty() || SameText(bait, "both")) {
        states = {0, 1};
    } else if (SameText(bait, "yes")) {
        states = {1};
    } else if (SameText(bait, "no")) {
        states = {0};
    } else {
        return in.Fail("bait", "bait must be yes, no or both");
    }
    for (const int state : states) {
        spec.elements.push_back({
            {"type", creature ? "0" : "1"},
            {"id", "", what},
            {"rate", std::to_string(rate)},
            {"checkStatusName", "bait"},
            {"checkStatusValue", std::to_string(state)},
        });
    }
    return true;
}

bool ReadForageItem(const Reader& in, AppendSpec& spec) {
    spec.schema = ts::FindPatchable("placementitem.dat");
    spec.allRows = true;
    spec.outer = "tableInfo";
    spec.match = "tableID";
    spec.list = "itemInfo";
    in.Ignored("areas", "crop");
    in.Ignored("seasons", "crop");
    in.Ignored("daily", "crop");
    in.Ignored("most", "crop");
    int item = -1;
    int32_t weight = 10;
    int32_t on = 0;
    int32_t off = 0;
    if (!in.Rows("gimmick.dat", "spots", spec.allOuter, spec.matches) || !in.AddRef("item.dat", "item", true, item) ||
        !in.Integer("weight", 1, kMaxRate, 10, weight) || !in.Flag("flag", on) || !in.Flag("unless", off)) {
        return false;
    }
    std::vector<ElementValue> element = {
        {"weight", std::to_string(weight)},
        {"itemID", "", item},
        {"itemName", spec.fullKey},
    };
    const std::string weather = in.Value("weather");
    if (!weather.empty() && !SameText(weather, "all")) {
        bool all = false;
        std::vector<AppendRef> weathers;
        if (!in.Rows("weather.dat", "weather", all, weathers)) {
            return false;
        }
        element.push_back({"popWeather.count", std::to_string(weathers.size())});
        for (size_t index = 0; index < weathers.size(); ++index) {
            element.push_back({"popWeather." + std::to_string(index), "", static_cast<int>(spec.refs.size())});
            spec.refs.push_back(std::move(weathers[index]));
        }
    }
    element.push_back({"condOnFlag", std::to_string(on)});
    element.push_back({"condOffFlag", std::to_string(off)});
    // Every vanilla element has -1: no limit of its own.
    element.push_back({"popMax", "-1"});
    spec.elements.push_back(std::move(element));
    return true;
}

bool ReadWildCrop(const Reader& in, AppendSpec& spec) {
    spec.schema = ts::FindPatchable("naturaloccurrence.dat");
    spec.list = "cropsInfo";
    in.Ignored("spots", "item");
    in.Ignored("weight", "item");
    in.Ignored("weather", "item");
    in.Ignored("unless", "item");
    int crop = -1;
    int32_t most = 5;
    int32_t on = 0;
    if (!in.Rows("naturaloccurrence.dat", "areas", spec.allRows, spec.rows) ||
        !in.AddRef("crops.dat", "crop", false, crop) || !in.Integer("most", 1, 100, 5, most) || !in.Flag("flag", on)) {
        return false;
    }
    int32_t low = 1;
    int32_t high = 2;
    const std::string daily = in.Value("daily");
    if (!daily.empty()) {
        const size_t dash = daily.find('-');
        const bool parsed = dash != std::string::npos && ParseI32(daily.substr(0, dash), low) &&
                            ParseI32(daily.substr(dash + 1), high);
        if (!parsed || low < 0 || high > 100 || low > high) {
            return in.Fail("daily", "daily must be min-max from 0 to 100, such as 1-3");
        }
    }
    std::vector<int> seasons;
    bool allSeasons = false;
    if (!in.Seasons(seasons, allSeasons)) {
        return false;
    }
    if (allSeasons) {
        seasons = {kEverySeason};
    }
    for (const int season : seasons) {
        spec.elements.push_back({
            {"cropsName", spec.fullKey},
            {"cropsID", "", crop},
            {"popSeason", std::to_string(season)},
            {"dayPopMin", std::to_string(low)},
            {"dayPopMax", std::to_string(high)},
            {"objectMaxCount", std::to_string(most)},
            {"condOnFlag", std::to_string(on)},
        });
    }
    return true;
}

// The one of keys the section has, null with the section reported when it
// has none or several.
const char* OneOf(const Reader& in, std::initializer_list<const char*> keys) {
    const char* chosen = nullptr;
    std::vector<std::string> names;
    for (const char* key : keys) {
        names.push_back(key);
        if (in.Value(key).empty()) {
            continue;
        }
        if (chosen) {
            in.Fail(key, std::string("it has both ") + chosen + " and " + key + ", and takes one");
            return nullptr;
        }
        chosen = key;
    }
    if (!chosen) {
        std::string list = Joined(names);
        const size_t last = list.rfind(" and ");
        in.Fail(nullptr, "it needs " + (last == std::string::npos ? list : list.replace(last, 5, " or ")));
    }
    return chosen;
}

const Kind kKinds[] = {
    {&kOfferingSection, "offering", "offerings"},
    {&kCatchSection, "catch", "catches"},
    {&kForageSection, "forage entry", "forage entries"},
};

bool ReadSpec(
    const Kind& kind, const ModFolder& folder, const std::string& modKey, const canopy_ini::IniSection& section,
    const Refs& refs, Report& report, AppendSpec& spec
) {
    spec.section = kind.section;
    spec.modKey = modKey;
    spec.folder = folder.name;
    spec.key = SectionKey(section, kind.section->prefix);
    spec.fullKey = modKey + "." + spec.key;
    spec.line = section.line;
    spec.where = "[" + modKey + "] " + kind.section->title + " " + spec.key;
    spec.label = "Mod " + std::string(kind.one) + " " + spec.fullKey;
    const Reader in{folder, section, refs, report, spec};
    if (!KeyValid(spec.key)) {
        return in.Fail(nullptr, "its key must be 1 to 32 letters, digits, '_' or '-'");
    }
    for (size_t index = 0; index < section.values.size(); ++index) {
        const std::string& key = section.values[index].first;
        const bool known = std::any_of(kind.section->fields, kind.section->fields + kind.section->fieldCount,
                                       [&](const FieldInfo& field) { return key == field.key; });
        if (!known) {
            report.Warning(folder.name, LineOf(section, index),
                           spec.where + ": " + key + " is not a " + kind.one + " key, so it is ignored.");
        }
    }
    if (kind.section == &kOfferingSection) {
        return ReadOffering(in, spec);
    }
    if (kind.section == &kCatchSection) {
        const char* what = OneOf(in, {"fish", "item", "creature"});
        if (!what) {
            return false;
        }
        return std::string(what) == "fish" ? ReadFishCatch(in, spec)
                                           : ReadTrapCatch(in, spec, std::string(what) == "creature");
    }
    const char* what = OneOf(in, {"item", "crop"});
    if (!what) {
        return false;
    }
    return std::string(what) == "item" ? ReadForageItem(in, spec) : ReadWildCrop(in, spec);
}

// ---- Applying ------------------------------------------------------------

// The id ref names, checked against its tables. False, with why, when it
// does not resolve.
bool RefId(const AppendRef& ref, const RowTables& tables, uint64_t& id, std::string& why) {
    id = ref.id;
    if (!ref.enumName.empty() && !tables.EnumId(ref.table, ref.enumName, id, why)) {
        why = ref.key + ": " + why;
        return false;
    }
    for (const char* file : {ref.vanilla ? ref.table : nullptr, ref.alsoIn}) {
        if (file && !tables.VanillaFine(file, id, why)) {
            why = ref.key + ": " + why;
            return false;
        }
    }
    return true;
}

// The row of table a reference names, by id or by its enum name in this
// table, since the rows of the table that is loading are not known yet.
bool FindRow(const blob::Table& table, const ts::TableSchema& schema, const AppendRef& ref, size_t& index) {
    std::string name;
    for (size_t row = 0; row < table.rows.size(); ++row) {
        const bool same =
            ref.enumName.empty()
                ? blob::RowId(table.rows[row]) == ref.id
                : schema.enumOffset >= 0 &&
                      blob::ReadText(table, table.rows[row], static_cast<size_t>(schema.enumOffset), name) &&
                      name == ref.enumName;
        if (same) {
            index = row;
            return true;
        }
    }
    return false;
}

// The lists of row the spec's elements go to, adding the match ids it found
// to seen.
std::vector<std::string> ListsOf(
    const AppendSpec& spec, const std::vector<uint8_t>& row, const std::vector<uint64_t>& matches,
    std::set<uint64_t>& seen
) {
    if (spec.outer.empty()) {
        return {spec.list};
    }
    std::vector<std::string> lists;
    uint32_t count = 0;
    std::string why;
    if (!ts::ElementCount(*spec.schema, row.data(), row.size(), spec.outer, count, why)) {
        return lists;
    }
    for (uint32_t element = 0; element < count; ++element) {
        const std::string at = spec.outer + "." + std::to_string(element);
        if (!spec.allOuter) {
            std::vector<ts::Location> places;
            if (!ts::ResolveRow(*spec.schema, at + "." + spec.match, row.data(), row.size(), places, why) ||
                places.size() != 1) {
                continue;
            }
            const uint64_t id = ts::ReadNumber(row.data(), places[0]);
            if (std::find(matches.begin(), matches.end(), id) == matches.end()) {
                continue;
            }
            seen.insert(id);
        }
        lists.push_back(at + "." + spec.list);
    }
    return lists;
}

std::vector<uint8_t> RowBytes(const blob::Row& row) {
    return std::vector<uint8_t>(row.data, row.data + row.size);
}

}  // namespace

const SectionInfo kOfferingSection = {"offering.", "Offering", "docs/SHOPS.md", kOfferingFields,
                                      std::size(kOfferingFields)};
const SectionInfo kCatchSection = {"catch.", "Catch", "docs/CATCHES.md", kCatchFields, std::size(kCatchFields)};
const SectionInfo kForageSection = {"forage.", "Forage", "docs/CATCHES.md", kForageFields, std::size(kForageFields)};

std::vector<AppendSpec> ReadAppends(
    const std::vector<ModFolder>& folders, const Refs& refs, Report& report, const FolderAccept& accept
) {
    std::vector<AppendSpec> all;
    // The mod keys that own each kind, so a second folder with one is refused.
    std::map<const SectionInfo*, std::vector<std::string>> owners;
    bool full = false;
    for (const ModFolder& folder : folders) {
        const std::string modKey = ModKey(folder);
        const int infoLine = canopy_ini::ValueLine(canopy_ini::FindSection(folder.sections, "mod"), "key");
        std::vector<AppendSpec> specs;
        std::set<const SectionInfo*> seen;
        std::set<const SectionInfo*> refused;
        for (const canopy_ini::IniSection& section : folder.sections) {
            const Kind* kind = nullptr;
            for (const Kind& candidate : kKinds) {
                if (!kind && !SectionKey(section, candidate.section->prefix).empty()) {
                    kind = &candidate;
                }
            }
            if (!kind || refused.count(kind->section) || full) {
                continue;
            }
            if (seen.insert(kind->section).second) {
                const std::vector<std::string>& taken = owners[kind->section];
                const bool owned = std::any_of(taken.begin(), taken.end(), [&](const std::string& owner) {
                    return SameText(owner, modKey.c_str());
                });
                if (!KeyValid(modKey) || owned) {
                    report.Error(folder.name, infoLine,
                                 Capitalized(kind->many) + " in folder " + folder.name + " skipped, " +
                                     (owned ? "another folder already has " + std::string(kind->many) +
                                                  " under the key " + modKey
                                            : "its key " + modKey + " must be 1 to 32 letters, digits, '_' or '-'") +
                                     ".");
                    refused.insert(kind->section);
                    continue;
                }
            }
            AppendSpec spec;
            if (!ReadSpec(*kind, folder, modKey, section, refs, report, spec)) {
                continue;
            }
            const bool twice = std::any_of(specs.begin(), specs.end(), [&](const AppendSpec& other) {
                return other.section == spec.section && SameText(other.key, spec.key.c_str());
            });
            if (twice) {
                report.Error(folder.name, section.line,
                             spec.where + " skipped, an earlier " + kind->section->title +
                                 " section of the mod has that key.");
                continue;
            }
            if (all.size() + specs.size() >= kMaxAppends) {
                report.Error(folder.name, section.line,
                             spec.where + " skipped, Canopy takes at most " + std::to_string(kMaxAppends) +
                                 " offering, catch and forage sections in all.");
                full = true;
                continue;
            }
            specs.push_back(std::move(spec));
        }
        for (const SectionInfo* section : seen) {
            if (!refused.count(section)) {
                owners[section].push_back(modKey);
            }
        }
        if (specs.empty() || (accept && !accept(folder, modKey, specs.size()))) {
            continue;
        }
        for (AppendSpec& spec : specs) {
            all.push_back(std::move(spec));
        }
    }
    return all;
}

std::string CountAppends(const std::vector<const AppendSpec*>& specs) {
    std::vector<std::string> parts;
    for (const Kind& kind : kKinds) {
        const size_t count = static_cast<size_t>(std::count_if(
            specs.begin(), specs.end(), [&](const AppendSpec* spec) { return spec->section == kind.section; }
        ));
        if (count != 0) {
            parts.push_back(Plural(count, kind.one, kind.many));
        }
    }
    return Joined(parts);
}

bool ApplyAppends(
    const blob::Table& table, const std::vector<const AppendSpec*>& specs, const RowTables& tables,
    std::vector<uint8_t>& out, AppendResult& result
) {
    result = AppendResult();
    out.clear();
    blob::Pool pool(table.poolBytes);
    // The rows that got elements, as they are now, by index.
    std::map<size_t, std::vector<uint8_t>> changed;
    for (const AppendSpec* spec : specs) {
        AppendOutcome outcome;
        outcome.spec = spec;
        const ts::TableSchema& schema = *spec->schema;
        std::string why;
        bool resolved = true;
        std::vector<std::string> texts;
        for (const AppendRef& ref : spec->refs) {
            uint64_t id = 0;
            resolved = resolved && RefId(ref, tables, id, why);
            texts.push_back(std::to_string(id));
        }
        std::vector<uint64_t> matches;
        for (const AppendRef& ref : spec->matches) {
            uint64_t id = 0;
            resolved = resolved && RefId(ref, tables, id, why);
            matches.push_back(id);
        }
        if (!resolved) {
            outcome.failed.push_back({0, why});
            result.outcomes.push_back(std::move(outcome));
            continue;
        }
        std::vector<size_t> targets;
        if (spec->allRows) {
            for (size_t row = 0; row < table.rows.size(); ++row) {
                targets.push_back(row);
            }
        }
        for (size_t index = 0; index < spec->rows.size(); ++index) {
            size_t row = 0;
            if (FindRow(table, schema, spec->rows[index], row)) {
                outcome.foundRows.push_back(index);
                if (std::find(targets.begin(), targets.end(), row) == targets.end()) {
                    targets.push_back(row);
                }
            }
        }
        std::set<uint64_t> seen;
        for (const size_t row : targets) {
            const uint64_t id = blob::RowId(table.rows[row]);
            const auto known = changed.find(row);
            std::vector<uint8_t> work = known != changed.end() ? known->second : RowBytes(table.rows[row]);
            size_t added = 0;
            bool fits = true;
            for (const std::string& list : ListsOf(*spec, work, matches, seen)) {
                for (const std::vector<ElementValue>& element : spec->elements) {
                    std::vector<std::pair<std::string, std::string>> values;
                    for (const ElementValue& value : element) {
                        values.emplace_back(value.path, value.ref >= 0 ? texts[static_cast<size_t>(value.ref)]
                                                                       : value.value);
                    }
                    uint32_t index = 0;
                    fits = ts::AppendElement(schema, work, list, values, pool, index, why);
                    if (!fits) {
                        break;
                    }
                    ++added;
                }
                if (!fits) {
                    break;
                }
            }
            if (!fits) {
                outcome.failed.push_back({id, why});
            } else if (added != 0) {
                changed[row] = std::move(work);
                outcome.rows.push_back({id, added});
                result.elements += added;
            }
        }
        for (size_t index = 0; index < matches.size(); ++index) {
            if (seen.count(matches[index])) {
                outcome.foundMatches.push_back(index);
            }
        }
        result.outcomes.push_back(std::move(outcome));
    }
    if (result.elements == 0) {
        return false;
    }
    result.rows = changed.size();
    std::vector<std::vector<uint8_t>> rows;
    rows.reserve(table.rows.size());
    for (size_t row = 0; row < table.rows.size(); ++row) {
        const auto known = changed.find(row);
        rows.push_back(known != changed.end() ? known->second : RowBytes(table.rows[row]));
    }
    std::string texts(reinterpret_cast<const char*>(table.pool), table.poolBytes);
    texts += pool.Bytes();
    blob::Build(rows, texts, out);
    return true;
}

bool Unresolved(const AppendOutcome& outcome) {
    return outcome.failed.size() == 1 && outcome.failed[0].first == 0 && outcome.rows.empty();
}

std::vector<std::string> FailureMessages(const AppendOutcome& outcome) {
    const AppendSpec& spec = *outcome.spec;
    // Each reason with the rows it kept the spec out of, in first-seen order.
    std::vector<std::pair<std::string, std::vector<uint64_t>>> reasons;
    for (const auto& failure : outcome.failed) {
        auto reason = std::find_if(reasons.begin(), reasons.end(), [&](const auto& entry) {
            return entry.first == failure.second;
        });
        if (reason == reasons.end()) {
            reasons.push_back({failure.second, {}});
            reason = std::prev(reasons.end());
        }
        if (failure.first != 0) {
            reason->second.push_back(failure.first);
        }
    }
    std::vector<std::string> lines;
    for (const auto& [why, ids] : reasons) {
        std::vector<std::string> parts;
        for (size_t index = 0; index < ids.size() && index < kListedIds; ++index) {
            parts.push_back(std::to_string(ids[index]));
        }
        if (ids.size() > kListedIds) {
            parts.push_back(std::to_string(ids.size() - kListedIds) + " more");
        }
        const std::string where = ids.empty() ? "" : " on " + Stem(spec.schema->file) + " " + Joined(parts);
        lines.push_back(spec.label + " skipped" + where + ", " + why + ".");
    }
    return lines;
}

std::vector<std::string> MissingMessages(
    const AppendSpec& spec, const std::vector<size_t>& rows, const std::vector<size_t>& matches
) {
    auto missing = [](const std::vector<AppendRef>& refs, const std::vector<size_t>& found) {
        std::vector<std::string> texts;
        for (size_t index = 0; index < refs.size(); ++index) {
            if (std::find(found.begin(), found.end(), index) == found.end()) {
                texts.push_back(refs[index].text);
            }
        }
        return texts;
    };
    std::vector<std::string> lines;
    const std::vector<std::string> noRow = missing(spec.rows, rows);
    if (!noRow.empty()) {
        lines.push_back(spec.label + " skipped on " + spec.rows.front().key + " " + Joined(noRow) + ", which " +
                        spec.schema->file + " does not have.");
    }
    const std::vector<std::string> noMatch = missing(spec.matches, matches);
    if (!noMatch.empty()) {
        lines.push_back(spec.label + " skipped on " + spec.matches.front().key + " " + Joined(noMatch) +
                        ", which no " + spec.schema->file + " row lists.");
    }
    return lines;
}

void CheckAppends(Context& context) {
    const std::vector<AppendSpec> specs = ReadAppends(context.folders, context.refs, context.report, AcceptAll);
    if (!context.vanilla || specs.empty()) {
        return;
    }
    const VanillaRowTables tables(*context.vanilla);
    std::vector<const ts::TableSchema*> schemas;
    for (const AppendSpec& spec : specs) {
        if (std::find(schemas.begin(), schemas.end(), spec.schema) == schemas.end()) {
            schemas.push_back(spec.schema);
        }
    }
    for (const ts::TableSchema* schema : schemas) {
        std::vector<const AppendSpec*> mine;
        for (const AppendSpec& spec : specs) {
            if (spec.schema == schema) {
                mine.push_back(&spec);
            }
        }
        std::map<const AppendSpec*, std::vector<size_t>> foundRows;
        std::map<const AppendSpec*, std::vector<size_t>> foundMatches;
        std::set<const AppendSpec*> unresolved;
        std::set<std::string> said;
        bool any = false;
        for (const std::string& file : {std::string(kFrameworkPrefix) + schema->file, std::string(schema->file)}) {
            const blob::Table* table = context.vanilla->Table(file);
            if (!table) {
                continue;
            }
            any = true;
            std::vector<uint8_t> out;
            AppendResult result;
            ApplyAppends(*table, mine, tables, out, result);
            for (const AppendOutcome& outcome : result.outcomes) {
                const AppendSpec& spec = *outcome.spec;
                std::vector<size_t>& rows = foundRows[&spec];
                std::vector<size_t>& matches = foundMatches[&spec];
                rows.insert(rows.end(), outcome.foundRows.begin(), outcome.foundRows.end());
                matches.insert(matches.end(), outcome.foundMatches.begin(), outcome.foundMatches.end());
                if (Unresolved(outcome)) {
                    unresolved.insert(&spec);
                }
                for (const std::string& line : FailureMessages(outcome)) {
                    if (said.insert(line).second) {
                        context.report.Error(spec.folder, spec.line, line);
                    }
                }
            }
        }
        if (!any) {
            continue;
        }
        for (const AppendSpec* spec : mine) {
            if (unresolved.count(spec)) {
                continue;
            }
            for (const std::string& line : MissingMessages(*spec, foundRows[spec], foundMatches[spec])) {
                context.report.Error(spec->folder, spec->line, line);
            }
        }
    }
}

}  // namespace mod_rules
