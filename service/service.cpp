// pacer-svc: LocalSystem Windows service that owns privileged injection.
// Detection (window enumeration) cannot run here because the service lives in
// session 0; the interactive PacerUI performs detection and feeds the service
// over the shared control channel.
#include <windows.h>

#include <cstdio>

#include "service_core.h"

#pragma comment(lib, "advapi32.lib")

static SERVICE_STATUS_HANDLE g_svc_status = nullptr;
static SERVICE_STATUS g_status{};
static HANDLE g_stop = nullptr;

static void report_status(DWORD state, DWORD exit = 0) {
    g_status.dwCurrentState = state;
    g_status.dwWin32ExitCode = exit;
    if (g_svc_status) SetServiceStatus(g_svc_status, &g_status);
}

static VOID WINAPI svc_ctrl(DWORD ctrl) {
    if (ctrl == SERVICE_CONTROL_STOP && g_stop) SetEvent(g_stop);
}

static VOID WINAPI svc_main(DWORD, LPWSTR*) {
    g_svc_status = RegisterServiceCtrlHandlerW(L"PacerSvc", svc_ctrl);
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_status.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_status.dwCurrentState = SERVICE_START_PENDING;
    report_status(SERVICE_START_PENDING);

    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    svc::enable_debug_privilege();
    svc::Options opt;
    opt.fps = 60.0;
    opt.scan_ms = 200;
    opt.mode = pacer::PacerMode_LatencyFirst;
    opt.delay_bias = 0.0;
    svc::start(opt);  // runs the privileged watchdog (injection + election)

    report_status(SERVICE_RUNNING);
    WaitForSingleObject(g_stop, INFINITE);
    svc::stop();
    if (g_stop) { CloseHandle(g_stop); g_stop = nullptr; }
    report_status(SERVICE_STOPPED);
}

int wmain(int argc, wchar_t* argv[]) {
    // Debug helper: run in-process (with detection) instead of as a service.
    if (argc > 1 && wcscmp(argv[1], L"--console") == 0) {
        svc::enable_debug_privilege();
        svc::Options opt;
        opt.fps = 60.0;
        opt.scan_ms = 200;
        opt.mode = pacer::PacerMode_LatencyFirst;
        opt.delay_bias = 0.0;
        svc::start(opt);
        svc::start_detection();
        for (;;) Sleep(3600 * 1000);
    }

    SERVICE_TABLE_ENTRYW table[] = {
        {const_cast<LPWSTR>(L"PacerSvc"), svc_main},
        {nullptr, nullptr}};
    if (!StartServiceCtrlDispatcherW(table)) {
        fwprintf(stderr, L"StartServiceCtrlDispatcher failed: %lu\n", GetLastError());
        return 1;
    }
    return 0;
}
