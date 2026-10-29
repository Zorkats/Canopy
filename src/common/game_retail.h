#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

// Retail village.exe. Names are descriptive labels from static analysis.
// Addresses are found at startup for the running build (game_builds.txt for
// a checked build, the patterns of game_addresses.txt for any other), so
// none is written here. Offsets and sizes are compiled in. Those declared
// with GAME_FIELD are also read out of the running build's code, and a
// feature whose field has another value there stays off. GAME_FIELD_VIA
// checks others through a GAME_FIELD of the same layout. The rest were
// checked by hand on every build in game_builds.txt.
namespace retail {

// Something a feature needs of the running build. A feature checks its
// requirement list before it runs, and stays off while one is unmet.
class Requirement {
public:
    virtual bool Met() const = 0;
    // Why Met is false, one line for the log.
    virtual std::string Problem() const = 0;

protected:
    ~Requirement() = default;
};

// An RVA from the module base, looked up by name in the running build's
// resolved addresses. Converts to 0 while it is not resolved or was not
// found, so a feature checks its requirement list before it uses any.
class Address : public Requirement {
public:
    constexpr explicit Address(const char* name) : name_(name) {}
    Address(const Address&) = delete;
    Address& operator=(const Address&) = delete;

    const char* Name() const { return name_; }
    // Defined by core.cpp, from core::ResolveAddresses.
    operator uintptr_t() const;
    bool Found() const { return static_cast<uintptr_t>(*this) != 0; }
    bool Met() const override { return Found(); }
    std::string Problem() const override;

private:
    const char* name_;
    // The looked-up RVA, or all ones once a lookup failed. Lookups only run
    // after resolution, so a stored value never changes.
    mutable std::atomic<uintptr_t> rva_{0};
};

#define GAME_ADDRESS(Name) inline Address k##Name{#Name}

// An offset or size of a game layout that game_addresses.txt reads out of an
// instruction. Met when the running build has the compiled value.
class Field : public Requirement {
public:
    constexpr Field(const char* name, size_t value) : name_(name), value_(value) {}
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;

    const char* Name() const { return name_; }
    size_t Value() const { return value_; }
    // Defined by core.cpp.
    bool Met() const override;
    std::string Problem() const override;

private:
    const char* name_;
    size_t value_;
};

// k##Name is the compiled value, and k##Name##Field its requirement.
#define GAME_FIELD(Name, value)       \
    constexpr size_t k##Name = value; \
    inline constexpr Field k##Name##Field{#Name, k##Name}

// An offset or size no instruction gives away, checked through a field of
// the same layout that a change moving it would change too: the layout's
// size, a field after it, or its partner in the same library object. Met
// when the witness is.
class FieldVia : public Requirement {
public:
    constexpr FieldVia(const char* name, const Field& witness) : name_(name), witness_(witness) {}
    FieldVia(const FieldVia&) = delete;
    FieldVia& operator=(const FieldVia&) = delete;

    const char* Name() const { return name_; }
    const Field& Witness() const { return witness_; }
    bool Met() const override { return witness_.Met(); }
    std::string Problem() const override {
        return std::string("Game field ") + name_ + " is checked through " + witness_.Name() + ". " +
               witness_.Problem();
    }

private:
    const char* name_;
    const Field& witness_;
};

// k##Name##Field, the requirement of the plain constant k##Name, met when
// the GAME_FIELD Witness is.
#define GAME_FIELD_VIA(Name, Witness) inline constexpr FieldVia k##Name##Field{#Name, k##Witness##Field}

// =============================================================
// FUNCTIONS: SHARED
// =============================================================
// Application frame update (virtual slot +8 of the app object), called once
// per frame from WinMain on the game thread.
GAME_ADDRESS(FrameUpdate);
// Appends a task to a parent task: (parent, &task, mode).
GAME_ADDRESS(AttachTask);
// Engine heap allocation: (heap, alignment, size).
GAME_ADDRESS(Allocate);
// Returns the UTF-8 logtext row for the current language.
GAME_ADDRESS(GetLogTextById);
// CTask_Message_Popup_Select ctor(task, &messages, yesText, noText, &onYes,
// &onNo, a7, a8). messages is a std::vector<Ngf::CString>, the callbacks are
// std::function<void()> whose targets the ctor takes over.
GAME_ADDRESS(PopupSelectCtor);
// Returns to the title screen: starts a fade on layer 7, then attaches a new
// CTask_Title to the UI root with mode 1, which destroys every field layer.
// Takes no arguments and only reads globals. The engine runs it from the
// "save too new" popup of SaveFunc::startLoadGame.
GAME_ADDRESS(ReturnToTitle);
// Ngf::CSoundPlayer volume fade: (player, volume, frames, &onDone), where
// frames counts 60 fps frames and onDone is a std::function<void(CSoundPlayer*)>
// the player copies.
GAME_ADDRESS(SoundFade);
// Pad receiver "has input" check: enabled, active, no lockout timer, no
// blocker, then recursively the parent receiver, and only the root's own
// focus byte counts. The player's field input gate (CCom_Unit_Player method
// 0x20E5D0) calls it first.
GAME_ADDRESS(IsReceiverActive);
// CGame's scalar deleting destructor (game, flags), vtable slot 0. WinMain
// calls it once on the game thread when its main loop ends, before any game
// system is torn down. Every way of closing the game ends the loop the same
// way: the window gets WM_CLOSE (the close button, Alt+F4, and the Exit Game
// popup's Yes, which sends it), and its procedure sets the quit flag the loop
// checks.
GAME_ADDRESS(GameDestroy);

// =============================================================
// FUNCTIONS: GAME EVENTS
// =============================================================
// CTask_Title ctor(task). Every path to the title screen builds a new one:
// game start, Return to Title, and the title rebuilding itself after its
// options change the language. Called directly, inside task updates.
GAME_ADDRESS(TitleCtor);
// SaveFunc::startLoadGame(), no arguments. Every load runs it once the save
// data is in memory, to set up the field from it: the load menu at the title
// and in game, and the auto load. Called through a std::function thunk.
GAME_ADDRESS(StartLoadGame);

// =============================================================
// FUNCTIONS: SETTINGS HOTKEY
// =============================================================
// CTask_Menu_MainMenu ctor(task, startTab). The field's menu button runs
// alloc, this ctor and an attach to the UI root with mode 2.
GAME_ADDRESS(MainMenuCtor);
// Plays a system sound effect by id: (seId).
GAME_ADDRESS(PlaySystemSe);

// =============================================================
// FUNCTIONS: INPUT
// =============================================================
// CPadManager capture(padManager, &outRef, deviceType, mode, &query): the
// first pad of deviceType that passes the button check in mode, or null. The
// key config's wait state calls it with mode 0 for the pressed key. outRef
// points to a pointer to the pad record, which starts with the pad id.
GAME_ADDRESS(PadCapture);
// The CPadManager singleton.
GAME_ADDRESS(PadManager);
// The engine's static empty string, every empty Ngf::CString's data.
GAME_ADDRESS(EmptyStringData);

// =============================================================
// FUNCTIONS: OPTIONS MENU
// =============================================================
// CTask_Menu_Option::initialize_Inner(menu). Builds one row per entry of the
// gamesettinglist table, each from the gamesetting record its entry names.
GAME_ADDRESS(OptionMenuInitialize);
// Database registry lookups: gamesettinglist (list entries) and gamesetting
// (setting records). Both return the CDatabase instance.
GAME_ADDRESS(GetSettingListTable);
GAME_ADDRESS(GetSettingTable);
// ITreeChild::findParentClass(task, classId): the nearest parent task of
// that class, or null.
GAME_ADDRESS(FindParentClass);
// CTask_Menu_MainMenu ClassID getter.
GAME_ADDRESS(MainMenuClassId);
// CListItem_PartsControl: appends an input handler, (item, &fn, input) with
// fn a std::function<void()> it copies.
GAME_ADDRESS(BindListItemInput);
// CTask_Menu_Option ctor(task, &parentPad, 0). The new menu's pad becomes a
// child of the pad parentPad points to, which takes the input from it.
GAME_ADDRESS(OptionMenuCtor);
// CTask_Menu_Option::closeMethod(menu). Queues the menu's end, once.
GAME_ADDRESS(OptionMenuClose);
// CTask_Title ClassID getter.
GAME_ADDRESS(TitleClassId);
// std::unordered_map<int64, T>::find(map, &outNode, &key), one template
// instance shared by every int64-keyed map in the engine.
GAME_ADDRESS(Int64MapFind);
// Setting value getter (gameManager, settingId) and setter (gameManager,
// settingId, value). Both look the record up with no null check.
GAME_ADDRESS(GetSettingValue);
GAME_ADDRESS(SetSettingValue);
// Ngf::CCom_List::setFilter(list, filter, cursor), both std::functions taken
// by value and destroyed by the callee. Stores the filter, rebuilds the
// visible items from every item it accepts, then sets the cursor to what
// cursor returns, or keeps the current index when it is empty. The quest menu
// calls it at the end of its initialize (0x30F153).
GAME_ADDRESS(ListSetFilter);
// Ngf::CPad action check (pad, actionId, &query): the action's input record
// when the pad and its parents take input and the action is down, else null.
GAME_ADDRESS(PadActionCheck);
// CTask_Menu_MainMenu update step (menu): Cancel on the menu's pad closes
// every tab and the menu, whatever page is open over the options tab.
GAME_ADDRESS(MainMenuCancelStep);
// CTask_Title::CState_Option Cancel handler (&state), bound to action 1350
// (right click sends it too). Saves the system settings and leaves the state
// with attach mode 1, which destroys every layer of the state.
GAME_ADDRESS(TitleOptionCancel);

// =============================================================
// FUNCTIONS: TOASTS
// =============================================================
// Queues a popup log: (logManager, source, tag, count, logId, &parentTask).
// The logtext row for logId must exist; the engine dereferences it unchecked.
GAME_ADDRESS(PushLog);
// CGameLog::getText(gameLog, outCString). Builds the popup string from logtext.
GAME_ADDRESS(GetLogText);
// LOG_TEST_RESTAURANT_EATING: a leftover test row with no sound effect and no
// duplicate suppression, never queued by retail scripts. Toasts carry it.
constexpr uint64_t kToastCarrierLogId = 1089;

// =============================================================
// FUNCTIONS: BGM INFO
// =============================================================
// Ngf::CSoundManager::makeBgmParam(manager, outParam, bgmId). Every BGM play
// path (BgmPlay, BgmPlaywithFade, BgmPlaywithCrossFade, Sound Test) calls it
// on the game thread, after applying the engine's BGM id remap.
GAME_ADDRESS(MakeBgmParam);
// Returns the CDatabase<BGMData> instance from the database registry.
GAME_ADDRESS(GetBgmTable);

// =============================================================
// FUNCTIONS: QUICK SAVE
// =============================================================
// CTask_Save_Process_AutoSave: ctor(task, &setting), then the lambda that
// builds CTaskSetting_SaveData_AutoSave into task+520 and picks the slot, and
// the lambda that runs after the file is written (rotation and .systemsave).
GAME_ADDRESS(AutoSaveProcessCtor);
GAME_ADDRESS(AutoSaveBuildSetting);
GAME_ADDRESS(AutoSaveSucceeded);

// Ngf::CTask_SaveData_AutoDelete ctor(task, &setting) and the shared
// CTaskSetting_SaveData_Auto_Base ctor(setting).
GAME_ADDRESS(AutoDeleteCtor);
GAME_ADDRESS(SaveSettingBaseCtor);

// Save utility: clears a finished request. On entry the request and its
// status are still readable.
GAME_ADDRESS(SaveUtilAcknowledge);

// Ngf::CCom_List cursor index.
GAME_ADDRESS(GetListCursor);
// Save list entry info object ctor (1072 bytes).
GAME_ADDRESS(SaveInfoCtor);

// CTask_GameSaveData_Load::CState_SaveDataSelect.
GAME_ADDRESS(LoadMenuFillRows);
// initialize_Inner lambda_1: header text ("saveNo") for the cursor row.
GAME_ADDRESS(LoadMenuHeaderText);

// Builds the 24-byte render layer key a field popup draws on (out, layerId,
// unused, layerName). ScriptCommand::TaskCreate_GamePopupSelect (0x26D450)
// calls it with the Lua default layer 6 and an empty name.
GAME_ADDRESS(MakeLayerKey);
// Starts the Lua event_game_load script that wakes the player in bed. Only the
// normal branch of SaveFunc::startLoadGame reaches it, through the Do_call of
// its lambda_4 (0x4711D0). Takes no arguments.
GAME_ADDRESS(LoadEventStart);
// GameSetFlag core: (flagId, value), notifies the main game on a change.
GAME_ADDRESS(SetGameFlag);
// ScreenFade core: (fadeManager, renderStep, color, &FadeParam, &onEnd,
// &outTask), where onEnd is a std::function<void()>.
GAME_ADDRESS(ScreenFade);
// Ngf::CString append(text, cString).
GAME_ADDRESS(CStringAppend);
// Ngf::CCom_List setCursor(list, index, top), top -1 keeps the scroll top.
// Clamps the index to the items that exist.
GAME_ADDRESS(SetListCursor);

// Clears a game flag: (saveData, flagId), and notifies the main game when it
// was set. The normal branch of startLoadGame calls it at kDailyFlagClearCall
// for every flag the gameflag table marks isDailyOff, since a normal load
// starts a new morning.
GAME_ADDRESS(ClearGameFlag);
GAME_ADDRESS(DailyFlagClearCall);
// Event conditions check: (saveData, &conditions, count, &flagConditions,
// flagCount), true when the event may start. CCom_MapEvent::setupScript calls
// it at kSetupScriptConditionsCall once for every body its own body already
// touches, in the order of that body's contact list, and starts the event's
// script on true.
GAME_ADDRESS(EventConditions);
GAME_ADDRESS(SetupScriptConditionsCall);
// CCom_MapEvent::setupScript(mapEvent), and the contactBegin handler it
// registers on its body when the event starts on contact: (&mapEvent,
// selfBody, &otherBody), which checks the same conditions and starts the
// script.
GAME_ADDRESS(MapEventSetupScript);
GAME_ADDRESS(MapEventContact);
// Collision component of an entity, or null.
GAME_ADDRESS(FindCollisionComponent);
// Length of a rel32 call, from a call site to its return address.
constexpr size_t kCallLength = 5;

// CTask_GameSaveData_Load (the three group cards). Its save list listener
// (vtable slot 35) runs every save util tick while the menu idles: it fills
// the card of each group from the newest autosave, refills the open
// carousel's rows instead when one exists, and commits a chosen load.
// Returns 1 once the menu is done.
GAME_ADDRESS(LoadCardsListener);
// Card text04 provider (the mode label) Do_call: (functor, &outText), functor
// {+8 task, +16 group}.
GAME_ADDRESS(CardModeText);
// CState_SaveDataSelect (the Start carousel): initialize_Inner(state), and
// the play time text provider Do_call (functor, &outText), functor {+8 state,
// +16 list UI object}.
GAME_ADDRESS(CarouselInitialize);
GAME_ADDRESS(CarouselPlayTimeText);

// =============================================================
// FUNCTIONS: WALK SPEED
// =============================================================
// CState_Player_Walk::update(state, frameTime), the walk state's vtable slot.
// Builds the player's free movement velocity from getMoveSpeed, the stick
// and the state's own gait factors, then moves the unit and sets its
// animation speed.
GAME_ADDRESS(PlayerWalkUpdate);
// CCom_Unit_Player::getMoveSpeed(unit), vtable 0xE52068 slot +0x138: the
// player's base speed with the area and status slowdowns applied. Lua
// UnitGetMoveSpeed calls it too.
GAME_ADDRESS(PlayerGetMoveSpeed);

// =============================================================
// FUNCTIONS: EVENT FAST FORWARD
// =============================================================
// CCom_Event::setup(event), run once by the event factory 0x16EF50 when an
// event script starts, and the destructor body (event) its deleting
// destructor runs.
GAME_ADDRESS(EventSetup);
GAME_ADDRESS(EventDestroy);
// Time manager update(timeManager, outInfo), once per frame from the frame
// update. Checked only, so the field layout below is trusted.
GAME_ADDRESS(TimeUpdate);
// CPadManager push command (padManager, commandId, &vector, flag): queues a
// command that fires on the next frame, as Lua PadSetCommand does.
GAME_ADDRESS(PadPushCommand);
// Pad database (registry), as the pad manager's resolve fetches it from
// kDatabaseRegistry. Its pad table is an unordered_map<u64, record*>.
GAME_ADDRESS(PadDatabase);
// std::unordered_map<u64, ...>::find(map, &outNode, &key), the instance the
// pad tables share: the node, or the map's end node on a miss.
GAME_ADDRESS(U64MapFind);
// CPadManager resolve (padManager, &outHolder, holder): follows a virtual
// pad's record holder through the key config to the holder of the pad it is
// bound to.
GAME_ADDRESS(ResolvePad);

// =============================================================
// FUNCTIONS: WHEEL ZOOM
// =============================================================
// CCom_CameraControl::update(control, frameTime), a vtable slot. Polls the
// change camera command (1039), which steps the zoom level and starts a
// tween to that level's zoom, then writes the tween's value to the camera.
GAME_ADDRESS(CameraControlUpdate);
// Float tween start (tween, &from, &to, &FadeParam), as the zoom poll calls it.
GAME_ADDRESS(TweenStart);

// =============================================================
// FUNCTIONS: KEY BINDING BACKUP
// =============================================================
// AutoSystemLoad success (task), a functor slot: installs the loaded system
// data on the game manager, then applies its key table to the pad manager.
GAME_ADDRESS(SystemLoadSucceeded);
// std::unordered_map<u64, u64>::operator[](map, &outNode, &key), one template
// instance shared by many maps. setVKey calls it at kSetVKeyIndexCall + 0x12.
GAME_ADDRESS(U64MapIndex);
GAME_ADDRESS(SetVKeyIndexCall);

// =============================================================
// FUNCTIONS: STARTUP
// =============================================================
// Game init (app, config), returns a success byte. WinMain's setup 0x164C60
// calls it once, on the main thread, with the config it has just filled.
// It creates the file system from that config, mounts data.dat, builds the
// database registry, which reads every table, and mounts the archives that
// archive.dat lists, all before it returns.
GAME_ADDRESS(GameInit);

// =============================================================
// FUNCTIONS: FILE OVERRIDES
// =============================================================
// File system archive lookup (fs, path, root): the entry index of path in the
// mounted archives, or -1. Root 7 means the default root, and any root but
// the archive root returns -1. It copies path into a 512-byte buffer,
// lowercases it with _strlwr (ASCII letters only) and compares it byte for
// byte with the archive names, which are lowercase with forward slashes.
GAME_ADDRESS(FindArchiveEntry);
// File system path builder (unused, root, path): the loose path of path under
// root, in a 0x2000-byte buffer per thread that the next call overwrites. The
// first argument is never read. Roots 2 and 4 give a WORD 1 marker, then the
// wide root prefix and path; root 6 the narrow path as it is; any other root
// the narrow prefix and path. Every caller checks the marker and then uses
// the wide or narrow file function to match.
GAME_ADDRESS(BuildPath);
// File system exists (fs, path, root), size (fs, path, root) and open
// (fs, request, path, root, mode, ...). Each asks the archive lookup first,
// then stats the path builder's path for the same root. An open that finds
// the loose file keeps the root and the relative path in its request, and the
// file system's worker threads build the path again from them. A read only
// uses the archive in mode 1; writes go to the path builder's path.
GAME_ADDRESS(FsExists);
GAME_ADDRESS(FsGetSize);
GAME_ADDRESS(FsOpen);
// Opens a file handle from a path builder result (path, mode, ...): the wide
// CreateFileW after the marker, otherwise CreateFileA.
GAME_ADDRESS(OpenFileHandle);

// =============================================================
// FUNCTIONS: LUA HOST
// =============================================================
// Game command registrar (L): registers every game command in the script
// root's globals. The engine calls it while it builds the root, after the
// standard libraries and the framework commands are open and before any game
// script runs.
GAME_ADDRESS(RegisterGameCommands);
// The script module loader, a lua_CFunction (L) that luaL_requiref calls with
// the module name: it fetches the module's code, loads it as text and calls
// it. A name that is not a string, or a module that does not exist, makes it
// dereference null.
GAME_ADDRESS(ScriptModuleLoader);
// Script manager getCode (manager, &outCode, name): a referenced code object
// (refcount at +8, text at +16, size at +24), or null when there is none.
GAME_ADDRESS(ScriptGetCode);
// Script manager createPlayer (manager, &outPlayer, name, module, entry,
// rootSlot): a referenced CScriptPlayer with one thread that requires module
// and runs its entry function. rootSlot points at the root to use; the
// manager's own slot at +1192 is the usual one. The player registers an update
// item, but one with no parent task is never enrolled, so its owner updates it.
GAME_ADDRESS(ScriptCreatePlayer);
// CScriptPlayer update (player, updateInfo): resumes its threads once.
// Returns at once while the player's stopped byte is set.
GAME_ADDRESS(ScriptPlayerUpdate);
// Lua 5.4, linked statically. getStr and setStr are the API's internal field
// get and set (L, value, key): get pushes value[key] and returns its type,
// set pops the value into value[key]. Both work on a value pointer instead of
// a stack index.
GAME_ADDRESS(LuaSetTop);
GAME_ADDRESS(LuaType);
GAME_ADDRESS(LuaToLString);
GAME_ADDRESS(LuaPushLString);
GAME_ADDRESS(LuaPushString);
GAME_ADDRESS(LuaPushCClosure);
GAME_ADDRESS(LuaGetStr);
GAME_ADDRESS(LuaCreateTable);
GAME_ADDRESS(LuaSetStr);
GAME_ADDRESS(LuaSetGlobal);
GAME_ADDRESS(LuaPCallK);
GAME_ADDRESS(LuaError);
GAME_ADDRESS(LuaLoadBufferX);

// =============================================================
// FUNCTIONS: SCRIPT DEBUG COMMANDS
// =============================================================
// Script commands, each a lua_CFunction (L) whose retail body is compiled
// out: it checks and reads its arguments, then does nothing with them.
// DebugIsOn pushes false, DebugGetConfig (config) pushes the number 0,
// DebugPrint (text) and DebugDisp (text) push nothing, and DebugScreenText
// (key, text) pushes nothing. Each records its name in the script context
// first, so a missing argument raises the game's own "over arg count" error.
GAME_ADDRESS(DebugIsOn);
GAME_ADDRESS(DebugGetConfig);
GAME_ADDRESS(DebugPrint);
GAME_ADDRESS(DebugDisp);
GAME_ADDRESS(DebugScreenText);
// The instruction in each that loads the command's name, followed by its
// length: what tells the five apart, since their prologues match.
GAME_ADDRESS(DebugIsOnName);
GAME_ADDRESS(DebugGetConfigName);
GAME_ADDRESS(DebugPrintName);
GAME_ADDRESS(DebugDispName);
GAME_ADDRESS(DebugScreenTextName);

// =============================================================
// GLOBALS
// =============================================================
// The game manager is also the popup log manager.
GAME_ADDRESS(GameManager);
GAME_ADDRESS(DatabaseRegistry);
GAME_ADDRESS(SaveWork);
GAME_ADDRESS(SaveUtil);
// The file system object, created by game init.
GAME_ADDRESS(FileSystem);
GAME_ADDRESS(UiTaskRoot);
GAME_ADDRESS(Language);
GAME_ADDRESS(EngineHeap);
GAME_ADDRESS(SoundManager);
// Ngf::CSoundMgr, which keeps the BGM players apart from kSoundManager's.
GAME_ADDRESS(SoundSystem);
GAME_ADDRESS(TimeManager);
// The script manager, which owns the script root and the code cache.
GAME_ADDRESS(ScriptManager);
GAME_ADDRESS(MouseManager);
GAME_ADDRESS(TalkManager);
// DirectInput keyboard state: the current key bytes, by DIK code, sit at +0x10;
// a key is down while bit 7 is set.
GAME_ADDRESS(KeyboardState);
GAME_FIELD(KeyboardDown, 0x10);
// Controller states, one block per controller: the held button mask first,
// as the button check reads it in hold mode.
GAME_ADDRESS(ControllerStates);
constexpr size_t kControllerStateSize = 624;
constexpr int kControllerCount = 32;

// Vtables.
GAME_ADDRESS(AutoSaveProcessSettingVtable);
GAME_ADDRESS(AutoDeleteSettingVtable);
GAME_ADDRESS(PlayerComponentVtable);
GAME_ADDRESS(CStringVtable);
// std::_Func_impl_no_alloc of the captureless lambda in
// CSoundPlayer::stopWithFade, which stops the player once its fade ends.
GAME_ADDRESS(StopAfterFadeVtable);

// =============================================================
// LAYOUTS
// =============================================================
// Offset of the resolved BGM id inside the param makeBgmParam fills.
constexpr size_t kBgmParamIdOffset = 0x08;

// File system object fields: the root the archives are mounted on, and the
// root that root 7 stands for. Retail sets both to 2 from the config WinMain
// fills, so every data path uses root 2.
constexpr size_t kFsArchiveRoot = 0x08;
GAME_FIELD(FsDefaultRoot, 0x24);
constexpr int32_t kFsRootSave = 4;
constexpr int32_t kFsRootRaw = 6;
constexpr int32_t kFsRootDefault = 7;
// A path builder result that starts with this WORD is a wide path after it.
constexpr uint16_t kWidePathMarker = 1;
// The path builder's buffer, in wide characters.
GAME_FIELD(BuildPathCapacity, 0x1000);

// CGameLog fields.
GAME_FIELD(GameLogId, 0x40);
GAME_FIELD(GameLogSource, 0x58);
GAME_FIELD(GameLogTag, 0x60);

// CDatabase<BGMData>: std::vector of row slots; each slot points to a row.
GAME_FIELD(BgmTableSlotsBegin, 0x98);
constexpr size_t kBgmTableSlotsEnd = 0xA0;
// BGMData row: u64 id at +0, then one UTF-8 title pointer per language.
constexpr size_t kBgmRowId = 0x00;
constexpr size_t kBgmRowTitles = 0xA0;

// Game manager.
GAME_FIELD(ManagerSystemData, 512);
GAME_FIELD(ManagerSaveData, 520);
GAME_FIELD(ManagerGroup, 952);
GAME_FIELD(ManagerRotation, 956);

// System data (.systemsave): group, newest rotation index per group, time.
GAME_FIELD(SystemGroup, 96);
GAME_FIELD(SystemNewestRotation, 100);
GAME_FIELD(SystemSaveTime, 112);

// Save work counters: save or load in progress, live save tasks.
GAME_FIELD(SaveWorkBusy, 720);
GAME_FIELD(SaveWorkLiveTasks, 1160);

// Save utility request state.
constexpr size_t kSaveUtilSlotCapacity = 8;
constexpr size_t kSaveUtilState = 12;
GAME_FIELD(SaveUtilStatus, 16);
GAME_FIELD(SaveUtilOperation, 24);
constexpr size_t kSaveUtilSlot = 36;
// The util's save list (same layout as the list the load menu fills from).
// A load by list index leaves kSaveUtilSlot at 0 and reads the slot from
// the selected entry instead.
constexpr size_t kSaveUtilList = 2264;
constexpr int32_t kSaveUtilStateDone = 2;
constexpr int32_t kSaveUtilStatusOk = 1;
constexpr int32_t kSaveUtilOperationSave = 0;
constexpr int32_t kSaveUtilOperationLoad = 1;
constexpr int32_t kSaveUtilOperationDelete = 2;
// The request's data size: a game save's buffer, or the smaller system
// data's (options and key config). 0 for a delete.
constexpr size_t kSaveUtilDataSize = 52;
constexpr uint32_t kGameSaveDataSize = 0x300000;
constexpr uint32_t kSystemSaveDataSize = 0x80000;

// Player input gate: CSaveData -> player status -> CCom_Unit_Player -> pad
// receiver. The game's own field gate (0x20E5D0) also requires the save data
// field at +0x3470 to be empty and bit 9 of the flags at +0xA8 to be clear.
GAME_FIELD(SaveDataPlayerStatus, 0x32B8);
constexpr size_t kPlayerStatusComponent = 1048;
constexpr size_t kPlayerComponentReceiver = 1056;
GAME_FIELD(SaveDataFieldBlocker, 0x3470);
constexpr size_t kSaveDataStateFlags = 0xA8;
// Game flag 201, EVENT_RUNNING: 0xA8 is the fourth word of the flag bits at
// kSaveDataFlags.
constexpr uint32_t kStateFlagFieldLocked = 1u << 9;

// The running map: game manager -> map owner -> CMapInformation, the one the
// load event hands its script as mapInfo. Its player unit holds the player
// entity.
GAME_FIELD(ManagerMapOwner, 616);
GAME_FIELD(MapOwnerMapInfo, 216);
GAME_FIELD(MapInfoPlayerUnit, 88);
constexpr size_t kUnitEntity = 8;
// A component's entity, the one setupScript takes the map event's collision
// component from.
constexpr size_t kComponentEntity = 8;
// Collision component: std::vector of body pointers, the movement body first.
GAME_FIELD(CollisionBodiesBegin, 584);
GAME_FIELD(CollisionBodiesEnd, 592);
// Ngf::CCollision2DBody: the sentinel of the list of bodies it touches, whose
// nodes hold the next node first and the other body at +16.
GAME_FIELD(BodyContacts, 1016);
constexpr size_t kContactNodeNext = 0;
GAME_FIELD(ContactNodeBody, 16);
// CCom_MapEvent: its data holder, whose slot at +584 points at a pointer to
// the event's database record. setupScript names the event's script
// "demo<id>" from the record's first field.
GAME_FIELD(MapEventData, 824);
GAME_FIELD(MapEventDataRecord, 584);
// Map event record: the id, then the two flag lists eventConditions checks,
// each a count and up to six inline flag ids. Every required flag must be set
// and every forbidden one clear; a 0 id is skipped.
constexpr size_t kEventRecordId = 0;
GAME_FIELD(EventRecordRequiredCount, 224);
GAME_FIELD(EventRecordRequired, 232);
GAME_FIELD(EventRecordForbiddenCount, 280);
GAME_FIELD(EventRecordForbidden, 288);
constexpr size_t kEventRecordFlagSlots = 6;

// Task and setting objects. A task's parent is the task it is attached to.
constexpr size_t kTaskParent = 8;
GAME_FIELD(TaskRefCount, 24);
GAME_FIELD(TaskSaveSetting, 520);
constexpr size_t kSettingRefCount = 8;
GAME_FIELD(SettingTitle, 16);
GAME_FIELD(SettingSlot, 128);
GAME_FIELD(AutoSaveTaskSize, 544);
GAME_FIELD(AutoSaveProcessSettingSize, 144);
GAME_FIELD(AutoDeleteTaskSize, 536);
GAME_FIELD(AutoDeleteSettingSize, 272);
GAME_FIELD(AllocationAlignment, 32);
// Logtext row the engine uses as the title of every auto save request.
constexpr uint64_t kSaveTitleLogId = 1393;

// Attach modes are operations on the parent's layer stack (0x103400):
// 1 suspends the top layer and destroys every layer before pushing the task,
// which on the UI root tears down the running field. 2 suspends the top
// layer and pushes the task on a new one, which pops when the task ends.
// The engine launches its save tasks on the root with 2.
constexpr int32_t kAttachModePushLayer = 2;

// Yes/No popup: task size, the yes and no button rows, the a7/a8 values the
// Lua command defaults to, and the render layer key count and slot.
constexpr size_t kPopupSelectTaskSize = 1920;
constexpr uint64_t kPopupYesLogId = 1040;
constexpr uint64_t kPopupNoLogId = 1041;
constexpr int64_t kPopupSelectDefaultArg = 2;
constexpr size_t kTaskLayerKeyCount = 784;
constexpr size_t kTaskLayerKeys = 792;
GAME_FIELD(LayerKeySize, 24);
constexpr int64_t kFieldPopupLayer = 6;

// Main menu: task size, the Option tab (the last of its 8 tabs), and the
// sound effect the field plays when it opens the menu.
constexpr size_t kMainMenuTaskSize = 0x258;
constexpr int32_t kMainMenuOptionTab = 7;
constexpr uint64_t kMenuOpenSe = 0x19;

// Sound manager: intrusive list of sound players (sentinel node at +616,
// next at +0, player at +16), and the fade time, in frames, the engine's
// title option uses when it silences them.
GAME_FIELD(SoundManagerPlayers, 616);
constexpr size_t kSoundNodeNext = 0;
GAME_FIELD(SoundNodePlayer, 16);
constexpr double kTitleSoundFadeFrames = 10.0;
// BGM players: kSoundSystem's player manager at +0x1A8 holds a list of the
// same nodes at its own +0x1A8. BgmPlaywithCrossFade fades them out there.
GAME_FIELD(SoundSystemBgmManager, 0x1A8);
GAME_FIELD(BgmManagerPlayers, 0x1A8);

// Ngf::CString: vtable, data, size, capacity, assign mode.
GAME_FIELD(CStringSize, 40);
constexpr size_t kCStringData = 8;
GAME_FIELD(CStringLength, 16);
GAME_FIELD(CStringMode, 32);

// Load menu state: group, then 16-byte rows {rotation, list index, info}.
GAME_FIELD(LoadMenuGroup, 536);
GAME_FIELD(LoadMenuRows, 552);
GAME_FIELD(LoadMenuRowSize, 16);
constexpr size_t kRowRotation = 0;
constexpr size_t kRowListIndex = 4;
constexpr size_t kRowInfo = 8;
constexpr int kLoadMenuRetailRowCount = 5;
// Header functor.
constexpr size_t kHeaderFunctorState = 8;
GAME_FIELD(ListComponent, 32);

// Load cards task: per group, 16 bytes {newest rotation, list index, card
// info}. The card's Yes (0x486E20) loads the list index.
GAME_FIELD(CardRows, 600);
GAME_FIELD(CardRowSize, 16);
constexpr size_t kCardRowListIndex = 4;
constexpr size_t kCardRowInfo = 8;
// Text provider functors.
constexpr size_t kCardFunctorTask = 8;
GAME_FIELD(CardFunctorGroup, 16);
constexpr size_t kCarouselFunctorState = 8;
GAME_FIELD(CarouselFunctorList, 16);
// Ngf::CCom_List items vector: the items its filter shows, which the cursor
// indexes.
GAME_FIELD(ListItemsBegin, 2392);
GAME_FIELD(ListItemsEnd, 2400);

// Options menu: its Ngf::CCom_List, the list's vector of every item info,
// and the PartsControl each info wraps, whose key is the setting id.
GAME_FIELD(OptionMenuList, 0x230);
GAME_FIELD(ListAllItemsBegin, 0x940);
GAME_FIELD(ListAllItemsEnd, 0x948);
GAME_FIELD(ListItemInfoControl, 0x10);
GAME_FIELD(ListItemKey, 0x10);
constexpr int32_t kListInputDecide = 1;

// CPadManager: a byte set when a press was consumed this frame, and a frame
// count that blocks every button check while above zero. Its update clears
// the byte and counts the frames down.
constexpr size_t kPadManagerExclusive = 0x278;
constexpr size_t kPadManagerLockout = 0x27C;
// Device types of the capture: framework pad ids 1-4 and 9-20 are controller
// buttons, 65-187 keyboard keys and 191-195 mouse buttons.
constexpr int32_t kPadTypeController = 0;
constexpr int32_t kPadTypeKeyboard = 1;
constexpr int32_t kPadTypeMouse = 2;
// Button check modes.
constexpr int32_t kPadModeTrigger = 0;
constexpr int32_t kPadModeHold = 1;
// Button check query, as the key config's wait state builds it: two remap
// flags (the second runs the pad through the key config's remap tables),
// a device mask, a flag of unknown use the wait state sets, a flag
// that ignores the consumed byte, then an Ngf::CString the caller frees when
// its assign mode is 1.
GAME_FIELD(PadQuerySize, 0x60);
GAME_FIELD(PadQueryRemap, 0x20);
GAME_FIELD(PadQueryDeviceMask, 0x24);
GAME_FIELD(PadQueryWaitFlag, 0x28);
constexpr size_t kPadQueryIgnoreExclusive = 0x2A;
GAME_FIELD(PadQueryText, 0x30);
// The main menu's Cancel action, and the action check's record: a byte set
// while the action is down, and the checks made while it was down, which a
// check counts when its query sets the wait flag. The main menu acts on the
// first such check only.
constexpr uint64_t kPadActionCancel = 0x510;
GAME_FIELD(PadActionDown, 128);
GAME_FIELD(PadActionDownChecks, 188);

// Mouse manager: three gate bytes the mouse button check needs (off, active,
// focused), per button records whose flags hold bit 2 while the button is
// held, and the wheel bits of this frame (1 up, 2 down), which framework pads
// 196 and 197 read.
constexpr size_t kMouseGateOff = 1029;
constexpr size_t kMouseGateActive = 1030;
constexpr size_t kMouseGateFocused = 1028;
constexpr size_t kMouseButtons = 424;
constexpr size_t kMouseButtonSize = 104;
constexpr uint32_t kMouseButtonHeld = 2;
constexpr size_t kMouseWheel = 944;
constexpr uint32_t kMouseWheelUp = 1;
constexpr uint32_t kMouseWheelDown = 2;

// Player walk state: its gait (0 idle, 1 walk, 2 walk stop, 3 run, 4 run
// stop) and the animation speed it sets, 1.0 or the dash's 1.25. The player
// unit's animation component holds the animation player whose speed the walk
// update writes.
GAME_FIELD(WalkGait, 0x270);
GAME_FIELD(WalkAnimationSpeed, 0x280);
constexpr int32_t kGaitWalk = 1;
constexpr int32_t kGaitRun = 3;
GAME_FIELD(UnitAnimationComponent, 0x400);
constexpr size_t kAnimationComponentPlayer = 0x558;
GAME_FIELD(AnimationPlayerSpeed, 0x74);

// CCom_Event: its status (refcounted), the status's event data slot, and
// the data's isSkip flag, set on the events the game marks skippable.
GAME_FIELD(EventStatus, 792);
GAME_FIELD(EventStatusData, 224);
constexpr size_t kEventDataIsSkip = 0x110;
// Time manager: fast forward frames left and the magnification they apply,
// as Lua TimeSetFastforwardFrame writes them. Each update multiplies the
// frame time by the magnification, counts the frames down and resets the
// magnification to 1 once none are left. Message and sound waits pass on
// their own while it is above 1.
GAME_FIELD(TimeFastForwardFrames, 0x220);
constexpr size_t kTimeMagnification = 0x228;
// Talk manager byte: set when talk advances on CMD_FAST_NEXT, clear when it
// advances on CMD_NEXT_2.
constexpr size_t kTalkFastNext = 624;
constexpr int64_t kCommandFastNext = 131;
constexpr int64_t kCommandNext2 = 132;

// Event skip. The pad manager keeps the command queries made this frame, by
// the receivers waiting on input, as a vector of 192-byte entries. An entry's
// +16 points at a holder of the command's record, whose first field is the
// command id; the skip passes over records of kind 2.
constexpr size_t kCommandQueriesBegin = 608;
GAME_FIELD(CommandQueriesEnd, 616);
constexpr size_t kCommandQuerySize = 192;
GAME_FIELD(CommandQueryRecord, 16);
GAME_FIELD(CommandRecordKind, 120);
constexpr int32_t kCommandKindIgnored = 2;
// The talk and event commands the skip runs past: CMD_NEXT,
// CMD_FAST_NEXT, CMD_NEXT_2, CMD_FASTFORWARD, CMD_BACKLOG and CMD_AUTO_TEXT
// from the framework's table, CMD_TALK_BACKLOG_VIEW and
// CMD_UNIT_ITEM_INVENTORY_DETAIL from the game's. Talk asks for the backlog
// view every frame. Any other query, such as a choice's cursor or OK, stops
// it.
constexpr int64_t kSkipPassCommands[] = {129, 131, 132, 133, 134, 136, 1004, 1037};
// Pad database: its pad table and the table's end node. A node keeps the
// pad's record holder as its value (kMapNodeValue), and a record's first
// field is the pad id.
GAME_FIELD(PadDatabaseTable, 88);
GAME_FIELD(PadDatabaseTableEnd, 96);
// The virtual Cancel pad of the CMD_FAST_NEXT chord, and the controller B
// button it maps to by default.
constexpr uint64_t kVirtualPadCancel = 274;
constexpr int32_t kControllerPadB = 10;

// Camera control: enabled, zoom enabled, the zoom level (0 to 2), the zoom
// tween and its current value, and the queue of scripted camera requests,
// which must be empty for the zoom to start a tween.
GAME_FIELD(CameraControlEnabled, 784);
constexpr size_t kCameraZoomEnabled = 786;
GAME_FIELD(CameraZoomLevel, 624);
constexpr size_t kCameraZoomTween = 632;
constexpr size_t kCameraZoomValue = 636;
GAME_FIELD(CameraRequestsBegin, 792);
GAME_FIELD(CameraRequestsEnd, 800);
// The zoom of each level, from the poll's table at 0xEB1090, and its tween:
// 5 frames of FadeParam type 27.
constexpr float kCameraZoomLevels[] = {1.2f, 1.35f, 1.5f};
constexpr double kCameraZoomTweenFrames = 5.0;

// CSystemSaveData key table, an unordered_map<u64, u64> of vkey to pad that
// holds only the keys rebound away from their defaults: its list sentinel
// and its size. Nodes are {next, prev, key, value}. The game manager's dirty
// byte makes the next main menu or title options close save the system data.
// The load task's owner holds the loaded system data.
constexpr size_t kSystemKeyTable = 120;
constexpr size_t kMapListHead = 8;
GAME_FIELD(MapSize, 16);
constexpr size_t kMapNodeNext = 0;
constexpr size_t kManagerSystemDirty = 960;
constexpr size_t kTaskOwner = 8;
GAME_FIELD(LoadOwnerSystemData, 536);

// Setting tables: a std::vector of slots at +0x98; each slot points to a
// pointer to the row. A gamesettinglist row names its setting id at +8. A
// gamesetting record has its id at +0, the widget type at +0x88 and one
// UTF-8 label pointer per language at +0xA0.
GAME_FIELD(TableSlotsBegin, 0x98);
GAME_FIELD(TableSlotsEnd, 0xA0);
constexpr size_t kSettingListRowId = 8;
constexpr size_t kSettingRecordId = 0;
GAME_FIELD(SettingRecordType, 0x88);
GAME_FIELD(SettingRecordLabels, 0xA0);
constexpr int32_t kSettingTypeButton = 4;
// SETTING_ETC_ONLINE_MANUAL: a button record no list entry uses, so the
// options menu builds it as a plain button with no handler of its own.
constexpr int64_t kUnusedButtonSettingId = 26;
// SETTINC_ETC_GAME_END, the Exit Game row.
constexpr int64_t kExitGameSettingId = 27;
// The Exit Game popup is attached to the options menu with this mode.
constexpr int32_t kAttachModeMenuPopup = 1;

// Options menu task size, and the pad every task holds at +424.
GAME_FIELD(OptionMenuTaskSize, 0x248);
// Set by closeMethod once the menu's end is queued.
GAME_FIELD(OptionMenuClosing, 0x221);
GAME_FIELD(TaskPad, 424);
// The gamesetting table's unordered_map<int64, record*>, and a map node's
// key and value.
GAME_FIELD(SettingTableMap, 0x58);
GAME_FIELD(MapNodeKey, 16);
GAME_FIELD(MapNodeValue, 24);
GAME_FIELD(MapNodeSize, 32);
// More gamesetting record fields: storage class (0 per save, 1 or 2 system,
// anything else stores nothing), default value, min and max, the choice
// count, and the inline choices, each with one name and one description per
// language and a value.
GAME_FIELD(SettingRecordStorage, 0x30);
GAME_FIELD(SettingRecordDefault, 0x8C);
GAME_FIELD(SettingRecordMin, 0x90);
GAME_FIELD(SettingRecordMax, 0x94);
GAME_FIELD(SettingRecordChoiceCount, 0xD8);
GAME_FIELD(SettingRecordChoices, 0xE0);
GAME_FIELD(SettingChoiceSize, 0x78);
constexpr size_t kSettingChoiceNames = 0;
GAME_FIELD(SettingChoiceDescriptions, 0x38);
GAME_FIELD(SettingChoiceValue, 0x70);
constexpr int32_t kSettingTypeChoice = 0;
// A slider record with no choices lists every number from its min to its
// max, each keyed by the number itself.
constexpr int32_t kSettingTypeSlider = 1;
constexpr int32_t kSettingStorageNone = 3;
// The menu builds a two-choice row as a toggle, and keys each choice item by
// its index, which is the value the setter gets.
constexpr int32_t kToggleChoiceCount = 2;
// SETTING_VSYNC: a two-choice record whose header the mod's toggles copy.
constexpr int64_t kToggleTemplateSettingId = 18;

// Game flags: bit n of the bit array at saveData+144, n being the flag's id in
// the gameflag table.
GAME_FIELD(SaveDataFlags, 144);
constexpr int32_t kFlagGameTimeStop = 200;
constexpr int32_t kFlagPlayerDead = 216;
constexpr int32_t kFlagLateNight = 270;
constexpr int32_t kFlagComingHomeMorning = 280;
constexpr int32_t kFlagStoryStarted = 10000;
// The save data's running events: a std::list sentinel whose nodes hold the
// next node first and the event status at +16. A status's data slot
// (kEventStatusData) points to the event data, whose first field is its id.
GAME_FIELD(SaveDataEvents, 13416);
constexpr size_t kEventNodeNext = 0;
GAME_FIELD(EventNodeStatus, 16);
// Events startLoadGame removes or branches on: one sets the late night NPC
// schedule, one is removed on load, and the rest are late night events.
constexpr uint64_t kLoadBranchEvents[] = {
    100010710000, 100069990000, 100080020000, 100080050000, 100080070000, 100010000,
};

// Screen fade: the fade manager on the UI root, the RENDER_STEP_FADE step,
// COLOR_CLEAR_BLACK (fade in from black), and the 48-byte FadeParam that Lua
// FadeMake builds: {frames, 0, type 27, 0, 0.0, 1.0, 0}.
GAME_FIELD(UiRootFadeManager, 672);
constexpr int32_t kRenderStepFade = 7;
constexpr uint32_t kColorClearBlack = 0;
constexpr int32_t kFadeParamType = 27;
// SLEEP_FADE_IN_TIME, the fade the bed wake-up uses.
constexpr double kWakeFadeFrames = 25.0;

// Save list passed to the row fill: selected index, then a std::vector of
// 88-byte entries.
constexpr size_t kSaveListSelected = 4;
constexpr size_t kSaveListBegin = 8;
GAME_FIELD(SaveListEnd, 16);
GAME_FIELD(SaveEntrySize, 88);
constexpr size_t kSaveEntrySlot = 0;
constexpr size_t kSaveEntryValid = 4;
GAME_FIELD(SaveEntryInfo, 64);
GAME_FIELD(SaveInfoSize, 1072);
constexpr size_t kSaveInfoRefCount = 8;
constexpr size_t kSaveInfoAssignSlot = 6;

// Retail slot layout: 3 groups x 5 rotating autosaves = slots 1..15, and the
// save utility accepts slots up to 16.
constexpr int kGroupCount = 3;
constexpr int kRotationCount = 5;
constexpr int32_t kRetailSlotCapacity = 16;

constexpr int kLanguageCount = 7;
constexpr int kLanguageJapanese = 0;
constexpr int kLanguageEnglish = 1;
constexpr int kLanguageFrench = 2;
constexpr int kLanguageSpanish = 3;

// Script manager: the script root, set once the registrars have run. The
// root keeps its lua_State at +16.
GAME_FIELD(ScriptManagerRoot, 1192);
GAME_FIELD(ScriptRootState, 16);
constexpr size_t kScriptCodeRefCount = 8;
// CScriptPlayer: its refcount, and the byte that stops every update.
constexpr size_t kScriptPlayerRefCount = 24;
GAME_FIELD(ScriptPlayerStopped, 600);
// The 40-byte update info the time manager's update writes each frame, which
// the engine passes to every script player.
constexpr size_t kTimeUpdateInfo = 624;
GAME_FIELD(UpdateInfoSize, 40);
// The engine's script stack, in slot 0 of the thread's TLS array: the depth,
// then one context pointer per level from the bottom. A context's thread
// keeps its name, <module>-<entry>, at +56.
constexpr size_t kTlsScriptDepth = 22216;
constexpr size_t kTlsScriptStack = 23072;
GAME_FIELD(ScriptContextThread, 16);
GAME_FIELD(ScriptBaseName, 56);
// lua_State: top, global state and current call info, whose first field is
// the running function's stack slot. The global state's registry value
// points at a table whose array part holds the main thread, then the globals.
GAME_FIELD(LuaStateTop, 16);
GAME_FIELD(LuaStateGlobal, 24);
GAME_FIELD(LuaStateCallInfo, 32);
GAME_FIELD(LuaGlobalRegistry, 64);
GAME_FIELD(LuaTableArray, 16);
// A Lua 5.4 stack value: the value, then its type tag byte.
GAME_FIELD(LuaValueSize, 16);
constexpr size_t kLuaValueTag = 8;
constexpr uint8_t kLuaTagFalse = 0x01;
constexpr uint8_t kLuaTagTrue = 0x11;
constexpr uint8_t kLuaTagInteger = 0x03;
constexpr uint8_t kLuaTagFloat = 0x13;
// lua_type results.
constexpr int kLuaTypeNil = 0;
constexpr int kLuaTypeBoolean = 1;
constexpr int kLuaTypeNumber = 3;
constexpr int kLuaTypeString = 4;

// debugconfig.dat ids of the configs shipped scripts read with
// DebugGetConfig. Each is 0 or 1, except late night's, which is 0, 1 (a
// late night event can start every night) or 2 (none starts).
constexpr uint64_t kDebugConfigNpcHouse = 229;
constexpr uint64_t kDebugConfigLikabilityEvents = 230;
constexpr uint64_t kDebugConfigLateNight = 240;
constexpr uint64_t kDebugConfigEnvironmentSound = 244;
constexpr uint64_t kDebugConfigStaffRoll = 246;
constexpr uint64_t kDebugConfigRouteWarp = 249;

// =============================================================
// CUSTOM ITEMS
// =============================================================
// CDatabase<ItemData>::load (db, name, blob): parses one table file's blob
// and appends its rows to the database. The game calls it once for the item
// table, on the boot worker. A second call would grow and move the record
// vector and rebuild the maps around it.
GAME_ADDRESS(DatabaseLoad);
// The database getters, each (registry) with kDatabaseRegistry's value.
GAME_ADDRESS(ItemDatabase);
GAME_ADDRESS(CategoryDatabase);
GAME_ADDRESS(IconDatabase);
// Save data addItem (saveData, &status, &outSlot, dryRun): puts a referenced
// CItemStatus into the bag, merging stacks, and returns the count that did
// not fit. On the way it bumps the letter and quest counters, registers the
// encyclopedia, refreshes the item lists and, for the first item ever, sets
// flag 700. GameAddInventoryItem 0x27DF60 builds the status and calls it.
GAME_ADDRESS(AddInventoryItem);
// Letter status condition counter (letters, kind, target, value, count):
// counts towards every lettersend row that matches, sends its letter and
// sets its flag at the threshold.
GAME_ADDRESS(LetterCondition);
// Encyclopedia register (saveData, id, rank, notify): inserts the id, and
// for an eligible item recounts completion, which can send milestone
// letters, set flags and unlock achievements.
GAME_ADDRESS(EncyclopediaRelease);
// Story flag set (saveData, flag).
GAME_ADDRESS(SetSaveFlag);
// Save data prepareForSave (saveData), the save task's prepare step, on the
// main thread before the save data is serialized. Its only caller is the
// auto save task's init, which every save goes through.
GAME_ADDRESS(PrepareForSave);
// The "SAVEDATA" serialize function, which writes the save data into the
// save buffer.
GAME_ADDRESS(SerializeSave);
// CItemStatus constructor (status, saveData, id): takes a new uid from the
// save data, registers the status in the uid map, finds the item record and
// its icon, and sets the count to 1.
GAME_ADDRESS(ItemStatusCtor);
// Frees an unordered_map node the way the game's erase does (unused, node).
GAME_ADDRESS(MapNodeFree);
// Save data bag subtract (saveData, slot, count): takes up to count from
// the bag slot, frees the slot when it empties, recounts the bag and sets
// flag 700. True when the slot emptied.
GAME_ADDRESS(SubInventoryItem);
// CItemStatus vtable.
GAME_ADDRESS(ItemStatusVtable);

// A database's id map, an unordered_map<u64, record holder>. The end
// sentinel is the map's list head (kMapListHead).
GAME_FIELD(DatabaseMap, 88);
// unordered_map: buckets, each the first and last node of its run, and the
// bucket mask. A node's previous link.
GAME_FIELD(MapBuckets, 24);
GAME_FIELD(MapMask, 48);
constexpr size_t kMapNodePrev = 8;
// ItemData record: id, category, icon and outline icon.
constexpr size_t kItemRecordId = 0;
GAME_FIELD(ItemRecordCategory, 352);
GAME_FIELD(ItemRecordIcon, 960);
constexpr size_t kItemRecordOutline = 968;
// CItemStatus, 704 bytes: refcount, owning save data, uid, the record
// holder (its first field is the record), count, rank and slot state.
constexpr size_t kItemStatusSize = 704;
constexpr size_t kItemStatusRefCount = 8;
GAME_FIELD(ItemStatusSaveData, 216);
GAME_FIELD(ItemStatusUid, 224);
GAME_FIELD(ItemStatusData, 576);
GAME_FIELD(ItemStatusCount, 608);
GAME_FIELD(ItemStatusRank, 640);
constexpr size_t kItemStatusState = 660;
// Save data: the bag size bits (30 slots with the large bit, 20 with the
// medium one, else 10), the bag's 30 slots and the 11 tool slots.
GAME_FIELD(SaveDataCapacity, 904);
constexpr uint64_t kCapacityLarge = 0x400000;
constexpr uint64_t kCapacityMedium = 0x200000;
GAME_FIELD(SaveDataInventory, 12992);
constexpr int32_t kInventorySlots = 30;
GAME_FIELD(SaveDataTools, 13240);
constexpr int32_t kToolSlots = 11;
// The dropped item list, a std::list of CItemStatus*.
GAME_FIELD(SaveDataDropList, 13336);
// Vectors of CItemStatus*: shipping, hunt rewards, construction, bundle
// rewards and the mail order catalog list. The day change mails the catalog
// list as letter 212 and clears it.
constexpr size_t kSaveDataItemVectors[] = {13632, 13656, 13680, 13704, 14488};
// unordered_maps keyed by item id: encyclopedia, favorite crafts and
// favorite recipes. The uid map, keyed by uid, holds every live status.
GAME_FIELD(SaveDataEncyclopedia, 13984);
GAME_FIELD(SaveDataFavoriteCraft, 14112);
GAME_FIELD(SaveDataFavoriteCook, 14176);
GAME_FIELD(SaveDataUidMap, 14512);
// std::list: head node and size; a node's links and value.
constexpr size_t kListHead = 0;
constexpr size_t kListSize = 8;
constexpr size_t kListNodeNext = 0;
constexpr size_t kListNodePrev = 8;
GAME_FIELD(ListNodeValue, 16);
// The log line "got an item", as GameAddInventoryItem pushes it.
constexpr int64_t kItemGetLogId = 1000;

// =============================================================
// OVERLAY SAVE
// =============================================================
// Save data reset (saveData): fills a fresh save data with the new game
// state, the default-on game flags included, telling no flag listener.
// Called at boot (0x4DE810) and by the title menu (0x4513D0), each with the
// save data it just installed. The save task's prepare step and write are
// kPrepareForSave and kSerializeSave above.
GAME_ADDRESS(ResetSaveData);
// The post-load fixup, called right after a loaded save data is installed
// by the AutoLoad (0x472620) and ListLoad (0x479820) callbacks. Its rcx is
// not the save data; read the live one after it.
GAME_ADDRESS(AfterLoadFixup);
// Continue mode reload (continueData): reads the continue snapshot back
// into the live save data in place. Its only caller is
// GameLoadContinueData 0x48FB20.
GAME_ADDRESS(LoadContinueInPlace);
// Game flag write (saveData, id, on): sets or clears one bit of the flag
// set at kSaveDataFlags and, when it changed, tells the flag listeners.
GAME_ADDRESS(SetFlagValue);
// The flag set is CBitFlag<99999>, with no bounds check.
constexpr int32_t kFlagCount = 99999;
// The mod flag band. No game table row (highest vanilla id 90120), Lua
// script constant or native constant uses an id in it.
constexpr int32_t kModFlagFirst = 95000;
constexpr int32_t kModFlagLast = 99998;

// =============================================================
// CUSTOM VILLAGERS
// =============================================================
// CDatabase<CharaData>::load and CDatabase<CharaScheduleData>::load, (db,
// name, blob) like kDatabaseLoad. The game calls each once, on the boot
// worker, for character.dat and characterschedule.dat.
GAME_ADDRESS(CharaTableLoad);
GAME_ADDRESS(CharaScheduleLoad);
// The steps the new game reset (kResetSaveData) takes for each character
// row. CMapObjectStatus constructor (status, saveData, &unitHolder): takes a
// uid, registers the status in the uid map and keeps the unit holder.
GAME_ADDRESS(MapObjectStatusCtor);
// CUnitStatus member init (member), on status + kUnitStatusMemberData.
GAME_ADDRESS(UnitStatusMemberInit);
// An empty unordered_map<u64, T> (map), on status + kNpcStatusMap.
GAME_ADDRESS(Int64MapInit);
// The villager's talk history (history), on status + kNpcStatusTalkHistory.
GAME_ADDRESS(NpcTalkHistoryInit);
// An area's origin (float out[4], unitHolder, areaId), 16-byte aligned out.
GAME_ADDRESS(AreaOrigin);
// Unit status setPosition (status, float position[4], mode 1).
GAME_ADDRESS(UnitSetPosition);
// CNpcStatus init, its primary vtable's slot kNpcStatusInitSlot.
GAME_ADDRESS(NpcStatusInitCode);
// Vtables: CUnitStatus, CNpcStatus (each primary and second), and the love
// gauge, Ngf::CFixGauge<int, 0, 2100, 0>.
GAME_ADDRESS(UnitStatusVtable);
GAME_ADDRESS(UnitStatusSecondVtable);
GAME_ADDRESS(NpcStatusVtable);
GAME_ADDRESS(NpcStatusSecondVtable);
GAME_ADDRESS(LoveGaugeVtable);

// A database's record holder vector, begin and end. Each element is a
// holder whose first field is the record.
GAME_FIELD(DatabaseRecords, 152);
// CharaData record: id, starting area, starts enabled, starting x and y.
constexpr size_t kCharaRecordId = 0;
constexpr size_t kCharaRecordInitArea = 352;
constexpr size_t kCharaRecordInitEnabled = 616;
constexpr size_t kCharaRecordInitX = 620;
constexpr size_t kCharaRecordInitY = 624;
// CNpcStatus, 864 bytes, zeroed when allocated: refcount, owning save data,
// uid, unit holder, position, enabled byte, the character record holder,
// index (-1), home position, love gauge vtable and value (0 to 2100), a
// u64 map, the talk history and a last field. The second vtable sits at
// kStatusSecondVtable, and the CUnitStatus members at kUnitStatusMembers
// and kUnitStatusMemberData.
constexpr size_t kNpcStatusSize = 864;
constexpr size_t kNpcStatusRefCount = 8;
GAME_FIELD(StatusSecondVtable, 16);
GAME_FIELD(NpcStatusSaveData, 216);
GAME_FIELD(NpcStatusUid, 224);
GAME_FIELD(NpcStatusUnitHolder, 232);
GAME_FIELD(NpcStatusPosition, 240);
constexpr size_t kNpcStatusEnabled = 304;
GAME_FIELD(UnitStatusMembers, 576);
GAME_FIELD(UnitStatusMemberData, 584);
GAME_FIELD(NpcStatusChara, 656);
constexpr size_t kNpcStatusIndex = 680;
constexpr size_t kNpcStatusHome = 688;
constexpr size_t kNpcStatusLove = 704;
GAME_FIELD(NpcStatusLoveValue, 712);
constexpr size_t kNpcStatusMap = 720;
constexpr size_t kNpcStatusTalkHistory = 784;
constexpr size_t kNpcStatusLast = 856;
GAME_FIELD(NpcStatusInitSlot, 40);
// Save data: the villager list, a std::list of CNpcStatus*. A list node is
// next, previous and value.
GAME_FIELD(SaveDataNpcList, 13352);
GAME_FIELD(ListNodeSize, 24);

// =============================================================
// MOD OBJECTS
// =============================================================
// Save data: every placed gimmick, a std::list of CGimmickStatus*.
constexpr size_t kSaveDataGimmickList = 13384;
// CGimmickStatus: the uid the save keeps, and pData_, a pointer to the
// value slot (kMapNodeValue) of the gimmick database node of its row. The
// save writes the row's id through it; a load of an id the table lacks
// stores null, which the status loader then reads.
constexpr size_t kGimmickStatusUid = 224;
constexpr size_t kGimmickStatusData = 576;

// =============================================================
// MOD CROPS
// =============================================================
// Save data (serialized by 0x145090): every map's status, an
// unordered_map<u64, intrusive_ptr<CMapStatus>> keyed by map id. A
// CMapStatus keeps its cells in an unordered_map<i64,
// intrusive_ptr<CMapCellStatus>> keyed by cell, and a cell holds its main and
// its sub crop, each a CCropsStatus* or null. A CCropsStatus's pData_, like a
// gimmick status's, points at the value slot (kMapNodeValue) of the crops
// database node of its row; the save writes the row's id through it
// (0xF4CA0), and a load of an id the table lacks stores null.
constexpr size_t kSaveDataMapStatusMap = 13728;
constexpr size_t kMapStatusCells = 88;
constexpr size_t kCellStatusCrops = 24;
constexpr size_t kCellStatusSubCrops = 32;
constexpr size_t kCropsStatusData = 16;
// An item record's action id. The plant check 0x24E610 looks a seed's action
// value up in the crops database only when the action id is Plant (1060), and
// reads the record it finds without a null check.
constexpr size_t kItemRecordAction = 560;

// =============================================================
// MOD SHOPS
// =============================================================
// Save data: every shop, a std::vector of intrusive pointers to
// CStoreStatus. Each day (GameDailyUpdate 0x48D460 -> 0x2BC150) every
// shop's list is rebuilt from the storesales rows of its storeID whose
// seasons and flags hold (filter 0x16BDA0), carrying the stock of an entry it
// already had when the row does not restock.
GAME_FIELD(SaveDataStoreList, 13432);
// CStoreStatus: storeID_ and saleItems_, a std::vector of CSaleItemInfo held
// by value.
constexpr size_t kStoreStatusId = 16;
constexpr size_t kStoreStatusSales = 24;
// CSaleItemInfo (ctor 0x1604D0), 56 bytes: the vtable, then pData_ and
// pSalesData_, pointers to the value slots of the item and storesales
// database nodes of its rows, which the save writes as their ids, then the
// stock (-1 for unlimited), rank, limit day, size and silhouette flag. The
// list sort (0x160E00) reads pSalesData_ of every entry.
GAME_ADDRESS(SaleItemInfoVtable);
// StoreSalesData, the storesales record: the item it sells.
constexpr size_t kStoreSalesItem = 48;
GAME_FIELD(SaleItemSize, 56);
constexpr size_t kSaleItemData = 16;
constexpr size_t kSaleItemSalesData = 24;
GAME_FIELD(SaleItemStock, 32);
constexpr size_t kSaleItemRank = 36;
constexpr size_t kSaleItemLimitDay = 40;
constexpr size_t kSaleItemSizeValue = 44;
constexpr size_t kSaleItemSilhouette = 48;
// std::allocator<T>: allocate(bytes) and deallocate(pointer, bytes), with
// the big block alignment of blocks of 4096 bytes and more.
GAME_ADDRESS(VectorAllocate);
GAME_ADDRESS(VectorDeallocate);

// =============================================================
// TITLE BADGE
// =============================================================
// CTask_Title::initialize_Inner(task, a2). Builds every layer and widget of
// the title in one pass, once per title task, on the game thread. Among them
// the version: a CUIUnion_Number on the title_base layer's anime, under the
// layout key kTitleVersionKey.
GAME_ADDRESS(TitleInit);
// Ngf::addLayerUIParts<CUIUnion_Text>(union, &partsSetting, &setting,
// const std::function& provider): builds a text widget and adds it to the
// union's anime as a callback of the parts setting's key. A key holds a
// list, so several widgets may share one.
GAME_ADDRESS(AddTextPart);
// Ngf::CUIPartsSetting ctor(out, type, name, &layerPath, &key, &transform,
// flags). Copies the name, the path, the key and the 96-byte transform.
GAME_ADDRESS(PartsSettingCtor);
// Layer path builder (out, layer): a path of one entry on that layer.
GAME_ADDRESS(MakeLayerPath);
// Ngf::CTextParam ctor(param, text): the default font, white, the text
// copied. Its copy ctor (out, &param) and its dtor (param).
GAME_ADDRESS(TextParamCtor);
GAME_ADDRESS(TextParamCopy);
GAME_ADDRESS(TextParamDtor);
// CUIUnion_Text::CSetting dtor(setting), not deleting, and its vtable.
GAME_ADDRESS(TextSettingDtor);
GAME_ADDRESS(TextSettingVtable);
// Ngf::CString dtor(text), not deleting.
GAME_ADDRESS(StringDtor);

// Title task: its title_base layer. A layer: its CUIUnion_Anime.
GAME_FIELD(TitleBaseLayer, 536);
GAME_FIELD(LayerUnion, 32);
// The layout key the version's widget hangs on, "number01" as 8 bytes, and
// the layer the title builds its path on.
constexpr uint64_t kTitleVersionKey = 0x31307265626D756E;
constexpr int32_t kTitleVersionLayer = 3;
// CUIPartsSetting: 920 bytes, of which the game reserves 928. Its name, a
// CString, at 8, its layer path at 48, and the flags the title passes.
GAME_FIELD(PartsSettingSize, 928);
constexpr size_t kPartsSettingName = 8;
GAME_FIELD(PartsSettingPath, 48);
constexpr int32_t kPartsSettingFlags = 0x1E387C7;
// Layer path: an entry count, then fixed entries.
constexpr size_t kLayerPathSize = 760;
// CTextParam, 336 bytes. CUIUnion_Text::CSetting, 512 bytes: the vtable, a
// 16-bit 1 at kTextSettingFlag, the text param at kTextSettingParam, and two
// optional callbacks whose presence bytes, at 424 and 504, stay 0.
constexpr size_t kTextParamSize = 336;
GAME_FIELD(TextSettingSize, 512);
constexpr size_t kTextSettingFlag = 8;
GAME_FIELD(TextSettingParam, 16);

// =============================================================
// CUSTOM WHISTLES
// =============================================================
// CDatabase<WhistleData>::load, (db, name, blob) like kDatabaseLoad. The
// game calls it once, on the boot worker, for whistle.dat.
GAME_ADDRESS(WhistleTableLoad);
// WhistleData record: learn flag (i32), notes (first note in the highest
// bit, 1 for long) and note count (i32).
constexpr size_t kWhistleRecordLearn = 104;
constexpr size_t kWhistleRecordNotes = 120;
constexpr size_t kWhistleRecordNoteCount = 128;

// =============================================================
// CUSTOM RECIPES
// =============================================================
// CDatabase<CraftData>::load and CDatabase<CookingData>::load, (db, name,
// blob) like kDatabaseLoad. The game calls each once, on the boot worker,
// for craft.dat and cooking.dat.
GAME_ADDRESS(CraftTableLoad);
GAME_ADDRESS(CookingTableLoad);
// unordered_map<u64, bool>::try_emplace (map, result, &key): writes the
// node and whether it was inserted to result. The node's bool starts false.
GAME_ADDRESS(FavoriteInsert);
// CraftData record: output item, output count (i32), unlock flag.
constexpr size_t kCraftRecordOutput = 176;
constexpr size_t kCraftRecordCount = 184;
constexpr size_t kCraftRecordUnlock = 208;
// CookingData record: output item, output count (i32), unlock flag.
constexpr size_t kCookingRecordOutput = 88;
GAME_FIELD(CookingRecordCount, 96);
constexpr size_t kCookingRecordUnlock = 240;

// =============================================================
// TABLE ENGINE AND MOD ART
// =============================================================
// CDatabaseManager::load (manager, CLoadQueue* or null). Game init calls it
// for the framework tables on the main thread, the startup task for the
// game tables on the boot loader thread. It loads every pending table
// through the job system, which calls each table's load through vtable
// slot 1 as (db, path, bytes, size).
GAME_ADDRESS(DatabaseManagerLoad);
// The pending map: a list whose head node pointer is at +8. A node holds
// a vector of 304-byte elements at +24..+32, each with its path (char*)
// at +8 and its CDatabase at +296.
constexpr size_t kManagerPendingList = 8;
GAME_FIELD(PendingNodeBegin, 24);
GAME_FIELD(PendingNodeEnd, 32);
GAME_FIELD(PendingElementSize, 304);
constexpr size_t kPendingElementPath = 8;
GAME_FIELD(PendingElementDatabase, 296);
// CDatabase<T>::load in the table's vtable.
constexpr size_t kDatabaseLoadSlot = 8;
// TextureData record: the file per language (char*, 7).
constexpr size_t kTextureRecordFile = 88;
// IconData record: the texture id, then the draw info per language, 32
// bytes each: source x, y, w, h, drawn w, h, offset x, y (floats).
constexpr size_t kIconRecordTexture = 88;
constexpr size_t kIconRecordDrawInfo = 96;
// ICON_ID_ITEM_TEST, whose texture stands in for a missing one.
constexpr uint64_t kFallbackIcon = 1000;

// =============================================================
// ADDRESS LISTS
// =============================================================
// Constants above that no instruction gives away, each checked through a
// GAME_FIELD of the same layout: its size, a field after it, or its
// partner in the same library object, so that adding or removing a field
// before it changes the witness too. A reorder that keeps both in place
// is not caught.
GAME_FIELD_VIA(FsArchiveRoot, FsDefaultRoot);
GAME_FIELD_VIA(BgmTableSlotsEnd, BgmTableSlotsBegin);
GAME_FIELD_VIA(SaveUtilSlotCapacity, SaveUtilOperation);
GAME_FIELD_VIA(SaveUtilState, SaveUtilOperation);
GAME_FIELD_VIA(SaveDataStateFlags, SaveDataEncyclopedia);
GAME_FIELD_VIA(UnitEntity, UnitAnimationComponent);
GAME_FIELD_VIA(ComponentEntity, CollisionBodiesBegin);
GAME_FIELD_VIA(ContactNodeNext, ContactNodeBody);
GAME_FIELD_VIA(EventRecordId, EventRecordForbidden);
GAME_FIELD_VIA(EventRecordFlagSlots, EventRecordForbiddenCount);
GAME_FIELD_VIA(TaskParent, TaskRefCount);
GAME_FIELD_VIA(SettingRefCount, SettingSlot);
GAME_FIELD_VIA(SoundNodeNext, SoundNodePlayer);
GAME_FIELD_VIA(CStringData, CStringSize);
GAME_FIELD_VIA(RowRotation, LoadMenuRowSize);
GAME_FIELD_VIA(RowListIndex, LoadMenuRowSize);
GAME_FIELD_VIA(RowInfo, LoadMenuRowSize);
GAME_FIELD_VIA(HeaderFunctorState, ListComponent);
GAME_FIELD_VIA(CardRowListIndex, CardRowSize);
GAME_FIELD_VIA(CardRowInfo, CardRowSize);
GAME_FIELD_VIA(CardFunctorTask, CardFunctorGroup);
GAME_FIELD_VIA(CarouselFunctorState, CarouselFunctorList);
GAME_FIELD_VIA(PadQueryIgnoreExclusive, PadQuerySize);
GAME_FIELD_VIA(CommandQueriesBegin, CommandQueriesEnd);
GAME_FIELD_VIA(CameraZoomTween, CameraControlEnabled);
GAME_FIELD_VIA(CameraZoomValue, CameraControlEnabled);
GAME_FIELD_VIA(MapListHead, MapSize);
GAME_FIELD_VIA(MapNodeNext, MapNodeSize);
GAME_FIELD_VIA(TaskOwner, TaskRefCount);
GAME_FIELD_VIA(SettingRecordId, SettingRecordChoices);
GAME_FIELD_VIA(SettingChoiceNames, SettingChoiceSize);
GAME_FIELD_VIA(EventNodeNext, EventNodeStatus);
GAME_FIELD_VIA(SaveListSelected, SaveListEnd);
GAME_FIELD_VIA(SaveListBegin, SaveListEnd);
GAME_FIELD_VIA(SaveEntrySlot, SaveEntrySize);
GAME_FIELD_VIA(SaveEntryValid, SaveEntrySize);
GAME_FIELD_VIA(SaveInfoRefCount, SaveInfoSize);
GAME_FIELD_VIA(SaveInfoAssignSlot, SaveInfoSize);
GAME_FIELD_VIA(LuaValueTag, LuaValueSize);
GAME_FIELD_VIA(MapNodePrev, MapNodeSize);
GAME_FIELD_VIA(ItemRecordId, ItemRecordIcon);
GAME_FIELD_VIA(ItemStatusRefCount, ItemStatusRank);
GAME_FIELD_VIA(ListNodeNext, ListNodeSize);
GAME_FIELD_VIA(ListNodePrev, ListNodeSize);
GAME_FIELD_VIA(NpcStatusRefCount, NpcStatusChara);
GAME_FIELD_VIA(NpcStatusEnabled, NpcStatusChara);
GAME_FIELD_VIA(SaveDataGimmickList, SaveDataEncyclopedia);
GAME_FIELD_VIA(SaveDataMapStatusMap, SaveDataEncyclopedia);
GAME_FIELD_VIA(ItemRecordAction, ItemRecordIcon);
GAME_FIELD_VIA(SaleItemData, SaleItemSize);
GAME_FIELD_VIA(SaleItemSalesData, SaleItemSize);
GAME_FIELD_VIA(SaleItemRank, SaleItemSize);
GAME_FIELD_VIA(SaleItemLimitDay, SaleItemSize);
GAME_FIELD_VIA(SaleItemSizeValue, SaleItemSize);
GAME_FIELD_VIA(SaleItemSilhouette, SaleItemSize);
GAME_FIELD_VIA(PartsSettingName, PartsSettingSize);
GAME_FIELD_VIA(TextSettingFlag, TextSettingSize);
GAME_FIELD_VIA(CookingRecordOutput, CookingRecordCount);
GAME_FIELD_VIA(PendingElementPath, PendingElementSize);

// Every address and field each feature uses, one list per feature. A
// feature whose list has a requirement the running build does not meet
// stays off.

constexpr const Requirement* kSharedRequirements[] = {
    &kFrameUpdate,
    &kAttachTask,
    &kAllocate,
    &kGetLogTextById,
    &kPopupSelectCtor,
    &kReturnToTitle,
    &kSoundFade,
    &kIsReceiverActive,
    &kCStringVtable,
    &kEngineHeap,
    &kGameManager,
    &kLanguage,
    &kPlayerComponentVtable,
    &kSoundManager,
    &kSoundSystem,
    &kStopAfterFadeVtable,
    &kDatabaseRegistry,
    &kInt64MapFind,
    &kItemDatabase,
    &kFileSystem,
    &kManagerSaveDataField,
    &kTaskRefCountField,
    &kAllocationAlignmentField,
    &kSoundNodePlayerField,
    &kSoundSystemBgmManagerField,
    &kBgmManagerPlayersField,
    &kCStringSizeField,
    &kCStringLengthField,
    &kCStringModeField,
    // Database lookups every mod table kind makes.
    &kMapSizeField,
    &kMapNodeValueField,
    &kDatabaseMapField,
    &kSaveDataPlayerStatusField,
    &kSaveDataFieldBlockerField,
    &kSaveDataStateFlagsField,
    &kSoundNodeNextField,
    &kCStringDataField,
    &kMapListHeadField,
    &kSoundManagerPlayersField,
    &kScriptManagerRootField,
};

// Mod art reads table records through these, beside the table engine.
constexpr const Requirement* kModArtRequirements[] = {
    &kDatabaseRecordsField,
};

// Mod objects walk these lists, beside the table engine.
constexpr const Requirement* kModObjectsRequirements[] = {
    &kListNodeValueField,
    &kListNodeNextField,
    &kSaveDataGimmickListField,
};

// Mod crops walk the saved crop maps through these.
constexpr const Requirement* kModCropsRequirements[] = {
    &kMapSizeField,
    &kMapNodeKeyField,
    &kMapNodeValueField,
    &kMapListHeadField,
    &kMapNodeNextField,
    &kSaveDataMapStatusMapField,
    &kItemRecordActionField,
};

constexpr const Requirement* kExitRequirements[] = {
    &kGameDestroy,
};

constexpr const Requirement* kStartupRequirements[] = {
    &kGameInit,
    &kFileSystem,
};

constexpr const Requirement* kFileOverrideRequirements[] = {
    &kFindArchiveEntry,
    &kBuildPath,
    &kFsExists,
    &kFsGetSize,
    &kFsOpen,
    &kOpenFileHandle,
    &kFileSystem,
    &kFsDefaultRootField,
    &kBuildPathCapacityField,
    &kFsArchiveRootField,
};

constexpr const Requirement* kEventRequirements[] = {
    &kTitleCtor,
    &kStartLoadGame,
};

// The same acknowledge VillageQoL's quick save hooks; the hook chain runs
// both.
constexpr const Requirement* kSaveDataRequirements[] = {
    &kSaveUtilAcknowledge,
    &kSaveUtilStatusField,
    &kSaveUtilOperationField,
    &kSaveListEndField,
    &kSaveEntrySizeField,
    &kSaveUtilStateField,
    &kSaveListSelectedField,
    &kSaveListBeginField,
    &kSaveEntrySlotField,
};

// The Lua host calls or detours every one of these. timeUpdate is the
// writer of the update info it copies.
constexpr const Requirement* kLuaHostRequirements[] = {
    &kRegisterGameCommands,
    &kScriptModuleLoader,
    &kScriptGetCode,
    &kScriptCreatePlayer,
    &kScriptPlayerUpdate,
    &kTimeUpdate,
    &kLuaSetTop,
    &kLuaType,
    &kLuaToLString,
    &kLuaPushLString,
    &kLuaPushString,
    &kLuaPushCClosure,
    &kLuaGetStr,
    &kLuaCreateTable,
    &kLuaSetStr,
    &kLuaSetGlobal,
    &kLuaPCallK,
    &kLuaError,
    &kLuaLoadBufferX,
    &kScriptManager,
    &kTimeManager,
    &kScriptRootStateField,
    &kScriptPlayerStoppedField,
    &kUpdateInfoSizeField,
    &kScriptContextThreadField,
    &kScriptBaseNameField,
    &kLuaStateTopField,
    &kLuaStateGlobalField,
    &kLuaStateCallInfoField,
    &kLuaGlobalRegistryField,
    &kLuaTableArrayField,
};

// Custom items detour or call every one of these.
constexpr const Requirement* kModItemsRequirements[] = {
    &kDatabaseLoad,
    &kAddInventoryItem,
    &kLetterCondition,
    &kEncyclopediaRelease,
    &kSetSaveFlag,
    &kItemStatusCtor,
    &kItemDatabase,
    &kCategoryDatabase,
    &kIconDatabase,
    &kInt64MapFind,
    &kAllocate,
    &kPushLog,
    &kMapNodeFree,
    &kSubInventoryItem,
    &kDatabaseRegistry,
    &kGameManager,
    &kItemStatusVtable,
    &kManagerSaveDataField,
    &kMapSizeField,
    &kMapNodeKeyField,
    &kMapNodeValueField,
    &kItemRecordCategoryField,
    &kItemRecordIconField,
    &kItemStatusSaveDataField,
    &kItemStatusUidField,
    &kItemStatusDataField,
    &kItemStatusCountField,
    &kItemStatusRankField,
    &kSaveDataCapacityField,
    &kSaveDataInventoryField,
    &kSaveDataEncyclopediaField,
    &kListNodeValueField,
    &kMapListHeadField,
    &kMapNodeNextField,
    &kItemRecordIdField,
    &kItemStatusRefCountField,
    &kSaveDataToolsField,
    &kSaveDataDropListField,
    &kListNodeNextField,
    &kListNodePrevField,
    &kSaveDataFavoriteCraftField,
    &kSaveDataFavoriteCookField,
    &kSaveDataUidMapField,
};

// The shop list splice calls both of these.
constexpr const Requirement* kModShopsRequirements[] = {
    &kVectorAllocate,
    &kVectorDeallocate,
    &kSaleItemInfoVtable,
    &kSaleItemSizeField,
    &kSaleItemStockField,
    &kSaveDataStoreListField,
    &kSaleItemDataField,
    &kSaleItemSalesDataField,
    &kSaleItemRankField,
    &kSaleItemLimitDayField,
    &kSaleItemSizeValueField,
    &kSaleItemSilhouetteField,
};

// The overlay save core detours every one of these.
constexpr const Requirement* kSaveOverlayRequirements[] = {
    &kResetSaveData,
    &kPrepareForSave,
    &kSerializeSave,
    &kAfterLoadFixup,
    &kLoadContinueInPlace,
    &kGameManager,
    &kManagerSaveDataField,
    &kMapSizeField,
    &kMapNodeKeyField,
    &kMapBucketsField,
    &kMapMaskField,
    &kMapListHeadField,
    &kMapNodeNextField,
    &kMapNodePrevField,
};

// Mod flags call this.
constexpr const Requirement* kModFlagsRequirements[] = {
    &kSetFlagValue,
    &kSaveDataFlagsField,
};

// Custom villagers detour or call every one of these.
constexpr const Requirement* kModVillagersRequirements[] = {
    &kCharaTableLoad,
    &kCharaScheduleLoad,
    &kMapObjectStatusCtor,
    &kUnitStatusMemberInit,
    &kInt64MapInit,
    &kNpcTalkHistoryInit,
    &kAreaOrigin,
    &kUnitSetPosition,
    &kNpcStatusInitCode,
    &kInt64MapFind,
    &kAllocate,
    &kGameManager,
    &kLoveGaugeVtable,
    &kNpcStatusVtable,
    &kNpcStatusSecondVtable,
    &kUnitStatusVtable,
    &kUnitStatusSecondVtable,
    &kManagerSaveDataField,
    &kMapSizeField,
    &kMapNodeValueField,
    &kDatabaseMapField,
    &kListNodeValueField,
    &kDatabaseRecordsField,
    &kStatusSecondVtableField,
    &kNpcStatusSaveDataField,
    &kNpcStatusUidField,
    &kNpcStatusUnitHolderField,
    &kNpcStatusPositionField,
    &kUnitStatusMemberDataField,
    &kNpcStatusCharaField,
    &kNpcStatusInitSlotField,
    &kListNodeSizeField,
    &kMapListHeadField,
    &kListNodeNextField,
    &kListNodePrevField,
    &kNpcStatusRefCountField,
    &kNpcStatusEnabledField,
    &kUnitStatusMembersField,
    &kSaveDataNpcListField,
    &kSaveDataUidMapField,
    &kNpcStatusLoveValueField,
};

// Custom whistles detour or call every one of these.
constexpr const Requirement* kModWhistlesRequirements[] = {
    &kWhistleTableLoad,
    &kInt64MapFind,
    &kDatabaseRecordsField,
};

// Custom recipes detour or call every one of these.
constexpr const Requirement* kModRecipesRequirements[] = {
    &kCraftTableLoad,
    &kCookingTableLoad,
    &kFavoriteInsert,
    &kItemDatabase,
    &kMapNodeFree,
    &kInt64MapFind,
    &kMapSizeField,
    &kMapNodeKeyField,
    &kMapNodeValueField,
    &kDatabaseRecordsField,
    &kCookingRecordCountField,
    &kMapListHeadField,
    &kMapNodeNextField,
    &kCookingRecordOutputField,
    &kSaveDataFavoriteCraftField,
    &kSaveDataFavoriteCookField,
};

// The table engine detours the manager's load; mod art finds records.
constexpr const Requirement* kTableEngineRequirements[] = {
    &kInt64MapFind,
    &kDatabaseManagerLoad,
    &kPendingNodeBeginField,
    &kPendingNodeEndField,
    &kPendingElementSizeField,
    &kPendingElementDatabaseField,
    &kPendingElementPathField,
};

constexpr const Requirement* kToastRequirements[] = {
    &kGetLogText,
    &kPushLog,
    &kGameManager,
    &kUiTaskRoot,
    &kGameLogIdField,
    &kGameLogSourceField,
    &kGameLogTagField,
    &kTaskRefCountField,
    &kLayerKeySizeField,
};

constexpr const Requirement* kPopupRequirements[] = {
    &kMakeLayerKey,
    &kGameManager,
    &kUiTaskRoot,
    &kGameLogIdField,
    &kGameLogSourceField,
    &kGameLogTagField,
    &kTaskRefCountField,
    &kLayerKeySizeField,
};

constexpr const Requirement* kModDebugRequirements[] = {
    &kDebugIsOn,
    &kDebugIsOnName,
    &kDebugGetConfig,
    &kDebugGetConfigName,
    &kDebugPrint,
    &kDebugPrintName,
    &kDebugDisp,
    &kDebugDispName,
    &kDebugScreenText,
    &kDebugScreenTextName,
    &kLuaType,
    &kLuaToLString,
    &kLuaStateTopField,
    &kLuaStateCallInfoField,
    &kLuaValueSizeField,
    &kLuaValueTagField,
};

constexpr const Requirement* kTitleBadgeRequirements[] = {
    &kTitleInit,
    &kAddTextPart,
    &kPartsSettingCtor,
    &kMakeLayerPath,
    &kTextParamCtor,
    &kTextParamCopy,
    &kTextParamDtor,
    &kTextSettingDtor,
    &kStringDtor,
    &kTextSettingVtable,
    &kTitleBaseLayerField,
    &kLayerUnionField,
    &kPartsSettingSizeField,
    &kPartsSettingPathField,
    &kTextSettingSizeField,
    &kTextSettingParamField,
    &kPartsSettingNameField,
    &kTextSettingFlagField,
};

constexpr const Requirement* kInputRequirements[] = {
    &kPadCapture,
    &kCStringVtable,
    &kControllerStates,
    &kDatabaseRegistry,
    &kEmptyStringData,
    &kKeyboardState,
    &kMouseManager,
    &kPadManager,
    &kKeyboardDownField,
    &kCStringModeField,
    &kPadQuerySizeField,
    &kPadQueryDeviceMaskField,
    &kPadQueryWaitFlagField,
    &kPadQueryTextField,
    &kCStringDataField,
    &kPadQueryIgnoreExclusiveField,
};

constexpr const Requirement* kWalkSpeedRequirements[] = {
    &kPlayerWalkUpdate,
    &kPlayerGetMoveSpeed,
    &kWalkGaitField,
    &kWalkAnimationSpeedField,
    &kUnitAnimationComponentField,
    &kAnimationPlayerSpeedField,
};

constexpr const Requirement* kEventFastForwardRequirements[] = {
    &kEventSetup,
    &kEventDestroy,
    &kTimeUpdate,
    &kPadPushCommand,
    &kDatabaseRegistry,
    &kPadManager,
    &kTalkManager,
    &kTimeManager,
    &kEventStatusField,
    &kEventStatusDataField,
    &kTimeFastForwardFramesField,
    &kCommandQueriesEndField,
    &kCommandQueryRecordField,
    &kCommandRecordKindField,
    &kPadDatabaseTableField,
    &kPadDatabaseTableEndField,
    &kMapNodeValueField,
    &kCommandQueriesBeginField,
};

constexpr const Requirement* kEventSkipRequirements[] = {
    &kPadDatabase,
    &kU64MapFind,
    &kResolvePad,
    &kEventStatusField,
    &kEventStatusDataField,
    &kTimeFastForwardFramesField,
    &kCommandQueriesEndField,
    &kCommandQueryRecordField,
    &kCommandRecordKindField,
    &kPadDatabaseTableField,
    &kPadDatabaseTableEndField,
    &kMapNodeValueField,
    &kCommandQueriesBeginField,
};

constexpr const Requirement* kWheelZoomRequirements[] = {
    &kCameraControlUpdate,
    &kTweenStart,
    &kCameraControlEnabledField,
    &kCameraZoomLevelField,
    &kCameraZoomTweenField,
    &kCameraZoomValueField,
    &kCameraRequestsBeginField,
    &kCameraRequestsEndField,
};

constexpr const Requirement* kKeyBindingRequirements[] = {
    &kSystemLoadSucceeded,
    &kU64MapIndex,
    &kSetVKeyIndexCall,
    &kGameManager,
    &kManagerSystemDataField,
    &kMapSizeField,
    &kLoadOwnerSystemDataField,
    &kMapNodeKeyField,
    &kMapNodeValueField,
    &kMapListHeadField,
    &kMapNodeNextField,
    &kTaskOwnerField,
};

constexpr const Requirement* kSettingsHotkeyRequirements[] = {
    &kMainMenuCtor,
    &kPlaySystemSe,
    &kUiTaskRoot,
    &kTaskRefCountField,
};

constexpr const Requirement* kOptionsMenuRequirements[] = {
    &kOptionMenuInitialize,
    &kGetSettingListTable,
    &kGetSettingTable,
    &kFindParentClass,
    &kMainMenuClassId,
    &kBindListItemInput,
    &kOptionMenuCtor,
    &kOptionMenuClose,
    &kTitleClassId,
    &kInt64MapFind,
    &kGetSettingValue,
    &kSetSettingValue,
    &kListSetFilter,
    &kPadActionCheck,
    &kMainMenuCancelStep,
    &kTitleOptionCancel,
    &kCStringVtable,
    &kDatabaseRegistry,
    &kEmptyStringData,
    &kTaskRefCountField,
    &kListItemsBeginField,
    &kListItemsEndField,
    &kOptionMenuListField,
    &kListAllItemsBeginField,
    &kListAllItemsEndField,
    &kListItemInfoControlField,
    &kListItemKeyField,
    &kPadQuerySizeField,
    &kPadQueryRemapField,
    &kPadQueryDeviceMaskField,
    &kPadQueryWaitFlagField,
    &kPadQueryTextField,
    &kPadActionDownField,
    &kPadActionDownChecksField,
    &kTableSlotsBeginField,
    &kTableSlotsEndField,
    &kSettingRecordTypeField,
    &kSettingRecordLabelsField,
    &kOptionMenuTaskSizeField,
    &kOptionMenuClosingField,
    &kTaskPadField,
    &kSettingTableMapField,
    &kMapNodeKeyField,
    &kMapNodeValueField,
    &kMapNodeSizeField,
    &kSettingRecordStorageField,
    &kSettingRecordDefaultField,
    &kSettingRecordMinField,
    &kSettingRecordMaxField,
    &kSettingRecordChoiceCountField,
    &kSettingRecordChoicesField,
    &kSettingChoiceSizeField,
    &kSettingChoiceDescriptionsField,
    &kSettingChoiceValueField,
    &kTaskParentField,
    &kCStringDataField,
    &kSettingRecordIdField,
    &kSettingChoiceNamesField,
};

constexpr const Requirement* kBgmInfoRequirements[] = {
    &kMakeBgmParam,
    &kGetBgmTable,
    &kDatabaseRegistry,
    &kLanguage,
    &kBgmTableSlotsBeginField,
    &kBgmTableSlotsEndField,
};

constexpr const Requirement* kQuickSaveRequirements[] = {
    &kAutoSaveProcessCtor,
    &kAutoSaveBuildSetting,
    &kAutoSaveSucceeded,
    &kAutoDeleteCtor,
    &kSaveSettingBaseCtor,
    &kSaveUtilAcknowledge,
    &kGetListCursor,
    &kSaveInfoCtor,
    &kLoadMenuFillRows,
    &kLoadMenuHeaderText,
    &kLoadEventStart,
    &kSetGameFlag,
    &kScreenFade,
    &kCStringAppend,
    &kSetListCursor,
    &kLoadCardsListener,
    &kCardModeText,
    &kCarouselInitialize,
    &kCarouselPlayTimeText,
    &kClearGameFlag,
    &kDailyFlagClearCall,
    &kSetupScriptConditionsCall,
    &kEventConditions,
    &kMapEventSetupScript,
    &kMapEventContact,
    &kFindCollisionComponent,
    &kAutoDeleteSettingVtable,
    &kAutoSaveProcessSettingVtable,
    &kGameManager,
    &kSaveUtil,
    &kSaveWork,
    &kUiTaskRoot,
    &kManagerSystemDataField,
    &kManagerSaveDataField,
    &kManagerGroupField,
    &kManagerRotationField,
    &kSystemGroupField,
    &kSystemNewestRotationField,
    &kSystemSaveTimeField,
    &kSaveWorkBusyField,
    &kSaveWorkLiveTasksField,
    &kSaveUtilStatusField,
    &kSaveUtilOperationField,
    &kManagerMapOwnerField,
    &kMapOwnerMapInfoField,
    &kMapInfoPlayerUnitField,
    &kCollisionBodiesBeginField,
    &kCollisionBodiesEndField,
    &kBodyContactsField,
    &kContactNodeBodyField,
    &kMapEventDataField,
    &kMapEventDataRecordField,
    &kEventRecordRequiredCountField,
    &kEventRecordRequiredField,
    &kEventRecordForbiddenCountField,
    &kEventRecordForbiddenField,
    &kTaskRefCountField,
    &kTaskSaveSettingField,
    &kSettingTitleField,
    &kSettingSlotField,
    &kAutoSaveTaskSizeField,
    &kAutoSaveProcessSettingSizeField,
    &kAutoDeleteTaskSizeField,
    &kAutoDeleteSettingSizeField,
    &kCStringLengthField,
    &kLoadMenuGroupField,
    &kLoadMenuRowsField,
    &kLoadMenuRowSizeField,
    &kListComponentField,
    &kCardRowsField,
    &kCardRowSizeField,
    &kCardFunctorGroupField,
    &kCarouselFunctorListField,
    &kListItemsBeginField,
    &kListItemsEndField,
    &kEventStatusDataField,
    &kSaveDataFlagsField,
    &kSaveDataEventsField,
    &kEventNodeStatusField,
    &kUiRootFadeManagerField,
    &kSaveListEndField,
    &kSaveEntrySizeField,
    &kSaveEntryInfoField,
    &kSaveInfoSizeField,
    &kSaveUtilSlotCapacityField,
    &kSaveUtilStateField,
    &kUnitEntityField,
    &kComponentEntityField,
    &kContactNodeNextField,
    &kEventRecordIdField,
    &kEventRecordFlagSlotsField,
    &kSettingRefCountField,
    &kRowRotationField,
    &kRowListIndexField,
    &kRowInfoField,
    &kHeaderFunctorStateField,
    &kCardRowListIndexField,
    &kCardRowInfoField,
    &kCardFunctorTaskField,
    &kCarouselFunctorStateField,
    &kEventNodeNextField,
    &kSaveListSelectedField,
    &kSaveListBeginField,
    &kSaveEntrySlotField,
    &kSaveEntryValidField,
    &kSaveInfoRefCountField,
    &kSaveInfoAssignSlotField,
};

// =============================================================
// LOAD MENU ROW PATCHES
// =============================================================
// The load menu builds its rows in code, so a sixth row only needs the state
// object to grow by one 16-byte row (0x278 -> 0x288, the rows are its last
// field) and every hard-coded row count or bound to move from 5 to 6.

// The sites the patches below rewrite.
GAME_ADDRESS(LoadMenuStateAllocSize);
GAME_ADDRESS(LoadMenuStateDebugClear);
GAME_ADDRESS(LoadMenuStateClear);
GAME_ADDRESS(LoadMenuRowConstruct);
GAME_ADDRESS(LoadMenuRowDestruct);
GAME_ADDRESS(LoadMenuRowItemLoop);
GAME_ADDRESS(LoadMenuPlayTimeBound);
GAME_ADDRESS(LoadMenuGameTimeBound);

struct CodePatch {
    const Address* address;
    uint8_t original[6];
    uint8_t patched[6];
    size_t length;
    // The build may have no such site, as when an update moved the code the
    // patch rewrote. A site the build has is still checked and patched.
    bool optional;
};

constexpr CodePatch kLoadMenuPatches[] = {
    // The state object's size where it is allocated and at both memsets of
    // it. The site is the 32-bit immediate, which every build has even where
    // the instructions around it differ.
    {&kLoadMenuStateAllocSize, {0x78, 0x02, 0x00, 0x00}, {0x88, 0x02, 0x00, 0x00}, 4, false},
    {&kLoadMenuStateDebugClear, {0x78, 0x02, 0x00, 0x00}, {0x88, 0x02, 0x00, 0x00}, 4, true},
    {&kLoadMenuStateClear, {0x78, 0x02, 0x00, 0x00}, {0x88, 0x02, 0x00, 0x00}, 4, false},
    // Row array constructor count.
    {&kLoadMenuRowConstruct, {0xBA, 0x05, 0x00, 0x00, 0x00}, {0xBA, 0x06, 0x00, 0x00, 0x00}, 5, false},
    // Row array destructor count.
    {&kLoadMenuRowDestruct, {0x41, 0xB8, 0x05, 0x00, 0x00, 0x00}, {0x41, 0xB8, 0x06, 0x00, 0x00, 0x00}, 6, false},
    // initialize_Inner: list item creation loop bound.
    {&kLoadMenuRowItemLoop, {0x83, 0xFF, 0x05}, {0x83, 0xFF, 0x06}, 3, false},
    // playTime and gameTime text lambdas: highest valid cursor row.
    {&kLoadMenuPlayTimeBound, {0x83, 0xF8, 0x04}, {0x83, 0xF8, 0x05}, 3, false},
    {&kLoadMenuGameTimeBound, {0x83, 0xF8, 0x04}, {0x83, 0xF8, 0x05}, 3, false},
};

}  // namespace retail
