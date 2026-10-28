#include "rules_report.h"

#include <algorithm>

#include "disabled_mods.h"
#include "json_out.h"
#include "platform_files.h"
#include "table_schema.h"

// =============================================================
// RULES REPORT
// =============================================================
// A desktop check of a whole mods folder and its JSON. The loader never
// calls this: it runs each kind's reader as it installs.

namespace mod_rules {
namespace {

using json_out::PutKey;
using json_out::PutOptional;
using json_out::PutString;

const char* KindName(ValueKind kind) {
    switch (kind) {
        case ValueKind::Text:
            return "text";
        case ValueKind::LangText:
            return "lang-text";
        case ValueKind::Integer:
            return "integer";
        case ValueKind::Number:
            return "number";
        case ValueKind::Switch:
            return "switch";
        case ValueKind::Choice:
            return "choice";
        case ValueKind::List:
            return "list";
        case ValueKind::Path:
            return "path";
        case ValueKind::VanillaId:
            return "vanilla-id";
        case ValueKind::FlagRef:
            return "flag-ref";
        case ValueKind::ItemRef:
            return "item-ref";
        case ValueKind::IconRef:
            return "icon-ref";
        case ValueKind::TextureRef:
            return "texture-ref";
        case ValueKind::SkinRef:
            return "skin-ref";
        case ValueKind::ObjectRef:
            return "object-ref";
        case ValueKind::Melody:
            return "melody";
        case ValueKind::Other:
            return "other";
    }
    return "other";
}

}  // namespace

const char* SeverityName(Severity severity) {
    switch (severity) {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warning";
        case Severity::Note:
            return "note";
    }
    return "error";
}

std::string ToUtf8(const std::string& text) {
    if (platform::IsUtf8(text)) {
        return text;
    }
    return ToUtf8(platform::AnsiToWide(text));
}

std::string ToUtf8(const std::wstring& text) {
    return platform::WideToUtf8(text);
}

std::string ToAnsi(const std::wstring& text) {
    std::string ansi;
    return platform::WideToAnsi(text, ansi) ? ansi : std::string();
}

Validation Validate(const std::wstring& gameFolder, const std::wstring& modsFolder) {
    Validation validation;
    validation.gameFolder = gameFolder;
    validation.modsFolder = modsFolder.empty() && !gameFolder.empty() ? platform::Join(gameFolder, L"mods") : modsFolder;
    if (validation.modsFolder.empty()) {
        validation.error = "Give a game folder, a mods folder, or both.";
        return validation;
    }
    if (!gameFolder.empty()) {
        validation.vanilla = std::make_unique<VanillaData>();
        std::string error;
        if (!validation.vanilla->Open(gameFolder, error)) {
            validation.error = "The game folder could not be opened: " + error;
            return validation;
        }
    }
    if (!platform::IsFolder(validation.modsFolder)) {
        validation.error = "The mods folder " + ToUtf8(validation.modsFolder) + " is not a folder.";
        return validation;
    }
    Context& context = validation.context;
    context.modsDir = ToAnsi(validation.modsFolder);
    if (context.modsDir.empty()) {
        validation.error = "The mods folder's path has characters the game cannot open files with.";
        return validation;
    }
    context.folders = ScanMods(context.modsDir);
    // ScanMods left out what mods\CanopyData\disabled.txt turns off. Say so,
    // so a missing folder in the result is not a mystery.
    for (const std::string& name : disabled_mods::Existing(context.modsDir)) {
        const bool isDll = name.size() > 4 && platform::CompareNoCase(name.c_str() + name.size() - 4, ".dll") == 0;
        context.report.Note(
            isDll ? std::string() : name, 0,
            "Mod " + name + " is turned off in mods\\CanopyData\\disabled.txt, so it is not checked."
        );
    }
    context.vanilla = validation.vanilla.get();
    CheckAll(context);
    validation.ok = true;
    return validation;
}

std::string ValidationJson(const Validation& validation) {
    const Context& context = validation.context;
    const Report& report = context.report;
    std::string out = "{";
    PutKey(out, "abi");
    out += std::to_string(kJsonAbi) + ",";
    PutKey(out, "ok");
    out += validation.ok ? "true," : "false,";
    PutKey(out, "error");
    PutOptional(out, validation.ok ? nullptr : validation.error.c_str());
    out += ',';
    PutKey(out, "game");
    PutString(out, ToUtf8(validation.gameFolder));
    out += ',';
    PutKey(out, "mods");
    PutString(out, ToUtf8(validation.modsFolder));
    out += ',';
    PutKey(out, "vanilla");
    out += validation.vanilla && validation.vanilla->IsOpen() ? "true," : "false,";
    PutKey(out, "folders");
    out += '[';
    for (size_t index = 0; index < context.folders.size(); ++index) {
        if (index > 0) {
            out += ',';
        }
        PutString(out, context.folders[index].name);
    }
    out += "],";
    PutKey(out, "errors");
    out += std::to_string(report.Count(Severity::Error)) + ",";
    PutKey(out, "warnings");
    out += std::to_string(report.Count(Severity::Warning)) + ",";
    PutKey(out, "notes");
    out += std::to_string(report.Count(Severity::Note)) + ",";
    PutKey(out, "diagnostics");
    out += '[';
    bool first = true;
    for (const Diagnostic& diagnostic : report.Items()) {
        out += first ? "{" : ",{";
        first = false;
        PutKey(out, "severity");
        PutString(out, SeverityName(diagnostic.severity));
        out += ',';
        PutKey(out, "folder");
        PutOptional(out, diagnostic.folder.empty() ? nullptr : diagnostic.folder.c_str());
        out += ',';
        PutKey(out, "file");
        PutOptional(out, diagnostic.folder.empty() ? nullptr : "canopy.ini");
        out += ',';
        PutKey(out, "line");
        out += std::to_string(diagnostic.line) + ",";
        PutKey(out, "message");
        PutString(out, diagnostic.message);
        out += '}';
    }
    out += "]}";
    return out;
}

std::string SectionsJson() {
    std::string out = "{";
    PutKey(out, "abi");
    out += std::to_string(kJsonAbi) + ",";
    PutKey(out, "sections");
    out += '[';
    bool firstSection = true;
    for (const SectionInfo* section : Sections()) {
        out += firstSection ? "{" : ",{";
        firstSection = false;
        PutKey(out, "prefix");
        PutString(out, section->prefix);
        out += ',';
        PutKey(out, "title");
        PutString(out, section->title);
        out += ',';
        PutKey(out, "doc");
        PutOptional(out, section->doc);
        out += ',';
        PutKey(out, "fields");
        out += '[';
        for (size_t index = 0; index < section->fieldCount; ++index) {
            const FieldInfo& field = section->fields[index];
            out += index == 0 ? "{" : ",{";
            PutKey(out, "key");
            PutString(out, field.key);
            out += ',';
            PutKey(out, "kind");
            PutString(out, KindName(field.kind));
            out += ',';
            PutKey(out, "required");
            out += field.required ? "true," : "false,";
            PutKey(out, "help");
            PutOptional(out, field.help);
            out += ',';
            PutKey(out, "default");
            PutOptional(out, field.defaultValue);
            out += ',';
            PutKey(out, "table");
            PutOptional(out, field.table);
            out += ',';
            const bool ranged = field.min < field.max;
            PutKey(out, "min");
            out += ranged ? std::to_string(field.min) + "," : "null,";
            PutKey(out, "max");
            out += ranged ? std::to_string(field.max) + "," : "null,";
            PutKey(out, "choices");
            out += '[';
            if (field.choices) {
                const std::string choices = field.choices;
                size_t start = 0;
                while (start <= choices.size()) {
                    size_t end = choices.find('|', start);
                    if (end == std::string::npos) {
                        end = choices.size();
                    }
                    if (start > 0) {
                        out += ',';
                    }
                    PutString(out, choices.substr(start, end - start));
                    start = end + 1;
                }
            }
            out += "]}";
        }
        out += "]}";
    }
    out += "],";
    // The tables a [patch.] section can name, by file.
    PutKey(out, "tables");
    out += '[';
    bool firstTable = true;
    for (const table_schema::TableSchema* schema : table_schema::Patchable()) {
        if (!firstTable) {
            out += ',';
        }
        firstTable = false;
        PutString(out, schema->file);
    }
    out += "]}";
    return out;
}

}  // namespace mod_rules
