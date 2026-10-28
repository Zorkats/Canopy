#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "game_addresses.h"

// Checks of the game address resolver: every kind of pattern line and the
// agreement rule against a small image built here, the build lookup, the
// built-in files against the names game_retail.h declares and the address
// lists each feature checks, and, when the samples under _work/bin are
// there, a search of each checked build against its checked addresses, both
// as the file lays out and as the loaded game would be.
namespace {

namespace fs = std::filesystem;
namespace ga = game_addresses;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED address_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

const fs::path kSource = fs::u8path(CANOPY_SOURCE_DIR);

std::string ReadText(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

// =============================================================
// A SMALL IMAGE
// =============================================================
constexpr uint64_t kImageBase = 0x140000000ull;
constexpr uint32_t kTimestamp = 0x12345678;
constexpr uint32_t kImageSize = 0x4000;
constexpr uint32_t kText = 0x1000;
constexpr uint32_t kData = 0x2000;
constexpr uint32_t kPdata = 0x3000;

template <typename T>
void Put(std::vector<uint8_t>& image, size_t at, T value) {
    std::memcpy(image.data() + at, &value, sizeof(T));
}

void PutBytes(std::vector<uint8_t>& image, size_t at, std::initializer_list<uint8_t> bytes) {
    std::copy(bytes.begin(), bytes.end(), image.begin() + at);
}

void PutSection(std::vector<uint8_t>& image, size_t at, const char* name, uint32_t address, uint32_t size, uint32_t flags) {
    std::memcpy(image.data() + at, name, std::strlen(name));
    Put<uint32_t>(image, at + 8, size);
    Put<uint32_t>(image, at + 12, address);
    Put<uint32_t>(image, at + 16, size);
    Put<uint32_t>(image, at + 20, address);
    Put<uint32_t>(image, at + 36, flags);
}

// A PE32+ image laid out at its RVAs: code, data with a string and the
// RTTI of one class, and an exception table with one chained range.
std::vector<uint8_t> SmallImage() {
    std::vector<uint8_t> image(kImageSize, 0);
    image[0] = 'M';
    image[1] = 'Z';
    const uint32_t pe = 0x80;
    Put<uint32_t>(image, 0x3C, pe);
    PutBytes(image, pe, {'P', 'E', 0, 0});
    Put<uint16_t>(image, pe + 4, 0x8664);
    Put<uint16_t>(image, pe + 6, 3);
    Put<uint32_t>(image, pe + 8, kTimestamp);
    Put<uint16_t>(image, pe + 20, 240);
    const uint32_t optional = pe + 24;
    Put<uint16_t>(image, optional, 0x20B);
    Put<uint64_t>(image, optional + 24, kImageBase);
    Put<uint32_t>(image, optional + 56, kImageSize);
    Put<uint32_t>(image, optional + 60, 0x400);
    Put<uint32_t>(image, optional + 108, 16);
    // The exception directory: three ranges of 12 bytes.
    Put<uint32_t>(image, optional + 112 + 3 * 8, kPdata);
    Put<uint32_t>(image, optional + 112 + 3 * 8 + 4, 36);
    const uint32_t sections = optional + 240;
    PutSection(image, sections, ".text", kText, 0x1000, 0x60000020);
    PutSection(image, sections + 40, ".rdata", kData, 0x1000, 0x40000040);
    PutSection(image, sections + 80, ".pdata", kPdata, 0x1000, 0x40000040);

    std::fill(image.begin() + kText, image.begin() + kText + 0x1000, 0xCC);
    // FuncA at 0x1000: a prologue, lea rcx of "hello_name", call FuncB.
    PutBytes(image, 0x1000, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20});
    PutBytes(image, 0x100A, {0x48, 0x8D, 0x0D});
    Put<int32_t>(image, 0x100D, static_cast<int32_t>(0x2011 - 0x1011));
    image[0x1011] = 0xE8;
    Put<int32_t>(image, 0x1012, static_cast<int32_t>(0x1100 - 0x1016));
    image[0x1016] = 0xC3;
    // A piece of FuncA the compiler split off, with a chained range.
    PutBytes(image, 0x1048, {0x66, 0x77, 0x88, 0x99, 0x55});
    // FuncB at 0x1100.
    PutBytes(image, 0x1100, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xAA, 0xBB, 0xCC, 0xDD});
    // The same bytes twice.
    PutBytes(image, 0x1200, {0x90, 0x90, 0x11, 0x22, 0x33, 0x44, 0x90});
    PutBytes(image, 0x1300, {0x90, 0x90, 0x11, 0x22, 0x33, 0x44, 0x90, 0xC3});

    std::memcpy(image.data() + 0x2011, "hello_name", 11);
    // Foo's type descriptor, complete object locator and vtable.
    std::memcpy(image.data() + 0x2110, ".?AVFoo@@", 10);
    Put<uint32_t>(image, 0x2200, 1);
    Put<uint32_t>(image, 0x2200 + 12, 0x2100);
    Put<uint64_t>(image, 0x2300, kImageBase + 0x2200);
    Put<uint64_t>(image, 0x2308, kImageBase + 0x1100);

    const uint32_t ranges[][3] = {{0x1000, 0x1040, 0x3100}, {0x1100, 0x1140, 0x3108}, {0x1040, 0x1060, 0x3110}};
    for (size_t index = 0; index < 3; ++index) {
        for (size_t field = 0; field < 3; ++field) {
            Put<uint32_t>(image, kPdata + index * 12 + field * 4, ranges[index][field]);
        }
    }
    image[0x3100] = 1;
    image[0x3108] = 1;
    // Version 1, UNW_FLAG_CHAININFO, no codes: the parent range follows.
    image[0x3110] = 1 | 4 << 3;
    Put<uint32_t>(image, 0x3114, 0x1000);
    Put<uint32_t>(image, 0x3118, 0x1040);
    Put<uint32_t>(image, 0x311C, 0x3100);
    return image;
}

const char* kSmallPatterns =
    "# A comment, then the lines.\n"
    "FuncB code 40 53 48 83 EC 20 AA BB CC DD\n"
    "FuncB ref disp=1 next=5 to=code E8 ?? ?? ?? ?? C3 CC\n"
    "FuncA string hello_name lea=3 at=start 48 8D 0D ?? ?? ?? ??\n"
    "Chained code at=start 66 77 88 99 55\n"
    "Shifted code at=-2 11 22 33 44 90 CC\n"
    "Twice code 90 90 11 22 33 44 90\n"
    "Conflict code 40 53 48 83 EC 20 AA BB\n"
    "Conflict code at=1 40 53 48 83 EC 20 AA BB\n"
    "WrongKind ref disp=1 next=5 to=data E8 ?? ?? ?? ?? C3 CC\n"
    "FooVtable rtti .?AVFoo@@\n"
    "FooFirst rtti .?AVFoo@@ slot=0\n"
    "BarVtable rtti .?AVBar@@\n"
    "NoText string goodbye lea=3 48 8D 0D ?? ?? ?? ??\n";

const ga::Entry& EntryOf(const ga::Resolution& resolution, const char* name) {
    static const ga::Entry none{"", ga::Status::Missing, 0};
    const ga::Entry* entry = resolution.Find(name);
    return entry ? *entry : none;
}

bool FoundAt(const ga::Resolution& resolution, const char* name, uintptr_t rva) {
    const ga::Entry& entry = EntryOf(resolution, name);
    return entry.status == ga::Status::Found && entry.rva == rva;
}

bool Is(const ga::Resolution& resolution, const char* name, ga::Status status) {
    return EntryOf(resolution, name).status == status;
}

void TestSearch() {
    const std::vector<uint8_t> bytes = SmallImage();
    const ga::Image image{bytes.data(), bytes.size(), kImageBase};
    ga::Header header;
    CHECK(ga::ReadHeader(image, header));
    CHECK(header.timestamp == kTimestamp && header.imageSize == kImageSize && header.imageBase == kImageBase);

    const ga::Resolution found = ga::Search(image, kSmallPatterns);
    CHECK(!found.Verified());
    CHECK(found.entries.size() == 11);
    CHECK(found.entries.front().name == "FuncB");
    CHECK(FoundAt(found, "FuncB", 0x1100));
    CHECK(FoundAt(found, "FuncA", 0x1000));
    CHECK(FoundAt(found, "Chained", 0x1000));
    CHECK(FoundAt(found, "Shifted", 0x1200));
    CHECK(Is(found, "Twice", ga::Status::Missing));
    CHECK(Is(found, "Conflict", ga::Status::Conflict));
    CHECK(Is(found, "WrongKind", ga::Status::Missing));
    CHECK(FoundAt(found, "FooVtable", 0x2308));
    CHECK(FoundAt(found, "FooFirst", 0x1100));
    CHECK(Is(found, "BarVtable", ga::Status::Missing));
    CHECK(Is(found, "NoText", ga::Status::Missing));

    // Pointers count from where the image is: the same image read as loaded
    // elsewhere finds no vtable.
    const ga::Resolution moved = ga::Search({bytes.data(), bytes.size(), kImageBase + 0x10000}, kSmallPatterns);
    CHECK(Is(moved, "FooVtable", ga::Status::Missing));
    CHECK(FoundAt(moved, "FuncB", 0x1100));

    // A line that fails to parse is left out, and the others still count.
    const ga::Resolution partly = ga::Search(image, "FuncB code 40 53 ZZ\nFuncB code 40 53 48 83 EC 20 AA BB CC DD\n");
    CHECK(FoundAt(partly, "FuncB", 0x1100));
    CHECK(ga::Search({nullptr, 0, 0}, kSmallPatterns).entries.size() == 11);
}

void TestBuilds() {
    const std::vector<uint8_t> bytes = SmallImage();
    const ga::Image image{bytes.data(), bytes.size(), kImageBase};
    const std::string builds =
        "# Checked builds.\n"
        "build 0x12345678 size=0x5000 Same stamp, other size\n"
        "FuncB 0x9999\n"
        "build 0x12345678 size=0x4000 Test build 1.0\n"
        "FuncB 0x1100\n"
        "Twice none\n";
    ga::Header header;
    ga::ReadHeader(image, header);
    ga::Resolution checked;
    CHECK(ga::LookUp(header, kSmallPatterns, builds, checked));
    CHECK(checked.Verified() && checked.build == "Test build 1.0");
    CHECK(FoundAt(checked, "FuncB", 0x1100));
    CHECK(Is(checked, "Twice", ga::Status::Absent));
    CHECK(Is(checked, "FuncA", ga::Status::Missing));
    CHECK(checked.entries.size() == 11);
    CHECK(ga::Resolve(image, kSmallPatterns, builds).Verified());

    ga::Header other = header;
    other.imageSize = 0x6000;
    ga::Resolution unlisted;
    CHECK(!ga::LookUp(other, kSmallPatterns, builds, unlisted));
    CHECK(!ga::Resolve(image, kSmallPatterns, "").Verified());

    // A listed none agrees with a miss, a different address does not.
    const ga::Resolution searched = ga::Search(image, kSmallPatterns);
    const std::string differences = ga::Compare(checked, searched);
    CHECK(differences.find("Twice") == std::string::npos);
    CHECK(differences.find("FuncA 0x1000") == std::string::npos);
    CHECK(differences.find("FuncA missing 0x1000") != std::string::npos);
    CHECK(ga::Compare(searched, searched).empty());

    const std::string section = ga::BuildSection(searched, "New build");
    CHECK(section.rfind("build 0x12345678 size=0x4000 New build\n", 0) == 0);
    CHECK(section.find("\nFuncA 0x1000\n") != std::string::npos);
    CHECK(section.find("\n# Twice missing") != std::string::npos);
}

bool Holds(const std::vector<std::string>& names, std::initializer_list<const char*> wanted) {
    std::set<std::string> left(names.begin(), names.end());
    std::set<std::string> right(wanted.begin(), wanted.end());
    return left == right;
}

void TestReview() {
    const std::vector<uint8_t> bytes = SmallImage();
    const ga::Image image{bytes.data(), bytes.size(), kImageBase};
    // The newest listed build is the one compared with.
    const std::string builds =
        "build 0x00000002 size=0x4000 Newer\n"
        "FuncB 0x1100\n"
        "FuncA 0x1001\n"
        "Twice none\n"
        "build 0x00000001 size=0x4000 Older\n"
        "FuncB 0x9999\n";
    const ga::BuildReview review = ga::ReviewBuild(image, kSmallPatterns, builds, "New", nullptr, {"NoText"});
    CHECK(!review.listed && review.error.empty());
    CHECK(review.against == "Newer");
    CHECK(review.unchanged == 1 && review.moved == 1);
    // A miss the compared build lists as none, or one given as none, is none.
    CHECK(Holds(review.absent, {"Twice", "NoText"}));
    CHECK(Holds(review.blocking, {"Conflict", "WrongKind", "BarVtable"}));
    CHECK(review.partial.empty() && review.resized.empty());
    CHECK(review.section.empty());

    // One of FuncB's two lines finds nothing.
    const std::string patterns = "FuncB code 40 53 48 83 EC 20 AA BB CC DD\nFuncB code 99 98 97 96 95 94\n";
    const ga::BuildReview partial = ga::ReviewBuild(image, patterns, builds, "New");
    CHECK(Holds(partial.partial, {"FuncB"}) && partial.blocking.empty());
    CHECK(partial.section == "build 0x12345678 size=0x4000 New\nFuncB 0x1100\n");

    // A listed build is not reviewed, and previous must be listed.
    const std::string listed = "build 0x12345678 size=0x4000 Test\nFuncB 0x1100\n";
    CHECK(ga::ReviewBuild(image, patterns, listed, "New").listed);
    const ga::BuildReview unlisted = ga::ReviewBuild(image, patterns, builds, "New", &image);
    CHECK(!unlisted.listed && !unlisted.error.empty());

    // A previous build where FuncB's function was 0x10 bytes longer.
    std::vector<uint8_t> older = SmallImage();
    Put<uint32_t>(older, 0x80 + 8, 0x00000003);
    Put<uint32_t>(older, kPdata + 12 + 4, 0x1150);
    const ga::Image previous{older.data(), older.size(), kImageBase};
    const std::string withPrevious = builds + "build 0x00000003 size=0x4000 Previous\nFuncB 0x1100\n";
    const ga::BuildReview resized = ga::ReviewBuild(image, patterns, withPrevious, "New", &previous);
    CHECK(resized.error.empty() && resized.against == "Previous");
    // FuncB's second line finds nothing on previous either, so it is no news.
    CHECK(resized.partial.empty());
    // A line that found FuncB on previous and does not on the new build is.
    std::vector<uint8_t> changed = SmallImage();
    changed[0x1106] = 0xAB;
    const std::string both = "FuncB code 40 53 48 83 EC 20\nFuncB code 40 53 48 83 EC 20 AA BB\n";
    const ga::BuildReview lost =
        ga::ReviewBuild({changed.data(), changed.size(), kImageBase}, both, withPrevious, "New", &previous);
    CHECK(lost.error.empty() && Holds(lost.partial, {"FuncB"}));
    CHECK(resized.resized.size() == 1 && resized.resized[0].name == "FuncB" && resized.resized[0].before == 0x50 &&
          resized.resized[0].after == 0x40);
}

void TestFields() {
    std::vector<uint8_t> bytes = SmallImage();
    // mov rax,[rcx+0x210]; mov rax,[rcx+0x48]; mov cx,0x1234; mov rdx,[rcx-8].
    PutBytes(bytes, 0x1400, {0x48, 0x8B, 0x81, 0x10, 0x02, 0x00, 0x00, 0xC3});
    PutBytes(bytes, 0x1410, {0x48, 0x8B, 0x41, 0x48, 0xC3});
    PutBytes(bytes, 0x1420, {0x66, 0xB9, 0x34, 0x12, 0xC3});
    PutBytes(bytes, 0x1430, {0x48, 0x8B, 0x51, 0xF8, 0xC3});
    const ga::Image image{bytes.data(), bytes.size(), kImageBase};
    const std::string patterns =
        "FuncB code 40 53 48 83 EC 20 AA BB CC DD\n"
        "Wide field read=3 size=4 48 8B 81 ?? ?? ?? ?? C3\n"
        "Narrow field read=3 size=1 48 8B 41 ?? C3\n"
        "Half field read=2 size=2 66 B9 ?? ?? C3\n"
        "Negative field read=3 size=1 48 8B 51 ?? C3\n"
        "BadSize field read=3 size=3 48 8B 81 ?? ?? ?? ?? C3\n"
        "BadRead field read=6 size=4 48 8B 81 ?? ?? ?? ?? C3\n";
    const ga::Resolution found = ga::Search(image, patterns);
    CHECK(FoundAt(found, "Wide", 0x210) && EntryOf(found, "Wide").field);
    CHECK(FoundAt(found, "Narrow", 0x48));
    CHECK(FoundAt(found, "Half", 0x1234));
    CHECK(Is(found, "Negative", ga::Status::Missing));
    CHECK(!EntryOf(found, "FuncB").field);
    // A size other than 1, 2 or 4, or a value past the bytes, is no line.
    CHECK(!found.Find("BadSize") && !found.Find("BadRead"));

    // A checked build marks its fields too, and the review lists a field
    // whose value changed apart from the addresses.
    const std::string builds = "build 0x00000002 size=0x4000 Newer\nFuncB 0x1100\nWide 0x200\nNarrow 0x48\n";
    ga::Header header{0x00000002, kImageSize, kImageBase};
    ga::Resolution checked;
    CHECK(ga::LookUp(header, patterns, builds, checked) && EntryOf(checked, "Wide").field);
    const ga::BuildReview review = ga::ReviewBuild(image, patterns, builds, "New", nullptr, {"Negative"});
    CHECK(review.unchanged == 1 && review.moved == 0);
    CHECK(review.fields.size() == 1 && review.fields[0].name == "Wide" && review.fields[0].before == 0x200 &&
          review.fields[0].after == 0x210);
    CHECK(review.section.find("\nWide 0x210\n") != std::string::npos);

    // What a feature's field requirement logs: nothing when the build has
    // the compiled value, otherwise the reason it stays off.
    CHECK(ga::FieldProblem(checked, "Wide", 0x200).empty());
    CHECK(ga::FieldProblem(found, "Wide", 0x200) == "Game field Wide is 0x210 in this game build, not 0x200.");
    CHECK(ga::FieldProblem(found, "Negative", 0x10) == "Game field Negative was not found in this game build.");
    CHECK(ga::FieldProblem(found, "Unlisted", 0x10) == "Game field Unlisted was not found in this game build.");
    CHECK(ga::FieldProblem(ga::Resolution{}, "Wide", 0x200) == "Game field Wide was not found in this game build.");
}

void TestLayOut() {
    // A file whose sections sit elsewhere than their RVAs.
    const std::vector<uint8_t> laid = SmallImage();
    std::vector<uint8_t> file(laid.begin(), laid.begin() + 0x400);
    const uint32_t sections = 0x80 + 24 + 240;
    for (uint32_t index = 0; index < 3; ++index) {
        const uint32_t address = 0x1000 * (index + 1);
        Put<uint32_t>(file, sections + index * 40 + 20, static_cast<uint32_t>(file.size()));
        file.insert(file.end(), laid.begin() + address, laid.begin() + address + 0x1000);
    }
    std::vector<uint8_t> image;
    std::string error;
    CHECK(ga::LayOut(file, image, error));
    CHECK(image.size() == kImageSize);
    CHECK(FoundAt(ga::Search({image.data(), image.size(), kImageBase}, kSmallPatterns), "FuncA", 0x1000));
    CHECK(!ga::LayOut(std::vector<uint8_t>(16, 0), image, error) && !error.empty());
}

// =============================================================
// THE BUILT-IN FILES
// =============================================================
std::vector<std::string> Words(const std::string& line) {
    std::istringstream stream(line);
    std::vector<std::string> words;
    std::string word;
    while (stream >> word) {
        words.push_back(word);
    }
    return words;
}

std::vector<std::string> Lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line[0] != '#') {
            lines.push_back(line);
        }
    }
    return lines;
}

// Every name between mark and the next ")" or "," in game_retail.h.
std::set<std::string> NamesAfter(const std::string& header, const std::string& mark) {
    std::set<std::string> names;
    for (size_t at = header.find(mark); at != std::string::npos; at = header.find(mark, at + 1)) {
        const size_t begin = at + mark.size();
        names.insert(header.substr(begin, header.find_first_of("),", begin) - begin));
    }
    return names;
}

// The GAME_FIELD names of game_retail.h, with their compiled values.
std::map<std::string, uint64_t> DeclaredFields(const std::string& header) {
    std::map<std::string, uint64_t> fields;
    const std::string mark = "\nGAME_FIELD(";
    for (size_t at = header.find(mark); at != std::string::npos; at = header.find(mark, at + 1)) {
        const size_t begin = at + mark.size();
        const size_t comma = header.find(',', begin);
        fields[header.substr(begin, comma - begin)] = std::strtoull(header.c_str() + comma + 1, nullptr, 0);
    }
    return fields;
}

// Every GAME_ADDRESS and GAME_FIELD name.
std::set<std::string> DeclaredNames(const std::string& header) {
    std::set<std::string> names = NamesAfter(header, "\nGAME_ADDRESS(");
    for (const auto& field : DeclaredFields(header)) {
        names.insert(field.first);
    }
    return names;
}

void TestBuiltInFiles() {
    const std::string header = ReadText(kSource / "src" / "common" / "game_retail.h");
    const std::set<std::string> declared = DeclaredNames(header);
    const std::map<std::string, uint64_t> fields = DeclaredFields(header);
    if (!CHECK(declared.size() > 100) || !CHECK(fields.size() > 100)) {
        return;
    }
    // Field lines are exactly the GAME_FIELD names.
    std::set<std::string> fieldLines;
    for (const std::string& line : Lines(ga::PatternsText())) {
        const std::vector<std::string> words = Words(line);
        if (words[1] == "field") {
            fieldLines.insert(words[0]);
        }
    }
    CHECK(fieldLines.size() == fields.size());
    for (const std::string& name : fieldLines) {
        CHECK(fields.count(name) == 1);
    }
    std::set<std::string> patterned;
    for (const std::string& line : Lines(ga::PatternsText())) {
        patterned.insert(Words(line)[0]);
    }
    CHECK(patterned == declared);
    for (const std::string& name : declared) {
        if (!patterned.count(name)) {
            std::printf("  no pattern line for %s\n", name.c_str());
        }
    }

    // Every checked build lists every declared name, and nothing else.
    std::map<std::string, std::set<std::string>> builds;
    std::string current;
    for (const std::string& line : Lines(ga::BuildsText())) {
        const std::vector<std::string> words = Words(line);
        if (words[0] == "build") {
            current = line;
            builds[current];
        } else if (CHECK(!current.empty() && words.size() == 2)) {
            CHECK(builds[current].insert(words[0]).second);
            // Canopy is compiled for one layout, so a checked build with
            // another field value would leave the features using it off.
            const auto field = fields.find(words[0]);
            if (field != fields.end() && !CHECK(std::strtoull(words[1].c_str(), nullptr, 0) == field->second)) {
                std::printf("  %s lists %s as %s, not the compiled 0x%llX\n", current.c_str(), words[0].c_str(),
                            words[1].c_str(), static_cast<unsigned long long>(field->second));
            }
        }
    }
    CHECK(builds.size() >= 2);
    for (const auto& build : builds) {
        CHECK(build.second == declared);
        for (const std::string& name : declared) {
            if (!build.second.count(name)) {
                std::printf("  %s does not list %s\n", build.first.c_str(), name.c_str());
            }
        }
    }
}

// Each source file that uses a game address or field checks a list holding
// it, or the shared list the frame tick checks, before anything runs.
void TestAddressLists() {
    const std::string header = ReadText(kSource / "src" / "common" / "game_retail.h");
    std::set<std::string> declared = DeclaredNames(header);
    std::map<std::string, uint64_t> fields = DeclaredFields(header);
    // A GAME_FIELD_VIA constant is checked like a field, and stays a plain
    // constant no line reads.
    const std::set<std::string> vias = NamesAfter(header, "\nGAME_FIELD_VIA(");
    CHECK(vias.size() > 50);
    for (const std::string& name : vias) {
        CHECK(header.find("\nconstexpr size_t k" + name + " = ") != std::string::npos && !declared.count(name));
        declared.insert(name);
        fields[name];
    }
    // The requirement lists and the patch table, each by the names it holds:
    // a field's requirement k<Name>Field holds <Name>.
    std::map<std::string, std::set<std::string>> lists;
    for (const std::string mark : {"constexpr const Requirement* ", "constexpr CodePatch "}) {
        for (size_t at = header.find(mark); at != std::string::npos; at = header.find(mark, at + 1)) {
            const size_t begin = at + mark.size();
            const std::string name = header.substr(begin, header.find('[', begin) - begin);
            const size_t end = header.find("\n};", begin);
            for (size_t entry = header.find("&k", begin); entry < end; entry = header.find("&k", entry + 1)) {
                const size_t first = entry + 2;
                std::string held = header.substr(first, header.find(',', first) - first);
                const size_t suffix = held.size() - std::min<size_t>(held.size(), 5);
                if (held.compare(suffix, 5, "Field") == 0 && fields.count(held.substr(0, suffix))) {
                    held.resize(suffix);
                }
                lists[name].insert(held);
            }
        }
    }
    CHECK(lists.size() > 25);
    CHECK(lists["kLoadMenuPatches"].size() == 8);
    const std::set<std::string>& shared = lists["kSharedRequirements"];
    CHECK(!shared.empty());

    for (const char* folder : {"Canopy", "VillageQoL", "common"}) {
        for (const auto& item : fs::directory_iterator(kSource / "src" / folder)) {
            if (item.path().extension() != ".cpp") {
                continue;
            }
            const std::string text = ReadText(item.path());
            std::set<std::string> checked = shared;
            std::set<std::string> used;
            for (size_t at = text.find("retail::k"); at != std::string::npos; at = text.find("retail::k", at + 1)) {
                size_t end = at + 9;
                while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
                    ++end;
                }
                const std::string name = text.substr(at + 9, end - at - 9);
                const auto list = lists.find("k" + name);
                if (list != lists.end()) {
                    checked.insert(list->second.begin(), list->second.end());
                } else if (declared.count(name)) {
                    used.insert(name);
                }
            }
            for (const std::string& name : used) {
                if (!CHECK(checked.count(name) == 1)) {
                    std::printf("  %s uses %s, which no list it checks holds\n",
                                item.path().filename().u8string().c_str(), name.c_str());
                }
            }
        }
    }
}

// =============================================================
// THE SAMPLES
// =============================================================
template <typename T>
T Get(const std::vector<uint8_t>& image, size_t at) {
    T value;
    std::memcpy(&value, image.data() + at, sizeof(T));
    return value;
}

// The image as the loader leaves it at base: every 64-bit pointer the
// relocation table lists moved by the difference, and the header's
// ImageBase rewritten to base.
std::vector<uint8_t> Relocated(const std::vector<uint8_t>& laid, uint64_t base) {
    std::vector<uint8_t> image = laid;
    const uint32_t optional = Get<uint32_t>(image, 0x3C) + 24;
    const uint64_t delta = base - Get<uint64_t>(image, optional + 24);
    Put<uint64_t>(image, optional + 24, base);
    const uint32_t directory = optional + 112 + 5 * 8;
    const uint32_t begin = Get<uint32_t>(image, directory);
    const uint32_t end = begin + Get<uint32_t>(image, directory + 4);
    for (uint32_t block = begin; block + 8 <= end;) {
        const uint32_t page = Get<uint32_t>(image, block);
        const uint32_t size = Get<uint32_t>(image, block + 4);
        if (size < 8) {
            break;
        }
        for (uint32_t entry = block + 8; entry + 2 <= block + size; entry += 2) {
            const uint16_t value = Get<uint16_t>(image, entry);
            // IMAGE_REL_BASED_DIR64; the image has no other kind but padding.
            if (value >> 12 == 10) {
                const uint32_t at = page + (value & 0xFFF);
                Put<uint64_t>(image, at, Get<uint64_t>(image, at) + delta);
            }
        }
        block += size;
    }
    return image;
}

// True when rva lies in an executable section of the image.
bool InCode(const std::vector<uint8_t>& image, uint32_t rva) {
    const uint32_t pe = Get<uint32_t>(image, 0x3C);
    const uint16_t count = Get<uint16_t>(image, pe + 6);
    const uint32_t sections = pe + 24 + Get<uint16_t>(image, pe + 20);
    for (uint16_t index = 0; index < count; ++index) {
        const uint32_t entry = sections + index * 40u;
        const uint32_t address = Get<uint32_t>(image, entry + 12);
        if ((Get<uint32_t>(image, entry + 36) & 0x20000000) && rva >= address &&
            rva < address + Get<uint32_t>(image, entry + 8)) {
            return true;
        }
    }
    return false;
}

// When the file cannot be used, Canopy searches the loaded game, whose
// pointers are relocated and whose hooked functions start with a jump. A
// relocated image must give the checked addresses; one with a jump over
// every checked code address may miss some, but must find none wrong.
void TestLoadedImage(const char* name, const std::vector<uint8_t>& laid, const ga::Resolution& checked) {
    constexpr uint64_t kLoadedBase = 0x7FF612340000ull;
    std::vector<uint8_t> loaded = Relocated(laid, kLoadedBase);
    const ga::Image image{loaded.data(), loaded.size(), kLoadedBase};
    const std::string differences = ga::Compare(checked, ga::Search(image));
    if (!CHECK(differences.empty())) {
        std::printf("  %s relocated, name checked searched:\n%s", name, differences.c_str());
    }

    for (const ga::Entry& entry : checked.entries) {
        if (entry.status == ga::Status::Found && InCode(loaded, static_cast<uint32_t>(entry.rva))) {
            // A MinHook jump: E9 and a rel32 to its trampoline.
            PutBytes(loaded, entry.rva, {0xE9, 0x44, 0x33, 0x22, 0x11});
        }
    }
    const ga::Resolution hooked = ga::Search(image);
    size_t missed = 0;
    for (const ga::Entry& entry : hooked.entries) {
        const ga::Entry& expected = EntryOf(checked, entry.name.c_str());
        if (entry.status == ga::Status::Found) {
            if (!CHECK(expected.status == ga::Status::Found && expected.rva == entry.rva)) {
                std::printf("  %s hooked: %s found at 0x%llX\n", name, entry.name.c_str(),
                            static_cast<unsigned long long>(entry.rva));
            }
        } else if (expected.status == ga::Status::Found) {
            ++missed;
        }
    }
    std::printf("  %s with every checked code address hooked: %zu of %zu addresses missed\n", name, missed,
                checked.entries.size());
}

void TestSamples() {
    // Every "village *.exe" there; scripts/add_game_build.bat copies each
    // build it adds.
    const fs::path folder = kSource / "_work" / "bin";
    std::vector<fs::path> samples;
    std::error_code error;
    for (fs::directory_iterator item(folder, error), end; !error && item != end; item.increment(error)) {
        const std::string file = item->path().filename().u8string();
        if (item->is_regular_file() && file.rfind("village ", 0) == 0 && item->path().extension() == ".exe") {
            samples.push_back(item->path());
        }
    }
    std::sort(samples.begin(), samples.end());
    size_t tried = 0;
    for (const fs::path& path : samples) {
        const std::string fileName = path.filename().u8string();
        const char* name = fileName.c_str();
        const std::string text = ReadText(path);
        const std::vector<uint8_t> file(text.begin(), text.end());
        std::vector<uint8_t> laid;
        std::string error;
        if (!CHECK(ga::LayOut(file, laid, error))) {
            continue;
        }
        ga::Header header;
        ga::ReadHeader({laid.data(), laid.size(), 0}, header);
        const ga::Image image{laid.data(), laid.size(), header.imageBase};
        ga::Resolution checked;
        if (!ga::LookUp(header, checked)) {
            std::printf("  skipped %s, not a checked build\n", name);
            continue;
        }
        ++tried;
        const std::string differences = ga::Compare(checked, ga::Search(image));
        if (!CHECK(differences.empty())) {
            std::printf("  %s, name checked searched:\n%s", name, differences.c_str());
        }
        CHECK(checked.Count(ga::Status::Missing) == 0 && checked.Count(ga::Status::Conflict) == 0);
        TestLoadedImage(name, laid, checked);
    }
    if (tried == 0) {
        std::printf("  skipped the sample builds, none under _work/bin\n");
    }
}

}  // namespace

void TestAddresses(int& checks, int& failures) {
    g_checks = 0;
    g_failures = 0;
    TestSearch();
    TestBuilds();
    TestReview();
    TestFields();
    TestLayOut();
    TestBuiltInFiles();
    TestAddressLists();
    TestSamples();
    checks += g_checks;
    failures += g_failures;
}
