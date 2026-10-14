#define NOMINMAX
#include "hook_chain.h"

#include <windows.h>

#include <MinHook.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

// =============================================================
// HOOK CHAIN DESIGN
// =============================================================
// MinHook allows one hook per function. The loader keeps that one hook and
// points it at a jump stub of its own, the chain's head. Every detour in the
// chain gets a stub too, handed out as its original:
//
//   game call -> head -> detour 1, whose original -> detour 2, ... whose
//   original -> MinHook's trampoline, which runs the game's code.
//
// A stub is `jmp qword ptr [rip+2]` followed by its 8-byte destination. It
// touches no register, so functions with unusual register contracts chain
// safely. Linking or unlinking a detour rewrites one destination with one
// aligned store, so a thread passing through sees the old chain or the new
// one, never half of either.
//
// Stubs are never freed: a thread may still be inside an unlinked detour and
// call its original afterwards.

namespace hook_chain {
namespace {

// jmp qword ptr [rip+2], then two int3 pads, so the destination that
// follows is 8-byte aligned.
constexpr uint8_t kJumpCode[] = {0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0xCC, 0xCC};
constexpr size_t kStubDestination = sizeof(kJumpCode);
constexpr size_t kStubSize = kStubDestination + sizeof(void*);
constexpr size_t kStubPageSize = 0x1000;

struct Link {
    int64_t owner;
    void* detour;
    // The detour's original: a stub to the next detour or the trampoline.
    uint8_t* next;
};

struct Chain {
    void* target;
    // MinHook's trampoline, which runs the game's own code.
    void* trampoline;
    // MinHook's detour: a stub to the first link, or the trampoline.
    uint8_t* head;
    std::vector<Link> links;
};

std::mutex g_Mutex;
std::vector<std::unique_ptr<Chain>> g_Chains;
uint8_t* g_StubPage = nullptr;
size_t g_StubsUsed = kStubPageSize;
bool g_Started = false;

void Retarget(uint8_t* stub, void* destination) {
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(stub + kStubDestination), destination);
}

uint8_t* NewStub(void* destination) {
    if (g_StubsUsed + kStubSize > kStubPageSize) {
        void* page = VirtualAlloc(nullptr, kStubPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!page) {
            return nullptr;
        }
        g_StubPage = static_cast<uint8_t*>(page);
        g_StubsUsed = 0;
    }
    uint8_t* stub = g_StubPage + g_StubsUsed;
    g_StubsUsed += kStubSize;
    std::memcpy(stub, kJumpCode, sizeof(kJumpCode));
    Retarget(stub, destination);
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);
    return stub;
}

Chain* Find(void* target) {
    for (const std::unique_ptr<Chain>& chain : g_Chains) {
        if (chain->target == target) {
            return chain.get();
        }
    }
    return nullptr;
}

// Hooks target through MinHook with a head stub, enabled at once after
// startup. Null with reason set on failure.
Chain* NewChain(void* target, const char*& reason) {
    uint8_t* head = NewStub(nullptr);
    if (!head) {
        reason = "no memory for its jump stub";
        return nullptr;
    }
    void* trampoline = nullptr;
    const MH_STATUS created = MH_CreateHook(target, head, &trampoline);
    if (created != MH_OK) {
        reason = MH_StatusToString(created);
        return nullptr;
    }
    Retarget(head, trampoline);
    if (g_Started) {
        const MH_STATUS enabled = MH_EnableHook(target);
        if (enabled != MH_OK) {
            MH_RemoveHook(target);
            reason = MH_StatusToString(enabled);
            return nullptr;
        }
    }
    g_Chains.push_back(std::make_unique<Chain>(Chain{target, trampoline, head, {}}));
    return g_Chains.back().get();
}

}  // namespace

const char* Add(void* target, int64_t owner, void* detour, void** original) {
    std::lock_guard<std::mutex> lock(g_Mutex);
    Chain* chain = Find(target);
    if (chain && std::any_of(chain->links.begin(), chain->links.end(), [owner](const Link& link) {
            return link.owner == owner;
        })) {
        return "the mod already hooks this function";
    }
    uint8_t* next = NewStub(nullptr);
    if (!next) {
        return "no memory for its jump stub";
    }
    const char* reason = nullptr;
    if (!chain && !(chain = NewChain(target, reason))) {
        return reason;
    }
    // The original must be in place before the first call reaches detour.
    Retarget(next, chain->trampoline);
    *original = next;
    Retarget(chain->links.empty() ? chain->head : chain->links.back().next, detour);
    chain->links.push_back({owner, detour, next});
    return nullptr;
}

bool Remove(void* target, int64_t owner) {
    std::lock_guard<std::mutex> lock(g_Mutex);
    Chain* chain = Find(target);
    if (!chain) {
        return false;
    }
    const auto link = std::find_if(chain->links.begin(), chain->links.end(), [owner](const Link& entry) {
        return entry.owner == owner;
    });
    if (link == chain->links.end()) {
        return false;
    }
    const auto after = link + 1;
    uint8_t* before = link == chain->links.begin() ? chain->head : (link - 1)->next;
    Retarget(before, after == chain->links.end() ? chain->trampoline : after->detour);
    chain->links.erase(link);

    // Before startup nothing is enabled, so an empty chain can go entirely.
    if (chain->links.empty() && !g_Started) {
        MH_RemoveHook(target);
        g_Chains.erase(std::find_if(g_Chains.begin(), g_Chains.end(), [chain](const std::unique_ptr<Chain>& entry) {
            return entry.get() == chain;
        }));
    }
    return true;
}

void Started() {
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Started = true;
}

}  // namespace hook_chain
