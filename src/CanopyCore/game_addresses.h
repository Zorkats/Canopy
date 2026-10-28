#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Finds the game addresses Canopy and its mods use in a village.exe image.
// A build listed in game_builds.txt takes its checked addresses from there;
// any other build is searched with the patterns of game_addresses.txt. Both
// files are built in. Nothing here reads memory outside the image it is
// given, so the loader runs it on the live game and the tools on a file.
namespace game_addresses {

// A PE image laid out at its RVAs, the way the Windows loader maps it.
struct Image {
    const uint8_t* bytes = nullptr;
    size_t size = 0;
    // What the image's absolute pointers count from: the load address of a
    // mapped image, the header's ImageBase for one laid out from a file.
    uint64_t pointerBase = 0;
};

struct Header {
    uint32_t timestamp = 0;
    uint32_t imageSize = 0;
    uint64_t imageBase = 0;
};

// The PE header of an image laid out at its RVAs. False when it is not a
// PE32+ image.
bool ReadHeader(const Image& image, Header& header);

// Lays a village.exe file out at its RVAs, as the loader would before
// relocating it. Returns false and an error on a file that is not PE32+.
bool LayOut(const std::vector<uint8_t>& file, std::vector<uint8_t>& image, std::string& error);

enum class Status : uint8_t {
    // At rva.
    Found,
    // The build is known to have no such site.
    Absent,
    // No pattern found it.
    Missing,
    // Patterns found it at more than one place, so none is trusted.
    Conflict,
};

struct Entry {
    std::string name;
    Status status = Status::Missing;
    // For a field, its value.
    uintptr_t rva = 0;
    // A field line names it: an offset or size of a game layout, not an
    // address.
    bool field = false;
    // For a search: the name's pattern lines, and how many found anything.
    uint16_t lines = 0;
    uint16_t hits = 0;
};

struct Resolution {
    Header header;
    // The game_builds.txt label of a checked build, empty for a searched one.
    std::string build;
    // One entry per name of game_addresses.txt, in its order.
    std::vector<Entry> entries;

    bool Verified() const { return !build.empty(); }
    const Entry* Find(const std::string& name) const;
    size_t Count(Status status) const;
};

// The checked addresses of the image's build, or else a search.
Resolution Resolve(const Image& image);
// Always searches, for checking the patterns against a known build.
Resolution Search(const Image& image);
// The checked addresses of the build the header names. False when
// game_builds.txt does not list it.
bool LookUp(const Header& header, Resolution& resolution);
// True when game_builds.txt lists the build.
bool IsChecked(const Header& header);

// The same against given file texts instead of the built-in ones.
Resolution Resolve(const Image& image, const std::string& patterns, const std::string& builds);
Resolution Search(const Image& image, const std::string& patterns);
bool LookUp(const Header& header, const std::string& patterns, const std::string& builds, Resolution& resolution);

// A game_builds.txt section for the image's build, from a resolution whose
// entries were all found or absent.
std::string BuildSection(const Resolution& resolution, const std::string& label);

// Names whose entries differ between two resolutions of the same image,
// with both values, one per line. Empty when they agree. An absent entry
// agrees with a missing one, since a search cannot prove a site absent.
std::string Compare(const Resolution& left, const Resolution& right);

// Why the resolution's field name does not have the compiled value, one line
// for the log. Empty when it has.
std::string FieldProblem(const Resolution& resolution, const std::string& name, uint64_t compiled);

// What adding an unchecked build to game_builds.txt rests on, for the
// person who reviews it.
struct BuildReview {
    // Why there is no review: the builds text lists the build already, or
    // previous is not a build it lists.
    std::string error;
    bool listed = false;
    Resolution searched;
    // The listed build the search is compared with: previous's, or else the
    // newest.
    std::string against;
    size_t moved = 0;
    size_t unchanged = 0;
    // Missing names the compared build lists as none, or given as none,
    // written as none.
    std::vector<std::string> absent;
    // Names nothing found or whose lines disagree. Any of them stops the
    // build from being added.
    std::vector<std::string> blocking;
    // Names only some of their lines found: the code around them changed.
    // With previous, only those fewer lines found than on previous, since a
    // line kept for an older build finds nothing on newer ones.
    std::vector<std::string> partial;
    // Code names whose function has another length than in previous.
    struct Resized {
        std::string name;
        uint64_t before = 0;
        uint64_t after = 0;
    };
    std::vector<Resized> resized;
    // Fields whose value differs from the compared build: that layout
    // changed, so what uses them stays off until game_retail.h has the new
    // value.
    std::vector<Resized> fields;
    // The section to add, empty while blocking is not.
    std::string section;
};

// Searches image for a new game_builds.txt section labelled label, and
// compares it with a listed build: previous, when given, must be one, and
// adds a check of function lengths. Names in none were checked by hand and
// the build has no such site: a miss of one is written as none.
BuildReview ReviewBuild(
    const Image& image, const std::string& patterns, const std::string& builds, const std::string& label,
    const Image* previous = nullptr, const std::vector<std::string>& none = {}
);

// The built-in file texts.
const std::string& PatternsText();
const std::string& BuildsText();

}  // namespace game_addresses
