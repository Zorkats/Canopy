#pragma once

// The Lua half of the Lua host (lua_host.cpp). It runs once in the script
// root, with the real globals, right after the game registers its commands
// and before any game script has run, so every standard function it keeps
// is the original. It is called with one table of natives and returns the
// host table, which lua_host.cpp keeps in the registry.
//
// host.module(key) builds the module a Lua mod's script player requires:
// its main entry is the bootstrap. The bootstrap gives the mod its own
// environment, loads main.lua into it and runs the top level, then loops
// once per frame: queued events to the canopy.on handlers and item uses to
// the canopy.items.on_use handlers, the frame handlers, one step of the
// optional main coroutine, then one yield to the engine. Every call into mod code is protected, and errors are reported
// to the natives, which stop the mod after 10.
//
// A safe environment sees only safe base functions, copies of table, math,
// utf8 and coroutine, a read-only string, the canopy table, a require that
// loads from the mod's own lua folder, and the allowlisted game commands,
// each refusing to run unless a save is loaded and the player is on a map.
// An unsafe environment reads the real globals. Writes always stay in the
// mod's environment.
//
// MSVC limits one string literal to about 16 KB, so the source is split
// into pieces that the compiler joins.
namespace lua_host_source {

constexpr char kSource[] =
    R"lua(local natives = ...

local type, pairs, ipairs, next, select = type, pairs, ipairs, next, select
local tostring, tonumber, error, pcall, xpcall, assert = tostring, tonumber, error, pcall, xpcall, assert
local rawget, rawset, rawequal, rawlen = rawget, rawset, rawequal, rawlen
local setmetatable, getmetatable, load = setmetatable, getmetatable, load
local G = _G
local version = _VERSION
local sethook = debug and debug.sethook
local loaded = package and package.loaded
local co_create, co_resume, co_status, co_yield, co_running =
  coroutine.create, coroutine.resume, coroutine.status, coroutine.yield, coroutine.running
local ssub, sgsub, sgmatch, slower = string.sub, string.gsub, string.gmatch, string.lower
local tconcat, tpack, tunpack = table.concat, table.pack, table.unpack
local tointeger = math.tointeger

local FRAME_BUDGET = 1000000
local START_BUDGET = 10000000
local HOOK_STEP = 1000
local EVENTS = { title = true, save_loaded = true, control = true, no_control = true, frame = true }
local WAIT = {}

local function copy(source, skip)
  local result = {}
  for k, v in next, source do
    if not (skip and skip[k]) then
      result[k] = v
    end
  end
  return result
end

local string_lib = copy(string, { dump = true })
local table_lib = copy(table)
local math_lib = copy(math, { randomseed = true })
local utf8_lib = utf8 and copy(utf8) or {}
local coroutine_lib = copy(coroutine)

local function in_game()
  return natives.state() == "11"
end

-- Shared by every safe environment, which reads it through a protected
-- metatable, so no mod can reach it or change it.
local shared = {}
for _, name in ipairs({ "assert", "error", "ipairs", "next", "pairs", "pcall", "select", "tonumber",
    "tostring", "type", "xpcall", "rawequal", "rawget", "rawlen", "rawset" }) do
  shared[name] = G[name]
end
for name in sgmatch(natives.allowed(), "[^\n]+") do
  local command = rawget(G, name)
  if type(command) == "function" then
    shared[name] = function(...)
      if not in_game() then
        error(name .. " can only be called while a save is loaded and the player is on the map", 2)
      end
      return command(...)
    end
  end
end
local safe_meta = { __index = shared, __metatable = false }

local function read_only(source, label)
  return setmetatable({}, {
    __index = source,
    __newindex = function()
      error(label .. " is read-only", 2)
    end,
    __pairs = function()
      return next, source, nil
    end,
    __metatable = false,
  })
end

local function decode(reply)
  local tag, rest = ssub(reply, 1, 1), ssub(reply, 2)
  if tag == "i" then
    return tointeger(tonumber(rest))
  elseif tag == "s" then
    return rest
  elseif tag == "b" then
    return rest == "1"
  end
  return nil
end

local function check_name(what, name)
  if type(name) ~= "string" then
    error("bad argument #1 to '" .. what .. "' (string expected, got " .. type(name) .. ")", 3)
  end
end

local function int32(what, value)
  local n = tointeger(value)
  if not n then
    error(what .. " needs a whole number, a string or a boolean", 3)
  end
  if n < -2147483648 or n > 2147483647 then
    error(what .. " needs a whole number from -2147483648 to 2147483647", 3)
  end
  return n
end
)lua"
    R"lua(
local function make_canopy(key, unsafe, st)
  local canopy = { key = key, unsafe = unsafe }
  canopy.log = function(...)
    local parts = {}
    for i = 1, select("#", ...) do
      parts[i] = tostring((select(i, ...)))
    end
    natives.log(key, tconcat(parts, " "))
  end
  canopy.setting = function(name)
    check_name("canopy.setting", name)
    return decode(natives.setting(key, name))
  end
  canopy.save = {
    get = function(name, default)
      check_name("canopy.save.get", name)
      local value = decode(natives.save_get(key, name))
      if value == nil then
        return default
      end
      return value
    end,
    set = function(name, value)
      check_name("canopy.save.set", name)
      local kind, text
      local t = type(value)
      if t == "nil" then
        return natives.save_delete(key, name) == "1"
      elseif t == "boolean" then
        kind, text = "i", value and "1" or "0"
      elseif t == "number" then
        kind, text = "i", tostring(int32("canopy.save.set", value))
      elseif t == "string" then
        kind, text = "s", value
      else
        error("canopy.save.set needs a whole number, a string or a boolean, got " .. t, 2)
      end
      return natives.save_set(key, name, kind, text) == "1"
    end,
    has = function(name)
      check_name("canopy.save.has", name)
      return natives.save_has(key, name) == "1"
    end,
    delete = function(name)
      check_name("canopy.save.delete", name)
      return natives.save_delete(key, name) == "1"
    end,
  }
  -- "i<n>" is a number, "r<why>" is 0 and why, "e<message>" is an error at
  -- the mod's call. No tail calls, so the error levels stay right.
  local function item_reply(what, reply)
    local tag, rest = ssub(reply, 1, 1), ssub(reply, 2)
    if tag == "i" then
      return tointeger(tonumber(rest))
    elseif tag == "r" then
      return 0, rest
    end
    error(what .. ": " .. rest, 3)
  end
  canopy.items = {
    give = function(name, count)
      check_name("canopy.items.give", name)
      local n = 1
      if count ~= nil then
        n = type(count) == "number" and tointeger(count) or nil
        if not n or n < 1 or n > 999 then
          error("canopy.items.give needs a count from 1 to 999", 2)
        end
      end
      local added, reason = item_reply("canopy.items.give", natives.items(key, "give", name, tostring(n)))
      return added, reason
    end,
    count = function(name)
      check_name("canopy.items.count", name)
      local n = item_reply("canopy.items.count", natives.items(key, "count", name))
      return n
    end,
    id = function(name)
      check_name("canopy.items.id", name)
      local n = item_reply("canopy.items.id", natives.items(key, "id", name))
      return n
    end,
    on_use = function(name, handler)
      check_name("canopy.items.on_use", name)
      if type(handler) ~= "function" then
        error("canopy.items.on_use needs a function", 2)
      end
      item_reply("canopy.items.on_use", natives.items(key, "on_use", name))
      st.uses[slower(name)] = handler
    end,
  }
  canopy.flags = {
    get = function(name)
      check_name("canopy.flags.get", name)
      local on, reason = item_reply("canopy.flags.get", natives.flags(key, "get", name))
      return on == 1, reason
    end,
    set = function(name, value)
      check_name("canopy.flags.set", name)
      if type(value) ~= "boolean" then
        error("canopy.flags.set needs true or false", 2)
      end
      local done, reason = item_reply("canopy.flags.set", natives.flags(key, "set", name, value and "1" or "0"))
      return done == 1, reason
    end,
    id = function(name)
      check_name("canopy.flags.id", name)
      local n = item_reply("canopy.flags.id", natives.flags(key, "id", name))
      return n
    end,
  }
  canopy.on = function(event, handler)
    if type(event) ~= "string" or not EVENTS[event] then
      error("canopy.on does not know the event " .. tostring(event), 2)
    end
    if type(handler) ~= "function" then
      error("canopy.on needs a function", 2)
    end
    local list = st.handlers[event]
    list[#list + 1] = handler
  end
  canopy.in_game = in_game
  canopy.wait = function(frames)
    if st.main == nil or co_running() ~= st.main then
      error("canopy.wait only works inside main", 2)
    end
    local n = 1
    if frames ~= nil then
      n = tointeger(frames)
      if not n or n < 1 then
        error("canopy.wait needs a whole number of frames, 1 or more", 2)
      end
    end
    co_yield(WAIT, n)
  end
  return canopy
end

-- require for one mod: loads <name>.lua from the mod's own lua folder only,
-- once, into the mod's environment.
local function make_require(key, env)
  local cache, loading = {}, {}
  return function(name)
    if type(name) ~= "string" then
      error("bad argument #1 to 'require' (string expected, got " .. type(name) .. ")", 2)
    end
    local value = cache[name]
    if value ~= nil then
      return value
    end
    if loading[name] then
      error("module '" .. name .. "' requires itself", 2)
    end
    local reply = natives.source(key, name)
    if ssub(reply, 1, 1) ~= "1" then
      error(ssub(reply, 2), 2)
    end
    local path = sgsub(name, "%.", "/")
    local chunk, problem = load(ssub(reply, 2), "=canopy/" .. key .. "/" .. path .. ".lua", "t", env)
    if not chunk then
      error(problem, 0)
    end
    loading[name] = true
    local ok, result = pcall(chunk, name)
    loading[name] = nil
    if not ok then
      error(result, 0)
    end
    if result == nil then
      result = true
    end
    cache[name] = result
    return result
  end
end

local function make_env(key, unsafe, st, hook)
  local env = {}
  local canopy = make_canopy(key, unsafe, st)
  env.canopy = canopy
  env.print = canopy.log
  env.require = make_require(key, env)
  if unsafe then
    return setmetatable(env, { __index = G, __metatable = false })
  end
  env._VERSION = version
  env.string = read_only(string_lib, "string")
  env.table = copy(table_lib)
  env.math = copy(math_lib)
  env.utf8 = copy(utf8_lib)
  local co = copy(coroutine_lib)
  -- Coroutines get the mod's instruction budget too.
  co.create = function(f)
    local thread = co_create(f)
    if sethook then
      sethook(thread, hook, "", HOOK_STEP)
    end
    return thread
  end
  co.wrap = function(f)
    local thread = co.create(f)
    return function(...)
      local result = tpack(co_resume(thread, ...))
      if result[1] then
        return tunpack(result, 2, result.n)
      end
      error(result[2], 0)
    end
  end
  env.coroutine = co
  -- Only metatables the mod set itself can be read or replaced.
  local owned = setmetatable({}, { __mode = "k" })
  env.setmetatable = function(t, mt)
    if type(t) ~= "table" then
      error("bad argument #1 to 'setmetatable' (table expected, got " .. type(t) .. ")", 2)
    end
    if mt ~= nil and type(mt) ~= "table" then
      error("bad argument #2 to 'setmetatable' (nil or table expected)", 2)
    end
    if getmetatable(t) ~= nil and not owned[t] then
      error("cannot change a metatable this mod did not set", 2)
    end
    setmetatable(t, mt)
    owned[t] = mt ~= nil or nil
    return t
  end
  env.getmetatable = function(value)
    if owned[value] then
      return getmetatable(value)
    end
    return nil
  end
  return setmetatable(env, safe_meta)
end
)lua"
    R"lua(
local function report(key, st, where, problem)
  local ok, text = pcall(tostring, problem)
  if not ok or type(text) ~= "string" then
    text = "an error value that cannot be shown"
  end
  if natives.fail(key, where, text, "") == "stop" then
    st.stopped = true
  end
end

local function call(key, st, where, f, ...)
  if st.over or st.stopped then
    return
  end
  st.active = true
  local ok, problem = pcall(f, ...)
  st.active = false
  if not ok then
    report(key, st, where, problem)
  end
end

-- One step of main. True when it made the whole script thread yield, which
-- uses up this frame's yield.
local function step_main(key, st)
  if st.wait > 0 then
    st.wait = st.wait - 1
    return false
  end
  if st.over then
    return false
  end
  local args = st.resume or { n = 0 }
  st.resume = nil
  st.active = true
  local result = tpack(co_resume(st.main, tunpack(args, 1, args.n)))
  st.active = false
  if not result[1] then
    st.main = nil
    report(key, st, "main", result[2])
    return false
  end
  if co_status(st.main) == "dead" then
    st.main = nil
    return false
  end
  if result[2] == WAIT then
    st.wait = (result[3] or 1) - 1
    return false
  end
  -- Something else yielded, such as a game command that waits. The script
  -- thread yields with it, and the engine's resume values go back to main.
  st.resume = tpack(co_yield(tunpack(result, 2, result.n)))
  return true
end

local function run(key, unsafe)
  local st = {
    handlers = { title = {}, save_loaded = {}, control = {}, no_control = {}, frame = {} },
    uses = {},
    stopped = false, active = false, over = false, budget = START_BUDGET, wait = 0,
  }
  local function hook()
    if st.active then
      st.budget = st.budget - HOOK_STEP
      if st.budget <= 0 then
        st.over = true
        error("ran too long, over the instruction budget for one frame", 2)
      end
    end
  end
  if sethook then
    sethook(hook, "", HOOK_STEP)
  end
  local env = make_env(key, unsafe, st, hook)
  local reply = natives.source(key, "main")
  local chunk, problem
  if ssub(reply, 1, 1) == "1" then
    chunk, problem = load(ssub(reply, 2), "=canopy/" .. key .. "/main.lua", "t", env)
  else
    problem = ssub(reply, 2)
  end
  if not chunk then
    natives.fail(key, "main.lua", tostring(problem), "fatal")
    st.stopped = true
  else
    call(key, st, "main.lua", chunk)
    local main = rawget(env, "main")
    if not st.stopped and type(main) == "function" then
      st.main = co_create(main)
      if sethook then
        sethook(st.main, hook, "", HOOK_STEP)
      end
    end
  end
  while true do
    local yielded = false
    if not st.stopped then
      st.budget = FRAME_BUDGET
      st.over = false
      for event in sgmatch(natives.events(key), "[^,]+") do
        local list = st.handlers[event]
        if list then
          for i = 1, #list do
            call(key, st, event .. " handler", list[i], event)
          end
        elseif ssub(event, 1, 4) == "use:" then
          local item = ssub(event, 5)
          local handler = st.uses[slower(item)]
          if handler then
            call(key, st, "on_use handler", handler, item)
          end
        end
      end
      if not st.stopped and in_game() then
        local list = st.handlers.frame
        for i = 1, #list do
          call(key, st, "frame handler", list[i])
        end
        if st.main and not st.stopped then
          yielded = step_main(key, st)
        end
      end
    end
    if not yielded then
      co_yield()
    end
  end
end

local host = {}

function host.module(key)
  local unsafe = natives.info(key) == "u"
  local name = "canopy_" .. key
  return {
    main = function()
      -- The module is built for this one player, so a later player of the
      -- same mod builds a new one.
      if loaded then
        loaded[name] = nil
      end
      run(key, unsafe)
    end,
  }
end

return host
)lua";

}  // namespace lua_host_source
