// Pacer UI - Ultra-Premium Steam Framepacer UI Implementation
// Layout & Style:
//  - Compact window (368x246) with Windows 11 dark title bar & rounded corners
//  - Top Toolbar: [☰ Menu] [⊞ Apps] [📌 Pin]  [ FPS Edit ]  [ Apply ]
//  - Status text:
//      Line 1: Limiting: <game.exe> / Observing (no game presenting)
//      Line 2: FPS: 60 | Time: 16.67 ms | API: D3D12 (64-bit)
//  - Rounded Frametime Graph Card with vertical ticks, gradient fill & live waveform

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <winsvc.h>
#pragma comment(lib, "advapi32.lib")

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../service/service_core.h"
#include "shared/shm_client.h"
#include "ui_graph.h"
#include "ui_i18n.h"
#include "ui_theme.h"

#pragma comment(lib, "wtsapi32.lib")

namespace {

// ---------------------------------------------------------------- Layout
constexpr int kBaseW = 368;
constexpr int kBaseH = 276;

HWND g_main = nullptr;
HWND g_graph_wnd = nullptr;
HWND g_fps_edit = nullptr;
WNDPROC g_orig_edit_proc = nullptr;

// Toolbar Button Rectangles
RECT g_rc_menu{14, 12, 48, 46};
RECT g_rc_apps{54, 12, 88, 46};
RECT g_rc_pin{94, 12, 128, 46};
RECT g_rc_apply{216, 12, 354, 46};

bool g_hover_menu = false;
bool g_hover_apps = false;
bool g_hover_pin = false;
bool g_hover_apply = false;
bool g_press_apply = false;

bool g_is_pinned = false;
bool g_mintray = true;
bool g_tray_icon = false;
bool g_hidden = false;

double g_target_fps = 60.0;
double g_live_fps = 0.0;
double g_live_ms = 0.0;
std::wstring g_current_exe;
DWORD g_current_pid = 0;
DWORD g_prev_limited_pid = 0;  // last game we flipped to Limited (UI owns per-game shm)
std::uint32_t g_current_api = 0;
std::uint32_t g_current_mode = pacer::PacerMode_VrrLive;  // display echo from core
std::uint32_t g_settings_mode = pacer::PacerMode_VrrLive;  // desired mode (UI writes to game shm)
double g_delay_bias = 0.0;  // Latent Sync Delay Bias: 0=smooth/tear-stable .. 1=lowest latency
double g_low1_fps = 0.0;    // 1% Low (avg FPS of worst 1% frames)
double g_p99_fps = 0.0;     // 99th percentile FPS

// Repaint gating: only redraw when displayed data actually changes (avoids
// needless full-window repaints / wasted CPU while idle).
std::wstring g_status_prev1, g_status_prev2, g_status_prev3;
LONG g_graph_prev_idx = -1;
bool g_graph_prev_valid = false;

void build_status_lines(wchar_t* l1, size_t l1n, wchar_t* l2, size_t l2n, wchar_t* l3, size_t l3n);
void compute_pacing_lows(double* out_1low, double* out_p99);
bool g_is_64bit = true;

NOTIFYICONDATAW g_nid{};

// Log Queue
CRITICAL_SECTION g_log_cs;
std::deque<std::wstring> g_log_queue;

void ui_log(const std::wstring& line) {
    EnterCriticalSection(&g_log_cs);
    g_log_queue.push_back(line);
    if (g_log_queue.size() > 200) g_log_queue.pop_front();
    LeaveCriticalSection(&g_log_cs);
}

// ------------------------------------------------------------- Autostart
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunName[] = L"Framepacer";

bool autostart_get() {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, size = 0;
    bool present = RegQueryValueExW(k, kRunName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
    RegCloseKey(k);
    return present;
}

void autostart_set(bool on) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                         nullptr, &k, nullptr) != ERROR_SUCCESS)
        return;
    if (on) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        RegSetValueExW(k, kRunName, 0, REG_SZ, (const BYTE*)path,
                       (DWORD)(wcslen(path) + 1) * sizeof(wchar_t));
    } else {
        RegDeleteValueW(k, kRunName);
    }
    RegCloseKey(k);
}

// ------------------------------------------------------------- Profiles
std::map<std::wstring, double> g_profiles;
std::wstring g_profile_path;

std::wstring profile_file() {
    PWSTR appdata = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata)))
        return L"profiles.json";
    std::wstring dir = std::wstring(appdata) + L"\\Framepacer";
    CoTaskMemFree(appdata);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\profiles.json";
}

void profiles_load() {
    g_profile_path = profile_file();
    HANDLE h = CreateFileW(g_profile_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD size = GetFileSize(h, nullptr);
    std::string buf(size, '\0');
    DWORD rd = 0;
    ReadFile(h, buf.data(), size, &rd, nullptr);
    CloseHandle(h);

    size_t i = 0;
    while ((i = buf.find('"', i)) != std::string::npos) {
        size_t end = buf.find('"', i + 1);
        if (end == std::string::npos) break;
        std::string name(buf.begin() + (int)i + 1, buf.begin() + (int)end);
        size_t colon = buf.find(':', end);
        size_t close = buf.find_first_of(",}", end);
        if (colon == std::string::npos || close == std::string::npos) break;
        double fps = atof(buf.substr(colon + 1, close - colon - 1).c_str());
        if (fps >= 0.0) {
            std::wstring w(name.begin(), name.end());
            g_profiles[w] = fps;
        }
        i = close;
    }
}

void profiles_save() {
    std::string out = "{\n";
    bool first = true;
    for (auto& [exe, fps] : g_profiles) {
        if (!first) out += ",\n";
        first = false;
        out += "  \"";
        for (wchar_t c : exe) out += (char)(c < 128 ? c : '?');
        char num[32];
        snprintf(num, 32, "\": %.2f", fps);
        out += num;
    }
    out += "\n}\n";
    HANDLE h = CreateFileW(g_profile_path.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteFile(h, out.data(), (DWORD)out.size(), &wr, nullptr);
    CloseHandle(h);
}

// ----------------------------------------------------------- Service Bridge
// Installs (if needed) and starts the privileged LocalSystem injection service.
// Returns true if the service is now running (preferred path); false means we
// fall back to an in-process watchdog (limited to unprotected games).
static bool g_using_service = false;

static bool ensure_service() {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash - path + 1), L"pacer-svc.exe");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return false;

    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"PacerSvc", SERVICE_ALL_ACCESS);
    if (!svc) {
        svc = CreateServiceW(scm, L"PacerSvc", L"Pacer Frame Pacer Service",
                             SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                             SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, path, nullptr,
                             nullptr, nullptr, L"LocalSystem", nullptr);
    }
    bool ok = false;
    if (svc) {
        SERVICE_STATUS st{};
        if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING) {
            ok = true;
        } else if (StartServiceW(svc, 0, nullptr)) {
            for (int i = 0; i < 25 && !ok; ++i) {
                if (QueryServiceStatus(svc, &st) && st.dwCurrentState == SERVICE_RUNNING)
                    ok = true;
                else
                    Sleep(200);
            }
        }
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
    return ok;
}

// ----------------------------------------------------------- Tray Menu
void tray_add() {
    if (g_tray_icon) return;
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_main;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP + 7;
    g_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"Framepacer");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_tray_icon = true;
}

void tray_remove() {
    if (g_tray_icon) {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_tray_icon = false;
    }
}

}  // namespace

namespace ui {
pacer::ShmBox g_shm_box;
}

namespace {
using ui::g_shm_box;

bool is_target_64bit(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return true;
    BOOL is_wow64 = FALSE;
    IsWow64Process(h, &is_wow64);
    CloseHandle(h);
    return is_wow64 == FALSE;
}

const wchar_t* get_api_string(std::uint32_t api, bool is_64) {
    const wchar_t* bit_str = is_64 ? L"(64-bit)" : L"(32-bit)";
    static wchar_t s_buf[64];
    switch (api) {
        case pacer::PacerApi_Dxgi:
            swprintf_s(s_buf, L"D3D12 %s", bit_str);
            break;
        case pacer::PacerApi_D3D9:
            swprintf_s(s_buf, L"D3D9 %s", bit_str);
            break;
        case pacer::PacerApi_Vulkan:
            swprintf_s(s_buf, L"Vulkan %s", bit_str);
            break;
        case pacer::PacerApi_OpenGL:
            swprintf_s(s_buf, L"OpenGL %s", bit_str);
            break;
        case pacer::PacerApi_DDraw:
            swprintf_s(s_buf, L"DirectDraw %s", bit_str);
            break;
        default:
            swprintf_s(s_buf, L"DirectX %s", bit_str);
            break;
    }
    return s_buf;
}

const wchar_t* get_mode_string(std::uint32_t mode) {
    switch (mode) {
        case pacer::PacerMode_Async:        return L"Async";
        case pacer::PacerMode_VrrLive:      return L"VRR Live";
        case pacer::PacerMode_LatencyFirst: return L"Latent Sync";
        case pacer::PacerMode_DisplayLocked:
        default:                            return L"Console/Front-edge";
    }
}

void apply_fps(double fps) {
    if (fps < 0.0) fps = 0.0;
    if (fps > 0.0 && fps < 10.0) fps = 10.0;
    if (fps > 1000.0) fps = 1000.0;

    g_target_fps = fps;
    double limit_fps = (fps <= 0.0) ? 10000.0 : fps;
    svc::set_target_fps(limit_fps);

    if (g_current_pid != 0 && !g_shm_box.valid()) {
        g_shm_box.open(g_current_pid, true);
    }

    if (g_shm_box.valid()) {
        g_shm_box.shm->ctl.state =
            (g_target_fps > 0.0) ? pacer::PacerState_Limited : pacer::PacerState_Unlimited;
        pacer::ctl_write_double(&g_shm_box.shm->ctl.target_fps_bits, g_target_fps);
        g_shm_box.shm->ctl.mode = g_settings_mode;
        pacer::ctl_write_double(&g_shm_box.shm->ctl.delay_bias_bits, g_delay_bias);
    }

    if (g_fps_edit) {
        wchar_t num[32];
        if (std::fabs(fps - std::round(fps)) < 0.001) {
            swprintf_s(num, L"%.0f", fps);
        } else {
            swprintf_s(num, L"%.2f", fps);
        }
        SetWindowTextW(g_fps_edit, num);
    }

    if (!g_current_exe.empty()) {
        g_profiles[g_current_exe] = fps;
        profiles_save();
    }
    InvalidateRect(g_main, nullptr, FALSE);
}

void apply_from_edit() {
    if (!g_fps_edit) return;
    wchar_t buf[32]{};
    GetWindowTextW(g_fps_edit, buf, 32);
    double fps = _wtof(buf);
    apply_fps(fps);
}

void toggle_pin() {
    g_is_pinned = !g_is_pinned;
    SetWindowPos(g_main, g_is_pinned ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    InvalidateRect(g_main, nullptr, FALSE);
}

// Subclassed Edit Control Procedure
static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            apply_from_edit();
            return 0;
        }
        if (wp == VK_UP) {
            apply_fps(g_target_fps + 1.0);
            return 0;
        }
        if (wp == VK_DOWN) {
            apply_fps((std::max)(0.0, g_target_fps - 1.0));
            return 0;
        }
    }
    if (msg == WM_MOUSEWHEEL) {
        short delta = GET_WHEEL_DELTA_WPARAM(wp);
        double step = (GetKeyState(VK_SHIFT) & 0x8000) ? 5.0 : 1.0;
        if (delta > 0) apply_fps(g_target_fps + step);
        else if (delta < 0) apply_fps((std::max)(0.0, g_target_fps - step));
        return 0;
    }
    return CallWindowProcW(g_orig_edit_proc, hwnd, msg, wp, lp);
}

void show_hamburger_menu(HWND hwnd, int x, int y) {
    HMENU m = CreatePopupMenu();
    HMENU fps_menu = CreatePopupMenu();
    AppendMenuW(fps_menu, MF_STRING, 1000, L"0 (Uncapped)");
    AppendMenuW(fps_menu, MF_STRING, 1030, L"30 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1040, L"40 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1048, L"48 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1060, L"60 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1090, L"90 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1120, L"120 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1144, L"144 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1165, L"165 FPS");
    AppendMenuW(fps_menu, MF_STRING, 1240, L"240 FPS");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)fps_menu, L"Target FPS");

    HMENU mode_menu = CreatePopupMenu();
    AppendMenuW(mode_menu, MF_STRING, 2003, L"VRR Live (Adaptive)");
    AppendMenuW(mode_menu, MF_STRING, 2004, L"Async (Compat)");
    UINT active_mode_cmd = (g_settings_mode == pacer::PacerMode_VrrLive) ? 2003 : 2004;
    CheckMenuItem(mode_menu, active_mode_cmd, MF_CHECKED);
    AppendMenuW(m, MF_POPUP, (UINT_PTR)mode_menu, L"Pacing Mode");

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, autostart_get() ? (MF_STRING | MF_CHECKED) : MF_STRING, 3001, L"Start with Windows");
    AppendMenuW(m, g_mintray ? (MF_STRING | MF_CHECKED) : MF_STRING, 3002, L"Minimize to Tray");
    AppendMenuW(m, g_is_pinned ? (MF_STRING | MF_CHECKED) : MF_STRING, 3003, L"Always on Top");

    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 4003, L"Reset Stats");
    AppendMenuW(m, MF_STRING, 4001, L"Reset / Eject Core Hooks");
    AppendMenuW(m, MF_STRING, 4002, L"Exit");

    SetForegroundWindow(hwnd);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD, x, y, 0, hwnd, nullptr);
    DestroyMenu(fps_menu);
    DestroyMenu(mode_menu);
    DestroyMenu(m);

    if (cmd == 1000) apply_fps(0.0);
    else if (cmd == 1030) apply_fps(30.0);
    else if (cmd == 1040) apply_fps(40.0);
    else if (cmd == 1048) apply_fps(48.0);
    else if (cmd == 1060) apply_fps(60.0);
    else if (cmd == 1090) apply_fps(90.0);
    else if (cmd == 1120) apply_fps(120.0);
    else if (cmd == 1144) apply_fps(144.0);
    else if (cmd == 1165) apply_fps(165.0);
    else if (cmd == 1240) apply_fps(240.0);
    else if (cmd == 3001) autostart_set(!autostart_get());
    else if (cmd == 3002) g_mintray = !g_mintray;
    else if (cmd == 3003) toggle_pin();
     else if (cmd == 4001) {
        wchar_t eject_exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, eject_exe, MAX_PATH);
        wchar_t* s = wcsrchr(eject_exe, L'\\');
        if (s) *(s + 1) = L'\0';
        std::wstring eject_cmd = std::wstring(eject_exe) + L"pacer-eject.exe --all";
        _wsystem(eject_cmd.c_str());
    }
     else if (cmd == 2003) {
        g_current_mode = pacer::PacerMode_VrrLive;
        g_settings_mode = g_current_mode; svc::set_mode(g_current_mode);
        if (g_shm_box.valid()) g_shm_box.shm->ctl.mode = g_current_mode;
    }
    else if (cmd == 2004) {
        g_current_mode = pacer::PacerMode_Async;
        g_settings_mode = g_current_mode; svc::set_mode(g_current_mode);
        if (g_shm_box.valid()) g_shm_box.shm->ctl.mode = g_current_mode;
    }
    else if (cmd == 4003) {
        if (g_shm_box.valid()) g_shm_box.shm->ctl.stats_reset_requested = 1;
        g_low1_fps = -1.0; g_p99_fps = -1.0;
        g_status_prev3.clear();
        InvalidateRect(g_main, nullptr, FALSE);
    }
    else if (cmd == 4002) SendMessageW(hwnd, WM_CLOSE, 0, 0);
}

void show_apps_menu(HWND hwnd, int x, int y) {
    HMENU m = CreatePopupMenu();
    std::set<DWORD> window_pids;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* pids = reinterpret_cast<std::set<DWORD>*>(lp);
            DWORD wp = 0;
            GetWindowThreadProcessId(hwnd, &wp);
            if (wp > 4) pids->insert(wp);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&window_pids));

    WTS_PROCESS_INFOW* pinfo = nullptr;
    DWORD count = 0;
    if (WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pinfo, &count)) {
        int item_id = 5000;
        std::map<int, std::wstring> proc_map;
        std::map<int, DWORD> pid_map;

        for (DWORD i = 0; i < count; ++i) {
            DWORD pid = pinfo[i].ProcessId;
            if (pid <= 4 || !window_pids.count(pid)) continue;
            if (!pinfo[i].pProcessName) continue;
            std::wstring exe = pinfo[i].pProcessName;

            // Only show non-system GUI processes
            if (item_id < 5050) {
                wchar_t line[128];
                swprintf_s(line, L"%s (PID %lu)", exe.c_str(), pid);
                AppendMenuW(m, MF_STRING, item_id, line);
                proc_map[item_id] = exe;
                pid_map[item_id] = pid;
                item_id++;
            }
        }
        WTSFreeMemory(pinfo);

        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD, x, y, 0, hwnd, nullptr);
        DestroyMenu(m);

        if (cmd >= 5000 && proc_map.count(cmd)) {
            DWORD pid = pid_map[cmd];
            g_current_exe = proc_map[cmd];
            g_current_pid = pid;
            svc::force_target(pid, g_current_exe);
            apply_fps(g_target_fps);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    } else {
        DestroyMenu(m);
    }
}

// ----------------------------------------------------- Periodic UI Tick
void ui_tick() {
    svc::control_log_drain([](const std::wstring& s) { ui_log(s); });
    DWORD pid = svc::elected_pid();
    if (pid != g_current_pid) {
        // Election changed: fully eject the framepacer from the game we're
        // leaving so it isn't left hooked or limited in the background. The UI
        // owns the per-game Local\ shm (the session-0 service can't open it),
        // so signal the core to self-eject.
        DWORD leaving_pid = g_current_pid;
        if (leaving_pid != 0 && leaving_pid != pid) {
            pacer::ShmBox prev;
            if (prev.open(leaving_pid, true)) {
                prev.shm->ctl.state = pacer::PacerState_Unlimited;
                prev.shm->ctl.exit_requested = 1;  // core self-ejects
            }
        }
        g_prev_limited_pid = pid;
        g_shm_box.close();
        if (pid) g_shm_box.open(pid, true);  // writable: UI controls this game
        g_current_pid = pid;
        g_current_exe = svc::elected_exe();
        g_is_64bit = is_target_64bit(pid);

        if (pid) {
            auto it = g_profiles.find(g_current_exe);
            if (it != g_profiles.end()) {
                apply_fps(it->second);
            } else {
                apply_fps(g_target_fps);
            }
        }
        InvalidateRect(g_main, nullptr, FALSE);
    }

    // (Re)open the elected game's shm until the core has created it (the DLL
    // loads a moment after injection). A single failed open at election time
    // would otherwise leave the game permanently unlimited.
    if (pid != 0 && !g_shm_box.valid()) {
        g_shm_box.open(pid, true);
    }

    if (g_shm_box.valid()) {
        // Write limiting state directly to the game's Local\ shm. The service
        // cannot (it lives in session 0), so the UI does it here.
        g_shm_box.shm->ctl.state =
            (g_target_fps > 0.0) ? pacer::PacerState_Limited : pacer::PacerState_Unlimited;
        pacer::ctl_write_double(&g_shm_box.shm->ctl.target_fps_bits, g_target_fps);
        g_shm_box.shm->ctl.mode = g_settings_mode;
        pacer::ctl_write_double(&g_shm_box.shm->ctl.delay_bias_bits, g_delay_bias);

        g_current_api = g_shm_box.shm->ctl.api;
        g_current_mode = g_shm_box.shm->ctl.mode;
        double freq = (double)g_shm_box.shm->ctl.qpc_frequency;
        if (freq <= 0.0) freq = 10.0e6;

        LONG idx = g_shm_box.shm->write_idx;
        if (idx > 0) {
            std::uint64_t ticks = g_shm_box.shm->ring[(std::uint32_t)(idx - 1) & (pacer::kRingCapacity - 1)];
            g_live_ms = (double)ticks / freq * 1000.0;
            g_live_fps = (g_live_ms > 0.001) ? (1000.0 / g_live_ms) : 0.0;
        }

        compute_pacing_lows(&g_low1_fps, &g_p99_fps);
    }

    // Gate redraws: only repaint when displayed data actually changed.
    wchar_t s1[160], s2[160], s3[160];
    build_status_lines(s1, _countof(s1), s2, _countof(s2), s3, _countof(s3));
    if (s1 != g_status_prev1 || s2 != g_status_prev2 || s3 != g_status_prev3) {
        g_status_prev1 = s1;
        g_status_prev2 = s2;
        g_status_prev3 = s3;
        InvalidateRect(g_main, nullptr, FALSE);
    }

    if (g_graph_wnd) {
        LONG gidx = g_shm_box.valid() ? (LONG)g_shm_box.shm->write_idx : -1;
        if (gidx != g_graph_prev_idx || g_shm_box.valid() != g_graph_prev_valid) {
            g_graph_prev_idx = gidx;
            g_graph_prev_valid = g_shm_box.valid();
            InvalidateRect(g_graph_wnd, nullptr, FALSE);
        }
    }
}

// Compute 1% Low (avg of worst 1% frames) and 99th-percentile FPS over the
// recent frametime ring so the UI can surface real-world consistency.
void compute_pacing_lows(double* out_1low, double* out_p99) {
    *out_1low = -1.0;
    *out_p99 = -1.0;
    if (!g_shm_box.valid()) return;
    double freq = (double)g_shm_box.shm->ctl.qpc_frequency;
    if (freq <= 0.0) freq = 10.0e6;

    LONG idx = g_shm_box.shm->write_idx;
    int avail = (int)(idx > (LONG)pacer::kRingCapacity ? (LONG)pacer::kRingCapacity : idx);
    if (avail < 20) return;

    std::vector<double> fps;
    fps.reserve(avail);
    for (int i = 0; i < avail; ++i) {
        LONG ri = idx - avail + i;
        std::uint64_t ticks = g_shm_box.shm->ring[(std::uint32_t)ri & (pacer::kRingCapacity - 1)];
        double ms = (double)ticks / freq * 1000.0;
        if (ms > 0.001) fps.push_back(1000.0 / ms);
    }
    if (fps.empty()) return;

    std::sort(fps.begin(), fps.end());  // ascending: worst frames first
    int n = (int)fps.size();
    int k = (std::max)(1, (int)(0.01 * n));  // worst 1% bucket
    double s = 0.0;
    for (int i = 0; i < k; ++i) s += fps[i];
    *out_1low = s / (double)k;
    int p = (std::min)(n - 1, (int)(0.01 * (n - 1)));  // 99% of frames are >= this
    *out_p99 = fps[p];
}

void build_status_lines(wchar_t* l1, size_t l1n, wchar_t* l2, size_t l2n, wchar_t* l3, size_t l3n) {
    if (g_current_pid != 0) {
        swprintf_s(l1, l1n, L"%s: %s",
                   (g_target_fps > 0.0) ? L"Limiting" : L"Observing",
                   g_current_exe.c_str());
    } else {
        swprintf_s(l1, l1n, L"Observing (no game presenting)");
    }
    if (g_current_pid != 0 && g_shm_box.valid()) {
        swprintf_s(l2, l2n, L"FPS: %.0f  |  %.2f ms  |  %s  |  %s",
                    g_live_fps, g_live_ms, get_mode_string(g_current_mode),
                    get_api_string(g_current_api, g_is_64bit));
    } else {
        swprintf_s(l2, l2n, L"FPS: --  |  -- ms  |  %s  |  Auto-Detect",
                    get_mode_string(g_current_mode));
    }

    if (g_current_pid != 0 && g_shm_box.valid() && g_low1_fps > 0.0 && g_p99_fps > 0.0) {
        swprintf_s(l3, l3n, L"1%% Low: %.0f FPS    |    99th: %.0f FPS",
                    g_low1_fps, g_p99_fps);
    } else {
        swprintf_s(l3, l3n, L"1%% Low: --    |    99th: --");
    }
}

void paint_main_window(HWND hwnd, HDC hdc) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left;
    int H = rc.bottom - rc.top;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // Deep Charcoal Background fill (#0F0F11)
    HBRUSH bgBrush = CreateSolidBrush(ui::colors::kBgWindow);
    FillRect(memDC, &rc, bgBrush);
    DeleteObject(bgBrush);

    auto& fm = ui::FontManager::instance();

    // 1. Draw Top Toolbar Buttons
    ui::draw_icon_button(memDC, g_rc_menu, 1, g_hover_menu, false);
    ui::draw_icon_button(memDC, g_rc_apps, 2, g_hover_apps, false);
    ui::draw_icon_button(memDC, g_rc_pin, 3, g_hover_pin, g_is_pinned);

    // FPS Edit Container Box (behind edit control: x=138, y=12, w=68, h=34)
    RECT editBoxRc{138, 12, 206, 46};
    ui::draw_rounded_box(memDC, editBoxRc, 6, ui::colors::kBgCard, ui::colors::kBorderCard, 1);

    // Apply Button
    ui::draw_text_button(memDC, g_rc_apply, L"Apply", g_hover_apply, g_press_apply, fm.bold_font());

    // 2. Draw Status Text
    SetBkMode(memDC, TRANSPARENT);

    // Line 1: Limiting / Observing
    HFONT oldFont = (HFONT)SelectObject(memDC, fm.bold_font());
    SetTextColor(memDC, ui::colors::kTextPrimary);

    wchar_t statusLine1[160];
    wchar_t statusLine2[160];
    wchar_t statusLine3[160];
    build_status_lines(statusLine1, _countof(statusLine1), statusLine2, _countof(statusLine2),
                        statusLine3, _countof(statusLine3));

    RECT line1Rc{16, 56, W - 16, 76};
    DrawTextW(memDC, statusLine1, -1, &line1Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Line 2: FPS | Time | API (Neon Cyan text: #38BDF8)
    SelectObject(memDC, fm.stats_font());
    SetTextColor(memDC, ui::colors::kTextStats);

    RECT line2Rc{16, 80, W - 16, 100};
    DrawTextW(memDC, statusLine2, -1, &line2Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Line 3: 1% Low | 99th percentile FPS
    RECT line3Rc{16, 104, W - 16, 124};
    DrawTextW(memDC, statusLine3, -1, &line3Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(memDC, oldFont);

    // Blit to screen
    BitBlt(hdc, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

void main_close() {
    // Eject every injected PacerCore instance so nothing stays hooked/limited
    // after the UI exits.
    {
        wchar_t eject_exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, eject_exe, MAX_PATH);
        wchar_t* s = wcsrchr(eject_exe, L'\\');
        if (s) *(s + 1) = L'\0';
        std::wstring eject_path = std::wstring(eject_exe) + L"pacer-eject.exe";
        if (GetFileAttributesW(eject_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            std::wstring cmd = L"\"" + eject_path + L"\" --all";
            std::vector<wchar_t> buf(cmd.begin(), cmd.end());
            buf.push_back(L'\0');
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                               DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                if (pi.hProcess) CloseHandle(pi.hProcess);
                if (pi.hThread) CloseHandle(pi.hThread);
            }
        }
    }
    tray_remove();
    svc::stop();
    PostQuitMessage(0);
}

LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            ui_tick();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            paint_main_window(hwnd, hdc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);

            POINT pt{x, y};
            bool hm = PtInRect(&g_rc_menu, pt);
            bool ha = PtInRect(&g_rc_apps, pt);
            bool hp = PtInRect(&g_rc_pin, pt);
            bool h_apply = PtInRect(&g_rc_apply, pt);

            if (hm != g_hover_menu || ha != g_hover_apps || hp != g_hover_pin || h_apply != g_hover_apply) {
                g_hover_menu = hm;
                g_hover_apps = ha;
                g_hover_pin = hp;
                g_hover_apply = h_apply;
                InvalidateRect(hwnd, nullptr, FALSE);

                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_hover_menu = g_hover_apps = g_hover_pin = g_hover_apply = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lp);
            int y = GET_Y_LPARAM(lp);
            POINT pt{x, y};

            if (PtInRect(&g_rc_menu, pt)) {
                show_hamburger_menu(hwnd, g_rc_menu.left, g_rc_menu.bottom + 4);
                return 0;
            }
            if (PtInRect(&g_rc_apps, pt)) {
                show_apps_menu(hwnd, g_rc_apps.left, g_rc_apps.bottom + 4);
                return 0;
            }
            if (PtInRect(&g_rc_pin, pt)) {
                toggle_pin();
                return 0;
            }
            if (PtInRect(&g_rc_apply, pt)) {
                g_press_apply = true;
                apply_from_edit();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        }

        case WM_LBUTTONUP:
            if (g_press_apply) {
                g_press_apply = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC hdcEdit = (HDC)wp;
            SetBkColor(hdcEdit, ui::colors::kBgCard);
            SetTextColor(hdcEdit, ui::colors::kTextPrimary);
            static HBRUSH s_editBrush = CreateSolidBrush(ui::colors::kBgCard);
            return (LRESULT)s_editBrush;
        }

        case WM_SIZE:
            if (wp == SIZE_MINIMIZED && g_mintray) {
                ShowWindow(hwnd, SW_HIDE);
                g_hidden = true;
            }
            return 0;

        case WM_APP + 7:  // System tray event
            if (LOWORD(lp) == WM_LBUTTONUP) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                g_hidden = false;
            } else if (LOWORD(lp) == WM_RBUTTONUP) {
                POINT p;
                GetCursorPos(&p);
                HMENU m = CreatePopupMenu();
                wchar_t statusLine[128];
                if (g_current_pid != 0) {
                    swprintf_s(statusLine, L"Framepacer: %s", g_current_exe.c_str());
                } else {
                    swprintf_s(statusLine, L"Framepacer — Idle");
                }
                AppendMenuW(m, MF_STRING | MF_GRAYED, 0, statusLine);
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, 1060, L"60 FPS");
                AppendMenuW(m, MF_STRING, 1120, L"120 FPS");
                AppendMenuW(m, MF_STRING, 1144, L"144 FPS");
                AppendMenuW(m, MF_STRING, 1000, L"Uncapped");
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(m, MF_STRING, 2001, L"Show Framepacer");
                AppendMenuW(m, MF_STRING, 2002, L"Exit");

                SetForegroundWindow(hwnd);
                int cmd = TrackPopupMenu(m, TPM_RETURNCMD, p.x, p.y, 0, hwnd, nullptr);
                DestroyMenu(m);

                if (cmd == 1060) apply_fps(60.0);
                else if (cmd == 1120) apply_fps(120.0);
                else if (cmd == 1144) apply_fps(144.0);
                else if (cmd == 1000) apply_fps(0.0);
                else if (cmd == 2001) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); g_hidden = false; }
                else if (cmd == 2002) SendMessageW(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            main_close();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
    // Enable Per-Monitor DPI Awareness v2
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    {  // Single instance check
        HANDLE m = CreateMutexW(nullptr, TRUE, L"Local\\Framepacer.UI.Single");
        if (m && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND other = FindWindowW(L"FramepacerMain", nullptr);
            if (other) {
                ShowWindow(other, SW_RESTORE);
                SetForegroundWindow(other);
            }
            return 0;
        }
    }

    InitializeCriticalSection(&g_log_cs);
    svc::enable_debug_privilege();
    ui::init_theme();
    profiles_load();

    // Register Frametime Graph Custom Class
    WNDCLASSEXW wcg{sizeof(wcg)};
    wcg.lpfnWndProc = ui::graph_control_proc;
    wcg.hInstance = hinst;
    wcg.lpszClassName = L"FramepacerGraph";
    wcg.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wcg);

    // Register Main Window Class
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = main_proc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"FramepacerMain";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // Calculate window rect to match client size exactly
    RECT wr{0, 0, kBaseW, kBaseH};
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_APPWINDOW);

    g_main = CreateWindowExW(WS_EX_APPWINDOW, L"FramepacerMain",
                             L"Framepacer",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, hinst, nullptr);
    if (!g_main) return 1;

    // Enable Windows 11 Dark Mode Title Bar
    ui::enable_dark_mode(g_main);

    auto& fm = ui::FontManager::instance();

    // --- 1. Target FPS Edit Box (x=142, y=17, w=60, h=24) ---
    g_fps_edit = CreateWindowExW(0, L"EDIT", L"60",
                                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                142, 18, 60, 22, g_main, nullptr, hinst, nullptr);
    SendMessageW(g_fps_edit, WM_SETFONT, (WPARAM)fm.num_font(), TRUE);

    // Subclass Edit Control for mouse wheel and Enter/Arrow key support
    g_orig_edit_proc = (WNDPROC)SetWindowLongPtrW(g_fps_edit, GWLP_WNDPROC, (LONG_PTR)edit_subclass_proc);

    // --- 2. Live Frametime Graph (x=14, y=132, w=340, h=124) ---
    g_graph_wnd = CreateWindowExW(0, L"FramepacerGraph", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                                   14, 132, kBaseW - 28, 124, g_main, nullptr, hinst, nullptr);

    // Wire up the privileged injection service. The interactive UI owns
    // detection (window enumeration) and feeds candidates over the shared
    // control channel; the LocalSystem service performs the injection.
    svc::control_open();
    g_using_service = ensure_service();
    if (g_using_service) {
        // Service runs the watchdog (privileged injection); UI only detects.
        svc::start_detection();
    } else {
        // Fallback: run the watchdog in-process (admin context, limited to
        // unprotected games).
        svc::Options opt;
        opt.fps = 60.0;
        opt.scan_ms = 200;
        opt.mode = pacer::PacerMode_VrrLive;
        svc::start(opt);
        svc::start_detection();
    }
    g_settings_mode = pacer::PacerMode_VrrLive; svc::set_mode(g_settings_mode);
    svc::set_delay_bias(g_delay_bias);  // 0.0 = Max Smooth
    tray_add();

    ShowWindow(g_main, SW_SHOW);
    SetTimer(g_main, 1, 16, nullptr);  // ~60 Hz UI refresh

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    svc::stop_detection();
    if (!g_using_service) svc::stop();
    ui::cleanup_theme();
    DeleteCriticalSection(&g_log_cs);
    return 0;
}
