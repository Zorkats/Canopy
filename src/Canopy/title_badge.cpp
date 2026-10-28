#define NOMINMAX
#include "title_badge.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core.h"
#include "file_overrides.h"
#include "mod_loader.h"

// =============================================================
// TITLE BADGE DESIGN
// =============================================================
// The title draws its version with the engine's number widget, which its
// init attaches to the title_base layer's anime under the layout key
// number01. A key holds a list of widgets, so the badge attaches two of the
// engine's text widgets under the same key, right after the title's init
// has built its own. A widget's position is relative to the key's locator,
// where the version's digits start, and the text is left-aligned on it. The
// copyright lines fill the space under the version, so the badge sits in the
// bottom-right corner instead, level with them, as a left-aligned block whose
// right margin mirrors the version's left one.
//
// Every mod loads at startup, before the first title, so the count is
// taken once. A mod counts once however many parts it has: a DLL, a Lua,
// item, flag or villager folder, and a folder of data overrides join by
// name.

namespace title_badge {
namespace {

using namespace core;

using TitleInitFn = int64_t(__fastcall*)(void* task, int64_t arg);
using AddTextPartFn = void*(__fastcall*)(void* uiUnion, void* partsSetting, void* setting, const void* provider);
using PartsSettingCtorFn = void*(__fastcall*)(
    void* out, int32_t type, const char* name, const void* layerPath, const uint64_t* key, const void* transform,
    int32_t flags
);
using MakeLayerPathFn = void*(__fastcall*)(void* out, int32_t layer);
using TextParamCtorFn = void*(__fastcall*)(void* param, const char* text);
using TextParamCopyFn = void*(__fastcall*)(void* out, const void* param);
using DestroyFn = void(__fastcall*)(void* object);

// A part's place relative to its key's locator, as the engine lays it out:
// position, rotation, scale, color, then two fields the title's own parts
// leave at 0 and 2.
struct alignas(16) PartTransform {
    float position[4];
    float rotation[4];
    float scale[4];
    int32_t color[4];
    float reserved[4];
    int32_t mode[4];
};
static_assert(sizeof(PartTransform) == 96, "the engine copies a 96-byte transform");

// Layout units from the version's locator, about a pixel each at 1080p,
// measured on screen: right across to the corner, and down to the copyright
// lines' rows.
constexpr float kOffsetX = 1445.0f;
constexpr float kFirstLineOffset = 35.0f;
constexpr float kLineStep = 30.0f;
// Draws above the title's background, like the version.
constexpr float kDepth = 1.0f;
constexpr int32_t kWhite = -1;
constexpr int32_t kTransformMode = 2;

TitleInitFn g_OriginalTitleInit = nullptr;
AddTextPartFn g_AddTextPart = nullptr;
PartsSettingCtorFn g_PartsSettingCtor = nullptr;
MakeLayerPathFn g_MakeLayerPath = nullptr;
TextParamCtorFn g_TextParamCtor = nullptr;
TextParamCopyFn g_TextParamCopy = nullptr;
DestroyFn g_TextParamDtor = nullptr;
DestroyFn g_TextSettingDtor = nullptr;
DestroyFn g_StringDtor = nullptr;

std::string g_VersionLine;
std::string g_CountLine;
bool g_Logged = false;

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return text;
}

size_t CountMods() {
    std::vector<std::string> names = mod_loader::StartedModNames();
    for (const std::string& name : file_overrides::OverrideFolders()) {
        names.push_back(name);
    }
    for (std::string& name : names) {
        name = Lower(name);
    }
    std::sort(names.begin(), names.end());
    return static_cast<size_t>(std::unique(names.begin(), names.end()) - names.begin());
}

std::string CountLine(size_t count) {
    if (count == 0) {
        return "No mods installed";
    }
    return std::to_string(count) + (count == 1 ? " mod installed" : " mods installed");
}

// Attaches one text widget under the version's key, offset down by offsetY.
// Builds and tears down everything the way the title does for the version.
void AddLine(uint8_t* uiUnion, const char* name, const char* text, float offsetY) {
    alignas(16) uint8_t param[retail::kTextParamSize] = {};
    g_TextParamCtor(param, text);

    // The optional callbacks' presence bytes stay 0.
    alignas(16) uint8_t setting[retail::kTextSettingSize] = {};
    WriteAt<uintptr_t>(setting, 0, g_ModuleBase + retail::kTextSettingVtable);
    WriteAt<uint16_t>(setting, retail::kTextSettingFlag, 1);
    g_TextParamCopy(setting + retail::kTextSettingParam, param);

    alignas(16) uint8_t path[retail::kLayerPathSize] = {};
    g_MakeLayerPath(path, retail::kTitleVersionLayer);

    PartTransform transform = {};
    transform.position[0] = kOffsetX;
    transform.position[1] = offsetY;
    transform.position[2] = kDepth;
    transform.scale[0] = 1.0f;
    transform.scale[1] = 1.0f;
    transform.scale[2] = 1.0f;
    transform.color[0] = kWhite;
    transform.mode[0] = kTransformMode;

    const uint64_t key = retail::kTitleVersionKey;
    alignas(16) uint8_t part[retail::kPartsSettingSize] = {};
    g_PartsSettingCtor(part, 0, name, path, &key, &transform, retail::kPartsSettingFlags);

    // An empty std::function<>: the text never changes, so no provider.
    alignas(16) const uint8_t provider[64] = {};
    g_AddTextPart(uiUnion, part, setting, provider);

    // The path entries own nothing, so the title only clears their count.
    WriteAt<uint64_t>(part, retail::kPartsSettingPath, 0);
    g_StringDtor(part + retail::kPartsSettingName);
    WriteAt<uint64_t>(path, 0, 0);
    g_TextSettingDtor(setting);
    g_TextParamDtor(param);
}

int64_t __fastcall Detour_TitleInit(void* task, int64_t arg) {
    const int64_t result = g_OriginalTitleInit(task, arg);
    uint8_t* layer = ReadAt<uint8_t*>(task, retail::kTitleBaseLayer);
    uint8_t* uiUnion = layer ? ReadAt<uint8_t*>(layer, retail::kLayerUnion) : nullptr;
    if (!uiUnion) {
        if (!g_Logged) {
            g_Logged = true;
            Log("Title badge: the title has no version layer, so the badge is not shown.");
        }
        return result;
    }
    if (g_CountLine.empty()) {
        g_CountLine = CountLine(CountMods());
    }
    AddLine(uiUnion, "canopyVersion", g_VersionLine.c_str(), kFirstLineOffset);
    AddLine(uiUnion, "canopyMods", g_CountLine.c_str(), kFirstLineOffset + kLineStep);
    if (!g_Logged) {
        g_Logged = true;
        Log("Title badge: " + g_VersionLine + ", " + g_CountLine + ".");
    }
    return result;
}

}  // namespace

bool Install() {
    if (!RequirementsMet(retail::kTitleBadgeRequirements)) {
        return false;
    }
    g_AddTextPart = GameFunction<AddTextPartFn>(retail::kAddTextPart);
    g_PartsSettingCtor = GameFunction<PartsSettingCtorFn>(retail::kPartsSettingCtor);
    g_MakeLayerPath = GameFunction<MakeLayerPathFn>(retail::kMakeLayerPath);
    g_TextParamCtor = GameFunction<TextParamCtorFn>(retail::kTextParamCtor);
    g_TextParamCopy = GameFunction<TextParamCopyFn>(retail::kTextParamCopy);
    g_TextParamDtor = GameFunction<DestroyFn>(retail::kTextParamDtor);
    g_TextSettingDtor = GameFunction<DestroyFn>(retail::kTextSettingDtor);
    g_StringDtor = GameFunction<DestroyFn>(retail::kStringDtor);
    g_VersionLine = std::string("Canopy v") + CANOPY_RELEASE;
    DetourSet detours("Title badge");
    return detours.Add(
        retail::kTitleInit,
        reinterpret_cast<void*>(&Detour_TitleInit),
        reinterpret_cast<void**>(&g_OriginalTitleInit),
        "titleInit"
    );
}

}  // namespace title_badge
