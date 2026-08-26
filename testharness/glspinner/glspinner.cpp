// glspinner: uncapped legacy-OpenGL presenter for validating the
// wglSwapBuffers/SwapBuffers hook path. Hue rotates with frames.
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>

int main() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PacerGLSpinner";
    RegisterClassExW(&wc);

    wchar_t title[128];
    swprintf_s(title, L"pacer-glspinner  pid=%lu", GetCurrentProcessId());
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 800, 450, nullptr, nullptr,
                                wc.hInstance, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, SW_SHOW);

    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{sizeof(pfd)};
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        fprintf(stderr, "pixel format failed\n");
        return 1;
    }
    HGLRC rc = wglCreateContext(hdc);
    if (!rc || !wglMakeCurrent(hdc, rc)) {
        fprintf(stderr, "wgl create/make-current failed\n");
        return 1;
    }
    // Ensure uncapped: ask driver to disable vsync if the ext is present.
    if (auto wglSwapIntervalEXT = (BOOL(WINAPI*)(int))wglGetProcAddress("wglSwapIntervalEXT"))
        wglSwapIntervalEXT(0);

    printf("glspinner running, pid=%lu\n", GetCurrentProcessId());
    std::uint64_t frame = 0;
    for (;;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (GetAsyncKeyState(VK_ESCAPE) & 1) return 0;
        float hue = std::fmod((float)frame * 0.7f, 360.0f) / 360.0f;
        glViewport(0, 0, 800, 450);
        glClearColor(hue, 0.3f, 1.0f - hue, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        SwapBuffers(hdc);  // -> wglSwapBuffers -> gdi32 SwapBuffers
        frame++;
    }
}
