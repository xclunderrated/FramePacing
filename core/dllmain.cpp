// PacerCore.dll entry. The injector forces a LoadLibraryW inside the game
// process; everything heavy happens on our own worker thread, never under
// loader lock.
#include <windows.h>
#include <timeapi.h>

#include "engine_context.h"
#include "hook_ddraw.h"
#include "hook_dx9.h"
#include "hook_dxgi.h"
#include "hook_exports.h"
#include "loader_watch.h"
#include "log.h"
#include "pacing.h"
#include "shm.h"

namespace pacer {

static HMODULE g_self = nullptr;
static volatile bool g_stop = false;

// Unhooks and frees this DLL from a one-shot thread. Called only from
// worker_main when dormancy decides we injected into a non-game.
static DWORD WINAPI self_eject(LPVOID) {
    hooks_uninstall();
    shm_close();
    FreeLibraryAndExitThread(g_self, 0);
}

static DWORD WINAPI worker_main(LPVOID) {
    PLOG("PacerCore loaded in pid=%lu", GetCurrentProcessId());

    const std::uint32_t pid = GetCurrentProcessId();
    shm_create(pid);
    ctx_init();

    // Safe Lazy Hook Arming:
    // Only install hooks for APIs that the host process has actually loaded.
    // Late-loaded APIs will be hooked dynamically by loader_watch.
    if (GetModuleHandleW(L"dxgi.dll") || GetModuleHandleW(L"d3d11.dll") || GetModuleHandleW(L"d3d12.dll")) {
        if (hooks_install()) {
            PLOG("ready: DXGI engine armed");
        }
    }

    if (GetModuleHandleW(L"d3d9.dll")) {
        if (hook_dx9_install()) {
            PLOG("ready: D3D9 engine armed");
        }
    }

    if (GetModuleHandleW(L"ddraw.dll")) {
        if (hook_ddraw_install()) {
            PLOG("ready: DDraw engine armed");
        }
    }

    // Vulkan/OpenGL may already be mapped; otherwise armed lazily.
    hook_exports_arm_loaded();
    loader_watch_install();

    const std::uint64_t start_qpc = qpc_now();
    const std::uint64_t freq = qpc_freq();

    while (!g_stop) {
        Sleep(250);
        ctx_tick_watch();
        loader_watch_poll();

        // Hygiene channel: tools/uninstaller can ask any core to eject itself.
        std::uint64_t now = qpc_now();
        std::uint64_t lp = last_present_qpc();
        bool dormant = (lp == 0) ? ((now - start_qpc) > 30 * freq) : ((now - lp) > 30 * freq);
        bool exit_req = shm_exit_requested();
        if (exit_req || (dormant && shm_state() == PacerState_Unlimited && !g_stop)) {
            PLOG("eject requested (%s) -> self-eject", exit_req ? "explicit" : "dormant");
            HANDLE t = CreateThread(nullptr, 0, self_eject, nullptr, 0, nullptr);
            if (t) CloseHandle(t);
            break;
        }
    }
    return 0;
}

}  // namespace pacer

#pragma comment(lib, "winmm.lib")

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        timeBeginPeriod(1);
        DisableThreadLibraryCalls(mod);
        pacer::g_self = mod;
        HANDLE t = CreateThread(nullptr, 0, pacer::worker_main, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        pacer::g_stop = true;
        timeEndPeriod(1);
    }
    return TRUE;
}
