#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "blob.h"

// A read-only look at a game install for the desktop tools: what the folder
// holds, the files its archives mount, the rows of its tables and its
// textures and files. Nothing here writes a file. Each JSON result has "abi", "ok" and
// "error"; ok is false, with error saying why, when nothing could be read.
namespace game_browse {

// What gameFolder holds, for a setup page:
// {"abi", "ok", "error", "folder", "exe", "build", "supported", "mods",
// "canopy", "log", "archives": [{"name", "version", "entries"}], "files",
// "tables"}. exe: village.exe is there. build: its PE TimeDateStamp, 0 when
// it cannot be read. supported: build is the one Canopy is made for. mods,
// canopy and log: the mods folder, dinput8.dll and Canopy.log are there.
// files and tables: the paths the archives mount, and those under
// data/database/ ending in .dat. ok is false when the archives cannot be
// opened; the flags are filled in either way.
std::string GameJson(const std::wstring& gameFolder);

// Each path the archives mount, once, in name order, with the copy the game
// reads: {"abi", "ok", "error", "files": [{"path", "size", "time",
// "archive", "copies"}]}. time is Unix seconds; copies counts the archives
// that hold the path, the one read included.
std::string FilesJson(const std::wstring& gameFolder);

// The rows of data/database/<file>: {"abi", "ok", "error", "file",
// "struct", "schema", "poolBytes", "rows": [{"index", "id", "enum",
// "size"}]}. schema is true when Canopy describes the table's fields, with
// a schema table_schema::FindPatchable gives, or its table's for a
// framework_ twin; struct is then the game's record name, else null. id is
// the u64 at +0. enum is the row's enum name, null when it has none Canopy
// can find.
std::string TableJson(const std::wstring& gameFolder, const std::string& file);

// Row index of data/database/<file>: {"abi", "ok", "error", "file", "index",
// "id", "enum", "size", "fields": [{"path", "type", "value", "table",
// "editable"}], "texts": [{"offset", "text"}], "hex"}. fields is empty for
// a table without a schema; with one, it lists every place of the row as
// table_schema::Layout does (drawInfo.0.x, name.en, effects.count), each
// value the text table_schema::Read gives, which is the text a
// [patch.<table>.<id>] section takes. type is id, enum, text, path, i32,
// u32, f32, bool, u64, ref or flag; table is the file a ref points into.
// editable is true where a patch can set the place. A row the schema does
// not describe lists what can be read of it, none editable. texts lists
// each 8 bytes that read as a ref to a whole text of the pool: a guess for
// tables without a schema, so a tool must call them that. hex is the row's
// bytes.
std::string RowJson(const std::wstring& gameFolder, const std::string& file, size_t index);

// Every row of data/database/<file> under its schema's columns, for a grid:
// {"abi", "ok", "error", "file", "struct", "columns": [{"path", "type",
// "table", "editable"}], "rows": [{"index", "id", "enum", "fits",
// "values"}]}. A column is a place RowJson lists, named and typed the same;
// a row with longer vectors than the others adds columns for its elements,
// placed after the place before them in that row. values holds one entry
// per column: the value as RowJson gives it, or null where the row has no
// such place. fits is false, with every value null, for a row the schema
// does not describe. A column is editable when a patch can set it in every
// row that has it. ok is false for a table Canopy has no schema for.
std::string GridJson(const std::wstring& gameFolder, const std::string& file);

// TableJson, RowJson and GridJson for a table already parsed.
std::string TableJsonOf(const blob::Table& table, const std::string& file);
std::string RowJsonOf(const blob::Table& table, const std::string& file, size_t index);
std::string GridJsonOf(const blob::Table& table, const std::string& file);

// The texture at gamePath, an .nltx the archives mount, as BGRA8 pixels.
bool Texture(
    const std::wstring& gameFolder, const std::string& gamePath, uint32_t& width, uint32_t& height,
    std::vector<uint8_t>& pixels, std::string& error
);

// The file at gamePath as the game reads it, YKCMP packing undone.
bool File(
    const std::wstring& gameFolder, const std::string& gamePath, std::vector<uint8_t>& bytes, std::string& error
);

// The JSON for a call that failed before it read anything.
std::string FailureJson(const std::string& error);

}  // namespace game_browse
