#pragma once
#include <cstddef>
#include <cstdint>

// Retail village.exe, Steam build rev.28536
// (sha256 979aebe31b42d181ee92e9db0086f22f65f2213029471a50723f61eaa93119a5).
// Every address is an RVA from the module base. Names are descriptive labels
// from static analysis of this executable.
namespace retail {

// The PE header's TimeDateStamp of this build, the id GameBuild reports.
constexpr uint32_t kBuildTimestamp = 0x6ABD400D;

// =============================================================
// FUNCTIONS: SHARED
// =============================================================
// Application frame update (virtual slot +8 of the app object), called once
// per frame from WinMain on the game thread.
constexpr uintptr_t kFrameUpdate = 0x8347C0;
// Appends a task to a parent task: (parent, &task, mode).
constexpr uintptr_t kAttachTask = 0x1024B0;
// Engine heap allocation: (heap, alignment, size).
constexpr uintptr_t kAllocate = 0x8FB5E0;
// Returns the UTF-8 logtext row for the current language.
constexpr uintptr_t kGetLogTextById = 0x69BB20;
// CTask_Message_Popup_Select ctor(task, &messages, yesText, noText, &onYes,
// &onNo, a7, a8). messages is a std::vector<Ngf::CString>, the callbacks are
// std::function<void()> whose targets the ctor takes over.
constexpr uintptr_t kPopupSelectCtor = 0x461460;
// Returns to the title screen: starts a fade on layer 7, then attaches a new
// CTask_Title to the UI root with mode 1, which destroys every field layer.
// Takes no arguments and only reads globals. The engine runs it from the
// "save too new" popup of SaveFunc::startLoadGame.
constexpr uintptr_t kReturnToTitle = 0x46D6F0;
// Ngf::CSoundPlayer volume fade: (player, volume, frames, &onDone), where
// frames counts 60 fps frames and onDone is a std::function<void(CSoundPlayer*)>
// the player copies.
constexpr uintptr_t kSoundFade = 0x78CD30;
// Pad receiver "has input" check: enabled, active, no lockout timer, no
// blocker, then recursively the parent receiver, and only the root's own
// focus byte counts. The player's field input gate (CCom_Unit_Player method
// 0x20E5D0) calls it first.
constexpr uintptr_t kIsReceiverActive = 0x81D8D0;
// CGame's scalar deleting destructor (game, flags), vtable slot 0. WinMain
// calls it once on the game thread when its main loop ends, before any game
// system is torn down. Every way of closing the game ends the loop the same
// way: the window gets WM_CLOSE (the close button, Alt+F4, and the Exit Game
// popup's Yes, which sends it), and its procedure sets the quit flag the loop
// checks.
constexpr uintptr_t kGameDestroy = 0x164AF0;

// =============================================================
// FUNCTIONS: GAME EVENTS
// =============================================================
// CTask_Title ctor(task). Every path to the title screen builds a new one:
// game start, Return to Title, and the title rebuilding itself after its
// options change the language. Called directly, inside task updates.
constexpr uintptr_t kTitleCtor = 0x44DEC0;
// SaveFunc::startLoadGame(), no arguments. Every load runs it once the save
// data is in memory, to set up the field from it: the load menu at the title
// and in game, and the auto load. Called through a std::function thunk.
constexpr uintptr_t kStartLoadGame = 0x46C940;

// =============================================================
// FUNCTIONS: SETTINGS HOTKEY
// =============================================================
// CTask_Menu_MainMenu ctor(task, startTab). The field's menu button runs
// alloc, this ctor and an attach to the UI root with mode 2.
constexpr uintptr_t kMainMenuCtor = 0x324640;
// Plays a system sound effect by id: (seId).
constexpr uintptr_t kPlaySystemSe = 0x8036F0;

// =============================================================
// FUNCTIONS: INPUT
// =============================================================
// CPadManager capture(padManager, &outRef, deviceType, mode, &query): the
// first pad of deviceType that passes the button check in mode, or null. The
// key config's wait state calls it with mode 0 for the pressed key. outRef
// points to a pointer to the pad record, which starts with the pad id.
constexpr uintptr_t kPadCapture = 0x8211B0;
// The CPadManager singleton.
constexpr uintptr_t kPadManager = 0x1124DB8;
// The engine's static empty string, every empty Ngf::CString's data.
constexpr uintptr_t kEmptyStringData = 0xA4450C;

// =============================================================
// FUNCTIONS: OPTIONS MENU
// =============================================================
// CTask_Menu_Option::initialize_Inner(menu). Builds one row per entry of the
// gamesettinglist table, each from the gamesetting record its entry names.
constexpr uintptr_t kOptionMenuInitialize = 0x3DD150;
// Database registry lookups: gamesettinglist (list entries) and gamesetting
// (setting records). Both return the CDatabase instance.
constexpr uintptr_t kGetSettingListTable = 0x2C0A0;
constexpr uintptr_t kGetSettingTable = 0x2BFD0;
// ITreeChild::findParentClass(task, classId): the nearest parent task of
// that class, or null.
constexpr uintptr_t kFindParentClass = 0x2D8640;
// CTask_Menu_MainMenu ClassID getter.
constexpr uintptr_t kMainMenuClassId = 0x328110;
// CListItem_PartsControl: appends an input handler, (item, &fn, input) with
// fn a std::function<void()> it copies.
constexpr uintptr_t kBindListItemInput = 0x7C13B0;
// CTask_Menu_Option ctor(task, &parentPad, 0). The new menu's pad becomes a
// child of the pad parentPad points to, which takes the input from it.
constexpr uintptr_t kOptionMenuCtor = 0x3DCD10;
// CTask_Menu_Option::closeMethod(menu). Queues the menu's end, once.
constexpr uintptr_t kOptionMenuClose = 0x3E2EA0;
// CTask_Title ClassID getter.
constexpr uintptr_t kTitleClassId = 0x4596D0;
// std::unordered_map<int64, T>::find(map, &outNode, &key), one template
// instance shared by every int64-keyed map in the engine.
constexpr uintptr_t kInt64MapFind = 0x42840;
// Setting value getter (gameManager, settingId) and setter (gameManager,
// settingId, value). Both look the record up with no null check.
constexpr uintptr_t kGetSettingValue = 0x0FBCE0;
constexpr uintptr_t kSetSettingValue = 0x0FBC00;
// Ngf::CCom_List::setFilter(list, filter, cursor), both std::functions taken
// by value and destroyed by the callee. Stores the filter, rebuilds the
// visible items from every item it accepts, then sets the cursor to what
// cursor returns, or keeps the current index when it is empty. The quest menu
// calls it at the end of its initialize (0x30F153).
constexpr uintptr_t kListSetFilter = 0x7BD7C0;
// Ngf::CPad action check (pad, actionId, &query): the action's input record
// when the pad and its parents take input and the action is down, else null.
constexpr uintptr_t kPadActionCheck = 0x81DB00;
// CTask_Menu_MainMenu update step (menu): Cancel on the menu's pad closes
// every tab and the menu, whatever page is open over the options tab.
constexpr uintptr_t kMainMenuCancelStep = 0x327480;
// CTask_Title::CState_Option Cancel handler (&state), bound to action 1350
// (right click sends it too). Saves the system settings and leaves the state
// with attach mode 1, which destroys every layer of the state.
constexpr uintptr_t kTitleOptionCancel = 0x458C50;

// =============================================================
// FUNCTIONS: TOASTS
// =============================================================
// Queues a popup log: (logManager, source, tag, count, logId, &parentTask).
// The logtext row for logId must exist; the engine dereferences it unchecked.
constexpr uintptr_t kPushLog = 0x0FC4B0;
// CGameLog::getText(gameLog, outCString). Builds the popup string from logtext.
constexpr uintptr_t kGetLogText = 0x1061C0;
// LOG_TEST_RESTAURANT_EATING: a leftover test row with no sound effect and no
// duplicate suppression, never queued by retail scripts. Toasts carry it.
constexpr uint64_t kToastCarrierLogId = 1089;

// =============================================================
// FUNCTIONS: BGM INFO
// =============================================================
// Ngf::CSoundManager::makeBgmParam(manager, outParam, bgmId). Every BGM play
// path (BgmPlay, BgmPlaywithFade, BgmPlaywithCrossFade, Sound Test) calls it
// on the game thread, after applying the engine's BGM id remap.
constexpr uintptr_t kMakeBgmParam = 0x784210;
// Returns the CDatabase<BGMData> instance from the database registry.
constexpr uintptr_t kGetBgmTable = 0x6A15A0;

// =============================================================
// FUNCTIONS: QUICK SAVE
// =============================================================
// CTask_Save_Process_AutoSave: ctor(task, &setting), then the lambda that
// builds CTaskSetting_SaveData_AutoSave into task+520 and picks the slot, and
// the lambda that runs after the file is written (rotation and .systemsave).
constexpr uintptr_t kAutoSaveProcessCtor = 0x473600;
constexpr uintptr_t kAutoSaveBuildSetting = 0x473CA0;
constexpr uintptr_t kAutoSaveSucceeded = 0x4740E0;

// Ngf::CTask_SaveData_AutoDelete ctor(task, &setting) and the shared
// CTaskSetting_SaveData_Auto_Base ctor(setting).
constexpr uintptr_t kAutoDeleteCtor = 0x7A2C10;
constexpr uintptr_t kSaveSettingBaseCtor = 0x7A5200;

// Save utility: clears a finished request. On entry the request and its
// status are still readable.
constexpr uintptr_t kSaveUtilAcknowledge = 0x966DC0;

// Ngf::CCom_List cursor index.
constexpr uintptr_t kGetListCursor = 0x7BD5F0;
// Save list entry info object ctor (1072 bytes).
constexpr uintptr_t kSaveInfoCtor = 0x15A3E0;

// CTask_GameSaveData_Load::CState_SaveDataSelect.
constexpr uintptr_t kLoadMenuFillRows = 0x485900;
// initialize_Inner lambda_1: header text ("saveNo") for the cursor row.
constexpr uintptr_t kLoadMenuHeaderText = 0x488060;

// Builds the 24-byte render layer key a field popup draws on (out, layerId,
// unused, layerName). ScriptCommand::TaskCreate_GamePopupSelect (0x26D450)
// calls it with the Lua default layer 6 and an empty name.
constexpr uintptr_t kMakeLayerKey = 0x796750;
// Starts the Lua event_game_load script that wakes the player in bed. Only the
// normal branch of SaveFunc::startLoadGame reaches it, through the Do_call of
// its lambda_4 (0x4711D0). Takes no arguments.
constexpr uintptr_t kLoadEventStart = 0x46D8A0;
// GameSetFlag core: (flagId, value), notifies the main game on a change.
constexpr uintptr_t kSetGameFlag = 0x27DB50;
// ScreenFade core: (fadeManager, renderStep, color, &FadeParam, &onEnd,
// &outTask), where onEnd is a std::function<void()>.
constexpr uintptr_t kScreenFade = 0x7F49E0;
// Ngf::CString append(text, cString).
constexpr uintptr_t kCStringAppend = 0x68FDE0;
// Ngf::CCom_List setCursor(list, index, top), top -1 keeps the scroll top.
// Clamps the index to the items that exist.
constexpr uintptr_t kSetListCursor = 0x7BD3F0;

// Clears a game flag: (saveData, flagId), and notifies the main game when it
// was set. The normal branch of startLoadGame calls it at kDailyFlagClearCall
// for every flag the gameflag table marks isDailyOff, since a normal load
// starts a new morning.
constexpr uintptr_t kClearGameFlag = 0x1479F0;
constexpr uintptr_t kDailyFlagClearCall = 0x46D4B0;
// Event conditions check: (saveData, &conditions, count, &flagConditions,
// flagCount), true when the event may start. CCom_MapEvent::setupScript calls
// it at kSetupScriptConditionsCall once for every body its own body already
// touches, in the order of that body's contact list, and starts the event's
// script on true.
constexpr uintptr_t kEventConditions = 0x147BC0;
constexpr uintptr_t kSetupScriptConditionsCall = 0x1D6BC1;
// CCom_MapEvent::setupScript(mapEvent), and the contactBegin handler it
// registers on its body when the event starts on contact: (&mapEvent,
// selfBody, &otherBody), which checks the same conditions and starts the
// script.
constexpr uintptr_t kMapEventSetupScript = 0x1D67D0;
constexpr uintptr_t kMapEventContact = 0x1D6FD0;
// Collision component of an entity, or null.
constexpr uintptr_t kFindCollisionComponent = 0x17FA40;
// Length of a rel32 call, from a call site to its return address.
constexpr size_t kCallLength = 5;

// CTask_GameSaveData_Load (the three group cards). Its save list listener
// (vtable slot 35) runs every save util tick while the menu idles: it fills
// the card of each group from the newest autosave, refills the open
// carousel's rows instead when one exists, and commits a chosen load.
// Returns 1 once the menu is done.
constexpr uintptr_t kLoadCardsListener = 0x482730;
// Card text04 provider (the mode label) Do_call: (functor, &outText), functor
// {+8 task, +16 group}.
constexpr uintptr_t kCardModeText = 0x4875C0;
// CState_SaveDataSelect (the Start carousel): initialize_Inner(state), and
// the play time text provider Do_call (functor, &outText), functor {+8 state,
// +16 list UI object}.
constexpr uintptr_t kCarouselInitialize = 0x4831B0;
constexpr uintptr_t kCarouselPlayTimeText = 0x487FB0;

// =============================================================
// FUNCTIONS: WALK SPEED
// =============================================================
// CState_Player_Walk::update(state, frameTime), the walk state's vtable slot.
// Builds the player's free movement velocity from getMoveSpeed, the stick
// and the state's own gait factors, then moves the unit and sets its
// animation speed.
constexpr uintptr_t kPlayerWalkUpdate = 0x256B10;
// CCom_Unit_Player::getMoveSpeed(unit), vtable 0xE52068 slot +0x138: the
// player's base speed with the area and status slowdowns applied. Lua
// UnitGetMoveSpeed calls it too.
constexpr uintptr_t kPlayerGetMoveSpeed = 0x20F9C0;

// =============================================================
// FUNCTIONS: EVENT FAST FORWARD
// =============================================================
// CCom_Event::setup(event), run once by the event factory 0x16EF50 when an
// event script starts, and the destructor body (event) its deleting
// destructor runs.
constexpr uintptr_t kEventSetup = 0x1BD360;
constexpr uintptr_t kEventDestroy = 0x1BD1C0;
// Time manager update(timeManager, outInfo), once per frame from the frame
// update. Checked only, so the field layout below is trusted.
constexpr uintptr_t kTimeUpdate = 0x81AEB0;
// CPadManager push command (padManager, commandId, &vector, flag): queues a
// command that fires on the next frame, as Lua PadSetCommand does.
constexpr uintptr_t kPadPushCommand = 0x820CC0;
// Pad database (registry), as the pad manager's resolve fetches it from
// kDatabaseRegistry. Its pad table is an unordered_map<u64, record*>.
constexpr uintptr_t kPadDatabase = 0x6A1A80;
// std::unordered_map<u64, ...>::find(map, &outNode, &key), the instance the
// pad tables share: the node, or the map's end node on a miss.
constexpr uintptr_t kU64MapFind = 0x042840;
// CPadManager resolve (padManager, &outHolder, holder): follows a virtual
// pad's record holder through the key config to the holder of the pad it is
// bound to.
constexpr uintptr_t kResolvePad = 0x820FF0;

// =============================================================
// FUNCTIONS: WHEEL ZOOM
// =============================================================
// CCom_CameraControl::update(control, frameTime), a vtable slot. Polls the
// change camera command (1039), which steps the zoom level and starts a
// tween to that level's zoom, then writes the tween's value to the camera.
constexpr uintptr_t kCameraControlUpdate = 0x188C80;
// Float tween start (tween, &from, &to, &FadeParam), as the zoom poll calls it.
constexpr uintptr_t kTweenStart = 0x1787E0;

// =============================================================
// FUNCTIONS: KEY BINDING BACKUP
// =============================================================
// AutoSystemLoad success (task), a functor slot: installs the loaded system
// data on the game manager, then applies its key table to the pad manager.
constexpr uintptr_t kSystemLoadSucceeded = 0x4767A0;
// std::unordered_map<u64, u64>::operator[](map, &outNode, &key), one template
// instance shared by many maps. setVKey calls it at kSetVKeyIndexCall + 0x12.
constexpr uintptr_t kU64MapIndex = 0x0F5320;
constexpr uintptr_t kSetVKeyIndexCall = 0x15B080;

// =============================================================
// GLOBALS
// =============================================================
// The game manager is also the popup log manager.
constexpr uintptr_t kGameManager = 0x111CC90;
constexpr uintptr_t kDatabaseRegistry = 0x1124CF0;
constexpr uintptr_t kSaveWork = 0x1124D58;
constexpr uintptr_t kSaveUtil = 0x1124C40;
constexpr uintptr_t kUiTaskRoot = 0x1124D78;
constexpr uintptr_t kLanguage = 0x1124CE0;
constexpr uintptr_t kEngineHeap = 0x3170EE8;
constexpr uintptr_t kSoundManager = 0x1124D38;
// Ngf::CSoundMgr, which keeps the BGM players apart from kSoundManager's.
constexpr uintptr_t kSoundSystem = 0x1124D40;
constexpr uintptr_t kTimeManager = 0x1124DB0;
constexpr uintptr_t kMouseManager = 0x1124DC0;
constexpr uintptr_t kTalkManager = 0x1124DC8;
// DirectInput keyboard state: the current key bytes, by DIK code, sit at +0x10;
// a key is down while bit 7 is set.
constexpr uintptr_t kKeyboardState = 0x3169030;
constexpr size_t kKeyboardDown = 0x10;
// Controller states, one block per controller: the held button mask first,
// as the button check reads it in hold mode.
constexpr uintptr_t kControllerStates = 0x3169B50;
constexpr size_t kControllerStateSize = 624;
constexpr int kControllerCount = 32;

// Vtables.
constexpr uintptr_t kAutoSaveProcessSettingVtable = 0xE73E10;
constexpr uintptr_t kAutoDeleteSettingVtable = 0xE9E418;
constexpr uintptr_t kPlayerComponentVtable = 0xE52068;
constexpr uintptr_t kCStringVtable = 0xE8B430;
// std::_Func_impl_no_alloc of the captureless lambda in
// CSoundPlayer::stopWithFade, which stops the player once its fade ends.
constexpr uintptr_t kStopAfterFadeVtable = 0xE9A8B8;

// =============================================================
// LAYOUTS
// =============================================================
// Offset of the resolved BGM id inside the param makeBgmParam fills.
constexpr size_t kBgmParamIdOffset = 0x08;

// CGameLog fields.
constexpr size_t kGameLogId = 0x40;
constexpr size_t kGameLogSource = 0x58;
constexpr size_t kGameLogTag = 0x60;

// CDatabase<BGMData>: std::vector of row slots; each slot points to a row.
constexpr size_t kBgmTableSlotsBegin = 0x98;
constexpr size_t kBgmTableSlotsEnd = 0xA0;
// BGMData row: u64 id at +0, then one UTF-8 title pointer per language.
constexpr size_t kBgmRowId = 0x00;
constexpr size_t kBgmRowTitles = 0xA0;

// Game manager.
constexpr size_t kManagerSystemData = 512;
constexpr size_t kManagerSaveData = 520;
constexpr size_t kManagerGroup = 952;
constexpr size_t kManagerRotation = 956;

// System data (.systemsave): group, newest rotation index per group, time.
constexpr size_t kSystemGroup = 96;
constexpr size_t kSystemNewestRotation = 100;
constexpr size_t kSystemSaveTime = 112;

// Save work counters: save or load in progress, live save tasks.
constexpr size_t kSaveWorkBusy = 720;
constexpr size_t kSaveWorkLiveTasks = 1160;

// Save utility request state.
constexpr size_t kSaveUtilSlotCapacity = 8;
constexpr size_t kSaveUtilState = 12;
constexpr size_t kSaveUtilStatus = 16;
constexpr size_t kSaveUtilOperation = 24;
constexpr size_t kSaveUtilSlot = 36;
// The util's save list (same layout as the list the load menu fills from).
// A load by list index leaves kSaveUtilSlot at 0 and reads the slot from
// the selected entry instead.
constexpr size_t kSaveUtilList = 2264;
constexpr int32_t kSaveUtilStateDone = 2;
constexpr int32_t kSaveUtilStatusOk = 1;
constexpr int32_t kSaveUtilOperationLoad = 1;

// Player input gate: CSaveData -> player status -> CCom_Unit_Player -> pad
// receiver. The game's own field gate (0x20E5D0) also requires the save data
// field at +0x3470 to be empty and bit 9 of the flags at +0xA8 to be clear.
constexpr size_t kSaveDataPlayerStatus = 0x32B8;
constexpr size_t kPlayerStatusComponent = 1048;
constexpr size_t kPlayerComponentReceiver = 1056;
constexpr size_t kSaveDataFieldBlocker = 0x3470;
constexpr size_t kSaveDataStateFlags = 0xA8;
// Game flag 201, EVENT_RUNNING: 0xA8 is the fourth word of the flag bits at
// kSaveDataFlags.
constexpr uint32_t kStateFlagFieldLocked = 1u << 9;

// The running map: game manager -> map owner -> CMapInformation, the one the
// load event hands its script as mapInfo. Its player unit holds the player
// entity.
constexpr size_t kManagerMapOwner = 616;
constexpr size_t kMapOwnerMapInfo = 216;
constexpr size_t kMapInfoPlayerUnit = 88;
constexpr size_t kUnitEntity = 8;
// A component's entity, the one setupScript takes the map event's collision
// component from.
constexpr size_t kComponentEntity = 8;
// Collision component: std::vector of body pointers, the movement body first.
constexpr size_t kCollisionBodiesBegin = 584;
constexpr size_t kCollisionBodiesEnd = 592;
// Ngf::CCollision2DBody: the sentinel of the list of bodies it touches, whose
// nodes hold the next node first and the other body at +16.
constexpr size_t kBodyContacts = 1016;
constexpr size_t kContactNodeNext = 0;
constexpr size_t kContactNodeBody = 16;
// CCom_MapEvent: its data holder, whose slot at +584 points at a pointer to
// the event's database record. setupScript names the event's script
// "demo<id>" from the record's first field.
constexpr size_t kMapEventData = 824;
constexpr size_t kMapEventDataRecord = 584;
// Map event record: the id, then the two flag lists eventConditions checks,
// each a count and up to six inline flag ids. Every required flag must be set
// and every forbidden one clear; a 0 id is skipped.
constexpr size_t kEventRecordId = 0;
constexpr size_t kEventRecordRequiredCount = 224;
constexpr size_t kEventRecordRequired = 232;
constexpr size_t kEventRecordForbiddenCount = 280;
constexpr size_t kEventRecordForbidden = 288;
constexpr size_t kEventRecordFlagSlots = 6;

// Task and setting objects. A task's parent is the task it is attached to.
constexpr size_t kTaskParent = 8;
constexpr size_t kTaskRefCount = 24;
constexpr size_t kTaskSaveSetting = 520;
constexpr size_t kSettingRefCount = 8;
constexpr size_t kSettingTitle = 16;
constexpr size_t kSettingSlot = 128;
constexpr size_t kAutoSaveTaskSize = 544;
constexpr size_t kAutoSaveProcessSettingSize = 144;
constexpr size_t kAutoDeleteTaskSize = 536;
constexpr size_t kAutoDeleteSettingSize = 272;
constexpr size_t kAllocationAlignment = 32;
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
constexpr size_t kLayerKeySize = 24;
constexpr int64_t kFieldPopupLayer = 6;

// Main menu: task size, the Option tab (the last of its 8 tabs), and the
// sound effect the field plays when it opens the menu.
constexpr size_t kMainMenuTaskSize = 0x258;
constexpr int32_t kMainMenuOptionTab = 7;
constexpr uint64_t kMenuOpenSe = 0x19;

// Sound manager: intrusive list of sound players (sentinel node at +616,
// next at +0, player at +16), and the fade time, in frames, the engine's
// title option uses when it silences them.
constexpr size_t kSoundManagerPlayers = 616;
constexpr size_t kSoundNodeNext = 0;
constexpr size_t kSoundNodePlayer = 16;
constexpr double kTitleSoundFadeFrames = 10.0;
// BGM players: kSoundSystem's player manager at +0x1A8 holds a list of the
// same nodes at its own +0x1A8. BgmPlaywithCrossFade fades them out there.
constexpr size_t kSoundSystemBgmManager = 0x1A8;
constexpr size_t kBgmManagerPlayers = 0x1A8;

// Ngf::CString: vtable, data, size, capacity, assign mode.
constexpr size_t kCStringSize = 40;
constexpr size_t kCStringData = 8;
constexpr size_t kCStringLength = 16;
constexpr size_t kCStringMode = 32;

// Load menu state: group, then 16-byte rows {rotation, list index, info}.
constexpr size_t kLoadMenuGroup = 536;
constexpr size_t kLoadMenuRows = 552;
constexpr size_t kLoadMenuRowSize = 16;
constexpr size_t kRowRotation = 0;
constexpr size_t kRowListIndex = 4;
constexpr size_t kRowInfo = 8;
constexpr int kLoadMenuRetailRowCount = 5;
// Header functor.
constexpr size_t kHeaderFunctorState = 8;
constexpr size_t kListComponent = 32;

// Load cards task: per group, 16 bytes {newest rotation, list index, card
// info}. The card's Yes (0x486E20) loads the list index.
constexpr size_t kCardRows = 600;
constexpr size_t kCardRowSize = 16;
constexpr size_t kCardRowListIndex = 4;
constexpr size_t kCardRowInfo = 8;
// Text provider functors.
constexpr size_t kCardFunctorTask = 8;
constexpr size_t kCardFunctorGroup = 16;
constexpr size_t kCarouselFunctorState = 8;
constexpr size_t kCarouselFunctorList = 16;
// Ngf::CCom_List items vector: the items its filter shows, which the cursor
// indexes.
constexpr size_t kListItemsBegin = 2392;
constexpr size_t kListItemsEnd = 2400;

// Options menu: its Ngf::CCom_List, the list's vector of every item info,
// and the PartsControl each info wraps, whose key is the setting id.
constexpr size_t kOptionMenuList = 0x230;
constexpr size_t kListAllItemsBegin = 0x940;
constexpr size_t kListAllItemsEnd = 0x948;
constexpr size_t kListItemInfoControl = 0x10;
constexpr size_t kListItemKey = 0x10;
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
constexpr size_t kPadQuerySize = 0x60;
constexpr size_t kPadQueryRemap = 0x20;
constexpr size_t kPadQueryDeviceMask = 0x24;
constexpr size_t kPadQueryWaitFlag = 0x28;
constexpr size_t kPadQueryIgnoreExclusive = 0x2A;
constexpr size_t kPadQueryText = 0x30;
// The main menu's Cancel action, and the action check's record: a byte set
// while the action is down, and the checks made while it was down, which a
// check counts when its query sets the wait flag. The main menu acts on the
// first such check only.
constexpr uint64_t kPadActionCancel = 0x510;
constexpr size_t kPadActionDown = 128;
constexpr size_t kPadActionDownChecks = 188;

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
constexpr size_t kWalkGait = 0x270;
constexpr size_t kWalkAnimationSpeed = 0x280;
constexpr int32_t kGaitWalk = 1;
constexpr int32_t kGaitRun = 3;
constexpr size_t kUnitAnimationComponent = 0x400;
constexpr size_t kAnimationComponentPlayer = 0x558;
constexpr size_t kAnimationPlayerSpeed = 0x74;

// CCom_Event: its status (refcounted), the status's event data slot, and
// the data's isSkip flag, set on the events the game marks skippable.
constexpr size_t kEventStatus = 792;
constexpr size_t kEventStatusData = 224;
constexpr size_t kEventDataIsSkip = 0x110;
// Time manager: fast forward frames left and the magnification they apply,
// as Lua TimeSetFastforwardFrame writes them. Each update multiplies the
// frame time by the magnification, counts the frames down and resets the
// magnification to 1 once none are left. Message and sound waits pass on
// their own while it is above 1.
constexpr size_t kTimeFastForwardFrames = 0x220;
constexpr size_t kTimeMagnification = 0x228;
// Talk manager byte: set when talk advances on CMD_FAST_NEXT, clear when it
// advances on CMD_NEXT_2.
constexpr size_t kTalkFastNext = 624;
constexpr int64_t kCommandFastNext = 131;
constexpr int64_t kCommandNext2 = 132;

// Event skip. The pad manager keeps the command queries made this frame, by
// the receivers waiting on input, as a vector of 192-byte entries. An entry's
// +16 points at a holder of the command's record, whose first field is the
// command id; the beta skip passes over records of kind 2.
constexpr size_t kCommandQueriesBegin = 608;
constexpr size_t kCommandQueriesEnd = 616;
constexpr size_t kCommandQuerySize = 192;
constexpr size_t kCommandQueryRecord = 16;
constexpr size_t kCommandRecordKind = 120;
constexpr int32_t kCommandKindIgnored = 2;
// The talk and event commands the beta skip runs past: CMD_NEXT,
// CMD_FAST_NEXT, CMD_NEXT_2, CMD_FASTFORWARD, CMD_BACKLOG and CMD_AUTO_TEXT
// from the framework's table, CMD_TALK_BACKLOG_VIEW and
// CMD_UNIT_ITEM_INVENTORY_DETAIL from the game's. Talk asks for the backlog
// view every frame. Any other query, such as a choice's cursor or OK, stops
// it.
constexpr int64_t kSkipPassCommands[] = {129, 131, 132, 133, 134, 136, 1004, 1037};
// Pad database: its pad table and the table's end node. A node keeps the
// pad's record holder as its value (kMapNodeValue), and a record's first
// field is the pad id.
constexpr size_t kPadDatabaseTable = 88;
constexpr size_t kPadDatabaseTableEnd = 96;
// The virtual Cancel pad of the CMD_FAST_NEXT chord, and the controller B
// button it maps to by default.
constexpr uint64_t kVirtualPadCancel = 274;
constexpr int32_t kControllerPadB = 10;

// Camera control: enabled, zoom enabled, the zoom level (0 to 2), the zoom
// tween and its current value, and the queue of scripted camera requests,
// which must be empty for the zoom to start a tween.
constexpr size_t kCameraControlEnabled = 784;
constexpr size_t kCameraZoomEnabled = 786;
constexpr size_t kCameraZoomLevel = 624;
constexpr size_t kCameraZoomTween = 632;
constexpr size_t kCameraZoomValue = 636;
constexpr size_t kCameraRequestsBegin = 792;
constexpr size_t kCameraRequestsEnd = 800;
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
constexpr size_t kMapSize = 16;
constexpr size_t kMapNodeNext = 0;
constexpr size_t kManagerSystemDirty = 960;
constexpr size_t kTaskOwner = 8;
constexpr size_t kLoadOwnerSystemData = 536;

// Setting tables: a std::vector of slots at +0x98; each slot points to a
// pointer to the row. A gamesettinglist row names its setting id at +8. A
// gamesetting record has its id at +0, the widget type at +0x88 and one
// UTF-8 label pointer per language at +0xA0.
constexpr size_t kTableSlotsBegin = 0x98;
constexpr size_t kTableSlotsEnd = 0xA0;
constexpr size_t kSettingListRowId = 8;
constexpr size_t kSettingRecordId = 0;
constexpr size_t kSettingRecordType = 0x88;
constexpr size_t kSettingRecordLabels = 0xA0;
constexpr int32_t kSettingTypeButton = 4;
// SETTING_ETC_ONLINE_MANUAL: a button record no list entry uses, so the
// options menu builds it as a plain button with no handler of its own.
constexpr int64_t kUnusedButtonSettingId = 26;
// SETTINC_ETC_GAME_END, the Exit Game row.
constexpr int64_t kExitGameSettingId = 27;
// The Exit Game popup is attached to the options menu with this mode.
constexpr int32_t kAttachModeMenuPopup = 1;

// Options menu task size, and the pad every task holds at +424.
constexpr size_t kOptionMenuTaskSize = 0x248;
// Set by closeMethod once the menu's end is queued.
constexpr size_t kOptionMenuClosing = 0x221;
constexpr size_t kTaskPad = 424;
// The gamesetting table's unordered_map<int64, record*>, and a map node's
// key and value.
constexpr size_t kSettingTableMap = 0x58;
constexpr size_t kMapNodeKey = 16;
constexpr size_t kMapNodeValue = 24;
constexpr size_t kMapNodeSize = 32;
// More gamesetting record fields: storage class (0 per save, 1 or 2 system,
// anything else stores nothing), default value, min and max, the choice
// count, and the inline choices, each with one name and one description per
// language and a value.
constexpr size_t kSettingRecordStorage = 0x30;
constexpr size_t kSettingRecordDefault = 0x8C;
constexpr size_t kSettingRecordMin = 0x90;
constexpr size_t kSettingRecordMax = 0x94;
constexpr size_t kSettingRecordChoiceCount = 0xD8;
constexpr size_t kSettingRecordChoices = 0xE0;
constexpr size_t kSettingChoiceSize = 0x78;
constexpr size_t kSettingChoiceNames = 0;
constexpr size_t kSettingChoiceDescriptions = 0x38;
constexpr size_t kSettingChoiceValue = 0x70;
constexpr int32_t kSettingTypeChoice = 0;
constexpr int32_t kSettingStorageNone = 3;
// The menu builds a two-choice row as a toggle, and keys each choice item by
// its index, which is the value the setter gets.
constexpr int32_t kToggleChoiceCount = 2;
// SETTING_VSYNC: a two-choice record whose header the mod's toggles copy.
constexpr int64_t kToggleTemplateSettingId = 18;

// Game flags: bit n of the bit array at saveData+144, n being the flag's id in
// the gameflag table.
constexpr size_t kSaveDataFlags = 144;
constexpr int32_t kFlagGameTimeStop = 200;
constexpr int32_t kFlagPlayerDead = 216;
constexpr int32_t kFlagLateNight = 270;
constexpr int32_t kFlagComingHomeMorning = 280;
constexpr int32_t kFlagStoryStarted = 10000;
// The save data's running events: a std::list sentinel whose nodes hold the
// next node first and the event status at +16. A status's data slot
// (kEventStatusData) points to the event data, whose first field is its id.
constexpr size_t kSaveDataEvents = 13416;
constexpr size_t kEventNodeNext = 0;
constexpr size_t kEventNodeStatus = 16;
// Events startLoadGame removes or branches on: one sets the late night NPC
// schedule, one is removed on load, and the rest are late night events.
constexpr uint64_t kLoadBranchEvents[] = {
    100010710000, 100069990000, 100080020000, 100080050000, 100080070000, 100010000,
};

// Screen fade: the fade manager on the UI root, the RENDER_STEP_FADE step,
// COLOR_CLEAR_BLACK (fade in from black), and the 48-byte FadeParam that Lua
// FadeMake builds: {frames, 0, type 27, 0, 0.0, 1.0, 0}.
constexpr size_t kUiRootFadeManager = 672;
constexpr int32_t kRenderStepFade = 7;
constexpr uint32_t kColorClearBlack = 0;
constexpr int32_t kFadeParamType = 27;
// SLEEP_FADE_IN_TIME, the fade the bed wake-up uses.
constexpr double kWakeFadeFrames = 25.0;

// Save list passed to the row fill: selected index, then a std::vector of
// 88-byte entries.
constexpr size_t kSaveListSelected = 4;
constexpr size_t kSaveListBegin = 8;
constexpr size_t kSaveListEnd = 16;
constexpr size_t kSaveEntrySize = 88;
constexpr size_t kSaveEntrySlot = 0;
constexpr size_t kSaveEntryValid = 4;
constexpr size_t kSaveEntryInfo = 64;
constexpr size_t kSaveInfoSize = 1072;
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

// =============================================================
// SIGNATURES
// =============================================================
// First 32 bytes of each function we call or detour, one table per feature.
// A mismatch means the game was updated and the RVAs above are stale, so
// that feature stays inert.
struct CodeSignature {
    const char* name;
    uintptr_t rva;
    uint8_t bytes[32];
};

constexpr CodeSignature kSharedSignatures[] = {
    {"frameUpdate", kFrameUpdate,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x4C,
      0x89, 0x70, 0x20, 0x55, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00, 0x0F}},
    {"attachTask", kAttachTask,
     {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x70, 0x45, 0x8B, 0xF0, 0x48, 0x8B,
      0xDA, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x0A, 0x48, 0x85, 0xC9, 0x74, 0x09, 0x48, 0x8B, 0x01, 0x48}},
    {"allocate", kAllocate,
     {0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49,
      0x8B, 0xF8, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xE9, 0xF6, 0xC2, 0x0F, 0x0F, 0x85, 0xC7, 0x00, 0x00}},
    {"getLogTextById", kGetLogTextById,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D, 0xC0, 0x91, 0xA8, 0x00,
      0xE8, 0x8B, 0x62, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x28, 0x4C, 0x8D, 0x44, 0x24, 0x28, 0x33}},
    {"popupSelectCtor", kPopupSelectCtor,
     {0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48,
      0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xC3, 0xBB, 0xBD, 0x00, 0x48, 0x33, 0xC4}},
    {"returnToTitle", kReturnToTitle,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x18, 0x49, 0x89, 0x4B, 0x08, 0x55, 0x56, 0x57, 0x48, 0x81,
      0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x6C, 0x76, 0xCB, 0x00, 0x33, 0xED, 0x49, 0x89}},
    {"soundFade", kSoundFade,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x0F,
      0x29, 0x70, 0xE8, 0x0F, 0x29, 0x78, 0xD8, 0x48, 0x8B, 0x05, 0xF2, 0x02, 0x8B, 0x00, 0x48, 0x33}},
    {"isReceiverActive", kIsReceiverActive,
     {0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0xAC, 0x01, 0x00, 0x00, 0x00, 0x74, 0x23, 0x80, 0xB9, 0xD9,
      0x00, 0x00, 0x00, 0x00, 0x74, 0x1A, 0x0F, 0x57, 0xC0, 0x0F, 0x2F, 0x81, 0xB0, 0x01, 0x00, 0x00}},
};

constexpr CodeSignature kExitSignatures[] = {
    {"gameDestroy", kGameDestroy,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
      0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x1D, 0x6D, 0x02, 0xFC, 0x00, 0x48, 0x8D, 0x05, 0xF6, 0x43}},
};

constexpr CodeSignature kEventSignatures[] = {
    {"titleCtor", kTitleCtor,
     {0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xD9, 0xE8, 0xBE, 0xE5,
      0x35, 0x00, 0x90, 0x48, 0x8D, 0x05, 0x06, 0x3F, 0xA2, 0x00, 0x48, 0x89, 0x03, 0x48, 0x8D, 0x05}},
    {"startLoadGame", kStartLoadGame,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xFA, 0xFF, 0xFF}},
};

constexpr CodeSignature kToastSignatures[] = {
    {"getLogText", kGetLogText,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24,
      0x60, 0xF0, 0xFF, 0xFF, 0xB8, 0xA0, 0x10, 0x00, 0x00, 0xE8, 0xB2, 0xF0, 0x8B, 0x00, 0x48, 0x2B}},
    {"pushLog", kPushLog,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x80, 0x48, 0x81, 0xEC}},
};

constexpr CodeSignature kPopupSignatures[] = {
    {"makeLayerKey", kMakeLayerKey,
     {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x33, 0xF6, 0x48,
      0x89, 0x11, 0x48, 0x89, 0x71, 0x10, 0x49, 0x8B, 0xF9, 0xC7, 0x41, 0x08, 0x01, 0x00, 0x00, 0x00}},
};

constexpr CodeSignature kInputSignatures[] = {
    {"padCapture", kPadCapture,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
      0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05, 0x75, 0xBE, 0x81, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89}},
};

constexpr CodeSignature kWalkSpeedSignatures[] = {
    {"playerWalkUpdate", kPlayerWalkUpdate,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
      0x41, 0x57, 0x48, 0x8D, 0xA8, 0x88, 0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x40, 0x02, 0x00, 0x00}},
    {"playerGetMoveSpeed", kPlayerGetMoveSpeed,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x0F, 0x29, 0xB4,
      0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x64, 0xD6, 0xE2, 0x00, 0x48, 0x33, 0xC4, 0x48}},
};

constexpr CodeSignature kEventFastForwardSignatures[] = {
    {"eventSetup", kEventSetup,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xE0, 0x48, 0x81, 0xEC}},
    {"eventDestroy", kEventDestroy,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
      0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x05, 0x6A, 0x0D, 0xC9, 0x00, 0x48, 0x89}},
    {"timeUpdate", kTimeUpdate,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
      0x8B, 0xD9, 0x48, 0x8B, 0xFA, 0x48, 0x8D, 0x0D, 0x1C, 0x1E, 0x90, 0x00, 0xFF, 0x15, 0x5E, 0x14}},
    {"padPushCommand", kPadPushCommand,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x10, 0x49, 0x89, 0x6B, 0x18, 0x49, 0x89, 0x73, 0x20, 0x57,
      0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x74, 0x24, 0x70}},
};

constexpr CodeSignature kEventSkipSignatures[] = {
    {"padDatabase", kPadDatabase,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x02, 0xCC, 0x00, 0x00, 0x44, 0x0F,
      0xB6, 0xC0, 0x49, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC8, 0x48}},
    {"u64MapFind", kU64MapFind,
     {0x45, 0x0F, 0xB6, 0x08, 0x4C, 0x8B, 0xD2, 0x48, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x48, 0xB8, 0x25, 0x23, 0x22, 0x84, 0xE4, 0x9C, 0xF2, 0xCB, 0x4C, 0x33, 0xC8, 0x41, 0x0F}},
    {"resolvePad", kResolvePad,
     {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xD8,
      0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xF1, 0x4D, 0x85, 0xC0, 0x75, 0x16, 0x33, 0xED, 0x48, 0x8B, 0xC2}},
};

constexpr CodeSignature kWheelZoomSignatures[] = {
    {"cameraControlUpdate", kCameraControlUpdate,
     {0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
      0xAC, 0x43, 0xEB, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x60, 0x80, 0xB9, 0x10, 0x03}},
    {"tweenStart", kTweenStart,
     {0x48, 0x83, 0xEC, 0x28, 0xF2, 0x41, 0x0F, 0x10, 0x01, 0x0F, 0x57, 0xC9, 0x66, 0x0F, 0x2E, 0xC1,
      0x4C, 0x8B, 0xD1, 0x7A, 0x37, 0x75, 0x35, 0xF2, 0x41, 0x0F, 0x10, 0x41, 0x08, 0x66, 0x0F, 0x2E}},
};

constexpr CodeSignature kKeyBindingSignatures[] = {
    {"systemLoadSucceeded", kSystemLoadSucceeded,
     {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x41, 0x08, 0x48, 0x8B, 0x90, 0x18, 0x02, 0x00, 0x00, 0x48,
      0x8B, 0x05, 0xDA, 0x64, 0xCA, 0x00, 0x48, 0x85, 0xD2, 0x74, 0x04, 0xF0, 0xFF, 0x42, 0x08, 0x48}},
    {"u64MapIndex", kU64MapIndex,
     {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4D,
      0x8B, 0xF8, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x41, 0x0F, 0xB6, 0x28, 0x48, 0xB8, 0x25, 0x23}},
    // setVKey's own call of the index and its value store, so the index is
    // the one the key table uses.
    {"setVKeyIndexCall", kSetVKeyIndexCall,
     {0x48, 0x8B, 0x03, 0x48, 0x89, 0x45, 0xBF, 0x4C, 0x8D, 0x45, 0xBF, 0x48, 0x8D, 0x55, 0xCF, 0x49,
      0x8B, 0xCC, 0xE8, 0x89, 0xA2, 0xF9, 0xFF, 0x48, 0x8B, 0x08, 0x4C, 0x89, 0x69, 0x18, 0x48, 0x83}},
};

constexpr CodeSignature kSettingsHotkeySignatures[] = {
    {"mainMenuCtor", kMainMenuCtor,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48,
      0x8D, 0xA8, 0xA8, 0xF9, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x40, 0x07, 0x00, 0x00, 0x0F, 0x29, 0x70}},
    {"playSystemSe", kPlaySystemSe,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0x78, 0xFB, 0xFF, 0xFF, 0x48}},
};

constexpr CodeSignature kOptionsMenuSignatures[] = {
    {"optionMenuInitialize", kOptionMenuInitialize,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x50, 0x16, 0xFF, 0xFF}},
    {"getSettingListTable", kGetSettingListTable,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x52, 0xA3, 0x08, 0x00, 0x44, 0x0F,
      0xB6, 0xC0, 0x49, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC8, 0x48}},
    {"getSettingTable", kGetSettingTable,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x22, 0xA5, 0x08, 0x00, 0x44, 0x0F,
      0xB6, 0xC0, 0x49, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC8, 0x48}},
    {"findParentClass", kFindParentClass,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x18, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48, 0x81, 0xEC, 0x90,
      0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xE6, 0x49, 0xD6, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84}},
    {"mainMenuClassId", kMainMenuClassId,
     {0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xB9, 0x10, 0x00,
      0x00, 0x00, 0x48, 0x8B, 0x00, 0x8B, 0x04, 0x01, 0x39, 0x05, 0x62, 0x35, 0xE8, 0x02, 0x7F, 0x0B}},
    {"bindListItemInput", kBindListItemInput,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24,
      0x60, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xA0, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x6E, 0xBC}},
    {"optionMenuCtor", kOptionMenuCtor,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x10,
      0x07, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x16, 0x03, 0xC6, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84}},
    {"optionMenuClose", kOptionMenuClose,
     {0x4C, 0x8B, 0xDC, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0xD1, 0x80, 0xB9, 0x21, 0x02, 0x00,
      0x00, 0x00, 0x75, 0x78, 0xC6, 0x81, 0x21, 0x02, 0x00, 0x00, 0x01, 0x48, 0x81, 0xC1, 0x00, 0x02}},
    {"titleClassId", kTitleClassId,
     {0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xB9, 0x10, 0x00,
      0x00, 0x00, 0x48, 0x8B, 0x00, 0x8B, 0x04, 0x01, 0x39, 0x05, 0xD2, 0x54, 0xD5, 0x02, 0x7F, 0x0B}},
    {"int64MapFind", kInt64MapFind,
     {0x45, 0x0F, 0xB6, 0x08, 0x4C, 0x8B, 0xD2, 0x48, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x48, 0xB8, 0x25, 0x23, 0x22, 0x84, 0xE4, 0x9C, 0xF2, 0xCB, 0x4C, 0x33, 0xC8, 0x41, 0x0F}},
    {"getSettingValue", kGetSettingValue,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
      0x8B, 0xF9, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x0D, 0xF4, 0x8F, 0x02, 0x01, 0xE8, 0xCF, 0x02, 0xF3}},
    {"setSettingValue", kSetSettingValue,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48,
      0x8B, 0xF1, 0x41, 0x8B, 0xF8, 0x48, 0x8B, 0x0D, 0xD4, 0x90, 0x02, 0x01, 0x48, 0x8B, 0xDA, 0xE8}},
    {"listSetFilter", kListSetFilter,
     {0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
      0x41, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x60, 0xF8, 0x87, 0x00}},
    {"padActionCheck", kPadActionCheck,
     {0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
      0x48, 0x8D, 0x6C, 0x24, 0x90, 0x48, 0x81, 0xEC, 0x70, 0x01, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24}},
    {"mainMenuCancelStep", kMainMenuCancelStep,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x10, 0xFF, 0xFF, 0xFF}},
    {"titleOptionCancel", kTitleOptionCancel,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x55, 0x57, 0x41, 0x54, 0x41, 0x56,
      0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xF1, 0x45}},
};

constexpr CodeSignature kBgmInfoSignatures[] = {
    {"makeBgmParam", kMakeBgmParam,
     {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xD0, 0x48, 0x81, 0xEC, 0x30,
      0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x16, 0x8E, 0x8B, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45}},
    {"getBgmTable", kGetBgmTable,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0xD2, 0xC2, 0x00, 0x00, 0x44, 0x0F,
      0xB6, 0xC0, 0x49, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC8, 0x48}},
};

constexpr CodeSignature kQuickSaveSignatures[] = {
    {"autoSaveProcessCtor", kAutoSaveProcessCtor,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x57,
      0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0xE8, 0x41, 0x7B, 0x33, 0x00, 0x90}},
    {"autoSaveBuildSetting", kAutoSaveBuildSetting,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
      0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD0, 0x48, 0x81, 0xEC, 0x30, 0x01, 0x00, 0x00}},
    {"autoSaveSucceeded", kAutoSaveSucceeded,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81,
      0xEC, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF9, 0x45, 0x33, 0xF6, 0x41, 0x8B, 0xF6, 0x44, 0x89}},
    {"autoDeleteCtor", kAutoDeleteCtor,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
      0xEC, 0x40, 0x4C, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x1A, 0x48, 0x89, 0x5C, 0x24, 0x68}},
    {"saveSettingBaseCtor", kSaveSettingBaseCtor,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
      0x8B, 0xD9, 0x33, 0xF6, 0x89, 0x71, 0x08, 0x48, 0x8D, 0x05, 0x62, 0x95, 0x6F, 0x00, 0x48, 0x89}},
    {"saveUtilAcknowledge", kSaveUtilAcknowledge,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x83, 0x79, 0x0C, 0x02, 0x48, 0x8B, 0xD9, 0x75, 0x45, 0xE8,
      0x1C, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0x8B, 0xA0, 0x08, 0x00, 0x00, 0x80, 0x79, 0x08, 0x00, 0x75}},
    {"getListCursor", kGetListCursor,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0x91, 0xB8, 0x05, 0x00, 0x00, 0x85, 0xD2, 0x78,
      0x59, 0x4C, 0x8B, 0x81, 0x58, 0x09, 0x00, 0x00, 0x48, 0x8B, 0x81, 0x60, 0x09, 0x00, 0x00, 0x49}},
    {"saveInfoCtor", kSaveInfoCtor,
     {0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0xC9, 0xE8, 0x94, 0x49, 0x6D, 0x00, 0x33, 0xC9, 0x48, 0x8D,
      0x05, 0x9B, 0x96, 0x8E, 0x00, 0x4C, 0x8D, 0x15, 0x5C, 0xE0, 0xCE, 0x00, 0x66, 0x41, 0x89, 0x89}},
    {"loadMenuFillRows", kLoadMenuFillRows,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0xFA, 0x4C}},
    {"loadMenuHeaderText", kLoadMenuHeaderText,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0xB8, 0x80, 0x10, 0x00, 0x00, 0xE8, 0x20, 0xD2, 0x53, 0x00,
      0x48, 0x2B, 0xE0, 0x48, 0x8B, 0xFA, 0x33, 0xDB, 0x48, 0x89, 0x5C, 0x24, 0x60, 0x48, 0x8D, 0x05}},
    {"loadEventStart", kLoadEventStart,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55,
      0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00}},
    {"setGameFlag", kSetGameFlag,
     {0x40, 0x53, 0x45, 0x33, 0xDB, 0x44, 0x0F, 0xB6, 0xD2, 0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0x2D,
      0xF1, 0xE9, 0x00, 0x4C, 0x8B, 0xCA, 0x48, 0x63, 0xC2, 0x48, 0xC1, 0xF8, 0x06, 0x41, 0x83, 0xE1}},
    {"screenFade", kScreenFade,
     {0x44, 0x89, 0x44, 0x24, 0x18, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
      0x57, 0x48, 0x8D, 0xAC, 0x24, 0x38, 0xFD, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xC8, 0x03, 0x00, 0x00}},
    {"cStringAppend", kCStringAppend,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
      0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x48, 0xC7, 0xC3, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x1F, 0x40, 0x00}},
    {"setListCursor", kSetListCursor,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0x83, 0xFA, 0xFF, 0x75, 0x08, 0x32, 0xC0,
      0x48, 0x83, 0xC4, 0x30, 0x5B, 0xC3, 0x48, 0x8B, 0x83, 0x60, 0x09, 0x00, 0x00, 0x48, 0x89, 0x6C}},
    {"loadCardsListener", kLoadCardsListener,
     {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
      0x48, 0x8D, 0xAC, 0x24, 0xB0, 0xEE, 0xFF, 0xFF, 0xB8, 0x50, 0x12, 0x00, 0x00, 0xE8, 0x3E, 0x2B}},
    {"cardModeText", kCardModeText,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x63, 0x41, 0x10, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0x41,
      0x08, 0x49, 0x83, 0xC0, 0x26, 0x4D, 0x03, 0xC0, 0x4E, 0x8B, 0x0C, 0xC0, 0x4D, 0x85, 0xC9, 0x74}},
    {"carouselInitialize", kCarouselInitialize,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xF0, 0xDB, 0xFF, 0xFF}},
    // The last three bytes are the playTimeBound patch site, checked before
    // the patch is written.
    {"carouselPlayTimeText", kCarouselPlayTimeText,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xDA,
      0x48, 0x8B, 0x49, 0x10, 0x48, 0x8B, 0x49, 0x20, 0xE8, 0x23, 0x56, 0x33, 0x00, 0x83, 0xF8, 0x04}},
    {"clearGameFlag", kClearGameFlag,
     {0x4C, 0x8B, 0xD2, 0x48, 0x63, 0xC2, 0x48, 0xC1, 0xF8, 0x06, 0x41, 0x83, 0xE2, 0x3F, 0x4C, 0x8B,
      0xC9, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xCA, 0x49, 0xD3, 0xE0, 0x4D, 0x23, 0x84}},
    // The call sites whose return address the detours look for.
    {"dailyFlagClearCall", kDailyFlagClearCall,
     {0xE8, 0x3B, 0xA5, 0xCD, 0xFF, 0x48, 0x83, 0xC3, 0x08, 0x48, 0x3B, 0xDE, 0x75, 0xD5, 0xE8, 0x6D,
      0x9D, 0xD0, 0xFF, 0x48, 0x8B, 0x05, 0xC6, 0xF7, 0xCA, 0x00, 0x48, 0x8B, 0x88, 0x08, 0x02, 0x00}},
    {"setupScriptConditionsCall", kSetupScriptConditionsCall,
     {0xE8, 0xFA, 0x0F, 0xF7, 0xFF, 0x84, 0xC0, 0x0F, 0x84, 0xBD, 0x03, 0x00, 0x00, 0x4C, 0x89, 0x74,
      0x24, 0x30, 0x4C, 0x8B, 0x07, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x4D, 0x8B, 0x88, 0x18, 0x02, 0x00}},
    {"eventConditions", kEventConditions,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x7C, 0x24, 0x10, 0x33, 0xC0, 0x4C, 0x8D, 0x99, 0x90,
      0x00, 0x00, 0x00, 0x49, 0x8B, 0xF9, 0x48, 0x8B, 0xDA, 0x44, 0x8B, 0xD0, 0x4D, 0x85, 0xC0, 0x74}},
    {"mapEventSetupScript", kMapEventSetupScript,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
      0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xEF, 0xFF, 0xFF}},
    {"mapEventContact", kMapEventContact,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
      0x48, 0x8D, 0x6C, 0x24, 0xD9, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x4D}},
    {"findCollisionComponent", kFindCollisionComponent,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x12, 0x31, 0x56, 0x00, 0x44, 0x0F,
      0xB6, 0xC0, 0x49, 0xBA, 0xB3, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC8, 0x48}},
};

// =============================================================
// LOAD MENU ROW PATCHES
// =============================================================
// The load menu builds its rows in code, so a sixth row only needs the state
// object to grow by one 16-byte row (0x278 -> 0x288, the rows are its last
// field) and every hard-coded row count or bound to move from 5 to 6.
struct CodePatch {
    const char* name;
    uintptr_t rva;
    uint8_t original[6];
    uint8_t patched[6];
    size_t length;
};

constexpr CodePatch kLoadMenuPatches[] = {
    // sub_140482050: allocation size and both memsets of the state object.
    {"stateAllocSize", 0x4822DA, {0x41, 0xB8, 0x78, 0x02, 0x00, 0x00}, {0x41, 0xB8, 0x88, 0x02, 0x00, 0x00}, 6},
    {"stateDebugClear", 0x4822FF, {0x41, 0xB8, 0x78, 0x02, 0x00, 0x00}, {0x41, 0xB8, 0x88, 0x02, 0x00, 0x00}, 6},
    {"stateClear", 0x48231C, {0x41, 0xB8, 0x78, 0x02, 0x00, 0x00}, {0x41, 0xB8, 0x88, 0x02, 0x00, 0x00}, 6},
    // Row array constructor count.
    {"rowConstruct", 0x48238F, {0xBA, 0x05, 0x00, 0x00, 0x00}, {0xBA, 0x06, 0x00, 0x00, 0x00}, 5},
    // sub_1404830B0: row array destructor count.
    {"rowDestruct", 0x483100, {0x41, 0xB8, 0x05, 0x00, 0x00, 0x00}, {0x41, 0xB8, 0x06, 0x00, 0x00, 0x00}, 6},
    // initialize_Inner: list item creation loop bound.
    {"rowItemLoop", 0x484855, {0x83, 0xFF, 0x05}, {0x83, 0xFF, 0x06}, 3},
    // playTime and gameTime text lambdas: highest valid cursor row.
    {"playTimeBound", 0x487FCD, {0x83, 0xF8, 0x04}, {0x83, 0xF8, 0x05}, 3},
    {"gameTimeBound", 0x487E4C, {0x83, 0xF8, 0x04}, {0x83, 0xF8, 0x05}, 3},
};

}  // namespace retail
