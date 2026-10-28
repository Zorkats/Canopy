#pragma once

// canopy_core.dll (libcanopy_core.so on Linux): CanopyCore's checks for
// tools in other languages, such as Canopy Studio. Every function is safe
// to call from any thread; each call opens what it needs and keeps nothing.
// Every string passed in, folder paths included, is UTF-8. Strings handed
// out are UTF-8 JSON, owned by the caller, and freed with canopy_core_free.
// No function throws or returns null, except canopy_core_free.

#if defined(_WIN32)
#ifdef CANOPY_CORE_BUILD
#define CANOPY_CORE_API __declspec(dllexport)
#else
#define CANOPY_CORE_API __declspec(dllimport)
#endif
#elif defined(CANOPY_CORE_BUILD)
#define CANOPY_CORE_API __attribute__((visibility("default")))
#else
#define CANOPY_CORE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// The JSON layout's version, the "abi" field of every result.
CANOPY_CORE_API int canopy_core_abi(void);

// What every section kind of canopy.ini accepts:
// {"abi", "sections": [{"prefix", "title", "doc", "fields": [{"key",
// "kind", "required", "help", "default", "table", "min", "max",
// "choices"}]}]}.
CANOPY_CORE_API char* canopy_core_sections(void);

// Checks every folder of modsFolder the way Canopy does at startup. Either
// path may be null or empty, not both: without modsFolder the game's mods
// folder is checked, and without gameFolder vanilla ids are not.
// {"abi", "ok", "error", "game", "mods", "vanilla", "folders", "errors",
// "warnings", "notes", "diagnostics": [{"severity", "folder", "file",
// "line", "message"}]}. ok is false, with error set, when a folder could
// not be opened.
CANOPY_CORE_API char* canopy_core_validate(const char* gameFolder, const char* modsFolder);

// Checks one mod being made, in projectFolder, which may be outside the
// mods folder, with iniText as its canopy.ini, saved or not. The other mods
// in modsFolder (gameFolder\mods when null or empty) are read for the
// keys the project may name. The layout of canopy_core_validate, with only
// the project's diagnostics. See rules_project.h.
CANOPY_CORE_API char* canopy_core_validate_project(
    const char* gameFolder, const char* modsFolder, const char* projectFolder, const char* iniText
);

// What gameFolder holds, for a setup page: {"abi", "ok", "error",
// "folder", "exe", "build", "supported", "mods", "canopy", "log",
// "archives": [{"name", "version", "entries"}], "files", "tables"}.
// supported is true for the village.exe build Canopy is made for. The
// flags are filled in even when ok is false.
CANOPY_CORE_API char* canopy_core_game(const char* gameFolder);

// Each path the game's archives mount, once, with the copy the game
// reads: {"abi", "ok", "error", "files": [{"path", "size", "time",
// "archive", "copies"}]}.
CANOPY_CORE_API char* canopy_core_files(const char* gameFolder);

// The rows of data/database/<file>, file such as "item.dat" or "item":
// {"abi", "ok", "error", "file", "struct", "schema", "poolBytes",
// "rows": [{"index", "id", "enum", "size"}]}.
CANOPY_CORE_API char* canopy_core_table(const char* gameFolder, const char* file);

// One row of that table: {"abi", "ok", "error", "file", "index", "id",
// "enum", "size", "fields": [{"path", "type", "value", "table",
// "editable"}], "texts": [{"offset", "text"}], "hex"}. fields come from
// Canopy's schema and are empty without one, each value the text a
// [patch.] section takes, editable where a patch can set it; texts are
// guessed from the bytes.
CANOPY_CORE_API char* canopy_core_row(const char* gameFolder, const char* file, int index);

// Every row of that table under its schema's columns, for a grid:
// {"abi", "ok", "error", "file", "struct", "columns": [{"path", "type",
// "table", "editable"}], "rows": [{"index", "id", "enum", "fits",
// "values"}]}, values one per column, null where the row has no such
// place. ok is false for a table Canopy has no schema for.
CANOPY_CORE_API char* canopy_core_grid(const char* gameFolder, const char* file);

// The texture gamePath, such as "data/texture/x.nltx", decoded:
// {"abi", "ok", "error", "width", "height"}. When ok, *pixels holds
// width x height BGRA8 pixels, rows top first, freed with
// canopy_core_free_pixels; else it is set to null.
CANOPY_CORE_API char* canopy_core_texture(
    const char* gameFolder, const char* gamePath, unsigned char** pixels
);

// The file gamePath as the game reads it, such as "data/misc/x.ttf":
// {"abi", "ok", "error", "size"}. When ok, *bytes holds size bytes, freed
// with canopy_core_free_bytes; else it is set to null.
CANOPY_CORE_API char* canopy_core_file(const char* gameFolder, const char* gamePath, unsigned char** bytes);

// The cell of icon.dat row iconId cut out of its texture: {"abi", "ok",
// "error", "width", "height"}. When ok, *pixels holds width x height BGRA8
// pixels, rows top first, freed with canopy_core_free_pixels; else it is
// set to null.
CANOPY_CORE_API char* canopy_core_icon(const char* gameFolder, unsigned int iconId, unsigned char** pixels);

// The count icons in ids, each cut as canopy_core_icon cuts it and saved as
// outFolder\<id>.png with each texture decoded once: {"abi", "ok", "error",
// "written", "problems": ["sentence", ...]}. An icon that cannot be cut or
// saved is a problem and the rest go on.
CANOPY_CORE_API char* canopy_core_icons(
    const char* gameFolder, const unsigned int* ids, int count, const char* outFolder
);

// What a picker lists for kind ("item", "villager", "whistle", "craft",
// "cooking" or "area") in language (a canopy.ini code such as "en"):
// {"abi", "ok", "error", "kind", "language", "entries": [{"id", "enum",
// "name", "icon", "group"}]}.
CANOPY_CORE_API char* canopy_core_names(const char* gameFolder, const char* kind, const char* language);

// The sound gamePath, such as "data/bgm/x.nlsd", as the Ogg Vorbis stream
// inside it: {"abi", "ok", "error", "size"}. When ok, *bytes holds size
// bytes, freed with canopy_core_free_bytes; else it is set to null.
CANOPY_CORE_API char* canopy_core_sound(const char* gameFolder, const char* gamePath, unsigned char** bytes);

// The structure of canopy.ini text: {"abi", "ok", "error", "eol", "lines":
// [{"line", "kind", "section", "key", "value"}]}. See ini_document.h.
CANOPY_CORE_API char* canopy_core_ini_parse(const char* text);

// text with one edit applied and every other byte kept: {"abi", "ok",
// "error", "text"}. op is "set", "remove", "addSection", "removeSection"
// or "renameSection"; see ini_document.h. Null arguments read as empty.
CANOPY_CORE_API char* canopy_core_ini_edit(
    const char* text, const char* op, const char* section, const char* key, const char* value
);

// Frees a string from this DLL. Null is fine.
CANOPY_CORE_API void canopy_core_free(char* text);
// Frees pixels from canopy_core_texture or canopy_core_icon. Null is fine.
CANOPY_CORE_API void canopy_core_free_pixels(unsigned char* pixels);
// Frees bytes from canopy_core_file or canopy_core_sound. Null is fine.
CANOPY_CORE_API void canopy_core_free_bytes(unsigned char* bytes);

#ifdef __cplusplus
}
#endif
