// pacer-spinner: synthetic DX11 workload. FLIP-model swapchain, uncapped
// presents (vsync off), a hue that rotates with each frame so pacing quality
// is visible to the eye and measurable through the shm ring.
// ESC or window close exits.
#include <cmath>
#include <cstdio>
#include <d3d11.h>

static IDXGISwapChain* g_sc = nullptr;
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static HWND g_hwnd = nullptr;

static void hsv_to_rgb(float h, float* r, float* g, float* b) {
    h = std::fmod(h, 360.0f);
    float c = 0.9f, x = c * (1 - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1));
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
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (msg == WM_SIZE && g_sc) {
        g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
        g_sc->ResizeBuffers(0, LOWORD(lp), HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
        ID3D11Texture2D* back = nullptr;
        g_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
        g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int main() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PacerSpinner";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"pacer-spinner (pid in title)", 
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                             1024, 576, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) { fprintf(stderr, "CreateWindow failed %lu\n", GetLastError()); return 1; }

    wchar_t title[128];
    swprintf_s(title, L"pacer-spinner  pid=%lu", GetCurrentProcessId());
    SetWindowTextW(g_hwnd, title);
    ShowWindow(g_hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.OutputWindow = g_hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               nullptr, 0, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev,
                                               nullptr, &g_ctx);
    if (FAILED(hr)) { fprintf(stderr, "D3D11 init failed 0x%08lx\n", hr); return 1; }

    ID3D11Texture2D* back = nullptr;
    g_sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back);
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();

    printf("spinner running, pid=%lu -- inject with: pacer-inject %lu\n",
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
        const float color[4] = {r, g, b, 1.0f};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, color);
        g_sc->Present(0, 0);  // uncapped; the limiter (if injected) is the only brake
        frame++;
    }

done:
    if (g_rtv) g_rtv->Release();
    if (g_ctx) g_ctx->Release();
    if (g_dev) g_dev->Release();
    if (g_sc) g_sc->Release();
    return 0;
}
