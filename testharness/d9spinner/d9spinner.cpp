// pacer-d9spinner: synthetic Direct3D 9 workload.
// Uncapped presents (D3DPRESENT_INTERVAL_IMMEDIATE, vsync off),
// a rotating hue background so pacing quality is visually noticeable
// and measurable through the shared memory ring buffer.
// ESC or window close exits.

#include <d3d9.h>
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

static IDirect3D9* g_d3d = nullptr;
static IDirect3DDevice9* g_dev = nullptr;
static HWND g_hwnd = nullptr;

static void hsv_to_rgb(float h, float* r, float* g, float* b) {
    h = std::fmod(h, 360.0f);
    float c = 0.9f, x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = c; gg = x; }
    else if (h < 120) { rr = x; gg = c; }
    else if (h < 180) { gg = c; bb = x; }
    else if (h < 240) { gg = x; bb = c; }
    else if (h < 300) { rr = x; bb = c; }
    else { rr = c; bb = x; }
    *r = rr; *g = gg; *b = bb;
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PacerD9Spinner";
    RegisterClassExW(&wc);

    wchar_t title[128];
    swprintf_s(title, L"pacer-d9spinner  pid=%lu", GetCurrentProcessId());

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, title,
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, 800, 450,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) {
        fprintf(stderr, "CreateWindow failed %lu\n", GetLastError());
        return 1;
    }
    ShowWindow(g_hwnd, SW_SHOW);

    // Try Direct3DCreate9Ex first (standard on Windows 10/11 WDDM), with Direct3DCreate9 fallback
    HMODULE d9mod = LoadLibraryW(L"d3d9.dll");
    using Direct3DCreate9Ex_t = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    auto pDirect3DCreate9Ex = d9mod ? (Direct3DCreate9Ex_t)GetProcAddress(d9mod, "Direct3DCreate9Ex") : nullptr;

    IDirect3D9Ex* d3d9ex = nullptr;
    IDirect3DDevice9Ex* dev9ex = nullptr;

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = g_hwnd;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

    if (pDirect3DCreate9Ex && SUCCEEDED(pDirect3DCreate9Ex(D3D_SDK_VERSION, &d3d9ex)) && d3d9ex) {
        D3DDISPLAYMODEEX modeex{sizeof(modeex)};
        modeex.Size = sizeof(modeex);
        d3d9ex->GetAdapterDisplayModeEx(D3DADAPTER_DEFAULT, &modeex, nullptr);
        pp.BackBufferFormat = modeex.Format;

        HRESULT hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
                                            D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &dev9ex);
        if (FAILED(hr)) {
            hr = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
                                        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, nullptr, &dev9ex);
        }
        if (SUCCEEDED(hr) && dev9ex) {
            g_dev = dev9ex;
            g_d3d = d3d9ex;
        }
    }

    if (!g_dev) {
        g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!g_d3d) {
            fprintf(stderr, "Direct3DCreate9 failed\n");
            return 1;
        }
        D3DDISPLAYMODE d3ddm{};
        g_d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm);
        pp.BackBufferFormat = d3ddm.Format;

        HRESULT hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
                                         D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
        if (FAILED(hr)) {
            hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_dev);
        }
        if (FAILED(hr)) {
            hr = g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, g_hwnd,
                                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &g_dev);
        }
        if (FAILED(hr) || !g_dev) {
            fprintf(stderr, "CreateDevice failed: 0x%08lx\n", hr);
            g_d3d->Release();
            return 1;
        }
    }

    printf("d9spinner running, pid=%lu -- inject with: pacer-inject %lu\n",
           GetCurrentProcessId(), GetCurrentProcessId());

    std::uint64_t frame = 0;
    MSG msg{};
    for (;;) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) break;

        float r, g, b;
        hsv_to_rgb((float)frame * 0.7f, &r, &g, &b);
        D3DCOLOR color = D3DCOLOR_COLORVALUE(r, g, b, 1.0f);

        g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, color, 1.0f, 0);
        if (SUCCEEDED(g_dev->BeginScene())) {
            g_dev->EndScene();
        }
        HRESULT phr = g_dev->Present(nullptr, nullptr, nullptr, nullptr);
        if (phr == D3DERR_DEVICELOST) {
            if (g_dev->TestCooperativeLevel() == D3DERR_DEVICENOTRESET) {
                g_dev->Reset(&pp);
            }
        }
        frame++;
    }

done:
    if (g_dev) g_dev->Release();
    if (g_d3d) g_d3d->Release();
    return 0;
}
