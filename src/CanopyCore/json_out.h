#pragma once
#include <string>

// The few pieces the desktop JSON is written with. Strings go out as UTF-8,
// converted from the ANSI code page when they are not UTF-8 already.
namespace json_out {

// text as a JSON string, quotes included.
void PutString(std::string& out, const std::string& text);
// text as a JSON string, or null when text is null.
void PutOptional(std::string& out, const char* text);
// "key": ready for the value.
void PutKey(std::string& out, const char* key);

}  // namespace json_out
