#include "ini_document.h"

#include <cstring>

#include "canopy_ini.h"
#include "json_out.h"
#include "rules_report.h"

// =============================================================
// INI DOCUMENT
// =============================================================
// canopy.ini as lines of text. Every line keeps the bytes it was written
// with; an edit rewrites or inserts whole lines and leaves the rest alone.
// A line means here what canopy_ini.cpp makes of it: the same trimming, the
// same section test, the same first '=' and the same comment marks. Targets
// are found with canopy_ini itself, so the first section of a name and the
// first key of a section are the ones edited, which is what Value() reads.

namespace ini_document {
namespace {

using json_out::PutKey;
using json_out::PutOptional;
using json_out::PutString;

enum class Kind { Section, Value, Comment, Blank, Other };

const char* KindName(Kind kind) {
    switch (kind) {
        case Kind::Section:
            return "section";
        case Kind::Value:
            return "value";
        case Kind::Comment:
            return "comment";
        case Kind::Blank:
            return "blank";
        default:
            return "other";
    }
}

// One line of the text, as byte positions in it. The terminator is not part
// of the content: "\n", "\r\n" or nothing for a last line without one.
struct Line {
    size_t start = 0;
    size_t length = 0;
    size_t eolLength = 0;
    Kind kind = Kind::Other;
    // The section the line is in; the line's own name for a section line.
    std::string section;
    // For a value line, the key lowercased and the value, trimmed.
    std::string key;
    std::string value;

    size_t End() const {
        return start + length + eolLength;
    }
};

struct Document {
    const std::string* text = nullptr;
    // 3 when the text starts with a UTF-8 BOM, else 0.
    size_t bom = 0;
    std::vector<Line> lines;
    // The line ending the text mostly uses, "\n" when it uses none.
    std::string eol = "\n";

    std::string Content(const Line& line) const {
        return text->substr(line.start, line.length);
    }
};

// The characters canopy_ini::Trim removes.
bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Reads the line the way canopy_ini::ParseIni does and fills in its kind.
// haveSection tells whether a header came before, current is that header's
// name.
void Classify(const std::string& content, bool& haveSection, std::string& current, Line& line) {
    const std::string trimmed = canopy_ini::Trim(content);
    line.section = current;
    if (trimmed.empty()) {
        line.kind = Kind::Blank;
    } else if (trimmed[0] == ';' || trimmed[0] == '#') {
        line.kind = Kind::Comment;
    } else if (trimmed.front() == '[' && trimmed.back() == ']') {
        line.kind = Kind::Section;
        current = canopy_ini::Trim(trimmed.substr(1, trimmed.size() - 2));
        line.section = current;
        haveSection = true;
    } else {
        const size_t equals = trimmed.find('=');
        if (equals == std::string::npos || !haveSection) {
            line.kind = Kind::Other;
        } else {
            line.kind = Kind::Value;
            line.key = canopy_ini::Lower(canopy_ini::Trim(trimmed.substr(0, equals)));
            line.value = canopy_ini::Trim(trimmed.substr(equals + 1));
        }
    }
}

Document Load(const std::string& text) {
    Document document;
    document.text = &text;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        document.bom = 3;
    }
    size_t crlf = 0;
    size_t lf = 0;
    bool haveSection = false;
    std::string current;
    size_t position = document.bom;
    while (position < text.size()) {
        Line line;
        line.start = position;
        const size_t newline = text.find('\n', position);
        if (newline == std::string::npos) {
            line.length = text.size() - position;
            position = text.size();
        } else {
            line.length = newline - position;
            line.eolLength = 1;
            if (line.length > 0 && text[newline - 1] == '\r') {
                --line.length;
                line.eolLength = 2;
                ++crlf;
            } else {
                ++lf;
            }
            position = newline + 1;
        }
        Classify(text.substr(line.start, line.length), haveSection, current, line);
        document.lines.push_back(std::move(line));
    }
    if (crlf > lf) {
        document.eol = "\r\n";
    }
    return document;
}

// ---- JSON ----

void Open(std::string& out, bool ok, const std::string& error) {
    out = "{";
    PutKey(out, "abi");
    out += std::to_string(mod_rules::kJsonAbi) + ",";
    PutKey(out, "ok");
    out += ok ? "true," : "false,";
    PutKey(out, "error");
    PutOptional(out, ok ? nullptr : error.c_str());
}

std::string EditResult(bool ok, const std::string& text, const std::string& error) {
    std::string out;
    Open(out, ok, error);
    out += ',';
    PutKey(out, "text");
    PutString(out, text);
    out += '}';
    return out;
}

// ---- Names ----

// The reason name cannot be written as a key or a section name and read back
// as itself, or an empty string when it can. what is "key" or "section".
std::string NameProblem(const std::string& name, const char* what) {
    const std::string label = std::string("A ") + what + " name";
    if (name.empty()) {
        return label + " cannot be empty, so give it at least one character.";
    }
    if (name.find_first_of("\r\n") != std::string::npos) {
        return label + " cannot contain a line break, so write it on one line.";
    }
    if (canopy_ini::Trim(name) != name) {
        return label + " cannot start or end with spaces because Canopy ignores them, so remove them.";
    }
    if (name.find_first_of("=[]") != std::string::npos) {
        return label + " cannot contain '=', '[' or ']' because Canopy would read it differently, so use other characters.";
    }
    if (std::strcmp(what, "key") == 0 && (name[0] == ';' || name[0] == '#')) {
        return "A key name cannot start with ';' or '#' because Canopy would read the line as a comment, so use another first character.";
    }
    return std::string();
}

std::string ValueProblem(const std::string& value) {
    if (value.find_first_of("\r\n") != std::string::npos) {
        return "A value cannot contain a line break, so write it on one line.";
    }
    if (canopy_ini::Trim(value) != value) {
        return "A value cannot start or end with spaces because Canopy ignores them, so remove them.";
    }
    return std::string();
}

// ---- Editing ----

struct Edit {
    bool ok = false;
    std::string text;
    std::string error;
};

Edit Fail(const std::string& error) {
    Edit edit;
    edit.error = error;
    return edit;
}

Edit Done(std::string text) {
    Edit edit;
    edit.ok = true;
    edit.text = std::move(text);
    return edit;
}

// The spacing a key line is written with.
struct Style {
    std::string before = " ";
    std::string after = " ";
};

// The key line for key and value in style. An empty value leaves no spacing
// after the '=', so the line does not end in spaces.
std::string KeyLine(const std::string& key, const std::string& value, const Style& style) {
    return key + style.before + "=" + (value.empty() ? std::string() : style.after + value);
}

// The spacing around the '=' of content, a value line with a value.
bool StyleOf(const std::string& content, Style& style) {
    const size_t equals = content.find('=');
    if (equals == std::string::npos) {
        return false;
    }
    size_t first = 0;
    while (first < equals && IsSpace(content[first])) {
        ++first;
    }
    size_t keyEnd = equals;
    while (keyEnd > first && IsSpace(content[keyEnd - 1])) {
        --keyEnd;
    }
    if (keyEnd == first) {
        return false;
    }
    size_t valueStart = equals + 1;
    while (valueStart < content.size() && IsSpace(content[valueStart])) {
        ++valueStart;
    }
    if (valueStart >= content.size()) {
        return false;
    }
    style.before = content.substr(keyEnd, equals - keyEnd);
    style.after = content.substr(equals + 1, valueStart - equals - 1);
    return true;
}

// The index of the line a 1-based canopy_ini line number names, or -1.
long IndexOf(const Document& document, int lineNumber) {
    if (lineNumber < 1 || static_cast<size_t>(lineNumber) > document.lines.size()) {
        return -1;
    }
    return lineNumber - 1;
}

// The index of the last line of the block that starts at line header: up to
// the line before the next section header, or the last line.
long BlockEnd(const Document& document, const std::vector<canopy_ini::IniSection>& sections, size_t sectionIndex) {
    if (sectionIndex + 1 < sections.size()) {
        return static_cast<long>(sections[sectionIndex + 1].line) - 2;
    }
    return static_cast<long>(document.lines.size()) - 1;
}

// text with lines added after the line at index after (-1 for the start of
// an empty text). The new lines use the text's line ending. When the text
// does not end in one, the added lines do not either, so it still does not.
std::string InsertAfter(const Document& document, long after, const std::vector<std::string>& add) {
    const std::string& text = *document.text;
    if (document.lines.empty() || after < 0) {
        std::string added;
        for (const std::string& line : add) {
            added += line + document.eol;
        }
        std::string out = text;
        out.insert(document.bom, added);
        return out;
    }
    const Line& last = document.lines[static_cast<size_t>(after)];
    std::string added;
    if (last.eolLength == 0) {
        for (const std::string& line : add) {
            added += document.eol + line;
        }
    } else {
        for (const std::string& line : add) {
            added += line + document.eol;
        }
    }
    std::string out = text;
    out.insert(last.End(), added);
    return out;
}

// text without lines first to last. A removed last line that has no
// terminator takes the one before it along, so the text still ends the way
// the modder left it.
std::string RemoveLines(const Document& document, long first, long last) {
    const std::vector<Line>& lines = document.lines;
    size_t from = lines[static_cast<size_t>(first)].start;
    const size_t to = lines[static_cast<size_t>(last)].End();
    if (lines[static_cast<size_t>(last)].eolLength == 0 && first > 0) {
        from -= lines[static_cast<size_t>(first) - 1].eolLength;
    }
    std::string out = *document.text;
    out.erase(from, to - from);
    return out;
}

// Whether each section after the first has a blank line before its header,
// so a new section can be set apart the same way. False when there is not
// enough to tell.
bool SectionsSetApart(const Document& document, const std::vector<canopy_ini::IniSection>& sections) {
    if (sections.size() < 2) {
        return false;
    }
    for (size_t index = 1; index < sections.size(); ++index) {
        const long header = IndexOf(document, sections[index].line);
        if (header < 1 || document.lines[static_cast<size_t>(header) - 1].kind != Kind::Blank) {
            return false;
        }
    }
    return true;
}

// text with a new section header, and key lines after it, at the end.
std::string AppendSection(
    const Document& document,
    const std::vector<canopy_ini::IniSection>& sections,
    const std::string& name,
    const std::vector<std::string>& keyLines
) {
    std::vector<std::string> add;
    const bool lastIsBlank = !document.lines.empty() && document.lines.back().kind == Kind::Blank;
    if (!document.lines.empty() && !lastIsBlank && SectionsSetApart(document, sections)) {
        add.push_back(std::string());
    }
    add.push_back("[" + name + "]");
    for (const std::string& line : keyLines) {
        add.push_back(line);
    }
    return InsertAfter(document, static_cast<long>(document.lines.size()) - 1, add);
}

// The index in sections of the first one named name, without case, or -1.
long FindIndex(const std::vector<canopy_ini::IniSection>& sections, const std::string& name) {
    const canopy_ini::IniSection* found = canopy_ini::FindSection(sections, name.c_str());
    return found ? static_cast<long>(found - sections.data()) : -1;
}

Edit SetValue(
    const Document& document,
    const std::string& section,
    const std::string& key,
    const std::string& value
) {
    std::string problem = NameProblem(key, "key");
    if (problem.empty()) {
        problem = ValueProblem(value);
    }
    if (!problem.empty()) {
        return Fail(problem);
    }
    const std::string& text = *document.text;
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(text);
    const long sectionIndex = FindIndex(sections, section);
    if (sectionIndex < 0) {
        problem = NameProblem(section, "section");
        if (!problem.empty()) {
            return Fail(problem);
        }
        return Done(AppendSection(document, sections, section, {KeyLine(key, value, Style())}));
    }
    const canopy_ini::IniSection& found = sections[static_cast<size_t>(sectionIndex)];
    const std::string wanted = canopy_ini::Lower(key);
    for (size_t index = 0; index < found.values.size() && index < found.valueLines.size(); ++index) {
        if (found.values[index].first != wanted) {
            continue;
        }
        const long at = IndexOf(document, found.valueLines[index]);
        if (at < 0) {
            break;
        }
        // Only the value changes: the indentation, the key, the spacing and
        // whatever trails the value stay as written.
        const Line& line = document.lines[static_cast<size_t>(at)];
        const std::string content = document.Content(line);
        const size_t equals = content.find('=');
        size_t valueStart = equals + 1;
        while (valueStart < content.size() && IsSpace(content[valueStart])) {
            ++valueStart;
        }
        size_t valueEnd = content.size();
        while (valueEnd > valueStart && IsSpace(content[valueEnd - 1])) {
            --valueEnd;
        }
        std::string spacing;
        if (valueStart == equals + 1 && valueStart == valueEnd && !value.empty()) {
            // "key =" with nothing after it: a space keeps "key = value".
            size_t keyEnd = equals;
            while (keyEnd > 0 && IsSpace(content[keyEnd - 1])) {
                --keyEnd;
            }
            if (keyEnd < equals) {
                spacing = " ";
            }
        }
        const std::string rewritten = content.substr(0, valueStart) + spacing + value + content.substr(valueEnd);
        std::string out = text;
        out.replace(line.start, line.length, rewritten);
        return Done(std::move(out));
    }
    // A new key goes after the section's last value, or after its header.
    const long header = IndexOf(document, found.line);
    if (header < 0) {
        return Fail("The section could not be found in the text, so nothing was changed.");
    }
    const long end = BlockEnd(document, sections, static_cast<size_t>(sectionIndex));
    long after = header;
    Style style;
    for (long index = header + 1; index <= end; ++index) {
        const Line& line = document.lines[static_cast<size_t>(index)];
        if (line.kind != Kind::Value) {
            continue;
        }
        after = index;
        StyleOf(document.Content(line), style);
    }
    return Done(InsertAfter(document, after, {KeyLine(key, value, style)}));
}

Edit RemoveKey(const Document& document, const std::string& section, const std::string& key) {
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(*document.text);
    const canopy_ini::IniSection* found = canopy_ini::FindSection(sections, section.c_str());
    if (!found) {
        return Fail("The section '" + section + "' is not in the file, so no key was removed.");
    }
    const std::string wanted = canopy_ini::Lower(key);
    for (size_t index = 0; index < found->values.size() && index < found->valueLines.size(); ++index) {
        if (found->values[index].first == wanted) {
            const long at = IndexOf(document, found->valueLines[index]);
            if (at >= 0) {
                return Done(RemoveLines(document, at, at));
            }
        }
    }
    return Fail("The key '" + key + "' is not in the section '" + section + "', so nothing was removed.");
}

Edit AddSection(const Document& document, const std::string& section) {
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(*document.text);
    if (FindIndex(sections, section) >= 0) {
        return Done(*document.text);
    }
    const std::string problem = NameProblem(section, "section");
    if (!problem.empty()) {
        return Fail(problem);
    }
    return Done(AppendSection(document, sections, section, {}));
}

Edit RemoveSection(const Document& document, const std::string& section) {
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(*document.text);
    const long sectionIndex = FindIndex(sections, section);
    if (sectionIndex < 0) {
        return Fail("The section '" + section + "' is not in the file, so nothing was removed.");
    }
    const long first = IndexOf(document, sections[static_cast<size_t>(sectionIndex)].line);
    if (first < 0) {
        return Fail("The section could not be found in the text, so nothing was changed.");
    }
    const long end = BlockEnd(document, sections, static_cast<size_t>(sectionIndex));
    // The section ends at its last value or unread line. Comments after that
    // belong to the next section, but blank lines after it go with it.
    long last = first;
    for (long index = first + 1; index <= end; ++index) {
        const Kind kind = document.lines[static_cast<size_t>(index)].kind;
        if (kind == Kind::Value || kind == Kind::Other) {
            last = index;
        }
    }
    while (last + 1 <= end && document.lines[static_cast<size_t>(last) + 1].kind == Kind::Blank) {
        ++last;
    }
    return Done(RemoveLines(document, first, last));
}

Edit RenameSection(const Document& document, const std::string& section, const std::string& name) {
    const std::vector<canopy_ini::IniSection> sections = canopy_ini::ParseIni(*document.text);
    const long sectionIndex = FindIndex(sections, section);
    if (sectionIndex < 0) {
        return Fail("The section '" + section + "' is not in the file, so it was not renamed.");
    }
    const std::string problem = NameProblem(name, "section");
    if (!problem.empty()) {
        return Fail(problem);
    }
    const long taken = FindIndex(sections, name);
    if (taken >= 0 && taken != sectionIndex) {
        return Fail("A section named '" + name + "' is already in the file, so choose another name.");
    }
    const long at = IndexOf(document, sections[static_cast<size_t>(sectionIndex)].line);
    if (at < 0) {
        return Fail("The section could not be found in the text, so nothing was changed.");
    }
    // Only the name changes: the spaces inside the brackets and around the
    // header stay as written.
    const Line& line = document.lines[static_cast<size_t>(at)];
    const std::string content = document.Content(line);
    const size_t open = content.find('[');
    const size_t close = content.rfind(']');
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return Fail("The section header could not be read, so nothing was changed.");
    }
    const std::string inner = content.substr(open + 1, close - open - 1);
    size_t lead = 0;
    while (lead < inner.size() && IsSpace(inner[lead])) {
        ++lead;
    }
    size_t trail = 0;
    while (lead + trail < inner.size() && IsSpace(inner[inner.size() - 1 - trail])) {
        ++trail;
    }
    const std::string rewritten = content.substr(0, open + 1) + inner.substr(0, lead) + name +
                                  inner.substr(inner.size() - trail) + content.substr(close);
    std::string out = *document.text;
    out.replace(line.start, line.length, rewritten);
    return Done(std::move(out));
}

}  // namespace

std::string ParseJson(const std::string& text) {
    const Document document = Load(text);
    std::string out;
    Open(out, true, std::string());
    out += ',';
    PutKey(out, "eol");
    PutString(out, document.eol);
    out += ',';
    PutKey(out, "lines");
    out += '[';
    for (size_t index = 0; index < document.lines.size(); ++index) {
        const Line& line = document.lines[index];
        if (index > 0) {
            out += ',';
        }
        out += '{';
        PutKey(out, "line");
        out += std::to_string(index + 1) + ',';
        PutKey(out, "kind");
        PutString(out, KindName(line.kind));
        out += ',';
        PutKey(out, "section");
        PutString(out, line.section);
        out += ',';
        PutKey(out, "key");
        PutString(out, line.key);
        out += ',';
        PutKey(out, "value");
        PutString(out, line.value);
        out += '}';
    }
    out += "]}";
    return out;
}

std::string EditJson(
    const std::string& text,
    const std::string& op,
    const std::string& section,
    const std::string& key,
    const std::string& value
) {
    const Document document = Load(text);
    Edit edit;
    if (op == "set") {
        edit = SetValue(document, section, key, value);
    } else if (op == "remove") {
        edit = RemoveKey(document, section, key);
    } else if (op == "addSection") {
        edit = AddSection(document, section);
    } else if (op == "removeSection") {
        edit = RemoveSection(document, section);
    } else if (op == "renameSection") {
        edit = RenameSection(document, section, value);
    } else {
        edit = Fail("The edit '" + op + "' is not known, so use set, remove, addSection, removeSection or renameSection.");
    }
    return EditResult(edit.ok, edit.ok ? edit.text : text, edit.error);
}

}  // namespace ini_document
