#include "json_out.h"

#include <cstdio>

#include "rules_report.h"

namespace json_out {

void PutString(std::string& out, const std::string& text) {
    out += '"';
    for (const char c : mod_rules::ToUtf8(text)) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if (byte < 0x20 || byte == 0x7F) {
            char escaped[8] = {};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
}

void PutOptional(std::string& out, const char* text) {
    if (text) {
        PutString(out, text);
    } else {
        out += "null";
    }
}

void PutKey(std::string& out, const char* key) {
    PutString(out, key);
    out += ':';
}

}  // namespace json_out
