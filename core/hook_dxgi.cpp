#include "hook_dxgi.h"

#include <MinHook.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>

#include "engine_context.h"
#include "log.h"

namespace pacer {

namespace {

// IDXGISwapChain vtable layout (COM order, stable ABI):
//   0-2  IUnknown
//   3-6  IDXGIObject
//   7    IDXGIDeviceSubObject::GetDevice
//   8    Present
//   9    GetBuffer
//   10   SetFullscreenState
//   11   GetFullscreenState
//   12   GetDesc
//   13   ResizeBuffers
//   14   ResizeTarget
//   15   GetContainingOutput
//   16   GetFrameStatistics
//   17   GetLastPresentCount
//   ... IDXGISwapChain1:
//   22   Present1
constexpr int kIdxPresent = 8;
constexpr int kIdxResizeBuffers = 13;
constexpr int kIdxPresent1 = 22;

using PFN_Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using PFN_Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT,
                                                 const DXGI_PRESENT_PARAMETERS*);
using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                      DXGI_FORMAT, UINT);

PFN_Present g_origPresent = nullptr;
PFN_Present1 g_origPresent1 = nullptr;
PFN_ResizeBuffers g_origResizeBuffers = nullptr;

HWND g_dummy_hwnd = nullptr;

// Latent Sync (Latency-First): when actively limiting in that mode, force VSYNC
// OFF so the frame is shown at the steered phase instead of being queued. Tear
// free-ness comes from presenting just before VBlank (handled by the engine's
// VBI phase lock); ALLOW_TEARING lets the flip-model swapchain actually tear.
bool latent_sync_active() {
    return shm_mode() == PacerMode_LatencyFirst && shm_state() == PacerState_Limited;
}

bool is_own_swapchain(IDXGISwapChain* sc) {
    DXGI_SWAP_CHAIN_DESC d{};
    if (FAILED(sc->GetDesc(&d))) return true;
    return d.OutputWindow == g_dummy_hwnd;
}

HRESULT STDMETHODCALLTYPE Hooked_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!sc || !g_origPresent) return DXGI_ERROR_INVALID_CALL;

    UINT sync_out = sync;
    UINT flags_out = flags;
    if (latent_sync_active()) {
        sync_out = 0;
        flags_out |= DXGI_PRESENT_ALLOW_TEARING;
    }

    __try {
        bool own = is_own_swapchain(sc);
        pre_present(PacerApi_Dxgi, sc, own);
        HRESULT hr = g_origPresent(sc, sync_out, flags_out);
        post_present(PacerApi_Dxgi, sc, own);
        return hr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return g_origPresent(sc, sync_out, flags_out);
    }
}

HRESULT STDMETHODCALLTYPE Hooked_Present1(IDXGISwapChain* sc, UINT sync, UINT flags,
                                          const DXGI_PRESENT_PARAMETERS* pp) {
    if (!sc || !g_origPresent1) return DXGI_ERROR_INVALID_CALL;

    UINT sync_out = sync;
    UINT flags_out = flags;
    if (latent_sync_active()) {
        sync_out = 0;
        flags_out |= DXGI_PRESENT_ALLOW_TEARING;
    }

    __try {
        bool own = is_own_swapchain(sc);
        pre_present(PacerApi_Dxgi, sc, own);
        HRESULT hr = g_origPresent1(sc, sync_out, flags_out, pp);
        post_present(PacerApi_Dxgi, sc, own);
        return hr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return g_origPresent1(sc, sync_out, flags_out, pp);
    }
}

HRESULT STDMETHODCALLTYPE Hooked_ResizeBuffers(IDXGISwapChain* sc, UINT count, UINT w, UINT h,
                                                DXGI_FORMAT fmt, UINT flags) {
    PLOG("ResizeBuffers %ux%u (swapchain %p) -- re-anchoring display clock", w, h, sc);
    ctx().display.reset();
    ctx().engine.reanchor(qpc_now());
    if (!sc || !g_origResizeBuffers) return DXGI_ERROR_INVALID_CALL;

    // Enable tearing capability on the swapchain so Latent Sync can present
    // VSYNC-OFF without DWM forcing vsync. Harmless for other modes.
    UINT flags_out = flags | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    __try {
        return g_origResizeBuffers(sc, count, w, h, fmt, flags_out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return DXGI_ERROR_INVALID_CALL;
    }
}

// Create a hidden dummy swapchain purely to read the class vtable locations.
bool resolve_swapchain_vtable(void*** vt_out) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PacerDummyWnd";
    RegisterClassExW(&wc);
    g_dummy_hwnd = CreateWindowExW(0, wc.lpszClassName, L"pacer", 0, 0, 0, 1, 1,
                                   HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g_dummy_hwnd) {
        PLOG("dummy window creation failed, err=%lu", GetLastError());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 1;
    sd.BufferDesc.Height = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_dummy_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctxdev = nullptr;
    IDXGISwapChain* sc = nullptr;

    // Use NULL driver first to avoid touching physical GPU driver
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_NULL, nullptr, 0,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                               nullptr, &ctxdev);
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                           nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                           nullptr, &ctxdev);
    }
    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                           nullptr, 0, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                           nullptr, &ctxdev);
    }
    if (FAILED(hr) || !sc) {
        PLOG("dummy device creation failed, hr=0x%08lx", hr);
        return false;
    }
    *vt_out = *reinterpret_cast<void***>(sc);
    PLOG("swapchain vtable resolved at %p (Present=%p)", *vt_out, (*vt_out)[kIdxPresent]);
    sc->Release();
    ctxdev->Release();
    dev->Release();
    return true;
}

}  // namespace

bool hooks_install() {
    if (MH_Initialize() != MH_OK && MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED) {
        PLOG("MinHook init failed");
        return false;
    }
    void** vt = nullptr;
    if (!resolve_swapchain_vtable(&vt)) return false;

    if (MH_CreateHook(vt[kIdxPresent], &Hooked_Present,
                      reinterpret_cast<void**>(&g_origPresent)) != MH_OK ||
        MH_CreateHook(vt[kIdxPresent1], &Hooked_Present1,
                      reinterpret_cast<void**>(&g_origPresent1)) != MH_OK ||
        MH_CreateHook(vt[kIdxResizeBuffers], &Hooked_ResizeBuffers,
                      reinterpret_cast<void**>(&g_origResizeBuffers)) != MH_OK) {
        PLOG("MinHook CreateHook failed");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        PLOG("MinHook EnableHook failed");
        return false;
    }
    PLOG("DXGI hooks installed");
    return true;
}

void hooks_uninstall() {
    MH_DisableHook(MH_ALL_HOOKS);
    ctx().display.reset();
    if (g_dummy_hwnd) DestroyWindow(g_dummy_hwnd);
    g_dummy_hwnd = nullptr;
}

}  // namespace pacer
