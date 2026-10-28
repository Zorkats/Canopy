#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "blob.h"

// One load hook for every game table. The game loads each CDatabase table
// through CDatabaseManager::load, which calls the table's own load through
// vtable slot 1 with the file's path and bytes. The engine detours the
// manager and points slot 1 of every table it is about to load at one
// function, which lets the modules that patch rows rewrite them and the
// modules that add rows append them to the file's bytes before the game
// reads them, and keeps the ids and enum names of the
// tables they watch for reference checks. See the design notes in
// table_engine.cpp.
namespace table_engine {

// Called on the thread that loads file (lowercase, "icon.dat"), a game
// worker, with the parsed table and an appender over it. Rows added to
// appender are merged into the bytes the game reads. db is the table's
// CDatabase.
using AppendFn = void (*)(const char* file, uint8_t* db, const blob::Table& table, blob::Appender& appender);

// Called on the thread that loads file (lowercase, "icon.dat") or its
// framework_ twin, with the parsed table as the rewrites registered before
// this one left it, before any rows are appended. Writing a whole new blob
// to rewritten replaces the table's bytes; leaving it empty keeps them. The
// new blob must parse, and is what the next rewrite gets and appended rows
// then go after.
using RewriteFn =
    void (*)(const char* file, bool framework, const blob::Table& table, std::vector<uint8_t>& rewritten);

// Creates the manager hook. Startup thread, from the game init detour,
// before the hooks are enabled. False, logged, when Canopy started after
// game init, on an unsupported game build or when the hook cannot be made.
// Runs once; later calls return the first answer.
bool Install(bool beforeGameInit);

// Registers append for file ("texture.dat"). Rows only ever go into the
// file itself, never into its framework_ twin. Several modules may
// contribute to one file: each append runs in the order they registered,
// over the same appender, so a later one sees the rows of an earlier one
// in appender but not in table. A contributed file is also watched.
// Startup thread, after Install, before the hooks are enabled.
void Contribute(const char* file, AppendFn append);
// Registers rewrite for file and its framework_ twin. Several modules may
// rewrite one file: each runs in the order they registered, on the blob the
// one before returned. A rewritten file is also watched. Startup thread,
// after Install, before the hooks are enabled.
void Rewrite(const char* file, RewriteFn rewrite);
// Keeps the ids and enum names of file and its framework_ twin once each
// loads. Startup thread, before the hooks are enabled.
void Watch(const char* file);

// ---- Any thread ----------------------------------------------------------

// True when file itself has loaded, with any rows a module added.
bool Known(const char* file);
// Waits up to milliseconds for Known(file). Workers only, never the main
// thread while it loads tables.
bool WaitKnown(const char* file, uint32_t milliseconds);
// Which manager load queued file, 1 for the framework batch inside game
// init and 2 for the game tables. 0 when it was never queued.
int Batch(const char* file);
// True when a watched file, its framework_ twin or a row a module added
// has id.
bool HasId(const char* file, uint64_t id);
// The id of a watched file's enum name, false when none has it.
bool EnumId(const char* file, const std::string& name, uint64_t& id);
// True when a watched file or its twin has name, or a name of its CRC.
bool HasEnum(const char* file, const std::string& name);
// The table's CDatabase once file or its twin has begun loading, null
// before.
uint8_t* Database(const char* file);
// True when the engine saw the framework batch and so every table.
bool Complete();

}  // namespace table_engine
