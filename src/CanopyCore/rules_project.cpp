#include "rules_project.h"

#include <algorithm>
#include <utility>

#include "canopy_ini.h"
#include "platform_files.h"
#include "rules_report.h"

// =============================================================
// RULES PROJECT
// =============================================================
// The check of one mod project as Create runs it: Validate's check of a
// mods folder, with the project put in place of its folder. The project's
// files are read from where it lives, through ModFolder::dir, and its
// canopy.ini is the editor's text. The loader never calls this.

namespace mod_rules {
namespace {

// path without its trailing slashes, except a drive root's.
std::wstring WithoutTrailingSlash(std::wstring path) {
    while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/') && path[path.size() - 2] != L':') {
        path.pop_back();
    }
    return path;
}

// The last part of path, the folder's own name.
std::string LastPart(const std::string& path) {
    const size_t slash = path.find_last_of("\\/:");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Fills validation the way Validate does, with the project in the folder
// list. False, with validation.error set, when nothing could be checked.
// modsGiven is true when the caller named the mods folder, which must then
// exist; the game's own mods folder may not exist yet.
bool Check(
    Validation& validation, bool modsGiven, const std::wstring& projectFolder, const std::string& iniText,
    std::string& projectName
) {
    if (projectFolder.empty()) {
        validation.error = "Give the project's folder.";
        return false;
    }
    const std::wstring project = WithoutTrailingSlash(projectFolder);
    if (!platform::IsFolder(project)) {
        validation.error = "The project folder " + ToUtf8(project) + " is not a folder.";
        return false;
    }
    const std::string projectDir = ToAnsi(project);
    if (projectDir.empty()) {
        validation.error =
            "The project folder's path has characters the game cannot open files with. Move the project to a "
            "folder whose path has only plain letters.";
        return false;
    }
    projectName = LastPart(projectDir);
    if (projectName.empty()) {
        validation.error = "The project folder " + ToUtf8(project) + " has no name of its own.";
        return false;
    }
    if (!validation.gameFolder.empty()) {
        validation.vanilla = std::make_unique<VanillaData>();
        std::string error;
        if (!validation.vanilla->Open(validation.gameFolder, error)) {
            validation.error = "The game folder could not be opened: " + error;
            return false;
        }
    }

    Context& context = validation.context;
    // The other mods.
    if (!validation.modsFolder.empty()) {
        if (platform::IsFolder(validation.modsFolder)) {
            context.modsDir = ToAnsi(validation.modsFolder);
            if (context.modsDir.empty()) {
                validation.error = "The mods folder's path has characters the game cannot open files with.";
                return false;
            }
            context.folders = ScanMods(context.modsDir);
        } else if (modsGiven) {
            validation.error = "The mods folder " + ToUtf8(validation.modsFolder) + " is not a folder.";
            return false;
        }
    }
    // A folder of the project's name stands for the project: the loader
    // would read one of the two, and the user is editing this one.
    context.folders.erase(
        std::remove_if(context.folders.begin(), context.folders.end(), [&](const ModFolder& folder) {
            return platform::CompareNoCase(folder.name.c_str(), projectName.c_str()) == 0;
        }),
        context.folders.end()
    );
    ModFolder folder;
    folder.name = projectName;
    folder.dir = projectDir;
    folder.hasIni = true;
    folder.sections = canopy_ini::ParseIni(iniText);
    context.folders.push_back(std::move(folder));
    // ScanMods' order, which is the loader's: the first folder owns a key.
    std::sort(context.folders.begin(), context.folders.end(), [](const ModFolder& left, const ModFolder& right) {
        return platform::CompareNoCase(left.name.c_str(), right.name.c_str()) < 0;
    });

    context.vanilla = validation.vanilla.get();
    CheckAll(context);
    return true;
}

}  // namespace

std::string ValidateProjectJson(
    const std::wstring& gameFolder,
    const std::wstring& modsFolder,
    const std::wstring& projectFolder,
    const std::string& iniText
) {
    Validation validation;
    validation.gameFolder = gameFolder;
    validation.modsFolder = modsFolder.empty() && !gameFolder.empty() ? platform::Join(gameFolder, L"mods") : modsFolder;
    std::string projectName;
    validation.ok = Check(validation, !modsFolder.empty(), projectFolder, iniText, projectName);
    if (!validation.ok) {
        return ValidationJson(validation);
    }
    // Only the project's own messages. Folder names come from the ANSI file
    // APIs, so each message is made UTF-8 for the JSON.
    Report& report = validation.context.report;
    Report mine;
    for (const Diagnostic& diagnostic : report.Items()) {
        if (platform::CompareNoCase(diagnostic.folder.c_str(), projectName.c_str()) == 0) {
            mine.Add(diagnostic.severity, ToUtf8(diagnostic.folder), diagnostic.line, ToUtf8(diagnostic.message));
        }
    }
    report = std::move(mine);
    return ValidationJson(validation);
}

}  // namespace mod_rules
