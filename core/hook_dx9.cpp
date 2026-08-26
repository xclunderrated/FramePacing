#include <algorithm>
#include "hook_dx9.h"

#include <MinHook.h>
#include <d3d9.h>

#include "engine_context.h"
#include "hooks_common.h"
#include "log.h"

namespace pacer {

namespace {

using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*,
                                               HWND, const RGNDATA*);
using PFN_Reset = HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

PFN_Present g_origPresent = nullptr;
PFN_Reset g_origReset = nullptr;

HRESULT STDMETHODCALLTYPE Hooked_Present(IDirect3DDevice9* dev, const RECT* a, const RECT* b,
                                         HWND w, const RGNDATA* r) {
    if (!dev || !g_origPresent) return D3DERR_INVALIDCALL;
    if (hooks_is_ejecting()) {
        return g_origPresent(dev, a, b, w, r);
    }

    HookGuard guard;

    return [&]() -> HRESULT {
        __try {
            pre_present(PacerApi_D3D9, nullptr, false);
            HRESULT hr = g_origPresent(dev, a, b, w, r);
            post_present(PacerApi_D3D9, nullptr, false);
            return hr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return g_origPresent(dev, a, b, w, r);
        }
    }();
}

HRESULT STDMETHODCALLTYPE Hooked_Reset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    if (!dev || !g_origReset) return D3DERR_INVALIDCALL;
    if (hooks_is_ejecting()) {
        return g_origReset(dev, pp);
    }

    HookGuard guard;

    PLOG("D3D9 Reset (swapchain resize) -- pacing re-anchors on next frame");
    ctx().engine.reanchor(qpc_now());

    // Latent Sync: force VSYNC OFF (immediate interval) so the frame shows at
    // the steered phase instead of being queued.
    if (shm_mode() == PacerMode_LatencyFirst && shm_state() == PacerState_Limited) {
        pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    }

    return [&]() -> HRESULT {
        __try {
            return g_origReset(dev, pp);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return D3DERR_INVALIDCALL;
        }
    }();
}

bool resolve_device_vtable(void*** vt_out) {
    HMODULE d9 = GetModuleHandleW(L"d3d9.dll");
    if (!d9) d9 = LoadLibraryW(L"d3d9.dll");
    if (!d9) {
        PLOG("d3d9.dll not present; DX9 hook skipped");
        return false;
    }

    HWND dummy_hwnd = GetDesktopWindow();

    // 1. Try Direct3DCreate9Ex (standard on Windows 10/11)
    using Direct3DCreate9Ex_t = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    auto Direct3DCreate9Ex = (Direct3DCreate9Ex_t)GetProcAddress(d9, "Direct3DCreate9Ex");
    if (Direct3DCreate9Ex) {
        IDirect3D9Ex* d3d9ex = nullptr;
        if (SUCCEEDED(Direct3DCreate9Ex(D3D_SDK_VERSION, &d3d9ex)) && d3d9ex) {
            D3DDISPLAYMODEEX dm{sizeof(dm)};
            dm.Size = sizeof(dm);
            d3d9ex->GetAdapterDisplayModeEx(D3DADAPTER_DEFAULT, &dm, nullptr);

            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed = TRUE;
            pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.BackBufferFormat = dm.Format;
            pp.BackBufferCount = 1;
            pp.hDeviceWindow = dummy_hwnd;
            pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

            IDirect3DDevice9Ex* dev9ex = nullptr;
            // Prefer NULLREF to avoid physical GPU hardware driver contention
            HRESULT hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, dummy_hwnd,
                                                D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, nullptr, &dev9ex);
            if (FAILED(hr)) {
                hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummy_hwnd,
                                            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, nullptr, &dev9ex);
            }
            if (SUCCEEDED(hr) && dev9ex) {
                *vt_out = *reinterpret_cast<void***>(dev9ex);
                PLOG("d3d9ex dummy device vtable resolved at %p (Present=%p)", *vt_out, (*vt_out)[17]);
                return true;
            }
            d3d9ex->Release();
        }
    }

    // 2. Try Direct3DCreate9
    using Direct3DCreate9_t = IDirect3D9*(WINAPI*)(UINT);
    auto Direct3DCreate9 = (Direct3DCreate9_t)GetProcAddress(d9, "Direct3DCreate9");
    if (!Direct3DCreate9) return false;

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return false;

    D3DDISPLAYMODE d3ddm{};
    d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm);

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = d3ddm.Format;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = dummy_hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, dummy_hwnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, dummy_hwnd,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &dev);
    }
    if (FAILED(hr) || !dev) {
        PLOG("d3d9 dummy device creation failed, hr=0x%08lx", hr);
        return false;
    }
    *vt_out = *reinterpret_cast<void***>(dev);
    PLOG("d3d9 dummy device vtable resolved at %p (Present=%p)", *vt_out, (*vt_out)[17]);
    return true;
}

}  // namespace

bool hook_dx9_install() {
    if (!GetModuleHandleW(L"d3d9.dll")) {
        return false;
    }
    void** vt = nullptr;
    if (!resolve_device_vtable(&vt)) return false;

    if (MH_CreateHook(vt[17], &Hooked_Present, reinterpret_cast<void**>(&g_origPresent)) != MH_OK ||
        MH_CreateHook(vt[16], &Hooked_Reset, reinterpret_cast<void**>(&g_origReset)) != MH_OK) {
        PLOG("D3D9 MinHook create failed");
        return false;
    }
    if (MH_EnableHook(vt[17]) != MH_OK || MH_EnableHook(vt[16]) != MH_OK) {
        PLOG("D3D9 MinHook enable failed");
        return false;
    }
    PLOG("D3D9 hooks installed (Present=%p Reset=%p)", vt[17], vt[16]);
    return true;
}

}  // namespace pacer
