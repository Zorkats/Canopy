#pragma once
#include <string>

// canopy.ini as text that can be edited without losing anything: comments,
// blank lines, key order, spacing, line endings and lines Canopy does not
// read all stay as they were. Sections and keys are read the way
// canopy_ini.h reads them, so a key means here what it means to the loader.
// Each JSON result has "abi", "ok" and "error".
namespace ini_document {

// The structure of text, UTF-8: {"abi", "ok", "error", "eol", "lines":
// [{"line", "kind", "section", "key", "value"}]}. eol is "\r\n" or "\n",
// the one the text mostly uses. line counts from 1, as canopy_ini.h does.
// kind is "section", "value", "comment", "blank" or "other"; section is the
// section the line belongs to, empty before the first one.
std::string ParseJson(const std::string& text);

// text with one edit applied and every other byte kept: {"abi", "ok",
// "error", "text"}. op is one of:
//   "set"            key = value in section, replacing the first such key,
//                    or added after the section's last value; the section is
//                    added at the end when missing.
//   "remove"         the first key in section.
//   "addSection"     an empty section at the end, unless it exists.
//   "removeSection"  section, with its values and the comments inside it.
//   "renameSection"  section to value, keeping its values.
// Unused arguments are ignored. ok is false, with text unchanged, when the
// edit cannot be made, such as a key or value that would not read back, or a
// section or key that is not there. Names are matched without case.
// canopy_ini keeps the first of a repeated key and the first section of a
// name, so every edit acts on the first one. New lines use the line ending
// the text mostly uses, and a text without a final line break still has none
// afterwards. "set" on an existing key changes only its value, so the
// indentation, the spacing around '=' and anything after the value stay.
std::string EditJson(
    const std::string& text,
    const std::string& op,
    const std::string& section,
    const std::string& key,
    const std::string& value
);

}  // namespace ini_document
