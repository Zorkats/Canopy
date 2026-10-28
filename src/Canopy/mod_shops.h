#pragma once

// Mod shops: keeps the shop list entries of mod sales, and of mod items,
// out of the game's save, and puts them back after the write and after a
// load. The sales themselves are storesales rows that mod_rows appends. See
// the design notes in mod_shops.cpp and docs/SHOPS.md.
namespace mod_shops {

// Joins the overlay save when needed is true: mod_rows took a [sale.] row,
// or there are mod items, which a patched vanilla sale may sell. Startup
// thread, after mod_items and mod_rows. The caller must have the frame tick
// and the per-save data. False, with nothing hooked, when not needed or the
// game build does not match.
bool Install(bool ticking, bool saveData, bool needed);

}  // namespace mod_shops
