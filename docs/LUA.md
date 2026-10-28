# Lua mods

A Lua mod is a folder with a `lua\main.lua` in it. It needs no DLL and no compiler: Canopy runs it inside the game's own Lua engine, gives it settings on the Mod Settings page, keeps its values with each save, and calls it on game events and every frame. By default it runs in a sandbox that cannot change the story, quests or the save.

New to Canopy? The DLL guide is [MODDING.md](MODDING.md), and the C API is [API.md](API.md). This page is everything about Lua mods.

## Contents

- [Quick path](#quick-path)
- [Folder layout](#folder-layout)
- [canopy.ini](#canopyini)
- [How a Lua mod runs](#how-a-lua-mod-runs)
- [Events](#events)
- [API](#api)
- [The sandbox](#the-sandbox)
- [The guard](#the-guard)
- [Unsafe mods](#unsafe-mods)
- [Errors and limits](#errors-and-limits)
- [What the log says](#what-the-log-says)
- [Known limitations](#known-limitations)

## Quick path

1. Make a folder in the game's `mods` folder, such as `mods\HelloLua`.
2. Put a `lua\main.lua` in it:

   ```lua
   canopy.log("Hello from Lua.")

   canopy.on("save_loaded", function()
     local loads = canopy.save.get("loads", 0) + 1
     canopy.save.set("loads", loads)
     canopy.log("This save was loaded " .. loads .. " times.")
   end)
   ```

3. Start the game and open `Canopy.log` next to `village.exe`. It shows `[HelloLua] Hello from Lua.` once the game has started, and the count each time you load a save.
4. To add settings, a name or a version, add a [canopy.ini](#canopyini).

Restart the game after every change: Canopy reads Lua mods once, as the game starts.

**Editing in Canopy Studio.** Open the mod in Studio's Create area and pick the **Lua** tab. It lists the mod's Lua files and adds one by its `require` name (`util.text` makes `lua\util\text.lua`). The editor colours Lua and completes the API on this page: type `canopy.` or press Ctrl+Space. It offers the guarded commands only when the mod is [unsafe](#unsafe-mods). Lua files are saved with `canopy.ini`, by Save or Ctrl+S.

## Folder layout

```text
Village in the Shade\
└── mods\
    └── HelloLua\                the mod folder
        ├── canopy.ini           optional: name, version, settings
        ├── lua\
        │   ├── main.lua         required: runs when the game starts
        │   └── util\
        │       └── text.lua     require("util.text") loads this
        └── data\                optional: replacement game files
```

- **Any folder in `mods` with a `lua\main.lua` is a Lua mod.** Folders load in name order, without case.
- **The key is the folder name**, unless `canopy.ini` sets one. Keys follow the same rules as DLL mod keys: 1 to 32 letters, digits, `_` or `-`. Settings are saved in `mods\<key>.ini`.
- **A DLL mod and a Lua mod can be one mod.** When a DLL mod already registered the same key, the Lua part joins it: its settings appear on the DLL mod's page, and it shares the DLL mod's per-save values.
- **Files are UTF-8 text.** Compiled Lua chunks are refused.
- The same folder can also ship replacement game files in `data\`, see [Replace game files](MODDING.md#replace-game-files).

## canopy.ini

Everything is optional. Lines starting with `;` or `#` are comments, keys are matched without case.

```ini
[mod]
key = HelloLua
name = Hello Lua
author = You
version = 1.0.0
unsafe = false

[setting.Greeting]
type = toggle
label = Log a greeting
help = Logs a greeting on the title screen.
default = true

[setting.Count]
type = slider
label = How many
min = 1
max = 10
step = 1
default = 3

[setting.Mood]
type = choice
label = Mood
choices = Calm|Cheerful|Grumpy
default = Cheerful
```

`[mod]`:

| Key | Default | Meaning |
|---|---|---|
| `key` | the folder name | The mod key. |
| `name` | the key | The name on the Mod Settings page and in the log. |
| `author` | `unknown author` | Shown in the log. |
| `version` | `0.0.0` | Shown in the log. |
| `unsafe` | `false` | `true` gives the mod full game access. Read [Unsafe mods](#unsafe-mods) first. |

`[setting.<Key>]` adds one setting, in file order. `<Key>` follows the setting key rules in [API.md](API.md#keys-and-limits).

| Key | Used by | Meaning |
|---|---|---|
| `type` | all | `toggle` (the default), `slider` or `choice`. |
| `label` | all | The row's text. Defaults to the key. |
| `help` | all | The help line under the list. |
| `default` | all | `true` or `false` for a toggle, a number for a slider, a choice's text or index for a choice. |
| `min`, `max`, `step` | slider | Whole numbers. `step` defaults to 1. The rules of [AddSlider](API.md#addslider) apply. |
| `choices` | choice | 2 to 8 choices, separated by `\|`. |

A setting that breaks a rule is skipped with a log line that says why; the rest of the mod still loads. Labels and choices show the same text in every language.

## How a Lua mod runs

Each Lua mod gets a script player of its own in the game's script engine, the same kind the game runs its events and NPCs with. The game resumes it once per frame.

1. **Startup.** Once the game's script engine exists, Canopy starts each mod's player, in folder order. The player loads `main.lua` into the mod's own environment and runs its top level once. This happens while the game boots, before the title screen, so no save is loaded yet.
2. **Every frame after that:**
   1. Events that happened since the last frame go to the mod's `canopy.on` handlers, in order.
   2. While [in game](#canopyin_game), the `frame` handlers run, then `main` runs one step.
   3. The player waits for the next frame.

`main` is optional. When `main.lua` defines a global function `main`, it runs as a coroutine that only advances while in game. Call `canopy.wait(frames)` in it to wait; when `main` returns, it is done.

```lua
function main()
  while true do
    canopy.wait(600)
    canopy.log("Ten more seconds on the map.")
  end
end
```

Mods never share globals with each other or with the game. A global a mod sets lives in its own environment.

## Events

Register handlers with `canopy.on(event, handler)`. Any number of handlers per event; they run in the order they were added.

| Event | When | Commands work |
|---|---|---|
| `title` | The title screen appears. Per-save values are already cleared. | No |
| `save_loaded` | A save finished loading, before the player has control. The per-save values of that save are already read. | Not yet |
| `control` | The player got control on a map: after a load, an event, a menu or a loading screen. | Yes |
| `no_control` | The player lost control: a menu, dialog, event or loading screen took over. | No |
| `frame` | Every frame while in game. Called with no arguments. | Yes |

A handler for the first four gets the event's name as its argument. "Commands work" means the game commands in the sandbox can be called: they need a loaded save and a player on the map.

## API

Every name is in the `canopy` table. Values are whole numbers, strings and booleans. A number with a fraction, or one outside -2147483648 to 2147483647, is refused.

### canopy.log(...)

Writes one line to `Canopy.log` as `[key] text`. Arguments are turned into text and joined with spaces, like `print`, which does the same. Line breaks become spaces, and a line is cut after 1000 bytes.

### canopy.key, canopy.unsafe

The mod key, and `true` for an [unsafe mod](#unsafe-mods).

### canopy.setting(key)

The current value of a setting from `canopy.ini`: `true` or `false` for a toggle, a number for a slider, the choice's text for a choice. `nil` for a key the mod did not declare. Read it when you need it: it changes as soon as the player changes it on the Mod Settings page.

### canopy.save.get(key, default)

The value stored under `key` with the save being played, or `default` when there is none. A value that reads as a whole number comes back as a number, anything else as a string.

### canopy.save.set(key, value)

Stores a whole number, a string or a boolean (stored as 1 or 0) under `key` with the save being played. `nil` deletes the key. Returns `true` when it was stored. Values are written to `mods\CanopyData\slot<N>.ini` when the game saves, never into the game's own save. `key` follows the setting key rules, a string is one line of at most 1024 bytes, and a mod keeps at most 1024 values. The rules and file format are the same as for DLL mods: see [Per-save data](API.md#per-save-data).

Before a save is loaded, and from the title screen on, values go to a fresh, empty set that is dropped at the next title screen.

### canopy.save.has(key), canopy.save.delete(key)

`has` is `true` when `key` has a value. `delete` removes it and is `true` when there was one.

### canopy.on(event, handler)

Adds a handler for one of the [events](#events). An unknown event name is an error.

### canopy.in_game()

`true` while a save is loaded and the player is on the map with control. Game commands only work while this is `true`.

### canopy.wait(frames)

Only inside `main`: waits `frames` in-game frames, 1 when left out.

### canopy.items.give(key, count), canopy.items.count(key), canopy.items.id(key)

The mod's own [custom items](ITEMS.md), by the key of their `[item.<key>]` section. A mod can only give and count its own items.

- `give` puts `count` (1 to 999, 1 when left out) of the item in the bag, the way a found item goes in, with the game's "got an item" line. It returns how many went in. When none did, it returns `0` and the reason, such as a full bag, or not being in game. It only works while `canopy.in_game()` is `true`.
- `count` is how many the player has in the bag and tool slots.
- `id` is the item's id in the game's item table.

A key the mod did not define is an error. See [ITEMS.md](ITEMS.md) for what an item is and what it can do.

### canopy.items.on_use(key, handler)

Sets the function that runs when the player uses the mod's item `key`, which must have `use = script` in its `[item.<key>]` section. Any other item, or a key the mod did not define, is an error. A second call for the same item replaces the handler.

```lua
canopy.items.on_use("SeedPouch", function(key)
  canopy.flags.set("RecipeBook", true)
  canopy.log("Used a " .. key .. ", " .. canopy.items.count(key) .. " left.")
end)
```

- The handler gets the item's key, and runs on the mod's next frame, protected like an event handler.
- With `consume = on` on the item, one is already gone when the handler runs, so `canopy.items.count` shows what is left.
- Uses while the mod's Lua is stopped run no handler, and the log says so.

### canopy.flags.get(key), canopy.flags.set(key, on), canopy.flags.id(key)

The mod's own [flags](FLAGS.md), by the key of their `[flag.<key>]` section. A mod can only read and set its own flags.

- `get` is `true` or `false`. Outside a game it returns `false` and the reason.
- `set` turns the flag on or off through the game's own flag write and returns `true`. `on` must be `true` or `false`. When it cannot set the flag, such as while the game is saving or outside a game, it returns `false` and the reason.
- `id` is the flag's id in the game's flag set, from 95000 to 99998.

A key the mod did not declare is an error. See [FLAGS.md](FLAGS.md) for ids and saves.

### require(name)

Loads `lua\<name>.lua` from the mod's own folder, once, into the mod's environment, and returns what it returned (`true` when it returned nothing). Dots are folders: `require("util.text")` loads `lua\util\text.lua`. Names are letters, digits and `_`, with dots between folders, up to 64 characters. Nothing outside the mod's `lua` folder can be loaded.

## The sandbox

A mod that is not unsafe sees only these:

- **Lua:** `assert`, `error`, `ipairs`, `next`, `pairs`, `pcall`, `select`, `tonumber`, `tostring`, `type`, `xpcall`, `rawequal`, `rawget`, `rawlen`, `rawset`, `setmetatable`, `getmetatable`, `_VERSION`, and `print`, which is `canopy.log`.
- **Libraries:** `string` (read only, without `dump`), and copies of `table`, `math` (without `randomseed`), `utf8` and `coroutine`.
- **Game commands:** the commands that only read the game, change what is on screen, or change the world for the moment, such as `GameGetFlag`, `TimeGetFrame` or `PlayerGetHP`. Commands that write nothing themselves but can start the game's own dialogue, events or reward screens, resume its scripts, warp the player, or switch collision off are left out too, since they can run story code or break a sequence. The full list is `kAllowed` in [src/Canopy/lua_commands.h](../src/Canopy/lua_commands.h). Each one raises an error unless `canopy.in_game()` is true. Their arguments are the game's own, and are not documented here.

Not there: `io`, `os`, `debug`, `load`, `loadfile`, `dofile`, `package`, `collectgarbage`, `_G`, the game's own globals, and every command that writes the save, changes the story, quests, flags, money, items, time or relationships, or starts or controls game scripts, threads and tasks.

`setmetatable` and `getmetatable` only work on tables whose metatable the mod set itself, so the sandbox's own tables cannot be opened.

## The guard

Canopy also wraps every game command that writes the save or starts game scripts, 231 of them, in a check that runs on every call, from any script. When the call comes from a sandboxed Lua mod, directly or through anything it called, the command is refused with a Lua error and a log line. When it comes from the game's own scripts, the command runs as it always did.

A sandboxed mod has no way to name these commands, so the guard is a second wall: it only fires if a command reaches one of them some other way, such as a game command that hands back a function. When it does, it is logged once per mod and command:

```text
Lua guard: HelloLua called GameSetFlag, which writes the save, and was refused.
```

## Unsafe mods

With `unsafe = true` in `canopy.ini`, a mod reads the game's real globals: every game command, `io`, `os`, `debug`, `load` and the rest. Its own globals still stay in its environment, so it cannot replace the game's. The guard lets it through and logs the first call of each command.

An unsafe mod can change story flags, quests and saves, break progression, and read or write any file. Only install one from someone you trust, and back up your saves first. Canopy warns in the log and on the mod's Mod Settings page:

```text
Warning: HelloLua runs Lua with full game access (unsafe). It can change story flags, quests and saves.
```

The Mod Settings page shows the row **Warning: unsafe Lua, full game access** above the mod's Lua settings.

## Errors and limits

- **Every call into a mod is protected.** An error is logged as `[key] Lua error in <where>: <message>`, with the file and line, and the mod goes on with its next handler or frame. The game never sees it.
- **10 errors stop the mod.** Its Lua stops for the rest of the session, and the game keeps running. A `main.lua` that does not compile or cannot be read stops the mod at once.
- **A frame's work has a budget.** About a million Lua instructions per frame for all of the mod's handlers and `main` together, and ten times that for the top level of `main.lua`. Going over raises an error in the code that was running, and the rest of that frame's handlers are skipped. A loop that never ends costs the mod one error per frame, not a frozen game. Game commands themselves are not counted.
- **Events queue up to 16** while the mod has not run yet; older ones are dropped.
- **A source file is at most 1 MB.**
- **Integers only:** settings and per-save values are whole numbers from -2147483648 to 2147483647, strings or booleans.

## What the log says

Lines the Lua host writes to `Canopy.log`, in the order they can appear:

| Line | Meaning |
|---|---|
| `No Lua mods, so the Lua host is off.` | No folder in `mods` has a `lua\main.lua`. Canopy changes nothing in the game's script engine. |
| `The Lua host needs the frame tick, so it is off.` | The frame hook is disabled, so players cannot run. |
| `Game address <name> was not found in this game build.` then `The Lua host is off.` | A game update moved code Canopy could not find again. Lua mods do not run until Canopy is updated. |
| `Lua mod <name> (<key> v<version> by <author>, folder <folder>) registered: <n> settings.` | The mod was found and its settings added. `joined the DLL mod with the same key` instead of `registered` when a DLL mod has the key. |
| `Lua mod in folder <folder> skipped, ...` | The key is invalid or taken, or the mod could not be registered. The line says which. |
| `[key] Setting <key> skipped, ...` | A `[setting.X]` section breaks a rule. |
| `Warning: <key> runs Lua with full game access (unsafe). ...` | The mod is unsafe. |
| `Lua host installed for <n> Lua mods.` | The host's hooks are ready. |
| `Lua host ready in the script root: <w> of 231 guarded commands wrapped, <f> of 932 sandbox commands found.` | The game built its script engine and the host is in it. |
| `[key] Lua script player started.` | The mod's player exists; its top level runs right after. |
| `[key] ...` | Anything the mod logs. |
| `[key] Lua error in <where>: <message>` | A Lua error, caught. `<where>` is `main.lua`, `main`, a module, or `<event> handler`. |
| `[key] Lua stopped after 10 errors. The game keeps running.` | The mod's Lua is off until the game restarts. |
| `[key] Lua stopped, main.lua did not load. The game keeps running.` | `main.lua` is missing, too large or does not compile. |
| `Lua guard: <key> called <command>, which writes the save, and was refused.` | See [The guard](#the-guard). `starts game scripts` instead of `writes the save` for script, thread and callback creators. `and was allowed because the mod is unsafe` for an unsafe mod. |
| `The script root has no Lua host, so Lua mods do not run.` | Canopy started too late to join the script engine, usually with the loader thread fallback. Restart the game. |
| `The script root changed, so the Lua mods restart.` | The game rebuilt its script engine. Each mod starts again from its top level. |
| `A game script required a module that does not exist. It got a Lua error instead of a crash.` | Only with a Lua mod installed: the game's own module loader would have crashed. |

## Known limitations

- **Restart to reload.** Changes to Lua files, `canopy.ini` or the mod list are read at the next start.
- **Game commands are not documented** and their arguments are not checked by Canopy. A wrong argument can make a command fail in ways a Lua error does not catch; test every command you use. Only call commands from `frame`, `control` and `main`.
- **A command that waits, such as `Wait`, pauses the whole mod** when called from a handler: no handler or `main` step runs until it returns. Called from `main`, it only pauses `main`.
- **The budget is only per mod.** Coroutines an unsafe mod creates with the real `coroutine` library are not counted.
- **Items and flags** are limited to the mod's own [custom items](ITEMS.md) and [flags](FLAGS.md). Vanilla items, vanilla flags, shops and other content are not part of the Lua API.
