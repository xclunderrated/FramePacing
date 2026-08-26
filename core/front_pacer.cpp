#include "front_pacer.h"
#include <MinHook.h>
#include "engine_context.h"
#include "hooks_common.h"
#include "pacing.h"
#include "log.h"

namespace pacer {

bool FrontPacer::enabled_ = false;
std::uint64_t FrontPacer::last_input_aligned_qpc_ = 0;

using PFN_PeekMessageW = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using PFN_GetMessageW = BOOL(WINAPI*)(LPMSG, HWND, UINT, UINT);

static PFN_PeekMessageW g_origPeekMessageW = nullptr;
static PFN_GetMessageW g_origGetMessageW = nullptr;

BOOL WINAPI Hooked_PeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!g_origPeekMessageW) return FALSE;
    if (hooks_is_ejecting()) {
        return g_origPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    }

    HookGuard guard;

    if (FrontPacer::is_enabled()) {
        FrontPacer::on_message_polled();
    }
    return g_origPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
}

BOOL WINAPI Hooked_GetMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax) {
    if (!g_origGetMessageW) return FALSE;
    if (hooks_is_ejecting()) {
        return g_origGetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
    }

    HookGuard guard;

    if (FrontPacer::is_enabled()) {
        FrontPacer::on_message_polled();
    }
    return g_origGetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);
}

bool FrontPacer::install() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return false;

    void* peek = reinterpret_cast<void*>(GetProcAddress(user32, "PeekMessageW"));
    void* getm = reinterpret_cast<void*>(GetProcAddress(user32, "GetMessageW"));

    if (peek && MH_CreateHook(peek, &Hooked_PeekMessageW, reinterpret_cast<void**>(&g_origPeekMessageW)) == MH_OK) {
        MH_EnableHook(peek);
    }
    if (getm && MH_CreateHook(getm, &Hooked_GetMessageW, reinterpret_cast<void**>(&g_origGetMessageW)) == MH_OK) {
        MH_EnableHook(getm);
    }
    PLOG("FrontPacer: Windows input message hooks armed");
    return true;
}

void FrontPacer::uninstall() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;

    void* peek = reinterpret_cast<void*>(GetProcAddress(user32, "PeekMessageW"));
    void* getm = reinterpret_cast<void*>(GetProcAddress(user32, "GetMessageW"));
    if (peek) MH_DisableHook(peek);
    if (getm) MH_DisableHook(getm);
}

void FrontPacer::set_enabled(bool enabled) {
    enabled_ = enabled;
}

bool FrontPacer::is_enabled() {
    return enabled_ && (shm_state() == PacerState_Limited);
}

void FrontPacer::on_message_polled() {
    // Keep input sampling evenly spaced relative to render frames
    std::uint64_t now = qpc_now();
    std::uint64_t freq = qpc_freq();
    if (last_input_aligned_qpc_ == 0 || (now - last_input_aligned_qpc_) > freq) {
        last_input_aligned_qpc_ = now;
    }
}

}  // namespace pacer
