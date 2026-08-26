#include <algorithm>
#include "hook_exports.h"

#include <MinHook.h>
#include <windows.h>
#include <cstring>

#include "engine_context.h"
#include "hooks_common.h"
#include "log.h"
#include "shared/shm.h"

namespace pacer {

namespace {

// ------------------------------------------------------------ Vulkan (M2 / M2.1)
using PFN_vkQueuePresentKHR = int(__stdcall*)(void* queue, const void* pPresentInfo);
using PFN_vkGetDeviceProcAddr = void*(__stdcall*)(void* device, const char* pName);
using PFN_vkGetInstanceProcAddr = void*(__stdcall*)(void* instance, const char* pName);

PFN_vkQueuePresentKHR g_origQueuePresentKHR = nullptr;
PFN_vkGetDeviceProcAddr g_origGetDeviceProcAddr = nullptr;
PFN_vkGetInstanceProcAddr g_origGetInstanceProcAddr = nullptr;
bool g_vulkan_armed = false;

int __stdcall Hooked_vkQueuePresentKHR(void* queue, const void* present_info) {
    if (!queue || !g_origQueuePresentKHR) return 0;
    if (hooks_is_ejecting()) {
        return g_origQueuePresentKHR(queue, present_info);
    }

    HookGuard guard;

    return [&]() -> int {
        __try {
            pre_present(PacerApi_Vulkan, nullptr, false);
            int r = g_origQueuePresentKHR(queue, present_info);
            post_present(PacerApi_Vulkan, nullptr, false);
            return r;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return g_origQueuePresentKHR(queue, present_info);
        }
    }();
}

void* __stdcall Hooked_vkGetDeviceProcAddr(void* device, const char* pName) {
    if (!g_origGetDeviceProcAddr) return nullptr;
    void* ptr = g_origGetDeviceProcAddr(device, pName);
    if (pName && std::strcmp(pName, "vkQueuePresentKHR") == 0) {
        if (!g_origQueuePresentKHR && ptr) {
            g_origQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(ptr);
        }
        return reinterpret_cast<void*>(&Hooked_vkQueuePresentKHR);
    }
    return ptr;
}

void* __stdcall Hooked_vkGetInstanceProcAddr(void* instance, const char* pName) {
    if (!g_origGetInstanceProcAddr) return nullptr;
    void* ptr = g_origGetInstanceProcAddr(instance, pName);
    if (pName && std::strcmp(pName, "vkQueuePresentKHR") == 0) {
        if (!g_origQueuePresentKHR && ptr) {
            g_origQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(ptr);
        }
        return reinterpret_cast<void*>(&Hooked_vkQueuePresentKHR);
    }
    if (pName && std::strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        if (!g_origGetDeviceProcAddr && ptr) {
            g_origGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(ptr);
        }
        return reinterpret_cast<void*>(&Hooked_vkGetDeviceProcAddr);
    }
    return ptr;
}

// ------------------------------------------------------------ OpenGL (M2)
using PFN_wglSwapBuffers = BOOL(WINAPI*)(HDC);
PFN_wglSwapBuffers g_origWglSwapBuffers = nullptr;
PFN_wglSwapBuffers g_origGdiSwapBuffers = nullptr;
bool g_opengl_armed = false;

thread_local bool t_in_gl_present = false;

static BOOL WINAPI present_gl_common(PFN_wglSwapBuffers orig, HDC hdc) {
    if (!orig) return FALSE;
    if (hooks_is_ejecting()) return orig(hdc);
    if (t_in_gl_present) return orig(hdc);

    HookGuard guard;

    return [&]() -> BOOL {
        __try {
            t_in_gl_present = true;
            pre_present(PacerApi_OpenGL, nullptr, false);
            BOOL r = orig(hdc);
            post_present(PacerApi_OpenGL, nullptr, false);
            t_in_gl_present = false;
            return r;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            t_in_gl_present = false;
            return orig(hdc);
        }
    }();
}

BOOL WINAPI Hooked_wglSwapBuffers(HDC hdc) {
    return present_gl_common(g_origWglSwapBuffers, hdc);
}

BOOL WINAPI Hooked_GdiSwapBuffers(HDC hdc) {
    return present_gl_common(g_origGdiSwapBuffers, hdc);
}

}  // namespace

bool hook_exports_arm_vulkan() {
    if (g_vulkan_armed) return true;
    HMODULE vk = GetModuleHandleW(L"vulkan-1.dll");
    if (!vk) return false;

    void* qp = GetProcAddress(vk, "vkQueuePresentKHR");
    if (qp) {
        MH_CreateHook(qp, &Hooked_vkQueuePresentKHR, reinterpret_cast<void**>(&g_origQueuePresentKHR));
        MH_EnableHook(qp);
    }

    void* gdpa = GetProcAddress(vk, "vkGetDeviceProcAddr");
    if (gdpa) {
        MH_CreateHook(gdpa, &Hooked_vkGetDeviceProcAddr, reinterpret_cast<void**>(&g_origGetDeviceProcAddr));
        MH_EnableHook(gdpa);
    }

    void* gipa = GetProcAddress(vk, "vkGetInstanceProcAddr");
    if (gipa) {
        MH_CreateHook(gipa, &Hooked_vkGetInstanceProcAddr, reinterpret_cast<void**>(&g_origGetInstanceProcAddr));
        MH_EnableHook(gipa);
    }

    g_vulkan_armed = true;
    PLOG("Vulkan hooks installed (exports + GetProcAddr chaining armed)");
    return true;
}

bool hook_exports_arm_opengl() {
    if (g_opengl_armed) return true;
    HMODULE gl32 = GetModuleHandleW(L"opengl32.dll");
    HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
    if (!gl32 || !gdi32) return false;
    void* wgl = GetProcAddress(gl32, "wglSwapBuffers");
    void* sb = GetProcAddress(gdi32, "SwapBuffers");
    if (!wgl || !sb) return false;
    if (MH_CreateHook(wgl, &Hooked_wglSwapBuffers,
                      reinterpret_cast<void**>(&g_origWglSwapBuffers)) != MH_OK ||
        MH_CreateHook(sb, &Hooked_GdiSwapBuffers,
                      reinterpret_cast<void**>(&g_origGdiSwapBuffers)) != MH_OK) {
        PLOG("OpenGL hook create failed");
        return false;
    }
    if (MH_EnableHook(wgl) != MH_OK || MH_EnableHook(sb) != MH_OK) {
        PLOG("OpenGL hook enable failed");
        return false;
    }
    g_opengl_armed = true;
    PLOG("OpenGL hooks installed (wglSwapBuffers=%p SwapBuffers=%p)", wgl, sb);
    return true;
}

void hook_exports_arm_loaded() {
    hook_exports_arm_vulkan();
    hook_exports_arm_opengl();
}

}  // namespace pacer

