#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "canopy_ini.h"
#include "ini_document.h"

// Checks of ini_document.h: every edit on small texts written here, the
// refusals, and two checks over the real canopy.ini files of the sample
// mods when CANOPY_TEST_MODS names a folder of them.
namespace {

namespace fs = std::filesystem;

int g_checks = 0;
int g_failures = 0;

#define CHECK(condition) Check((condition), #condition, __LINE__)

bool Check(bool passed, const char* text, int line) {
    ++g_checks;
    if (!passed) {
        ++g_failures;
        std::printf("  FAILED ini_document_tests.cpp line %d: %s\n", line, text);
    }
    return passed;
}

// ---- A small JSON reader for the results ----

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool flag = false;
    long number = 0;
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* Find(const char* name) const {
        for (const auto& member : members) {
            if (member.first == name) {
                return &member.second;
            }
        }
        return nullptr;
    }
    std::string Text(const char* name) const {
        const Json* found = Find(name);
        return found ? found->text : std::string();
    }
};

class Reader {
public:
    explicit Reader(const std::string& source) : source_(source) {}

    bool Read(Json& out) {
        return Value(out) && (Skip(), position_ == source_.size());
    }

private:
    void Skip() {
        while (position_ < source_.size() && (source_[position_] == ' ' || source_[position_] == '\n')) {
            ++position_;
        }
    }

    bool Literal(const char* word) {
        const size_t length = std::char_traits<char>::length(word);
        if (source_.compare(position_, length, word) != 0) {
            return false;
        }
        position_ += length;
        return true;
    }

    bool String(std::string& out) {
        if (position_ >= source_.size() || source_[position_] != '"') {
            return false;
        }
        ++position_;
        while (position_ < source_.size() && source_[position_] != '"') {
            char c = source_[position_++];
            if (c == '\\') {
                if (position_ >= source_.size()) {
                    return false;
                }
                c = source_[position_++];
                switch (c) {
                    case 'n':
                        c = '\n';
                        break;
                    case 'r':
                        c = '\r';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case 'u': {
                        if (position_ + 4 > source_.size()) {
                            return false;
                        }
                        c = static_cast<char>(std::strtol(source_.substr(position_, 4).c_str(), nullptr, 16));
                        position_ += 4;
                        break;
                    }
                    default:
                        break;
                }
            }
            out += c;
        }
        if (position_ >= source_.size()) {
            return false;
        }
        ++position_;
        return true;
    }

    bool Value(Json& out) {
        Skip();
        if (position_ >= source_.size()) {
            return false;
        }
        const char c = source_[position_];
        if (c == '"') {
            out.type = Json::Type::String;
            return String(out.text);
        }
        if (c == '{') {
            out.type = Json::Type::Object;
            ++position_;
            Skip();
            if (position_ < source_.size() && source_[position_] == '}') {
                ++position_;
                return true;
            }
            while (true) {
                Skip();
                std::string name;
                Json member;
                if (!String(name)) {
                    return false;
                }
                Skip();
                if (position_ >= source_.size() || source_[position_++] != ':' || !Value(member)) {
                    return false;
                }
                out.members.emplace_back(std::move(name), std::move(member));
                Skip();
                if (position_ >= source_.size()) {
                    return false;
                }
                const char next = source_[position_++];
                if (next == '}') {
                    return true;
                }
                if (next != ',') {
                    return false;
                }
            }
        }
        if (c == '[') {
            out.type = Json::Type::Array;
            ++position_;
            Skip();
            if (position_ < source_.size() && source_[position_] == ']') {
                ++position_;
                return true;
            }
            while (true) {
                Json item;
                if (!Value(item)) {
                    return false;
                }
                out.items.push_back(std::move(item));
                Skip();
                if (position_ >= source_.size()) {
                    return false;
                }
                const char next = source_[position_++];
                if (next == ']') {
                    return true;
                }
                if (next != ',') {
                    return false;
                }
            }
        }
        if (Literal("true")) {
            out.type = Json::Type::Bool;
            out.flag = true;
            return true;
        }
        if (Literal("false")) {
            out.type = Json::Type::Bool;
            return true;
        }
        if (Literal("null")) {
            return true;
        }
        char* end = nullptr;
        out.type = Json::Type::Number;
        out.number = std::strtol(source_.c_str() + position_, &end, 10);
        if (end == source_.c_str() + position_) {
            return false;
        }
        position_ = static_cast<size_t>(end - source_.c_str());
        return true;
    }

    const std::string& source_;
    size_t position_ = 0;
};

bool ReadJson(const std::string& source, Json& out) {
    Reader reader(source);
    return reader.Read(out);
}

// ---- Helpers ----

struct Outcome {
    bool ok = false;
    std::string text;
    std::string error;
};

Outcome Run(
    const std::string& text,
    const std::string& op,
    const std::string& section,
    const std::string& key,
    const std::string& value
) {
    Outcome outcome;
    Json json;
    const std::string result = ini_document::EditJson(text, op, section, key, value);
    if (!ReadJson(result, json)) {
        CHECK(false && "the edit result is JSON");
        return outcome;
    }
    const Json* ok = json.Find("ok");
    outcome.ok = ok && ok->flag;
    outcome.text = json.Text("text");
    outcome.error = json.Text("error");
    return outcome;
}

// The edit gives expected.
bool Expect(
    const std::string& text,
    const std::string& op,
    const std::string& section,
    const std::string& key,
    const std::string& value,
    const std::string& expected
) {
    const Outcome outcome = Run(text, op, section, key, value);
    const bool passed = outcome.ok && outcome.text == expected;
    if (!Check(passed, "an edit gives the expected text", __LINE__)) {
        std::printf("    op %s, got %s, text of %zu bytes, expected %zu bytes\n", op.c_str(),
                    outcome.ok ? "ok" : outcome.error.c_str(), outcome.text.size(), expected.size());
    }
    return passed;
}

// The edit is refused, with the text back unchanged and one sentence why.
bool Refuse(
    const std::string& text,
    const std::string& op,
    const std::string& section,
    const std::string& key,
    const std::string& value
) {
    const Outcome outcome = Run(text, op, section, key, value);
    const bool oneSentence = !outcome.error.empty() && outcome.error.back() == '.' &&
                             outcome.error.find(". ") == std::string::npos;
    const bool passed = !outcome.ok && outcome.text == text && oneSentence;
    if (!Check(passed, "an edit is refused with the text unchanged and one sentence", __LINE__)) {
        std::printf("    op %s, key length %zu, got %s\n", op.c_str(), key.size(),
                    outcome.ok ? "ok" : outcome.error.c_str());
    }
    return passed;
}

// text with every "\n" written as "\r\n".
std::string Crlf(const std::string& text) {
    std::string out;
    for (const char c : text) {
        if (c == '\n') {
            out += '\r';
        }
        out += c;
    }
    return out;
}

const std::string kBom = "\xEF\xBB\xBF";

// The structure of text, parsed.
Json Parse(const std::string& text) {
    Json json;
    CHECK(ReadJson(ini_document::ParseJson(text), json));
    return json;
}

// ---- Parsing ----

void TestParse() {
    std::printf("Ini document: parsing\n");
    const Json json = Parse(
        "; top\n"
        "stray = 1\n"
        "[mod]\n"
        "Name = Test = Mod  \n"
        "# note\n"
        "\n"
        "no equals here\n"
        "  [ other ]  \n"
        "k=v\n"
    );
    CHECK(json.Find("ok") && json.Find("ok")->flag);
    CHECK(json.Find("abi") && json.Find("abi")->number == 1);
    CHECK(json.Find("error") && json.Find("error")->type == Json::Type::Null);
    CHECK(json.Text("eol") == "\n");
    const Json* lines = json.Find("lines");
    CHECK(lines && lines->items.size() == 9);
    if (lines && lines->items.size() == 9) {
        const std::vector<Json>& items = lines->items;
        CHECK(items[0].Text("kind") == "comment" && items[0].Find("line")->number == 1);
        CHECK(items[1].Text("kind") == "other" && items[1].Text("section").empty());
        CHECK(items[2].Text("kind") == "section" && items[2].Text("section") == "mod");
        CHECK(items[3].Text("kind") == "value" && items[3].Text("key") == "name");
        CHECK(items[3].Text("value") == "Test = Mod" && items[3].Text("section") == "mod");
        CHECK(items[4].Text("kind") == "comment" && items[4].Text("section") == "mod");
        CHECK(items[5].Text("kind") == "blank");
        CHECK(items[6].Text("kind") == "other");
        CHECK(items[7].Text("kind") == "section" && items[7].Text("section") == "other");
        CHECK(items[8].Text("kind") == "value" && items[8].Text("section") == "other");
        CHECK(items[8].Find("line")->number == 9);
    }

    // The line ending is the one used most; a BOM is not part of line 1.
    CHECK(Parse("[a]\r\nk=v\r\nj=w\n").Text("eol") == "\r\n");
    CHECK(Parse("[a]\nk=v\r\n").Text("eol") == "\n");
    CHECK(Parse("").Text("eol") == "\n");
    const Json bom = Parse(kBom + "[a]\nk=v");
    CHECK(bom.Find("lines") && bom.Find("lines")->items.size() == 2);
    CHECK(bom.Find("lines")->items[0].Text("kind") == "section");
    CHECK(bom.Find("lines")->items[1].Text("value") == "v");
    // Text with a final line break has no extra blank line.
    CHECK(Parse("[a]\n").Find("lines")->items.size() == 1);
    CHECK(Parse("").Find("lines")->items.empty());
}

// The sections, keys, values and lines ParseJson reports are the ones
// canopy_ini reads from the same text.
bool AgreesWithLoader(const std::string& text) {
    Json json;
    if (!ReadJson(ini_document::ParseJson(text), json) || !json.Find("lines")) {
        return false;
    }
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(text);
    size_t sectionIndex = 0;
    size_t valueIndex = 0;
    bool haveSection = false;
    for (const Json& item : json.Find("lines")->items) {
        const std::string kind = item.Text("kind");
        const long line = item.Find("line")->number;
        if (kind == "section") {
            if (sectionIndex >= sections.size()) {
                return false;
            }
            const canopy_ini::IniSection& section = sections[sectionIndex];
            if (section.name != item.Text("section") || section.line != line) {
                return false;
            }
            ++sectionIndex;
            valueIndex = 0;
            haveSection = true;
        } else if (kind == "value") {
            if (!haveSection) {
                return false;
            }
            const canopy_ini::IniSection& section = sections[sectionIndex - 1];
            if (valueIndex >= section.values.size() || section.values[valueIndex].first != item.Text("key") ||
                section.values[valueIndex].second != item.Text("value") ||
                section.valueLines[valueIndex] != line || section.name != item.Text("section")) {
                return false;
            }
            ++valueIndex;
        } else if (kind == "other" && haveSection) {
            // A line in a section that holds no value: nothing to match.
            if (item.Text("key").size() || item.Text("value").size()) {
                return false;
            }
        }
        // The values of the section just read must all have been seen by the
        // time the next section starts.
    }
    if (sectionIndex != sections.size()) {
        return false;
    }
    size_t total = 0;
    for (const canopy_ini::IniSection& section : sections) {
        total += section.values.size();
    }
    size_t reported = 0;
    for (const Json& item : json.Find("lines")->items) {
        if (item.Text("kind") == "value") {
            ++reported;
        }
    }
    return total == reported;
}

void TestAgreement() {
    std::printf("Ini document: agreement with canopy_ini on awkward text\n");
    const std::vector<std::string> texts = {
        "",
        "\n",
        "[a]",
        "[a]\nk=v",
        "k=v\n[a]\nj=w\n",
        "[a] ; trailing\nk=v\n",
        "[ a ]\n  K  =  V  \n",
        "[]\n= x\n",
        "[a=b]\nk=1\n",
        "[a]\r\nk=v=w\r\n;c\r\n",
        kBom + "[a]\nk=1\n",
        "[a]\nk\n[b]\n\n\n",
        "[a]\rk=1\r",
        "[a]\n\tk\t=\tv\t\n",
        "[a]\n[a]\nk=1\nk=2\n",
        "[ ]\nk=1\n",
        "  # comment\n  ; comment\n[a]\n",
        "[a]\nk = [b]\n",
        "[a]\n[b]]\n[[c]\nx=1\n",
        "[a]\nk=v\n\n\n",
    };
    for (const std::string& text : texts) {
        if (!Check(AgreesWithLoader(text), "ParseJson agrees with canopy_ini", __LINE__)) {
            std::printf("    text of %zu bytes\n", text.size());
        }
        const std::string crlf = Crlf(text);
        CHECK(AgreesWithLoader(crlf));
    }
}

// ---- Editing ----

void TestSet() {
    std::printf("Ini document: set\n");
    // An existing key: LF and CRLF.
    Expect("[mod]\nname = A\nversion = 1\n", "set", "mod", "name", "B", "[mod]\nname = B\nversion = 1\n");
    Expect(Crlf("[mod]\nname = A\nversion = 1\n"), "set", "mod", "name", "B",
           Crlf("[mod]\nname = B\nversion = 1\n"));
    // The BOM and a missing final line break stay.
    Expect(kBom + "[a]\nx = 1", "set", "a", "x", "2", kBom + "[a]\nx = 2");
    // Indentation, spacing and what trails the value stay.
    Expect("[mod]\n  name   =   A   \n", "set", "mod", "name", "Bee", "[mod]\n  name   =   Bee   \n");
    Expect("[m]\nk=v\n", "set", "m", "k", "w", "[m]\nk=w\n");
    Expect("[m]\n\tk\t=\tv\n", "set", "m", "k", "w", "[m]\n\tk\t=\tw\n");
    // A value with ';' or '#' in it: canopy_ini keeps it whole, so it is replaced whole.
    Expect("[m]\nk = a ; b # c\n", "set", "m", "k", "z", "[m]\nk = z\n");
    // Names match without case; the file's own spelling stays.
    Expect("[M]\nKey = 1\n", "set", "m", "KEY", "2", "[M]\nKey = 2\n");
    // Empty values.
    Expect("[m]\nk =\n", "set", "m", "k", "x", "[m]\nk = x\n");
    Expect("[m]\nk=\n", "set", "m", "k", "x", "[m]\nk=x\n");
    Expect("[m]\nk = abc\n", "set", "m", "k", "", "[m]\nk = \n");
    // Other sections and lines are not touched.
    Expect("; head\n[a]\nx = 1\n\n[b]\nx = 2\n", "set", "b", "x", "9", "; head\n[a]\nx = 1\n\n[b]\nx = 9\n");

    // A new key goes after the section's last value, in the file's style.
    Expect("[a]\nx = 1\n\n[b]\ny = 2\n", "set", "a", "z", "3", "[a]\nx = 1\nz = 3\n\n[b]\ny = 2\n");
    Expect("[a]\nx=1\n", "set", "a", "y", "2", "[a]\nx=1\ny=2\n");
    Expect("[a]\nx   =  1\n", "set", "a", "y", "2", "[a]\nx   =  1\ny   =  2\n");
    Expect("[a]\nx = 1\n; trailing note\n\n[b]\n", "set", "a", "y", "2",
           "[a]\nx = 1\ny = 2\n; trailing note\n\n[b]\n");
    Expect("[a]\n; only a note\n[b]\n", "set", "a", "x", "1", "[a]\nx = 1\n; only a note\n[b]\n");
    Expect("[a]\nx = 1", "set", "a", "y", "2", "[a]\nx = 1\ny = 2");
    Expect(Crlf("[a]\nx = 1\n"), "set", "a", "y", "2", Crlf("[a]\nx = 1\ny = 2\n"));
    Expect(Crlf("[a]\nx = 1"), "set", "a", "y", "2", Crlf("[a]\nx = 1\ny = 2"));
    // The line ending the text mostly uses is the new line's.
    Expect("[a]\r\nx = 1\r\ny = 2\n", "set", "a", "z", "3", "[a]\r\nx = 1\r\ny = 2\nz = 3\r\n");
    // The first section of a name takes the new key, as canopy_ini reads only it.
    Expect("[a]\nx=1\n[a]\ny=2\n", "set", "a", "y", "3", "[a]\nx=1\ny=3\n[a]\ny=2\n");

    // A missing section is added at the end.
    Expect("[a]\nx = 1\n", "set", "b", "y", "2", "[a]\nx = 1\n[b]\ny = 2\n");
    Expect("[a]\nx = 1\n\n[b]\ny = 2\n", "set", "c", "z", "3", "[a]\nx = 1\n\n[b]\ny = 2\n\n[c]\nz = 3\n");
    Expect("[a]\nx = 1", "set", "b", "y", "2", "[a]\nx = 1\n[b]\ny = 2");
    Expect(Crlf("[a]\nx = 1\n"), "set", "b", "y", "2", Crlf("[a]\nx = 1\n[b]\ny = 2\n"));
    Expect("", "set", "a", "x", "1", "[a]\nx = 1\n");
    Expect(kBom, "set", "a", "x", "1", kBom + "[a]\nx = 1\n");
    Expect("; just a comment\n", "set", "a", "x", "", "; just a comment\n[a]\nx =\n");
    // A key line before any section does not count: the section is added.
    Expect("x = 1\n", "set", "a", "x", "2", "x = 1\n[a]\nx = 2\n");

    // The first of two equal keys is the one edited, as Value() reads it.
    Expect("[a]\nx = 1\nx = 2\n", "set", "a", "x", "9", "[a]\nx = 9\nx = 2\n");
    Expect("[a]\nx = 1\n[a]\nx = 2\n", "set", "a", "x", "5", "[a]\nx = 5\n[a]\nx = 2\n");
}

void TestIdentity() {
    std::printf("Ini document: setting a key to its own value\n");
    const std::vector<std::string> texts = {
        "[a]\nx = 1\n",
        "[a]\r\nx = 1\r\n",
        "[a]\r\nx = 1",
        kBom + "[a]\nx   =   1   \n",
        "[a]\nx =\n",
        "[a]\nx = \n",
        "[a]\n  X=Hello; world  \n\n; note\n[b]\n\ty\t=\t2\n",
    };
    for (const std::string& text : texts) {
        const Json json = Parse(text);
        for (const Json& item : json.Find("lines")->items) {
            if (item.Text("kind") != "value") {
                continue;
            }
            const Outcome outcome = Run(text, "set", item.Text("section"), item.Text("key"), item.Text("value"));
            CHECK(outcome.ok && outcome.text == text);
        }
    }
}

void TestRemove() {
    std::printf("Ini document: remove\n");
    Expect("[a]\nx = 1\ny = 2\n", "remove", "a", "x", "", "[a]\ny = 2\n");
    Expect("[a]\nx = 1\ny = 2\n", "remove", "A", "Y", "", "[a]\nx = 1\n");
    Expect(Crlf("[a]\nx = 1\ny = 2\n"), "remove", "a", "x", "", Crlf("[a]\ny = 2\n"));
    // Removing the last line, which has no line break, keeps it that way.
    Expect("[a]\nx = 1\ny = 2", "remove", "a", "y", "", "[a]\nx = 1");
    Expect(Crlf("[a]\nx = 1\ny = 2"), "remove", "a", "y", "", Crlf("[a]\nx = 1"));
    Expect(kBom + "[a]\nx = 1\n; c\n", "remove", "a", "x", "", kBom + "[a]\n; c\n");
    // The first of two equal keys goes.
    Expect("[a]\nx = 1\nx = 2\n", "remove", "a", "x", "", "[a]\nx = 2\n");
    // Adding a key and removing it again gives the text back.
    const std::string original = "[a]\nx = 1";
    const Outcome added = Run(original, "set", "a", "y", "2");
    CHECK(added.ok && Run(added.text, "remove", "a", "y", "").text == original);
    // What is not there cannot be removed.
    Refuse("[a]\nx = 1\n", "remove", "a", "nope", "");
    Refuse("[a]\nx = 1\n", "remove", "nope", "x", "");
    Refuse("x = 1\n[a]\n", "remove", "a", "x", "");
}

void TestAddSection() {
    std::printf("Ini document: addSection\n");
    Expect("[a]\nx = 1\n", "addSection", "b", "", "", "[a]\nx = 1\n[b]\n");
    Expect("[a]\nx = 1\n\n[b]\ny = 2\n", "addSection", "c", "", "", "[a]\nx = 1\n\n[b]\ny = 2\n\n[c]\n");
    Expect("[a]\nx = 1", "addSection", "b", "", "", "[a]\nx = 1\n[b]");
    Expect(Crlf("[a]\n"), "addSection", "b", "", "", Crlf("[a]\n[b]\n"));
    Expect("", "addSection", "b", "", "", "[b]\n");
    Expect(kBom, "addSection", "b", "", "", kBom + "[b]\n");
    // A section that exists, without case, is left as it is.
    Expect("[a]\nx = 1\n", "addSection", "A", "", "", "[a]\nx = 1\n");
    Expect("[a]\nx = 1", "addSection", "a", "", "", "[a]\nx = 1");
    // A new section after a blank last line does not add another.
    Expect("[a]\nx = 1\n\n", "addSection", "b", "", "", "[a]\nx = 1\n\n[b]\n");
}

void TestRemoveSection() {
    std::printf("Ini document: removeSection\n");
    // Its values and the comments inside it go, and the blank lines after.
    Expect("[a]\nx = 1\n; note\ny = 2\n\n[b]\nz = 3\n", "removeSection", "a", "", "", "[b]\nz = 3\n");
    // Comments after the last value belong to the next section.
    Expect("[a]\nx = 1\n\n; about b\n[b]\nz = 3\n", "removeSection", "a", "", "", "; about b\n[b]\nz = 3\n");
    // A comment above the header stays.
    Expect("; head\n[a]\nx=1\n", "removeSection", "a", "", "", "; head\n");
    Expect("[a]\nx = 1\n\n[b]\nz = 3\n", "removeSection", "b", "", "", "[a]\nx = 1\n\n");
    Expect("[a]\nx = 1\n\n[b]\nz = 3", "removeSection", "b", "", "", "[a]\nx = 1\n");
    Expect(Crlf("[a]\nx = 1\n[b]\nz = 3\n"), "removeSection", "A", "", "", Crlf("[b]\nz = 3\n"));
    Expect(kBom + "[a]\nx = 1\n", "removeSection", "a", "", "", kBom);
    Expect("[a]\n", "removeSection", "a", "", "", "");
    // A line the loader does not read, inside the section, goes with it.
    Expect("[a]\nx = 1\njunk\n[b]\n", "removeSection", "a", "", "", "[b]\n");
    // Only the first of two sections of a name.
    Expect("[a]\nx = 1\n[a]\nx = 2\n", "removeSection", "a", "", "", "[a]\nx = 2\n");
    Refuse("[a]\n", "removeSection", "nope", "", "");
}

void TestRenameSection() {
    std::printf("Ini document: renameSection\n");
    Expect("[a]\nx = 1\n\n[b]\n", "renameSection", "a", "", "c", "[c]\nx = 1\n\n[b]\n");
    Expect(Crlf("[a]\nx = 1\n"), "renameSection", "A", "", "c", Crlf("[c]\nx = 1\n"));
    Expect("  [ a ]  \n", "renameSection", "a", "", "c", "  [ c ]  \n");
    Expect("[a]", "renameSection", "a", "", "c", "[c]");
    Expect(kBom + "[a]\n", "renameSection", "a", "", "c", kBom + "[c]\n");
    // Only the case changes.
    Expect("[a]\n", "renameSection", "a", "", "A", "[A]\n");
    Expect("[a]\n[a]\n", "renameSection", "a", "", "c", "[c]\n[a]\n");
    Expect("[a]\n", "renameSection", "a", "", "a", "[a]\n");
    // The name is the new value; another section already has it, or none is there.
    Refuse("[a]\n[b]\n", "renameSection", "a", "", "b");
    Refuse("[a]\n", "renameSection", "nope", "", "c");
}

void TestRefusals() {
    std::printf("Ini document: edits that would not read back are refused\n");
    const std::string text = "[a]\nx = 1\n";
    // Key names.
    const std::vector<std::string> badKeys = {
        "", "a=b", "a]", "[a", "a\nb", "a\r", " a", "a ", "\ta", ";a", "#a", "=",
    };
    for (const std::string& key : badKeys) {
        Refuse(text, "set", "a", key, "v");
    }
    // Values.
    const std::vector<std::string> badValues = {"x\ny", "x\r", "\n", " x", "x ", "\tx"};
    for (const std::string& value : badValues) {
        Refuse(text, "set", "a", "k", value);
    }
    // Section names, when a section would be made or named.
    const std::vector<std::string> badSections = {"", "a=b", "a]", "[a", "a\nb", " a", "a ", "\ta"};
    for (const std::string& section : badSections) {
        Refuse(text, "set", section, "k", "v");
        Refuse(text, "addSection", section, "", "");
        Refuse(text, "renameSection", "a", "", section);
    }
    // Unknown operations.
    Refuse(text, "", "a", "x", "1");
    Refuse(text, "frob", "a", "x", "1");
    Refuse(text, "SET", "a", "x", "1");
    // Fine names are not refused.
    CHECK(Run(text, "set", "flag.Visited", "some key-name_1", "a;b#c=d [e]").ok);
    CHECK(Run(text, "set", "a", "k", "").ok);
    // The refused text reads back the same in the result.
    CHECK(Run(text, "set", "a", "a=b", "v").text == text);
}

// ---- Real files ----

bool ReadFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// ParseJson agrees with canopy_ini on text, and setting the first of each
// key of each section to its own value gives text back.
void CheckSampleText(const std::string& text, const std::string& label) {
    if (!Check(AgreesWithLoader(text), "a sample file reads the same as canopy_ini does", __LINE__)) {
        std::printf("    %s\n", label.c_str());
        return;
    }
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(text);
    for (const canopy_ini::IniSection& section : sections) {
        if (canopy_ini::FindSection(sections, section.name.c_str()) != &section) {
            continue;
        }
        std::vector<std::string> seen;
        for (const auto& entry : section.values) {
            bool repeated = false;
            for (const std::string& key : seen) {
                repeated = repeated || key == entry.first;
            }
            if (repeated) {
                continue;
            }
            seen.push_back(entry.first);
            const Outcome outcome = Run(text, "set", section.name, entry.first, entry.second);
            if (!Check(outcome.ok && outcome.text == text, "setting a key to its own value returns the file", __LINE__)) {
                std::printf("    %s, a key of %zu characters\n", label.c_str(), entry.first.size());
            }
        }
    }
}

void TestSampleFiles() {
    std::printf("Ini document: canopy.ini files of the sample mods\n");
    const char* folder = std::getenv("CANOPY_TEST_MODS");
    if (!folder || !*folder || !fs::is_directory(fs::path(folder))) {
        std::printf("  Note: CANOPY_TEST_MODS is not set to a folder, so the sample files are skipped.\n");
        return;
    }
    int files = 0;
    std::error_code error;
    for (fs::recursive_directory_iterator it(fs::path(folder), error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || it->path().extension() != ".ini") {
            continue;
        }
        std::string text;
        if (!ReadFile(it->path(), text)) {
            continue;
        }
        ++files;
        const std::string label = it->path().filename().string() + " #" + std::to_string(files);
        CheckSampleText(text, label);
        // The same file with Windows line endings and with a BOM.
        CheckSampleText(Crlf(text), label + " as CRLF");
        CheckSampleText(kBom + text, label + " with BOM");
        // A file with its last line break cut off.
        if (!text.empty() && text.back() == '\n') {
            CheckSampleText(text.substr(0, text.size() - 1), label + " without final line break");
        }
    }
    std::printf("  %d sample ini files read\n", files);
    if (files == 0) {
        std::printf("  Note: no .ini file was found under CANOPY_TEST_MODS.\n");
    }
}

}  // namespace

void TestIniDocument(int& checks, int& failures) {
    TestParse();
    TestAgreement();
    TestSet();
    TestIdentity();
    TestRemove();
    TestAddSection();
    TestRemoveSection();
    TestRenameSection();
    TestRefusals();
    TestSampleFiles();
    checks += g_checks;
    failures += g_failures;
}
