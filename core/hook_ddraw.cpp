#include <algorithm>
#include "hook_ddraw.h"

#include <MinHook.h>
#include <ddraw.h>

#include "engine_context.h"
#include "hooks_common.h"
#include "log.h"

namespace pacer {

namespace {

// IDirectDrawSurface7 vtable: 0-2 IUnknown, then
//   3 AddAttachedSurface, 4 AddOverlayDirtyRect, 5 Blt, 6 BltBatch, 7 BltFast,
//   8 DeleteAttachedSurface, 9 EnumAttachedSurfaces, 10 EnumOverlayZOrders,
//   11 Flip, ...
using PFN_Flip = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface7*, LPDDSURFACEDESC2, DWORD);
using PFN_Blt = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface7*, LPRECT, IDirectDrawSurface7*,
                                            LPRECT, DWORD, LPDDBLTFX);

PFN_Flip g_origFlip = nullptr;
PFN_Blt g_origBlt = nullptr;

HRESULT STDMETHODCALLTYPE Hooked_Flip(IDirectDrawSurface7* s, LPDDSURFACEDESC2 d, DWORD f) {
    if (!s || !g_origFlip) return DDERR_INVALIDPARAMS;
    if (hooks_is_ejecting()) {
        return g_origFlip(s, d, f);
    }

    HookGuard guard;

    return [&]() -> HRESULT {
        __try {
            pre_present(PacerApi_DDraw, nullptr, false);
            HRESULT hr = g_origFlip(s, d, f);
            post_present(PacerApi_DDraw, nullptr, false);
            return hr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return g_origFlip(s, d, f);
        }
    }();
}

HRESULT STDMETHODCALLTYPE Hooked_Blt(IDirectDrawSurface7* dst, LPRECT r,
                                     IDirectDrawSurface7* src, LPRECT sr, DWORD f, LPDDBLTFX fx) {
    if (!dst || !g_origBlt) return DDERR_INVALIDPARAMS;
    if (hooks_is_ejecting()) {
        return g_origBlt(dst, r, src, sr, f, fx);
    }

    HookGuard guard;

    return [&]() -> HRESULT {
        __try {
            pre_present(PacerApi_DDraw, nullptr, false);
            HRESULT hr = g_origBlt(dst, r, src, sr, f, fx);
            post_present(PacerApi_DDraw, nullptr, false);
            return hr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return g_origBlt(dst, r, src, sr, f, fx);
        }
    }();
}

bool resolve_vtable(void*** vt_out) {
    HMODULE dd = GetModuleHandleW(L"ddraw.dll");
    if (!dd) dd = LoadLibraryW(L"ddraw.dll");
    if (!dd) {
        PLOG("ddraw.dll not present; DDraw hook skipped");
        return false;
    }
    using DDCreateEx_t = HRESULT(WINAPI*)(GUID*, LPVOID*, REFIID, IUnknown*);
    auto DDEx = (DDCreateEx_t)GetProcAddress(dd, "DirectDrawCreateEx");
    if (!DDEx) return false;

    IDirectDraw7* dd7 = nullptr;
    HRESULT hr = DDEx(nullptr, (LPVOID*)&dd7, IID_IDirectDraw7, nullptr);
    if (FAILED(hr) || !dd7) {
        PLOG("DirectDrawCreateEx failed, hr=0x%08lx", hr);
        return false;
    }
    HRESULT coop = dd7->SetCooperativeLevel(nullptr, DDSCL_NORMAL);
    IDirectDrawSurface7* surf = nullptr;
    DDSURFACEDESC2 ddsd{sizeof(ddsd)};
    ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    ddsd.dwWidth = 1;
    ddsd.dwHeight = 1;
    ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
    if (SUCCEEDED(coop))
        hr = dd7->CreateSurface(&ddsd, &surf, nullptr);
    dd7->Release();
    if (FAILED(hr) || !surf) {
        PLOG("DDraw offscreen surface creation failed, hr=0x%08lx", hr);
        return false;
    }
    *vt_out = *reinterpret_cast<void***>(surf);
    surf->Release();
    return true;
}

}  // namespace

bool hook_ddraw_install() {
    if (!GetModuleHandleW(L"ddraw.dll")) {
        return false;
    }
    void** vt = nullptr;
    if (!resolve_vtable(&vt)) return false;
    if (MH_CreateHook(vt[11], &Hooked_Flip, reinterpret_cast<void**>(&g_origFlip)) != MH_OK ||
        MH_CreateHook(vt[5], &Hooked_Blt, reinterpret_cast<void**>(&g_origBlt)) != MH_OK) {
        PLOG("DDraw MinHook create failed");
        return false;
    }
    if (MH_EnableHook(vt[11]) != MH_OK || MH_EnableHook(vt[5]) != MH_OK) {
        PLOG("DDraw MinHook enable failed");
        return false;
    }
    PLOG("DDraw hooks installed (Flip=%p Blt=%p)", vt[11], vt[5]);
    return true;
}

}  // namespace pacer
