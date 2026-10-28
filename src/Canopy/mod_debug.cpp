#include "mod_debug.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>

#include "core.h"
#include "lua_host.h"
#include "mod_config.h"

// =============================================================
// DEBUG HOOKS DESIGN
// =============================================================
// Retail keeps five debug script commands whose bodies are compiled out
// (game_retail.h). Each detour calls the retail command first, so the
// argument checks, the game's own error for a missing argument and the
// script context's bookkeeping all stay the game's. Only then does it act:
// DebugIsOn and DebugGetConfig get the one value they pushed rewritten in
// place, and DebugPrint, DebugDisp and DebugScreenText, which push nothing,
// get their arguments logged. Nothing that needs a destructor is alive
// while the retail command runs, since a script error unwinds through it.
//
// Shipped scripts read six configs with DebugGetConfig and compare each
// with a number, so 0, retail's answer, plays the game as shipped. The
// other 46 configs of debugconfig.dat were read by native code retail does
// not contain, or are only set by a test script, so they get no setting.
//
// The settings form a Canopy debug entry on the Mod Settings page, saved in
// mods\CanopyDebug.ini, each starting at retail's answer. Commands read
// their setting on every call, so a change applies at once.

namespace mod_debug {
namespace {

using namespace core;
using mod_config::Text;

using CommandFn = int(__fastcall*)(void* state);
using LuaTypeFn = int(__fastcall*)(void* state, int index);
using LuaToLStringFn = const char*(__fastcall*)(void* state, int index, size_t* length);

// Script output is a debugging aid: past this many lines in one session,
// the rest is dropped rather than grow the log without end.
constexpr size_t kMaxScriptLines = 2000;
constexpr size_t kMaxArgumentText = 1000;

constexpr const char* kModKey = "CanopyDebug";

constexpr Text kModName = {
    "Canopy デバッグ", "Canopy debug", "Débogage Canopy", "Depuración de Canopy", "Canopy 除錯", "Canopy 디버그",
    "Canopy 调试",
};

constexpr Text kScriptOutputLabel = {
    "スクリプトのデバッグ出力", "Script debug output", "Sortie de débogage des scripts",
    "Salida de depuración de scripts", "腳本除錯輸出", "스크립트 디버그 출력", "脚本调试输出",
};
constexpr Text kScriptOutputHelp = {
    "ゲームのスクリプトがデバッグ用に出力する内容を Canopy.log に書き込みます。",
    "Writes what the game's scripts print for debugging to Canopy.log.",
    "Écrit dans Canopy.log ce que les scripts du jeu affichent pour le débogage.",
    "Escribe en Canopy.log lo que los scripts del juego muestran para depurar.",
    "將遊戲腳本的除錯輸出寫入 Canopy.log。",
    "게임 스크립트의 디버그 출력을 Canopy.log에 기록합니다.",
    "将游戏脚本的调试输出写入 Canopy.log。",
};

constexpr Text kDebugModeLabel = {
    "デバッグモード", "Debug mode", "Mode débogage", "Modo depuración", "除錯模式", "디버그 모드", "调试模式",
};
constexpr Text kDebugModeHelp = {
    "デバッグ版かどうかを確認するスクリプトに「はい」と答えます。ゲームのスクリプトは確認しませんが、Lua MOD は確認できます。",
    "Scripts that ask whether this is a debug build are told yes. No game script asks; Lua mods can.",
    "Les scripts qui demandent s'il s'agit d'une version de débogage reçoivent oui. Aucun script du jeu ne le "
    "demande ; les mods Lua le peuvent.",
    "Los scripts que preguntan si es una versión de depuración reciben un sí. Ningún script del juego lo pregunta; "
    "los mods Lua pueden.",
    "詢問是否為除錯版本的腳本會得到「是」。遊戲腳本不會詢問，Lua 模組可以。",
    "디버그 빌드인지 묻는 스크립트에 예라고 답합니다. 게임 스크립트는 묻지 않으며 Lua 모드는 물을 수 있습니다.",
    "询问是否为调试版本的脚本会得到“是”。游戏脚本不会询问，Lua 模组可以。",
};

constexpr Text kNpcHouseLabel = {
    "村人の家を開放", "Open villager houses", "Maisons ouvertes", "Casas abiertas", "開放村民住家", "주민 집 개방",
    "开放村民住宅",
};
constexpr Text kNpcHouseHelp = {
    "村人の家にいつでも入れます。",
    "Villagers' houses can be entered at any time.",
    "Les maisons des villageois sont ouvertes à toute heure.",
    "Se puede entrar en las casas de los aldeanos a cualquier hora.",
    "隨時都能進入村民的家。",
    "언제든지 주민의 집에 들어갈 수 있습니다.",
    "随时都能进入村民的家。",
};

constexpr Text kLikabilityLabel = {
    "好感度イベントを常に許可", "Friendship events at any level", "Événements d'amitié sans condition",
    "Eventos de amistad sin requisito", "好感度事件不受限制", "호감도 이벤트 제한 없음", "好感度事件不受限制",
};
constexpr Text kLikabilityHelp = {
    "好感度に関係なく村人の好感度イベントが始まります。",
    "Villagers' friendship events can start whatever their friendship level.",
    "Les événements d'amitié des villageois peuvent commencer quel que soit leur niveau d'amitié.",
    "Los eventos de amistad de los aldeanos pueden empezar sea cual sea su nivel de amistad.",
    "無論好感度高低，村民的好感度事件都能開始。",
    "호감도와 관계없이 주민의 호감도 이벤트가 시작될 수 있습니다.",
    "无论好感度高低，村民的好感度事件都能开始。",
};

constexpr Text kLateNightLabel = {
    "深夜イベント", "Late night events", "Événements de nuit", "Eventos nocturnos", "深夜事件", "심야 이벤트", "深夜事件",
};
constexpr Text kLateNightHelp = {
    "通常は深夜イベントの間に日数を空けます。「毎晩」は毎晩始められるようにし、「なし」は止めます。",
    "Normal leaves the usual days between late night events. Every night lets one start each night, and Never "
    "stops them.",
    "Normal laisse les jours habituels entre deux événements de nuit. Chaque nuit permet d'en commencer un chaque "
    "nuit, et Jamais les arrête.",
    "Normal deja los días habituales entre eventos nocturnos. Cada noche permite que empiece uno cada noche, y "
    "Nunca los detiene.",
    "一般會在深夜事件之間間隔數天。「每晚」讓事件每晚都能開始，「永不」則停止事件。",
    "보통은 심야 이벤트 사이에 며칠을 둡니다. 매일 밤은 매일 밤 시작될 수 있게 하고, 없음은 멈춥니다.",
    "一般会在深夜事件之间间隔数天。“每晚”让事件每晚都能开始，“永不”则停止事件。",
};
// In debugconfig.dat's order: 0 normal, 1 every night, 2 never.
constexpr Text kLateNightChoices[] = {
    {"通常", "Normal", "Normal", "Normal", "一般", "보통", "一般"},
    {"毎晩", "Every night", "Chaque nuit", "Cada noche", "每晚", "매일 밤", "每晚"},
    {"なし", "Never", "Jamais", "Nunca", "永不", "없음", "永不"},
};

constexpr Text kEnvironmentSoundLabel = {
    "環境音の情報", "Environment sound info", "Infos sur les sons d'ambiance", "Información de sonidos ambientales",
    "環境音資訊", "환경음 정보", "环境音信息",
};
constexpr Text kEnvironmentSoundHelp = {
    "スクリプトが再生する環境音を出力します。スクリプトのデバッグ出力がオンのときに記録されます。",
    "The environment sound script reports the sounds it plays. Logged while Script debug output is on.",
    "Le script des sons d'ambiance signale les sons qu'il joue. Enregistré quand la sortie de débogage des "
    "scripts est activée.",
    "El script de sonidos ambientales informa de los sonidos que reproduce. Se registra con la salida de "
    "depuración de scripts activada.",
    "環境音腳本會回報播放的聲音。開啟腳本除錯輸出時會記錄。",
    "환경음 스크립트가 재생하는 소리를 알립니다. 스크립트 디버그 출력이 켜져 있을 때 기록됩니다.",
    "环境音脚本会报告播放的声音。开启脚本调试输出时会记录。",
};

constexpr Text kStaffRollLabel = {
    "スタッフロールの情報", "Staff roll info", "Infos du générique", "Información de los créditos", "工作人員名單資訊",
    "스태프 롤 정보", "制作人员名单信息",
};
constexpr Text kStaffRollHelp = {
    "スタッフロールのスクリプトが進行状況を出力します。スクリプトのデバッグ出力がオンのときに記録されます。",
    "The staff roll script reports its progress. Logged while Script debug output is on.",
    "Le script du générique signale sa progression. Enregistré quand la sortie de débogage des scripts est "
    "activée.",
    "El script de los créditos informa de su progreso. Se registra con la salida de depuración de scripts "
    "activada.",
    "工作人員名單腳本會回報進度。開啟腳本除錯輸出時會記錄。",
    "스태프 롤 스크립트가 진행 상황을 알립니다. 스크립트 디버그 출력이 켜져 있을 때 기록됩니다.",
    "制作人员名单脚本会报告进度。开启脚本调试输出时会记录。",
};

constexpr Text kRouteWarpLabel = {
    "経路失敗時にワープ", "Warp on failed route", "Téléportation si le trajet échoue",
    "Teletransporte si falla la ruta", "路徑失敗時傳送", "경로 실패 시 워프", "路径失败时传送",
};
constexpr Text kRouteWarpHelp = {
    "経路が見つからない村人は目的地へワープします。",
    "A villager that cannot find a path warps to where it was going.",
    "Un villageois qui ne trouve pas de chemin se téléporte à sa destination.",
    "Un aldeano que no encuentra camino se teletransporta a su destino.",
    "找不到路徑的村民會直接傳送到目的地。",
    "길을 찾지 못한 주민은 목적지로 워프합니다.",
    "找不到路径的村民会直接传送到目的地。",
};

// A config shipped scripts read, its setting's handle, and whether its
// first read was logged.
struct Config {
    uint64_t id;
    const char* enumName;
    int setting;
    std::atomic<bool> logged;
};

Config g_Configs[] = {
    {retail::kDebugConfigNpcHouse, "DEBUG_CONFIG_ENABLE_NPC_HOUSE", mod_config::kNoSetting, {false}},
    {retail::kDebugConfigLikabilityEvents, "DEBUG_CONFIG_NPC_LIKABILITY_EVENT_ABS", mod_config::kNoSetting, {false}},
    {retail::kDebugConfigLateNight, "DEBUG_CONFIG_LATE_NIGHT_ABSOLUTE", mod_config::kNoSetting, {false}},
    {retail::kDebugConfigEnvironmentSound, "DEBUG_CONFIG_DISP_ENVIROMENT_SE", mod_config::kNoSetting, {false}},
    {retail::kDebugConfigStaffRoll, "DEBUG_CONFIG_DISP_STAFFROLL_INFO", mod_config::kNoSetting, {false}},
    {retail::kDebugConfigRouteWarp, "DEBUG_CONFIG_ROUTE__FAILED_WARP", mod_config::kNoSetting, {false}},
};

int g_ScriptOutput = mod_config::kNoSetting;
int g_DebugMode = mod_config::kNoSetting;
std::atomic<bool> g_DebugModeLogged{false};
std::atomic<size_t> g_ScriptLines{0};

CommandFn g_OriginalIsOn = nullptr;
CommandFn g_OriginalGetConfig = nullptr;
CommandFn g_OriginalPrint = nullptr;
CommandFn g_OriginalDisp = nullptr;
CommandFn g_OriginalScreenText = nullptr;
LuaTypeFn g_LuaType = nullptr;
LuaToLStringFn g_LuaToLString = nullptr;

// ---- Lua stack -----------------------------------------------------------

// The stack value of argument index (1 for the first) of the running
// command. The retail command has already checked that it exists.
uint8_t* Argument(void* state, int index) {
    const uint8_t* callInfo = ReadAt<uint8_t*>(state, retail::kLuaStateCallInfo);
    uint8_t* function = ReadAt<uint8_t*>(callInfo, 0);
    return function + retail::kLuaValueSize * static_cast<size_t>(index);
}

// The value the retail command pushed last.
uint8_t* Pushed(void* state) {
    return ReadAt<uint8_t*>(state, retail::kLuaStateTop) - retail::kLuaValueSize;
}

// A number argument as a whole number. False for anything else.
bool WholeNumber(const uint8_t* value, int64_t& number) {
    const uint8_t tag = ReadAt<uint8_t>(value, retail::kLuaValueTag);
    if (tag == retail::kLuaTagInteger) {
        number = ReadAt<int64_t>(value, 0);
        return true;
    }
    if (tag == retail::kLuaTagFloat) {
        const double real = ReadAt<double>(value, 0);
        number = static_cast<int64_t>(real);
        return static_cast<double>(number) == real;
    }
    return false;
}

// An argument as text, the way Lua's print shows it. A number is formatted
// here, since lua_tolstring would turn the script's own value into a
// string.
std::string ArgumentText(void* state, int index) {
    const uint8_t* value = Argument(state, index);
    switch (g_LuaType(state, index)) {
    case retail::kLuaTypeString: {
        size_t length = 0;
        const char* text = g_LuaToLString(state, index, &length);
        return text ? std::string(text, length) : std::string();
    }
    case retail::kLuaTypeNumber: {
        char text[32];
        if (ReadAt<uint8_t>(value, retail::kLuaValueTag) == retail::kLuaTagInteger) {
            std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(ReadAt<int64_t>(value, 0)));
        } else {
            std::snprintf(text, sizeof(text), "%.14g", ReadAt<double>(value, 0));
        }
        return text;
    }
    case retail::kLuaTypeBoolean:
        return ReadAt<uint8_t>(value, retail::kLuaValueTag) == retail::kLuaTagTrue ? "true" : "false";
    case retail::kLuaTypeNil:
        return "nil";
    default:
        return "(not text)";
    }
}

// One log line's worth of a script's text: control characters become
// spaces and a long text is cut.
std::string OneLine(std::string text) {
    if (text.size() > kMaxArgumentText) {
        text.resize(kMaxArgumentText);
        text += "...";
    }
    for (char& c : text) {
        if (static_cast<unsigned char>(c) < 0x20) {
            c = ' ';
        }
    }
    return text;
}

std::string ScriptName() {
    const std::string name = lua_host::RunningScript();
    return name.empty() ? "A script" : "Script " + name;
}

// ---- Output --------------------------------------------------------------

// A setting's value, or 0, retail's answer, when the setting did not
// register. mod_config::Enabled would read a missing setting as on.
bool On(int setting) {
    return mod_config::Value(setting, 0) != 0;
}

// True when the next script line may be logged. Logs the cut once.
bool TakeScriptLine() {
    if (!On(g_ScriptOutput)) {
        return false;
    }
    const size_t line = g_ScriptLines.fetch_add(1);
    if (line == kMaxScriptLines) {
        Log("Script debug output: " + std::to_string(kMaxScriptLines) +
            " lines this session, so the rest is not logged.");
    }
    return line < kMaxScriptLines;
}

int __fastcall Detour_Print(void* state) {
    const int results = g_OriginalPrint(state);
    if (TakeScriptLine()) {
        Log(ScriptName() + ": " + OneLine(ArgumentText(state, 1)));
    }
    return results;
}

int __fastcall Detour_Disp(void* state) {
    const int results = g_OriginalDisp(state);
    if (TakeScriptLine()) {
        Log(ScriptName() + " displays: " + OneLine(ArgumentText(state, 1)));
    }
    return results;
}

int __fastcall Detour_ScreenText(void* state) {
    const int results = g_OriginalScreenText(state);
    if (TakeScriptLine()) {
        Log(ScriptName() + " shows " + OneLine(ArgumentText(state, 1)) + ": " + OneLine(ArgumentText(state, 2)));
    }
    return results;
}

// ---- Answers -------------------------------------------------------------

int __fastcall Detour_IsOn(void* state) {
    const int results = g_OriginalIsOn(state);
    if (On(g_DebugMode)) {
        WriteAt<uint8_t>(Pushed(state), retail::kLuaValueTag, retail::kLuaTagTrue);
        if (!g_DebugModeLogged.exchange(true)) {
            Log(ScriptName() + " asked whether debug mode is on, and Canopy answered yes.");
        }
    }
    return results;
}

int __fastcall Detour_GetConfig(void* state) {
    const int results = g_OriginalGetConfig(state);
    int64_t id = 0;
    if (!WholeNumber(Argument(state, 1), id)) {
        return results;
    }
    for (Config& config : g_Configs) {
        if (static_cast<int64_t>(config.id) != id) {
            continue;
        }
        const int32_t value = mod_config::Value(config.setting, 0);
        if (value != 0) {
            // Retail pushed the float 0 here, so only the number changes.
            WriteAt<double>(Pushed(state), 0, static_cast<double>(value));
        }
        if (!config.logged.exchange(true)) {
            Log(ScriptName() + " read " + config.enumName + ", and Canopy answered " + std::to_string(value) + ".");
        }
        break;
    }
    return results;
}

// ---- Settings ------------------------------------------------------------

bool RegisterSettings() {
    const int mod = mod_config::RegisterMod(kModKey, kModName);
    if (mod == mod_config::kNoMod) {
        return false;
    }
    g_ScriptOutput = mod_config::AddToggle(mod, "ScriptOutput", kScriptOutputLabel, kScriptOutputHelp, false);
    g_DebugMode = mod_config::AddToggle(mod, "DebugMode", kDebugModeLabel, kDebugModeHelp, false);
    g_Configs[0].setting = mod_config::AddToggle(mod, "NpcHouse", kNpcHouseLabel, kNpcHouseHelp, false);
    g_Configs[1].setting =
        mod_config::AddToggle(mod, "LikabilityEvents", kLikabilityLabel, kLikabilityHelp, false);
    g_Configs[2].setting = mod_config::AddChoice(
        mod, "LateNight", kLateNightLabel, kLateNightHelp, kLateNightChoices,
        static_cast<int32_t>(std::size(kLateNightChoices)), 0
    );
    g_Configs[3].setting =
        mod_config::AddToggle(mod, "EnvironmentSound", kEnvironmentSoundLabel, kEnvironmentSoundHelp, false);
    g_Configs[4].setting = mod_config::AddToggle(mod, "StaffRoll", kStaffRollLabel, kStaffRollHelp, false);
    g_Configs[5].setting = mod_config::AddToggle(mod, "RouteWarp", kRouteWarpLabel, kRouteWarpHelp, false);
    return true;
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kModDebugRequirements)) {
        Log("Debug hooks are off.");
        return false;
    }
    g_LuaType = GameFunction<LuaTypeFn>(retail::kLuaType);
    g_LuaToLString = GameFunction<LuaToLStringFn>(retail::kLuaToLString);
    DetourSet hooks("Debug hooks");
    if (!hooks.Add(
            retail::kDebugIsOn, reinterpret_cast<void*>(&Detour_IsOn), reinterpret_cast<void**>(&g_OriginalIsOn),
            "debugIsOn"
        ) ||
        !hooks.Add(
            retail::kDebugGetConfig, reinterpret_cast<void*>(&Detour_GetConfig),
            reinterpret_cast<void**>(&g_OriginalGetConfig), "debugGetConfig"
        ) ||
        !hooks.Add(
            retail::kDebugPrint, reinterpret_cast<void*>(&Detour_Print), reinterpret_cast<void**>(&g_OriginalPrint),
            "debugPrint"
        ) ||
        !hooks.Add(
            retail::kDebugDisp, reinterpret_cast<void*>(&Detour_Disp), reinterpret_cast<void**>(&g_OriginalDisp),
            "debugDisp"
        ) ||
        !hooks.Add(
            retail::kDebugScreenText, reinterpret_cast<void*>(&Detour_ScreenText),
            reinterpret_cast<void**>(&g_OriginalScreenText), "debugScreenText"
        )) {
        hooks.RemoveAll();
        Log("Debug hooks are off, their hooks could not be created.");
        return false;
    }
    if (!RegisterSettings()) {
        hooks.RemoveAll();
        Log("Debug hooks are off, their settings entry " + std::string(kModKey) + " could not be registered.");
        return false;
    }
    Log("Debug hooks installed: script debug output and " + std::to_string(std::size(g_Configs)) +
        " debug configs are in the Canopy debug entry of Mod Settings.");
    return true;
}

}  // namespace mod_debug
