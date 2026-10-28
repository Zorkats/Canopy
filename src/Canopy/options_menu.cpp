#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "core.h"
#include "input.h"
#include "mod_config.h"
#include "mod_loader.h"
#include "options_menu.h"

// =============================================================
// RETURN TO TITLE DESIGN
// =============================================================
// The in-game options menu gets a native "Return to Title" button row right
// above Exit Game. The menu builds its rows from the gamesettinglist table,
// one per entry, each from the gamesetting record its entry names. For the
// length of one menu build, the table gets one extra entry naming the unused
// Online Manual button record, and that record gets our labels. The engine
// then builds a complete native button row with no handler of its own, and
// the row gets ours once the build is done.
//
// The row opens the engine's Yes/No popup, attached to the menu exactly like
// Exit Game's. Yes returns to the title from the next frame tick, outside
// every task update, since the title routine destroys the menu.

// =============================================================
// MOD SETTINGS DESIGN
// =============================================================
// The in-game and title options menus get a "Mod Settings" button row. It
// opens a second options menu, built by the engine like the first, that
// lists every registered mod as a dropdown of its settings, then a Back row.
//
// Each mod gets two header buttons, "+ Name" and "- Name", followed by its
// setting rows. The page's list filter (the one the quest menu uses to switch
// tabs) shows the "+" header alone while the mod is collapsed, and the "-"
// header with the settings while it is expanded. The two headers sit next to
// each other, so swapping them keeps the cursor on the same visible row.
// Pressing a header flips the mod and reapplies the filter on the next frame
// tick, outside the list's own input handling.
//
// Every row the mod adds names a fake gamesetting record with an id in a
// range the game never uses. A fake record is a copy of a real record of the
// same kind with our id, labels and descriptions, and a storage class the
// engine stores nothing for. The menu finds records through the gamesetting
// table's unordered_map, so the shared int64 map find answers our ids with
// static nodes when it searches that map. The value getter and setter answer
// our ids from mod_config before the engine looks anything up.
//
// Whole-program optimization let the find's callers keep values in the
// registers the find never writes (r8, r11, every xmm), so its detour is a
// few bytes of machine code that write only what the find itself writes. A
// C++ detour may use any volatile register and crashes the game at startup.
//
// A slider with step 1 is the engine's own numeric selector: a record of
// the slider type with no choices, which the menu fills with every number
// from min to max, keyed by the number. A slider with a larger step is a
// choice row with one generated number per step, keyed by index, in a
// record allocated to fit them all.
//
// A hotkey binding is a selector row with a single choice: its label on the
// left and, centered in the value column the way Key Config shows its keys,
// the bound key's glyph, drawn by the engine's padIcon text tag in the style
// of the controller in use. Pressing it makes input wait for the next key or
// button (see input.cpp), and the
// next press becomes the binding: Escape cancels, Delete clears. The engine
// draws a row's label once, when it builds the row, so the page is closed and
// built again, cursor on the same row, to show the wait and then the result.
//
// The page is a CTask_Menu_Option whose pad is a child of the first menu's,
// the way the main menu and the title open the first one, and it is attached
// to the first menu's owner on a new layer, like Key Config. Back closes it
// with the menu's own closeMethod.
//
// An options menu has no Cancel of its own: its owner handles Cancel and ends
// the whole options menu, the main menu by closing itself and the title by
// leaving its options state. While the page is open, both handlers close the
// page instead, which drops the player back on the first menu.

namespace options_menu {
namespace {

using namespace core;

// Ids of the rows the mod adds. The game's setting ids stop at 28.
constexpr int64_t kModSettingIdBase = 0x56510000;
constexpr int64_t kModSettingIdEnd = kModSettingIdBase + 0x200;
constexpr int64_t kModSettingsRowId = kModSettingIdBase;
constexpr int64_t kBackRowId = kModSettingIdBase + 1;
// Two headers per mod, collapsed then expanded.
constexpr int64_t kFirstHeaderId = kModSettingIdBase + 0x10;
constexpr int64_t kFirstSettingId = kModSettingIdBase + 0x100;
constexpr size_t kMaxMods = mod_config::kMaxMods;
constexpr size_t kMaxSettings = mod_config::kMaxSettings;
// Two buttons, two headers per mod and one row per setting.
constexpr size_t kMaxFakeRecords = 2 + 2 * kMaxMods + kMaxSettings;
static_assert(kFirstHeaderId + 2 * kMaxMods <= kFirstSettingId, "header ids overlap the setting ids");
static_assert(kFirstSettingId + kMaxSettings <= kModSettingIdEnd, "setting ids run past the id range");
// A record's header plus the most choices a setting can have.
constexpr size_t kFakeRecordSize =
    retail::kSettingRecordChoices + mod_config::kMaxChoices * retail::kSettingChoiceSize;
constexpr size_t kButtonCopySize = retail::kSettingRecordChoices;
constexpr size_t kToggleCopySize =
    retail::kSettingRecordChoices + retail::kToggleChoiceCount * retail::kSettingChoiceSize;
static_assert(kToggleCopySize <= kFakeRecordSize, "fake record too small");

constexpr const char* kModSettingsLabels[retail::kLanguageCount] = {
    "MOD設定", "Mod Settings", "Paramètres des mods", "Ajustes de mods", "模組設定", "모드 설정", "模组设置",
};

constexpr const char* kBackLabels[retail::kLanguageCount] = {
    "戻る", "Back", "Retour", "Volver", "返回", "돌아가기", "返回",
};

// Header prefixes. ASCII, since the Latin font lacks most symbols and the
// engine does no glyph fallback.
constexpr const char* kCollapsedPrefix = "+ ";
constexpr const char* kExpandedPrefix = "- ";

// A binding row's value: the bound key. The text tag draws the glyph of a
// framework pad id. It maps only virtual pads through the key config, so a
// physical key, button or mouse button shows as itself; every pad input.cpp
// can bind has a glyph.
constexpr size_t kBindingTextSize = 32;
constexpr const char* kPadGlyphFormat = "<padIcon %d>";
// A binding gives up waiting for a key after this long.
constexpr ULONGLONG kCaptureTimeoutMs = 8000;

constexpr const char* kNoneLabels[retail::kLanguageCount] = {
    "なし", "None", "Aucune", "Ninguna", "無", "없음", "无",
};

// Shown in place of the label while the row waits for its key.
constexpr const char* kWaitKeyLabels[retail::kLanguageCount] = {
    "キーを押してください (Esc: キャンセル / Del: 解除)",
    "Press a key (Esc: cancel, Del: clear)",
    "Appuyez sur une touche (Esc : annuler, Del : effacer)",
    "Pulsa una tecla (Esc: cancelar, Del: borrar)",
    "請按下按鍵 (Esc: 取消 / Del: 清除)",
    "키를 누르세요 (Esc: 취소 / Del: 해제)",
    "请按下按键 (Esc: 取消 / Del: 清除)",
};

constexpr const char* kWaitButtonLabels[retail::kLanguageCount] = {
    "ボタンを押してください (Esc: キャンセル / Del: 解除)",
    "Press a button (Esc: cancel, Del: clear)",
    "Appuyez sur un bouton (Esc : annuler, Del : effacer)",
    "Pulsa un botón (Esc: cancelar, Del: borrar)",
    "請按下按鈕 (Esc: 取消 / Del: 清除)",
    "버튼을 누르세요 (Esc: 취소 / Del: 해제)",
    "请按下按钮 (Esc: 取消 / Del: 清除)",
};

// "Return to Title" per language index (JA, EN, FR, ES, ZH-TW, KO, ZH-CN), UTF-8.
constexpr const char* kRowLabels[retail::kLanguageCount] = {
    "\xE3\x82\xBF\xE3\x82\xA4\xE3\x83\x88\xE3\x83\xAB\xE3\x81\xB8\xE6\x88\xBB\xE3\x82\x8B",
    "Return to Title",
    "Retour \xC3\xA0 l'\xC3\xA9"
    "cran titre",
    "Volver al t\xC3\xADtulo",
    "\xE8\xBF\x94\xE5\x9B\x9E\xE6\xA8\x99\xE9\xA1\x8C\xE7\x95\xAB\xE9\x9D\xA2",
    "\xED\x83\x80\xEC\x9D\xB4\xED\x8B\x80\xEB\xA1\x9C \xEB\x8F\x8C\xEC\x95\x84\xEA\xB0\x80\xEA\xB8\xB0",
    "\xE8\xBF\x94\xE5\x9B\x9E\xE6\xA0\x87\xE9\xA2\x98\xE7\x94\xBB\xE9\x9D\xA2",
};

// Confirmation per language index, UTF-8: the question, then a red warning line.
constexpr const char* kPrompts[retail::kLanguageCount] = {
    "\xE3\x82\xBF\xE3\x82\xA4\xE3\x83\x88\xE3\x83\xAB\xE7\x94\xBB\xE9\x9D\xA2\xE3\x81\xAB\xE6\x88\xBB\xE3\x82"
    "\x8A\xE3\x81\xBE\xE3\x81\x99\xE3\x81\x8B\xEF\xBC\x9F<br><c red>\xE3\x82\xBB\xE3\x83\xBC\xE3\x83\x96\xE3"
    "\x81\x97\xE3\x81\xA6\xE3\x81\x84\xE3\x81\xAA\xE3\x81\x84\xE9\x80\xB2\xE8\xA1\x8C\xE7\x8A\xB6\xE6\xB3\x81"
    "\xE3\x81\xAF\xE5\xA4\xB1\xE3\x82\x8F\xE3\x82\x8C\xE3\x81\xBE\xE3\x81\x99\xE3\x80\x82</c>",
    "Return to the title screen?<br><c red>Unsaved progress will be lost.</c>",
    "Revenir \xC3\xA0 l'\xC3\xA9"
    "cran titre ?<br><c red>La progression non sauvegard\xC3\xA9"
    "e sera perdue.</c>",
    "\xC2\xBFVolver a la pantalla de t\xC3\xADtulo?<br><c red>Se perder\xC3\xA1 el progreso no guardado.</c>",
    "\xE8\xA6\x81\xE8\xBF\x94\xE5\x9B\x9E\xE6\xA8\x99\xE9\xA1\x8C\xE7\x95\xAB\xE9\x9D\xA2\xE5\x97\x8E\xEF\xBC"
    "\x9F<br><c red>\xE6\x9C\xAA\xE5\x84\xB2\xE5\xAD\x98\xE7\x9A\x84\xE9\x80\xB2\xE5\xBA\xA6\xE5\xB0\x87\xE6"
    "\x9C\x83\xE9\x81\xBA\xE5\xA4\xB1\xE3\x80\x82</c>",
    "\xED\x83\x80\xEC\x9D\xB4\xED\x8B\x80 \xED\x99\x94\xEB\xA9\xB4\xEC\x9C\xBC\xEB\xA1\x9C \xEB\x8F\x8C\xEC"
    "\x95\x84\xEA\xB0\x80\xEC\x8B\x9C\xEA\xB2\xA0\xEC\x8A\xB5\xEB\x8B\x88\xEA\xB9\x8C?<br><c red>\xEC\xA0\x80"
    "\xEC\x9E\xA5\xED\x95\x98\xEC\xA7\x80 \xEC\x95\x8A\xEC\x9D\x80 \xEC\xA7\x84\xED\x96\x89 \xEC\x83\x81\xED"
    "\x99\xA9\xEC\x9D\x80 \xEC\x82\xAC\xEB\x9D\xBC\xEC\xA7\x91\xEB\x8B\x88\xEB\x8B\xA4.</c>",
    "\xE8\xA6\x81\xE8\xBF\x94\xE5\x9B\x9E\xE6\xA0\x87\xE9\xA2\x98\xE7\x94\xBB\xE9\x9D\xA2\xE5\x90\x97\xEF\xBC"
    "\x9F<br><c red>\xE6\x9C\xAA\xE4\xBF\x9D\xE5\xAD\x98\xE7\x9A\x84\xE8\xBF\x9B\xE5\xBA\xA6\xE5\xB0\x86\xE4"
    "\xBC\x9A\xE4\xB8\xA2\xE5\xA4\xB1\xE3\x80\x82</c>",
};


// A popup that never lets go is given up on after this many frames.
constexpr int kPopupTimeoutFrames = 600;

using MenuInitializeFn = void(__fastcall*)(uint8_t* menu);
using GetTableFn = uint8_t*(__fastcall*)(void* registry);
using FindParentClassFn = void*(__fastcall*)(const void* task, uint32_t classId);
using ClassIdFn = uint32_t(__fastcall*)();
using BindListItemInputFn = void*(__fastcall*)(void* item, PromptCallback* fn, int32_t input);
using OptionMenuCtorFn = uint8_t*(__fastcall*)(uint8_t* task, void* parentPad, uint64_t a3);
using OptionMenuCloseFn = void(__fastcall*)(uint8_t* menu);
using GetSettingValueFn = uint64_t(__fastcall*)(void* manager, int64_t settingId);
using SetSettingValueFn = uint64_t(__fastcall*)(void* manager, int64_t settingId, int32_t value);
// The list's filter gets each item as an intrusive_ptr<CListItemInfo>, and the
// cursor function the list. Same std::function layout as PromptCallback.
using ListFilter = std::function<bool(const uint8_t* const& info)>;
using ListCursor = std::function<int32_t(const uint8_t* const& list)>;
using ListSetFilterFn = void(__fastcall*)(uint8_t* list, ListFilter* filter, ListCursor* cursor);
using PadActionCheckFn = uint8_t*(__fastcall*)(void* pad, uint64_t actionId, uint8_t* query);
using MainMenuCancelStepFn = uint8_t(__fastcall*)(uint8_t* menu);
using TitleOptionCancelFn = void(__fastcall*)(uint8_t* const* state);

MenuInitializeFn g_OriginalMenuInitialize = nullptr;
MainMenuCancelStepFn g_OriginalMainMenuCancelStep = nullptr;
TitleOptionCancelFn g_OriginalTitleOptionCancel = nullptr;
// Only the find thunk calls it, through this pointer.
void* g_OriginalInt64MapFind = nullptr;
GetSettingValueFn g_OriginalGetSettingValue = nullptr;
SetSettingValueFn g_OriginalSetSettingValue = nullptr;
GetTableFn g_GetSettingListTable = nullptr;
GetTableFn g_GetSettingTable = nullptr;
FindParentClassFn g_FindParentClass = nullptr;
ClassIdFn g_MainMenuClassId = nullptr;
ClassIdFn g_TitleClassId = nullptr;
BindListItemInputFn g_BindListItemInput = nullptr;
OptionMenuCtorFn g_OptionMenuCtor = nullptr;
OptionMenuCloseFn g_OptionMenuClose = nullptr;
ListSetFilterFn g_ListSetFilter = nullptr;
PadActionCheckFn g_PadActionCheck = nullptr;

bool g_Installed = false;

// The injected gamesettinglist entry: the table's vector points to a slot,
// the slot to the row, and the row names its setting id at +8.
alignas(8) uint8_t g_RowEntry[32] = {};
void* g_RowSlot = g_RowEntry;
// The table's entries plus ours, while one menu build runs.
std::vector<void*> g_Slots;
bool g_Building = false;

// ModButton is a mod's own button row; Button is one of ours.
enum class RowRole { Button, CollapsedHeader, ExpandedHeader, Setting, Binding, ModButton };

// One row the mod adds: its record, the map node the find hook hands out
// (the menu keeps a pointer to the node's value), and its list entry. Rows
// live for the whole process, so every pointer the engine keeps stays valid.
struct FakeRecord {
    // The record the engine reads: inlineRecord, or a larger buffer for a
    // slider with more choices than it holds.
    uint8_t* record;
    alignas(16) uint8_t inlineRecord[kFakeRecordSize];
    alignas(8) uint8_t node[retail::kMapNodeSize];
    alignas(8) uint8_t listRow[32];
    void* listSlot;
    int64_t id;
    RowRole role;
    // The mod_config mod the row belongs to, or -1.
    int mod;
    // The mod_config setting the row shows, or -1.
    int setting;
};

enum class FakeState { NotBuilt, Built, Failed };

FakeRecord g_Fakes[kMaxFakeRecords] = {};
size_t g_FakeCount = 0;
FakeState g_FakeState = FakeState::NotBuilt;
// The gamesetting map, set once the fake records exist.
const uint8_t* g_SettingMap = nullptr;
// Each fake's map node by id - kModSettingIdBase, for the find thunk.
void* g_FakeNodes[kModSettingIdEnd - kModSettingIdBase] = {};
// Header labels, "+ Name" and "- Name" per language. A deque never moves its
// strings, so the records can point into them.
std::deque<std::string> g_HeaderTexts;
// Binding row values per setting and language. Rewritten in place before each
// page build, so the records' pointers stay valid.
char g_BindingTexts[kMaxSettings][retail::kLanguageCount][kBindingTextSize] = {};
// Records too large for a fake's inline storage, and the numbers their
// choices show. Neither ever moves, so the engine may keep pointers.
std::deque<std::vector<uint8_t>> g_LargeRecords;
std::deque<std::string> g_NumberTexts;

// The mod settings page while it lives, with our own reference to it, the
// options menu it opened from, and the task both are attached to.
uint8_t* g_ModPage = nullptr;
uint8_t* g_ModParent = nullptr;
uint8_t* g_ModOwner = nullptr;
// Pages closed to be built again, each with our own reference until the
// engine lets go of it.
std::vector<uint8_t*> g_ClosingPages;
bool g_RebuildPending = false;
// The row the next page build puts the cursor on, or 0.
int64_t g_CursorKey = 0;
// The binding waiting for its key, or -1.
int g_CaptureSetting = -1;
// Set once nothing is held, so the press that started the wait is not taken.
bool g_CaptureArmed = false;
ULONGLONG g_CaptureStart = 0;
// Which mods are expanded. Kept across page opens for the session.
std::vector<uint8_t> g_Expanded;
bool g_FilterPending = false;
// Mod button rows pressed this frame, run from the tick outside the page's
// update.
std::vector<int> g_PressedButtons;

// Return to title prompt. g_PromptTask holds our own reference to the open
// popup, so its refcount dropping to ours alone means the engine let go of it.
enum class PromptAnswer { None, Yes, No };
uint8_t* g_PromptTask = nullptr;
PromptAnswer g_PromptAnswer = PromptAnswer::None;
int g_PromptFrames = 0;

enum class MenuKind { Other, InGame, Title, ModPage };

// =============================================================
// SETTING TABLES
// =============================================================
// Every gamesetting record and gamesettinglist row sits behind two pointers.
const uint8_t* TableRow(void* const* slot) {
    const void* row = *static_cast<void* const*>(*slot);
    return static_cast<const uint8_t*>(row);
}

uint8_t* FindSettingRecord(const uint8_t* settingTable, int64_t settingId) {
    auto slot = ReadAt<void* const*>(settingTable, retail::kTableSlotsBegin);
    const auto end = ReadAt<void* const*>(settingTable, retail::kTableSlotsEnd);
    for (; slot != end; ++slot) {
        const uint8_t* record = TableRow(slot);
        if (record && ReadAt<int64_t>(record, retail::kSettingRecordId) == settingId) {
            return const_cast<uint8_t*>(record);
        }
    }
    return nullptr;
}

int32_t RecordType(const uint8_t* record) {
    return ReadAt<int32_t>(record, retail::kSettingRecordType);
}

// =============================================================
// FAKE RECORDS
// =============================================================
FakeRecord* FindFake(int64_t settingId) {
    if (settingId < kModSettingIdBase || settingId >= kModSettingIdEnd) {
        return nullptr;
    }
    for (size_t i = 0; i < g_FakeCount; ++i) {
        if (g_Fakes[i].id == settingId) {
            return &g_Fakes[i];
        }
    }
    return nullptr;
}

// storage, when given, holds the record instead of the fake's own.
FakeRecord* AddFake(
    int64_t id, const uint8_t* source, size_t copySize, const char* const* labels, RowRole role, int mod, int setting,
    uint8_t* storage = nullptr
) {
    if (g_FakeCount == kMaxFakeRecords) {
        return nullptr;
    }
    FakeRecord& fake = g_Fakes[g_FakeCount++];
    fake.record = storage ? storage : fake.inlineRecord;
    std::memcpy(fake.record, source, copySize);
    WriteAt<int64_t>(fake.record, retail::kSettingRecordId, id);
    WriteAt<int32_t>(fake.record, retail::kSettingRecordStorage, retail::kSettingStorageNone);
    std::memcpy(fake.record + retail::kSettingRecordLabels, labels, sizeof(const char*) * retail::kLanguageCount);
    WriteAt<int64_t>(fake.node, retail::kMapNodeKey, id);
    WriteAt<void*>(fake.node, retail::kMapNodeValue, fake.record);
    WriteAt<int64_t>(fake.listRow, retail::kSettingListRowId, id);
    fake.listSlot = fake.listRow;
    g_FakeNodes[id - kModSettingIdBase] = fake.node;
    fake.id = id;
    fake.role = role;
    fake.mod = mod;
    fake.setting = setting;
    return &fake;
}

void AddButton(int64_t id, const uint8_t* buttonTemplate, const char* const* labels, RowRole role, int mod) {
    if (FakeRecord* fake = AddFake(id, buttonTemplate, kButtonCopySize, labels, role, mod, -1)) {
        WriteAt<int32_t>(fake->record, retail::kSettingRecordChoiceCount, 0);
    }
}

int64_t HeaderId(int mod, bool expanded) {
    return kFirstHeaderId + 2 * mod + (expanded ? 1 : 0);
}

void AddHeader(const uint8_t* buttonTemplate, int mod, bool expanded) {
    const char* const* name = mod_config::ModAt(static_cast<size_t>(mod)).name;
    const char* labels[retail::kLanguageCount];
    for (int language = 0; language < retail::kLanguageCount; ++language) {
        g_HeaderTexts.push_back(std::string(expanded ? kExpandedPrefix : kCollapsedPrefix) + name[language]);
        labels[language] = g_HeaderTexts.back().c_str();
    }
    const RowRole role = expanded ? RowRole::ExpandedHeader : RowRole::CollapsedHeader;
    AddButton(HeaderId(mod, expanded), buttonTemplate, labels, role, mod);
}

// A toggle keeps the template's choice names (the game's own Off and On) and
// shows the setting's help as both choices' descriptions.
void AddToggle(int64_t id, const uint8_t* toggleTemplate, size_t setting) {
    const mod_config::Setting& config = mod_config::At(setting);
    FakeRecord* fake =
        AddFake(id, toggleTemplate, kToggleCopySize, config.label, RowRole::Setting, config.mod, static_cast<int>(setting));
    if (!fake) {
        Log(std::string("No room for the ") + config.key + " row.");
        return;
    }
    WriteAt<int32_t>(fake->record, retail::kSettingRecordDefault, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMin, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMax, retail::kToggleChoiceCount - 1);
    for (int choice = 0; choice < retail::kToggleChoiceCount; ++choice) {
        uint8_t* entry = fake->record + retail::kSettingRecordChoices + choice * retail::kSettingChoiceSize;
        std::memcpy(
            entry + retail::kSettingChoiceDescriptions, config.help, sizeof(const char*) * retail::kLanguageCount
        );
        WriteAt<int32_t>(entry, retail::kSettingChoiceValue, choice);
    }
}

// A choice row is the toggle's header with the setting's own choices. The
// menu builds a choice record with other than two choices as a selector that
// keys each choice by its index, like a toggle.
void AddChoiceRow(int64_t id, const uint8_t* toggleTemplate, size_t setting) {
    const mod_config::Setting& config = mod_config::At(setting);
    FakeRecord* fake =
        AddFake(id, toggleTemplate, kButtonCopySize, config.label, RowRole::Setting, config.mod, static_cast<int>(setting));
    if (!fake) {
        Log(std::string("No room for the ") + config.key + " row.");
        return;
    }
    WriteAt<int32_t>(fake->record, retail::kSettingRecordDefault, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMin, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMax, config.choiceCount - 1);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordChoiceCount, config.choiceCount);
    for (int32_t choice = 0; choice < config.choiceCount; ++choice) {
        uint8_t* entry = fake->record + retail::kSettingRecordChoices + choice * retail::kSettingChoiceSize;
        std::memset(entry, 0, retail::kSettingChoiceSize);
        std::memcpy(
            entry + retail::kSettingChoiceNames, config.choices[choice], sizeof(const char*) * retail::kLanguageCount
        );
        std::memcpy(
            entry + retail::kSettingChoiceDescriptions, config.help, sizeof(const char*) * retail::kLanguageCount
        );
        WriteAt<int32_t>(entry, retail::kSettingChoiceValue, choice);
    }
}

// A slider with a larger step shows its numbers as choices, keyed by index.
bool SliderByIndex(const mod_config::Setting& config) {
    return config.kind == mod_config::Kind::Slider && config.step > 1;
}

int32_t SliderCount(const mod_config::Setting& config) {
    return static_cast<int32_t>((static_cast<int64_t>(config.max) - config.min) / config.step + 1);
}

// The value the engine keys a row's items by: the setting's value, or a
// stepped slider's choice index.
int32_t RowValue(const mod_config::Setting& config) {
    if (!SliderByIndex(config)) {
        return config.value;
    }
    return static_cast<int32_t>((static_cast<int64_t>(config.value) - config.min) / config.step);
}

// The setting value for an item key the engine sets a row to. A key off
// the slider gives the current value, which changes nothing.
int32_t SettingValue(const mod_config::Setting& config, int32_t rowValue) {
    if (!SliderByIndex(config)) {
        return rowValue;
    }
    if (rowValue < 0 || rowValue >= SliderCount(config)) {
        return config.value;
    }
    return static_cast<int32_t>(config.min + static_cast<int64_t>(rowValue) * config.step);
}

// A slider row is the toggle's header. With step 1 it becomes the slider
// type with no choices, and the menu lists min to max itself; its help is
// written to the first choice's descriptions anyway, where a choice row's
// help is read. With a larger step it is a choice row of generated
// numbers, in a record sized for all of them.
void AddSliderRow(int64_t id, const uint8_t* toggleTemplate, size_t setting) {
    const mod_config::Setting& config = mod_config::At(setting);
    const int32_t count = SliderCount(config);
    uint8_t* storage = nullptr;
    if (SliderByIndex(config)) {
        g_LargeRecords.emplace_back(retail::kSettingRecordChoices + static_cast<size_t>(count) * retail::kSettingChoiceSize, 0);
        storage = g_LargeRecords.back().data();
    }
    FakeRecord* fake = AddFake(
        id, toggleTemplate, kButtonCopySize, config.label, RowRole::Setting, config.mod, static_cast<int>(setting),
        storage
    );
    if (!fake) {
        Log(std::string("No room for the ") + config.key + " row.");
        return;
    }
    if (!SliderByIndex(config)) {
        WriteAt<int32_t>(fake->record, retail::kSettingRecordType, retail::kSettingTypeSlider);
        WriteAt<int32_t>(fake->record, retail::kSettingRecordDefault, config.min);
        WriteAt<int32_t>(fake->record, retail::kSettingRecordMin, config.min);
        WriteAt<int32_t>(fake->record, retail::kSettingRecordMax, config.max);
        WriteAt<int32_t>(fake->record, retail::kSettingRecordChoiceCount, 0);
        uint8_t* entry = fake->record + retail::kSettingRecordChoices;
        std::memset(entry, 0, retail::kSettingChoiceSize);
        std::memcpy(
            entry + retail::kSettingChoiceDescriptions, config.help, sizeof(const char*) * retail::kLanguageCount
        );
        return;
    }
    WriteAt<int32_t>(fake->record, retail::kSettingRecordDefault, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMin, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMax, count - 1);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordChoiceCount, count);
    for (int32_t choice = 0; choice < count; ++choice) {
        g_NumberTexts.push_back(std::to_string(config.min + choice * config.step));
        const char* names[retail::kLanguageCount];
        for (const char*& name : names) {
            name = g_NumberTexts.back().c_str();
        }
        uint8_t* entry = fake->record + retail::kSettingRecordChoices + choice * retail::kSettingChoiceSize;
        std::memcpy(entry + retail::kSettingChoiceNames, names, sizeof(names));
        std::memcpy(
            entry + retail::kSettingChoiceDescriptions, config.help, sizeof(const char*) * retail::kLanguageCount
        );
        WriteAt<int32_t>(entry, retail::kSettingChoiceValue, choice);
    }
}

// A binding row's label, or the wait prompt while it waits for its key, and
// its value, the bound key's glyph.
void WriteBindingTexts(FakeRecord& fake) {
    const size_t setting = static_cast<size_t>(fake.setting);
    const mod_config::Setting& config = mod_config::At(setting);
    const bool waiting = g_CaptureSetting == fake.setting;
    const bool controller = config.device == input::Device::Controller;
    const char* labels[retail::kLanguageCount];
    for (int language = 0; language < retail::kLanguageCount; ++language) {
        labels[language] = !waiting ? config.label[language]
                           : controller ? kWaitButtonLabels[language]
                                        : kWaitKeyLabels[language];
        char* text = g_BindingTexts[setting][language];
        if (config.value != input::kNoPad && input::PadName(config.value)) {
            std::snprintf(text, kBindingTextSize, kPadGlyphFormat, config.value);
        } else {
            std::snprintf(text, kBindingTextSize, "%s", config.value == input::kNoPad ? kNoneLabels[language] : "?");
        }
    }
    std::memcpy(fake.record + retail::kSettingRecordLabels, labels, sizeof(labels));
}

// A binding is the toggle's header with one choice, the bound key, showing
// the setting's help as its description.
void AddBindingRow(int64_t id, const uint8_t* toggleTemplate, size_t setting) {
    const mod_config::Setting& config = mod_config::At(setting);
    FakeRecord* fake = setting < kMaxSettings ? AddFake(
                                                    id, toggleTemplate, kButtonCopySize, config.label,
                                                    RowRole::Binding, config.mod, static_cast<int>(setting)
                                                )
                                              : nullptr;
    if (!fake) {
        Log(std::string("No room for the ") + config.key + " row.");
        return;
    }
    WriteBindingTexts(*fake);
    const char* values[retail::kLanguageCount];
    for (int language = 0; language < retail::kLanguageCount; ++language) {
        values[language] = g_BindingTexts[setting][language];
    }
    WriteAt<int32_t>(fake->record, retail::kSettingRecordDefault, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMin, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordMax, 0);
    WriteAt<int32_t>(fake->record, retail::kSettingRecordChoiceCount, 1);
    uint8_t* entry = fake->record + retail::kSettingRecordChoices;
    std::memset(entry, 0, retail::kSettingChoiceSize);
    std::memcpy(entry + retail::kSettingChoiceNames, values, sizeof(values));
    std::memcpy(entry + retail::kSettingChoiceDescriptions, config.help, sizeof(const char*) * retail::kLanguageCount);
    WriteAt<int32_t>(entry, retail::kSettingChoiceValue, 0);
}

// A mod's button row is a plain button with the setting's label.
void AddModButtonRow(int64_t id, const uint8_t* buttonTemplate, size_t setting) {
    const mod_config::Setting& config = mod_config::At(setting);
    FakeRecord* fake = AddFake(
        id, buttonTemplate, kButtonCopySize, config.label, RowRole::ModButton, config.mod, static_cast<int>(setting)
    );
    if (!fake) {
        Log(std::string("No room for the ") + config.key + " row.");
        return;
    }
    WriteAt<int32_t>(fake->record, retail::kSettingRecordChoiceCount, 0);
}

void LogToggleTemplate(const uint8_t* toggleTemplate) {
    std::string text = "Toggle choices:";
    for (int choice = 0; choice < retail::kToggleChoiceCount; ++choice) {
        const uint8_t* entry = toggleTemplate + retail::kSettingRecordChoices + choice * retail::kSettingChoiceSize;
        const char* name = ReadAt<const char*>(
            entry, retail::kSettingChoiceNames + sizeof(const char*) * retail::kLanguageEnglish
        );
        text += std::string(" ") + std::to_string(choice) + "=\"" + (name ? name : "") + "\" value " +
                std::to_string(ReadAt<int32_t>(entry, retail::kSettingChoiceValue));
    }
    Log(text + ".");
}

// Built on the first menu build, once every feature registered its settings.
bool BuildFakeRecords(const uint8_t* settingTable) {
    if (g_FakeState != FakeState::NotBuilt) {
        return g_FakeState == FakeState::Built;
    }
    const uint8_t* buttonTemplate = FindSettingRecord(settingTable, retail::kUnusedButtonSettingId);
    const uint8_t* toggleTemplate = FindSettingRecord(settingTable, retail::kToggleTemplateSettingId);
    if (!buttonTemplate || RecordType(buttonTemplate) != retail::kSettingTypeButton || !toggleTemplate ||
        RecordType(toggleTemplate) != retail::kSettingTypeChoice ||
        ReadAt<int32_t>(toggleTemplate, retail::kSettingRecordChoiceCount) != retail::kToggleChoiceCount) {
        Log("Mod settings disabled, the template setting records are not as expected.");
        g_FakeState = FakeState::Failed;
        return false;
    }
    LogToggleTemplate(toggleTemplate);
    AddButton(kModSettingsRowId, buttonTemplate, kModSettingsLabels, RowRole::Button, -1);
    AddButton(kBackRowId, buttonTemplate, kBackLabels, RowRole::Button, -1);
    for (int mod = 0; mod < static_cast<int>(mod_config::ModCount()); ++mod) {
        AddHeader(buttonTemplate, mod, false);
        AddHeader(buttonTemplate, mod, true);
    }
    g_Expanded.assign(mod_config::ModCount(), 0);
    for (size_t setting = 0; setting < mod_config::Count(); ++setting) {
        const int64_t id = kFirstSettingId + static_cast<int64_t>(setting);
        switch (mod_config::At(setting).kind) {
            case mod_config::Kind::Binding:
                AddBindingRow(id, toggleTemplate, setting);
                break;
            case mod_config::Kind::Choice:
                AddChoiceRow(id, toggleTemplate, setting);
                break;
            case mod_config::Kind::Button:
                AddModButtonRow(id, buttonTemplate, setting);
                break;
            case mod_config::Kind::Slider:
                AddSliderRow(id, toggleTemplate, setting);
                break;
            default:
                AddToggle(id, toggleTemplate, setting);
                break;
        }
    }
    g_SettingMap = settingTable + retail::kSettingTableMap;
    g_FakeState = FakeState::Built;
    return true;
}

// =============================================================
// MENU BUILD
// =============================================================
MenuKind KindOf(const uint8_t* menu) {
    if (menu == g_ModPage) {
        return MenuKind::ModPage;
    }
    if (g_FindParentClass(menu, g_MainMenuClassId())) {
        return MenuKind::InGame;
    }
    if (g_FindParentClass(menu, g_TitleClassId())) {
        return MenuKind::Title;
    }
    return MenuKind::Other;
}

// The mod settings page lists each mod's two headers and settings, then Back.
// Other menus get our rows right before Exit Game, or at the end.
void BuildSlots(const uint8_t* listTable, MenuKind kind, bool modRows, bool titleRow) {
    g_Slots.clear();
    if (kind == MenuKind::ModPage) {
        for (int mod = 0; mod < static_cast<int>(mod_config::ModCount()); ++mod) {
            for (bool expanded : {false, true}) {
                if (FakeRecord* header = FindFake(HeaderId(mod, expanded))) {
                    g_Slots.push_back(&header->listSlot);
                }
            }
            for (size_t i = 0; i < g_FakeCount; ++i) {
                const RowRole role = g_Fakes[i].role;
                const bool modRow = role == RowRole::Setting || role == RowRole::Binding || role == RowRole::ModButton;
                if (modRow && g_Fakes[i].mod == mod) {
                    g_Slots.push_back(&g_Fakes[i].listSlot);
                }
            }
        }
        if (FakeRecord* back = FindFake(kBackRowId)) {
            g_Slots.push_back(&back->listSlot);
        }
        return;
    }

    std::vector<void*> added;
    if (FakeRecord* modSettings = modRows ? FindFake(kModSettingsRowId) : nullptr) {
        added.push_back(&modSettings->listSlot);
    }
    if (titleRow) {
        added.push_back(&g_RowSlot);
    }
    auto slot = ReadAt<void* const*>(listTable, retail::kTableSlotsBegin);
    const auto end = ReadAt<void* const*>(listTable, retail::kTableSlotsEnd);
    bool inserted = false;
    for (; slot != end; ++slot) {
        const uint8_t* row = TableRow(slot);
        if (!inserted && row && ReadAt<int64_t>(row, retail::kSettingListRowId) == retail::kExitGameSettingId) {
            g_Slots.insert(g_Slots.end(), added.begin(), added.end());
            inserted = true;
        }
        g_Slots.push_back(*slot);
    }
    if (!inserted) {
        g_Slots.insert(g_Slots.end(), added.begin(), added.end());
    }
}

// =============================================================
// PROMPT
// =============================================================
void OpenPrompt(uint8_t* menu) {
    if (g_PromptTask) {
        return;
    }
    g_PromptAnswer = PromptAnswer::None;
    uint8_t* task = CreateYesNoPopup(
        kPrompts[CurrentLanguage()], [] { g_PromptAnswer = PromptAnswer::Yes; },
        [] { g_PromptAnswer = PromptAnswer::No; }
    );
    if (!task) {
        return;
    }
    // Exit Game attaches its popup the same way, with no layer key.
    AddRef(task, retail::kTaskRefCount);
    AttachTask(menu, task, retail::kAttachModeMenuPopup);
    g_PromptTask = task;
    g_PromptFrames = 0;
    Log("Return to title prompt opened.");
}

// =============================================================
// MOD SETTINGS PAGE
// =============================================================
// Opened like the game opens Key Config: on the options menu's parent, on a
// new layer, which hides the options menu until the page ends and its layer
// pops.
void OpenModPage(uint8_t* menu) {
    uint8_t* owner = ReadAt<uint8_t*>(menu, retail::kTaskParent);
    if (g_ModPage || !owner) {
        return;
    }
    uint8_t* page = AllocateZeroed(retail::kOptionMenuTaskSize);
    if (!page) {
        return;
    }
    g_OptionMenuCtor(page, menu + retail::kTaskPad, 0);
    AddRef(page, retail::kTaskRefCount);
    g_ModPage = page;
    g_ModParent = menu;
    g_ModOwner = owner;
    AttachTask(owner, page, retail::kAttachModePushLayer);
    Log("Mod settings page opened.");
}

// closeMethod only queues the end, so the page's own row may call it.
void CloseModPage(uint8_t* page) {
    g_OptionMenuClose(page);
    Log("Mod settings page closed.");
}

// Whether the page is open over owner's options menu, closing or not.
bool PageOwnedBy(const uint8_t* owner) {
    return g_ModPage && owner && g_ModOwner == owner;
}

// Cancel on the page closes it, unless it is already closing or a binding is
// waiting for its key.
void CancelModPage() {
    if (g_CaptureSetting < 0 && ReadAt<uint8_t>(g_ModPage, retail::kOptionMenuClosing) == 0) {
        CloseModPage(g_ModPage);
    }
}

// Checks Cancel exactly like the main menu does. The check counts itself on
// the press, so the main menu's own check right after sees the press taken.
bool CancelPressed(void* pad) {
    alignas(16) uint8_t query[retail::kPadQuerySize] = {};
    // The main menu remaps both ways.
    query[retail::kPadQueryRemap] = 1;
    query[retail::kPadQueryRemap + 1] = 1;
    WriteAt<int32_t>(query, retail::kPadQueryDeviceMask, 0xFF);
    query[retail::kPadQueryWaitFlag] = 1;
    WriteAt<uintptr_t>(query, retail::kPadQueryText, g_ModuleBase + retail::kCStringVtable);
    WriteAt<uintptr_t>(query, retail::kPadQueryText + retail::kCStringData, g_ModuleBase + retail::kEmptyStringData);
    uint8_t* record = pad ? g_PadActionCheck(pad, retail::kPadActionCancel, query) : nullptr;
    if (!record || record[retail::kPadActionDown] == 0) {
        return false;
    }
    int32_t& checks = *reinterpret_cast<int32_t*>(record + retail::kPadActionDownChecks);
    return ++checks <= 1;
}

bool RowVisible(const uint8_t* info) {
    const uint8_t* control = info ? ReadAt<const uint8_t*>(info, retail::kListItemInfoControl) : nullptr;
    const FakeRecord* fake = control ? FindFake(ReadAt<int64_t>(control, retail::kListItemKey)) : nullptr;
    if (!fake || fake->mod < 0 || static_cast<size_t>(fake->mod) >= g_Expanded.size()) {
        return true;
    }
    const bool expanded = g_Expanded[fake->mod] != 0;
    switch (fake->role) {
        case RowRole::CollapsedHeader:
            return !expanded;
        case RowRole::ExpandedHeader:
        case RowRole::Setting:
        case RowRole::Binding:
        case RowRole::ModButton:
            return expanded;
        default:
            return true;
    }
}

// The visible row whose key is key, or the first row.
int32_t VisibleIndexOf(const uint8_t* list, int64_t key) {
    auto info = ReadAt<const uint8_t* const*>(list, retail::kListItemsBegin);
    const auto end = ReadAt<const uint8_t* const*>(list, retail::kListItemsEnd);
    for (int32_t index = 0; info != end; ++info, ++index) {
        const uint8_t* control = *info ? ReadAt<const uint8_t*>(*info, retail::kListItemInfoControl) : nullptr;
        if (control && ReadAt<int64_t>(control, retail::kListItemKey) == key) {
            return index;
        }
    }
    return 0;
}

// The list keeps a copy of the filter and runs it again whenever it
// refreshes. An empty cursor function keeps the cursor's index; a cursorKey
// moves the cursor to that row instead.
void ApplyFilter(uint8_t* page, int64_t cursorKey = 0) {
    uint8_t* list = ReadAt<uint8_t*>(page, retail::kOptionMenuList);
    if (!list) {
        return;
    }
    ListFilter filter = [](const uint8_t* const& info) { return RowVisible(info); };
    ListCursor cursor;
    if (cursorKey != 0) {
        cursor = [cursorKey](const uint8_t* const& target) { return VisibleIndexOf(target, cursorKey); };
    }
    // Takes both by value: it destroys them and leaves them empty.
    g_ListSetFilter(list, &filter, &cursor);
}

// Closes the page and opens a new one on the same options menu, so its rows
// show the current bindings. Runs from the tick, outside the page's update.
void RebuildModPage() {
    uint8_t* page = g_ModPage;
    uint8_t* parent = g_ModParent;
    if (!page || !parent || ReadAt<uint8_t>(page, retail::kOptionMenuClosing) != 0 ||
        ReadAt<uint8_t>(parent, retail::kOptionMenuClosing) != 0) {
        return;
    }
    g_ClosingPages.push_back(page);
    g_ModPage = nullptr;
    g_OptionMenuClose(page);
    OpenModPage(parent);
}

void StartCapture(int setting, int64_t key) {
    if (g_CaptureSetting >= 0 || setting < 0) {
        return;
    }
    g_CaptureSetting = setting;
    g_CaptureArmed = false;
    g_CaptureStart = GetTickCount64();
    g_CursorKey = key;
    g_RebuildPending = true;
    input::SetCapturing(true);
    Log(std::string("Waiting for a key for ") + mod_config::At(setting).key + ".");
}

void EndCapture(bool bind, int32_t pad) {
    const int setting = g_CaptureSetting;
    g_CaptureSetting = -1;
    input::SetCapturing(false);
    if (bind) {
        mod_config::SetValue(static_cast<size_t>(setting), pad);
    } else {
        Log("Key wait canceled.");
    }
    g_CursorKey = kFirstSettingId + setting;
    g_RebuildPending = true;
}

void ToggleMod(int mod) {
    if (mod < 0 || static_cast<size_t>(mod) >= g_Expanded.size()) {
        return;
    }
    g_Expanded[mod] = g_Expanded[mod] ? 0 : 1;
    g_FilterPending = true;
}

// The engine copies each callback into its row's decide handler. The menu
// outlives its rows, so the rows can hold it.
void BindRows(uint8_t* menu, bool titleRow) {
    const uint8_t* list = ReadAt<const uint8_t*>(menu, retail::kOptionMenuList);
    if (!list) {
        return;
    }
    auto info = ReadAt<const uint8_t* const*>(list, retail::kListAllItemsBegin);
    const auto end = ReadAt<const uint8_t* const*>(list, retail::kListAllItemsEnd);
    for (; info != end; ++info) {
        uint8_t* control = *info ? ReadAt<uint8_t*>(*info, retail::kListItemInfoControl) : nullptr;
        if (!control) {
            continue;
        }
        const int64_t key = ReadAt<int64_t>(control, retail::kListItemKey);
        PromptCallback onDecide;
        if (key == retail::kUnusedButtonSettingId && titleRow) {
            onDecide = [menu] { OpenPrompt(menu); };
        } else if (key == kModSettingsRowId) {
            onDecide = [menu] { OpenModPage(menu); };
        } else if (key == kBackRowId) {
            onDecide = [menu] { CloseModPage(menu); };
        } else if (const FakeRecord* fake = FindFake(key);
                   fake && (fake->role == RowRole::CollapsedHeader || fake->role == RowRole::ExpandedHeader)) {
            const int mod = fake->mod;
            onDecide = [mod] { ToggleMod(mod); };
        } else if (fake && fake->role == RowRole::Binding) {
            const int setting = fake->setting;
            onDecide = [setting, key] { StartCapture(setting, key); };
        } else if (fake && fake->role == RowRole::ModButton) {
            const int setting = fake->setting;
            onDecide = [setting] { g_PressedButtons.push_back(setting); };
        } else {
            continue;
        }
        g_BindListItemInput(control, &onDecide, retail::kListInputDecide);
    }
}

// =============================================================
// DETOURS
// =============================================================
void __fastcall Detour_MenuInitialize(uint8_t* menu) {
    void* registry = ReadGlobal<void*>(retail::kDatabaseRegistry);
    const MenuKind kind = g_Building || !registry ? MenuKind::Other : KindOf(menu);
    if (kind == MenuKind::Other) {
        g_OriginalMenuInitialize(menu);
        return;
    }
    uint8_t* listTable = g_GetSettingListTable(registry);
    const uint8_t* settingTable = g_GetSettingTable(registry);
    if (!listTable || !settingTable) {
        Log("Options menu rows skipped, the setting tables are missing.");
        g_OriginalMenuInitialize(menu);
        return;
    }
    const bool modRows = BuildFakeRecords(settingTable);
    if (kind == MenuKind::ModPage) {
        for (size_t i = 0; i < g_FakeCount; ++i) {
            if (g_Fakes[i].role == RowRole::Binding) {
                WriteBindingTexts(g_Fakes[i]);
            }
        }
    }
    uint8_t* titleRecord = nullptr;
    if (kind == MenuKind::InGame) {
        titleRecord = FindSettingRecord(settingTable, retail::kUnusedButtonSettingId);
        if (!titleRecord || RecordType(titleRecord) != retail::kSettingTypeButton) {
            Log("Return to title row skipped, the setting tables are not as expected.");
            titleRecord = nullptr;
        }
    }
    if (!modRows && !titleRecord) {
        g_OriginalMenuInitialize(menu);
        return;
    }

    BuildSlots(listTable, kind, modRows, titleRecord != nullptr);
    void* const tableBegin = ReadAt<void*>(listTable, retail::kTableSlotsBegin);
    void* const tableEnd = ReadAt<void*>(listTable, retail::kTableSlotsEnd);
    const char* savedLabels[retail::kLanguageCount];
    WriteAt<void*>(listTable, retail::kTableSlotsBegin, g_Slots.data());
    WriteAt<void*>(listTable, retail::kTableSlotsEnd, g_Slots.data() + g_Slots.size());
    if (titleRecord) {
        std::memcpy(savedLabels, titleRecord + retail::kSettingRecordLabels, sizeof(savedLabels));
        std::memcpy(titleRecord + retail::kSettingRecordLabels, kRowLabels, sizeof(kRowLabels));
    }
    g_Building = true;

    g_OriginalMenuInitialize(menu);

    g_Building = false;
    if (titleRecord) {
        std::memcpy(titleRecord + retail::kSettingRecordLabels, savedLabels, sizeof(savedLabels));
    }
    WriteAt<void*>(listTable, retail::kTableSlotsBegin, tableBegin);
    WriteAt<void*>(listTable, retail::kTableSlotsEnd, tableEnd);
    BindRows(menu, titleRecord != nullptr);
    // The quest menu sets its filter at the same point of its initialize.
    if (kind == MenuKind::ModPage) {
        ApplyFilter(menu, g_CursorKey);
        g_CursorKey = 0;
    }
}

// The int64 map find's detour, find(map rcx, outNode rdx, key r8). Shared by
// every int64-keyed map, so anything but the setting map passes straight
// through after one compare. It writes only rax, r9, r10 and the flags, all
// of which the find writes too, and leaves r10 = outNode like the find.
void* BuildMapFindThunk() {
    std::vector<uint8_t> code;
    auto emit = [&code](std::initializer_list<uint8_t> bytes) { code.insert(code.end(), bytes); };
    auto emitValue = [&code](uint64_t value, int size) {
        for (int i = 0; i < size; ++i) {
            code.push_back(static_cast<uint8_t>(value >> (8 * i)));
        }
    };
    // Ends of the rel8 jumps to the passthrough, patched once it is placed.
    std::vector<size_t> passthroughJumps;
    auto jumpToPassthrough = [&](uint8_t opcode) {
        emit({opcode, 0});
        passthroughJumps.push_back(code.size());
    };

    emit({0x48, 0xB8});  // mov rax, &g_SettingMap
    emitValue(reinterpret_cast<uintptr_t>(&g_SettingMap), 8);
    emit({0x48, 0x3B, 0x08});  // cmp rcx, [rax]
    jumpToPassthrough(0x75);   // jne
    emit({0x49, 0x8B, 0x00});  // mov rax, [r8]
    emit({0x49, 0xB9});        // mov r9, kModSettingIdBase
    emitValue(static_cast<uint64_t>(kModSettingIdBase), 8);
    emit({0x4C, 0x29, 0xC8});  // sub rax, r9
    emit({0x48, 0x3D});        // cmp rax, node count
    emitValue(kModSettingIdEnd - kModSettingIdBase, 4);
    jumpToPassthrough(0x73);  // jae
    emit({0x49, 0xB9});       // mov r9, g_FakeNodes
    emitValue(reinterpret_cast<uintptr_t>(g_FakeNodes), 8);
    emit({0x49, 0x8B, 0x04, 0xC1});  // mov rax, [r9 + rax * 8]
    emit({0x48, 0x85, 0xC0});        // test rax, rax
    jumpToPassthrough(0x74);         // jz
    emit({0x48, 0x89, 0x02});        // mov [rdx], rax
    emit({0x49, 0x89, 0xD2});        // mov r10, rdx
    emit({0x48, 0x89, 0xD0});        // mov rax, rdx
    emit({0xC3});                    // ret
    for (size_t end : passthroughJumps) {
        code[end - 1] = static_cast<uint8_t>(code.size() - end);
    }
    emit({0x48, 0xB8});  // mov rax, &g_OriginalInt64MapFind
    emitValue(reinterpret_cast<uintptr_t>(&g_OriginalInt64MapFind), 8);
    emit({0xFF, 0x20});  // jmp [rax]

    void* thunk = VirtualAlloc(nullptr, code.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!thunk) {
        return nullptr;
    }
    std::memcpy(thunk, code.data(), code.size());
    DWORD oldProtect = 0;
    if (!VirtualProtect(thunk, code.size(), PAGE_EXECUTE_READ, &oldProtect)) {
        VirtualFree(thunk, 0, MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(), thunk, code.size());
    return thunk;
}

uint64_t __fastcall Detour_GetSettingValue(void* manager, int64_t settingId) {
    // A binding's row shows its only choice: its pad is not a choice index.
    if (const FakeRecord* fake = FindFake(settingId)) {
        return fake->setting >= 0 && fake->role != RowRole::Binding
                   ? static_cast<uint32_t>(RowValue(mod_config::At(fake->setting)))
                   : 0;
    }
    return g_OriginalGetSettingValue(manager, settingId);
}

uint64_t __fastcall Detour_SetSettingValue(void* manager, int64_t settingId, int32_t value) {
    if (const FakeRecord* fake = FindFake(settingId)) {
        if (fake->setting >= 0 && fake->role != RowRole::Binding) {
            const size_t setting = static_cast<size_t>(fake->setting);
            mod_config::SetValue(setting, SettingValue(mod_config::At(setting), value));
        }
        return 0;
    }
    return g_OriginalSetSettingValue(manager, settingId, value);
}

// Takes the main menu's Cancel press while the page is open over its options
// tab. The rest of the main menu's step still runs.
uint8_t __fastcall Detour_MainMenuCancelStep(uint8_t* menu) {
    if (PageOwnedBy(menu) && CancelPressed(ReadAt<void*>(menu, retail::kTaskPad))) {
        CancelModPage();
    }
    return g_OriginalMainMenuCancelStep(menu);
}

// The title state's Cancel would leave the options state, so while the page
// is open over it, it closes the page instead.
void __fastcall Detour_TitleOptionCancel(uint8_t* const* state) {
    if (state && PageOwnedBy(*state)) {
        CancelModPage();
        return;
    }
    g_OriginalTitleOptionCancel(state);
}

void ResolveFunctions() {
    g_GetSettingListTable = GameFunction<GetTableFn>(retail::kGetSettingListTable);
    g_GetSettingTable = GameFunction<GetTableFn>(retail::kGetSettingTable);
    g_FindParentClass = GameFunction<FindParentClassFn>(retail::kFindParentClass);
    g_MainMenuClassId = GameFunction<ClassIdFn>(retail::kMainMenuClassId);
    g_TitleClassId = GameFunction<ClassIdFn>(retail::kTitleClassId);
    g_BindListItemInput = GameFunction<BindListItemInputFn>(retail::kBindListItemInput);
    g_OptionMenuCtor = GameFunction<OptionMenuCtorFn>(retail::kOptionMenuCtor);
    g_OptionMenuClose = GameFunction<OptionMenuCloseFn>(retail::kOptionMenuClose);
    g_ListSetFilter = GameFunction<ListSetFilterFn>(retail::kListSetFilter);
    g_PadActionCheck = GameFunction<PadActionCheckFn>(retail::kPadActionCheck);
}

// Waits for a press while a binding row asked for one. The page going away
// cancels the wait.
void TickCapture() {
    if (g_CaptureSetting < 0) {
        return;
    }
    if (!g_ModPage && !g_RebuildPending) {
        g_CaptureSetting = -1;
        input::SetCapturing(false);
        Log("Key wait canceled, the page closed.");
        return;
    }
    if (GetTickCount64() - g_CaptureStart > kCaptureTimeoutMs) {
        EndCapture(false, input::kNoPad);
        return;
    }
    if (!g_CaptureArmed) {
        g_CaptureArmed = !input::AnyHeld();
        return;
    }
    const input::Device device = mod_config::At(g_CaptureSetting).device;
    const int32_t pad = input::PressedOn(device);
    if (input::Pressed(input::kPadEscape)) {
        EndCapture(false, input::kNoPad);
    } else if (input::Pressed(input::kPadDelete)) {
        EndCapture(true, input::kNoPad);
    } else if (input::Bindable(device, pad)) {
        EndCapture(true, pad);
    }
}

void TickModPage() {
    if (g_ModPage && ReadAt<int32_t>(g_ModPage, retail::kTaskRefCount) <= 1) {
        Release(g_ModPage, retail::kTaskRefCount);
        g_ModPage = nullptr;
        g_ModOwner = nullptr;
    }
    for (auto page = g_ClosingPages.begin(); page != g_ClosingPages.end();) {
        if (ReadAt<int32_t>(*page, retail::kTaskRefCount) <= 1) {
            Release(*page, retail::kTaskRefCount);
            page = g_ClosingPages.erase(page);
        } else {
            ++page;
        }
    }
    if (g_RebuildPending) {
        g_RebuildPending = false;
        RebuildModPage();
    }
    if (g_FilterPending) {
        g_FilterPending = false;
        if (g_ModPage && ReadAt<uint8_t>(g_ModPage, retail::kOptionMenuClosing) == 0) {
            ApplyFilter(g_ModPage);
        }
    }
    // A callback may open a popup over the page, so the list is swapped out
    // first.
    std::vector<int> pressed;
    pressed.swap(g_PressedButtons);
    for (const int setting : pressed) {
        mod_loader::PressButton(setting);
    }
}

// The popup closes over a few frames after an answer. Yes returns to the
// title once the engine let go of it, outside every task update.
void TickPrompt() {
    if (!g_PromptTask) {
        return;
    }
    const bool released = ReadAt<int32_t>(g_PromptTask, retail::kTaskRefCount) <= 1;
    const bool answered = g_PromptAnswer != PromptAnswer::None;
    if (!released && !(answered && ++g_PromptFrames > kPopupTimeoutFrames)) {
        return;
    }
    const bool confirmed = g_PromptAnswer == PromptAnswer::Yes;
    Release(g_PromptTask, retail::kTaskRefCount);
    g_PromptTask = nullptr;
    g_PromptAnswer = PromptAnswer::None;
    if (confirmed) {
        Log("Returning to the title screen from the options menu.");
        ReturnToTitle();
    }
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kOptionsMenuRequirements)) {
        return false;
    }
    ResolveFunctions();
    WriteAt<int64_t>(g_RowEntry, retail::kSettingListRowId, retail::kUnusedButtonSettingId);
    void* mapFindThunk = BuildMapFindThunk();
    if (!mapFindThunk) {
        Log("Options menu: the map find thunk could not be allocated.");
        return false;
    }

    DetourSet detours("Options menu");
    const bool created =
        detours.Add(
            retail::kOptionMenuInitialize,
            reinterpret_cast<void*>(&Detour_MenuInitialize),
            reinterpret_cast<void**>(&g_OriginalMenuInitialize),
            "optionMenuInitialize"
        ) &&
        detours.Add(
            retail::kInt64MapFind,
            mapFindThunk,
            &g_OriginalInt64MapFind,
            "int64MapFind"
        ) &&
        detours.Add(
            retail::kGetSettingValue,
            reinterpret_cast<void*>(&Detour_GetSettingValue),
            reinterpret_cast<void**>(&g_OriginalGetSettingValue),
            "getSettingValue"
        ) &&
        detours.Add(
            retail::kSetSettingValue,
            reinterpret_cast<void*>(&Detour_SetSettingValue),
            reinterpret_cast<void**>(&g_OriginalSetSettingValue),
            "setSettingValue"
        ) &&
        detours.Add(
            retail::kMainMenuCancelStep,
            reinterpret_cast<void*>(&Detour_MainMenuCancelStep),
            reinterpret_cast<void**>(&g_OriginalMainMenuCancelStep),
            "mainMenuCancelStep"
        ) &&
        detours.Add(
            retail::kTitleOptionCancel,
            reinterpret_cast<void*>(&Detour_TitleOptionCancel),
            reinterpret_cast<void**>(&g_OriginalTitleOptionCancel),
            "titleOptionCancel"
        );
    if (!created) {
        detours.RemoveAll();
        return false;
    }
    g_Installed = true;
    return true;
}

uint8_t* ActivePage() {
    if (!g_ModPage || ReadAt<uint8_t>(g_ModPage, retail::kOptionMenuClosing) != 0) {
        return nullptr;
    }
    return g_ModPage;
}

void Tick() {
    if (g_Installed) {
        TickCapture();
        TickModPage();
        TickPrompt();
    }
}

}  // namespace options_menu
