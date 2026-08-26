#include "service_core.h"

#include <tlhelp32.h>
#include <wtsapi32.h>
#include <cstdio>
#include <cwchar>
#include <map>
#include <set>
#include <vector>

#pragma comment(lib, "wtsapi32.lib")

#include "shared/shm.h"
#include "shared/shm_client.h"

namespace svc {

// ---- cross-session control channel (UI <-> privileged service) ----
// Mapped as a single named file mapping using the Global\ namespace so a
// session-0 LocalSystem service and the interactive UI share the same memory.
// Locking uses a trivial cross-process spinlock (Interlocked CAS) because
// SRWLOCK is not guaranteed safe across processes.
struct ControlShm {
    volatile LONG spin;
    volatile std::uint64_t target_fps_bits;
    volatile std::uint64_t delay_bias_bits;
    volatile LONG mode_val;
    volatile LONG candidate_count;
    DWORD candidates[64];
    volatile LONG foreground_pid;
    volatile LONG force_pid;
    wchar_t force_exe[64];
    volatile LONG elected_pid;
    wchar_t elected_exe[64];
    volatile LONG log_head;
    volatile LONG log_tail;
    wchar_t log_lines[128][256];
};

static const wchar_t* kControlName = L"Global\\Pacer.ControlShm";
static HANDLE g_ctl_map = nullptr;
static ControlShm* g_ctl = nullptr;

static inline bool ctl_try_lock(volatile LONG* l) {
    return InterlockedCompareExchange(l, 1, 0) == 0;
}
static inline void ctl_lock(volatile LONG* l) {
    while (!ctl_try_lock(l)) SwitchToThread();
}
static inline void ctl_unlock(volatile LONG* l) { InterlockedExchange(l, 0); }

// Allow all local processes (notably the LocalSystem service, which lives in
// session 0) to open the cross-session control channel. A NULL DACL grants
// world access; acceptable for a local IPC object carrying only pids + config.
static SECURITY_ATTRIBUTES* permissive_sa() {
    static SECURITY_DESCRIPTOR sd;
    static SECURITY_ATTRIBUTES sa{sizeof(sa)};
    if (InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE)) {
        sa.lpSecurityDescriptor = &sd;
    }
    return &sa;
}

bool control_open() {
    if (g_ctl) return true;
    g_ctl_map =
        CreateFileMappingW(INVALID_HANDLE_VALUE, permissive_sa(), PAGE_READWRITE, 0,
                           sizeof(ControlShm), kControlName);
    if (!g_ctl_map) return false;
    bool created = (GetLastError() == ERROR_ALREADY_EXISTS);
    g_ctl = static_cast<ControlShm*>(
        MapViewOfFile(g_ctl_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ControlShm)));
    if (!g_ctl) {
        CloseHandle(g_ctl_map);
        g_ctl_map = nullptr;
        return false;
    }
    if (!created) {
        // First creator initializes the layout.
        memset(g_ctl, 0, sizeof(ControlShm));
        InterlockedExchange(&g_ctl->spin, 0);
    }
    return true;
}

void control_close() {
    if (g_ctl) UnmapViewOfFile(g_ctl);
    if (g_ctl_map) CloseHandle(g_ctl_map);
    g_ctl = nullptr;
    g_ctl_map = nullptr;
}

void control_set_config(double fps, std::uint32_t mode, double bias) {
    if (!g_ctl) return;
    pacer::ctl_write_double(&g_ctl->target_fps_bits, fps);
    pacer::ctl_write_double(&g_ctl->delay_bias_bits, bias);
    InterlockedExchange(&g_ctl->mode_val, (LONG)mode);
}

void control_set_candidates(const std::vector<DWORD>& pids) {
    if (!g_ctl) return;
    ctl_lock(&g_ctl->spin);
    DWORD n = (DWORD)pids.size();
    if (n > 64) n = 64;
    g_ctl->candidate_count = (LONG)n;
    for (DWORD i = 0; i < n; ++i) g_ctl->candidates[i] = pids[i];
    ctl_unlock(&g_ctl->spin);
}

void control_set_foreground(DWORD pid) {
    if (!g_ctl) return;
    InterlockedExchange(&g_ctl->foreground_pid, (LONG)pid);
}

void control_set_force(DWORD pid, const std::wstring& exe) {
    if (!g_ctl) return;
    ctl_lock(&g_ctl->spin);
    g_ctl->force_pid = (LONG)pid;
    memset(g_ctl->force_exe, 0, sizeof(g_ctl->force_exe));
    for (size_t i = 0; i < exe.size() && i < 63; ++i) g_ctl->force_exe[i] = exe[i];
    ctl_unlock(&g_ctl->spin);
}

void control_set_election(DWORD pid, const std::wstring& exe) {
    if (!g_ctl) return;
    ctl_lock(&g_ctl->spin);
    g_ctl->elected_pid = (LONG)pid;
    memset(g_ctl->elected_exe, 0, sizeof(g_ctl->elected_exe));
    for (size_t i = 0; i < exe.size() && i < 63; ++i) g_ctl->elected_exe[i] = exe[i];
    ctl_unlock(&g_ctl->spin);
}

DWORD control_get_election_pid() {
    if (!g_ctl) return 0;
    return (DWORD)InterlockedExchangeAdd(&g_ctl->elected_pid, 0);
}

std::wstring control_get_election_exe() {
    std::wstring out;
    if (!g_ctl) return out;
    ctl_lock(&g_ctl->spin);
    out = g_ctl->elected_exe;
    ctl_unlock(&g_ctl->spin);
    return out;
}

void control_log(const std::wstring& line) {
    if (!g_ctl) return;
    ctl_lock(&g_ctl->spin);
    size_t n = line.size();
    if (n > 255) n = 255;
    wchar_t* dst = g_ctl->log_lines[g_ctl->log_head & 127];
    for (size_t i = 0; i < n; ++i) dst[i] = line[i];
    dst[n] = L'\0';
    g_ctl->log_head = (g_ctl->log_head + 1);
    if (g_ctl->log_head - g_ctl->log_tail > 128) g_ctl->log_tail = g_ctl->log_head - 128;
    ctl_unlock(&g_ctl->spin);
}

void control_log_drain(void (*cb)(const std::wstring&)) {
    if (!g_ctl || !cb) return;
    std::vector<std::wstring> batch;
    ctl_lock(&g_ctl->spin);
    while (g_ctl->log_tail != g_ctl->log_head) {
        batch.emplace_back(g_ctl->log_lines[g_ctl->log_tail & 127]);
        g_ctl->log_tail = (g_ctl->log_tail + 1);
    }
    ctl_unlock(&g_ctl->spin);
    for (auto& l : batch) cb(l);
}

namespace {

// In-process anti-cheat module markers. WARN-ONLY, never skip (user policy).
const wchar_t* kAntiCheatModules[] = {
    L"easyanticheat.dll", L"easyanticheat_eos.dll", L"beservice.dll",
    L"vgc.dll", L"faceit.dll", L"eac_launcher.dll",
};

// ---- shared state ----
Options g_opt;
DWORD g_scan_ms = 200;
HANDLE g_thread = nullptr;
HANDLE g_stop_event = nullptr;
std::function<void(const std::wstring&)> g_sink;
SRWLOCK g_lock = SRWLOCK_INIT;

// Pids we've already logged an injection failure for (avoid per-scan spam).
std::set<DWORD> g_inject_fail_logged;
// Next-loop retry gate for failed pids (throttles helper invocations).
std::map<DWORD, DWORD> g_inject_retry;

// Detection thread (interactive session only) state.
HANDLE g_det_thread = nullptr;
HANDLE g_stop_det = nullptr;
HANDLE g_fg_event_det = nullptr;
HWINEVENTHOOK g_fg_hook_det = nullptr;

void CALLBACK on_foreground_change(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (g_fg_event_det) SetEvent(g_fg_event_det);
}

void emit(std::wstring line) {
    // Forward to the UI via the shared log ring (works even from the
    // session-0 service, where there is no console to print to).
    control_log(line);

    AcquireSRWLockExclusive(&g_lock);
    auto sink = g_sink;
    ReleaseSRWLockExclusive(&g_lock);
    if (sink) {
        sink(line);
    } else {
        wprintf(L"%s\n", line.c_str());
    }

    char path_a[MAX_PATH]{};
    GetTempPathA(MAX_PATH, path_a);
    strcat_s(path_a, "pacer-service.log");
    FILE* f = nullptr;
    fopen_s(&f, path_a, "a");
    if (f) {
        fprintf(f, "%ls\n", line.c_str());
        fclose(f);
    }
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

bool enum_modules(DWORD pid, std::set<std::wstring>& out) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE32, pid);
    }
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{sizeof(me)};
    if (Module32FirstW(snap, &me)) {
        do {
            const wchar_t* base = wcsrchr(me.szModule, L'\\');
            out.insert(lower(base ? base + 1 : me.szModule));
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return true;
}

void warn_if_anticheat(DWORD pid, const std::set<std::wstring>& mods) {
    for (auto* m : kAntiCheatModules)
        if (mods.count(m)) {
            wchar_t buf[256];
            swprintf_s(buf,
                       L"[warn] pid %lu has anti-cheat module %ls. Limiting anyway "
                       L"(RTSS-style); unknown injectors may trip EAC/BE/Vanguard.",
                       pid, m);
            emit(buf);
            return;
        }
}

// Game-like window signal: the process owns a visible, non-trivial top-level
// window. We deliberately do NOT gate on a title (many games/launchers — e.g.
// RDR2 — present a visible window with an empty/late title during boot) or on a
// graphics DLL (that silently drops titles whose renderer module isn't in our
// list, or that haven't created their device yet). Observe-mode injection plus
// the core's dormancy self-eject safely handle anything that isn't really a
// game (M2 safety model).
bool game_like_window(DWORD pid) {
    struct Ctx {
        DWORD pid;
        bool found;
    } ctx{pid, false};

    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD wp = 0;
            GetWindowThreadProcessId(hwnd, &wp);
            if (wp != c->pid) return TRUE;

            LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
            LONG style = GetWindowLongW(hwnd, GWL_STYLE);
            if ((style & WS_VISIBLE) == 0) return TRUE;
            if (ex & WS_EX_TOOLWINDOW) return TRUE;

            RECT rc{};
            if (!GetWindowRect(hwnd, &rc)) return TRUE;
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            // Ignore tiny / minimized-to-tray style helper windows.
            if (w < 200 || h < 150) return TRUE;

            c->found = true;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    return ctx.found;
}

bool inject_x64(DWORD pid, const wchar_t* dll_path) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) {
        h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    }
    if (!h) return false;
    SIZE_T bytes = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote =
        VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    bool ok = false;
    if (remote && WriteProcessMemory(h, remote, dll_path, bytes, nullptr)) {
        auto load_lib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        HANDLE th = CreateRemoteThread(h, nullptr, 0, load_lib, remote, 0, nullptr);
        if (th) {
            WaitForSingleObject(th, 15000);
            DWORD code = 0;
            GetExitCodeThread(th, &code);
            ok = code != 0;
            CloseHandle(th);
        }
    }
    if (remote) VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(h);
    return ok;
}

bool is_x64(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return true;  // Default to 64-bit native on x64 OS
    BOOL wow64 = FALSE;
    IsWow64Process(h, &wow64);
    CloseHandle(h);
    return wow64 == FALSE;
}

std::wstring exe_of_pid(DWORD pid) {
    std::wstring out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                const wchar_t* base = wcsrchr(pe.szExeFile, L'\\');
                out = lower(base ? base + 1 : pe.szExeFile);
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

std::wstring ansi_to_wstring(const char* s, int n) {
    int wn = MultiByteToWideChar(CP_ACP, 0, s, n, nullptr, 0);
    if (wn <= 0) return {};
    std::wstring w(wn, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, n, &w[0], wn);
    return w;
}

// Spawn the robust pacer-inject.exe helper (resolves x64 vs WOW64, prints a
// precise GetLastError reason) and scrape its output into the service log.
// Falls back to the in-process injector if the helper is missing.
bool run_injector_helper(DWORD pid, const wchar_t* dir) {
    std::wstring helper = std::wstring(dir) + L"pacer-inject.exe";
    if (GetFileAttributesW(helper.c_str()) == INVALID_FILE_ATTRIBUTES)
        return inject_x64(pid, (std::wstring(dir) + L"PacerCore.dll").c_str());

    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return inject_x64(pid, (std::wstring(dir) + L"PacerCore.dll").c_str());

    std::wstring cmd = L"\"" + helper + L"\" " + std::to_wstring(pid);
    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return inject_x64(pid, (std::wstring(dir) + L"PacerCore.dll").c_str());
    }
    CloseHandle(hWrite);

    std::string acc;
    char tmp[512];
    DWORD rd;
    while (ReadFile(hRead, tmp, sizeof(tmp) - 1, &rd, nullptr) && rd > 0) {
        acc.append(tmp, rd);
    }
    CloseHandle(hRead);

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0 && !acc.empty()) {
        std::string line;
        for (char c : acc) {
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) emit(ansi_to_wstring(line.c_str(), (int)line.size()));
                line.clear();
            } else if (c != '\r') {
                line.push_back(c);
            }
        }
        if (!line.empty()) emit(ansi_to_wstring(line.c_str(), (int)line.size()));
    }
    return code == 0;
}

bool inject_candidate(DWORD pid, const wchar_t* dir) {
    // The helper resolves target architecture and prints a precise failure
    // reason; in-process injection is only a last-resort fallback.
    return run_injector_helper(pid, dir);
}

bool process_alive(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    DWORD code = 0;
    bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

void set_election(DWORD pid, const std::wstring& exe) {
    control_set_election(pid, exe);
}

bool is_excluded_system_app(const std::wstring& exe) {
    static const std::set<std::wstring> kExclusions = {
        // Windows Core & Shell
        L"explorer.exe", L"dwm.exe", L"sihost.exe", L"shellhost.exe", L"searchhost.exe",
        L"searchapp.exe", L"searchindexer.exe", L"startmenuexperiencehost.exe",
        L"shellexperiencehost.exe", L"textinputhost.exe", L"ctfmon.exe", L"smartscreen.exe",
        L"taskhostw.exe", L"conhost.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe",
        L"wt.exe", L"windowsterminal.exe", L"openconsole.exe", L"applicationframehost.exe",
        L"systemsettings.exe", L"taskmgr.exe", L"mmc.exe", L"regedit.exe", L"runtimebroker.exe",
        L"fontdrvhost.exe", L"securityhealthservice.exe", L"securityhealthsystray.exe",
        L"lsass.exe", L"csrss.exe", L"smss.exe", L"wininit.exe", L"services.exe", L"svchost.exe",
        L"crossdeviceresume.exe", L"gamebar.exe", L"gamebarftserver.exe", L"gamecompressor.exe",

        // Framepacer Binaries
        L"pacerui.exe", L"pacerui86.exe", L"framepacer.exe", L"pacer-svc.exe",
        L"pacer-inject.exe", L"pacer-inject86.exe", L"pacer-stats.exe", L"pacer-eject.exe",

        // Web Browsers & Electron Runtime
        L"chrome.exe", L"msedge.exe", L"msedgewebview2.exe", L"firefox.exe", L"brave.exe",
        L"opera.exe", L"vivaldi.exe", L"zen.exe", L"waterfox.exe", L"librewolf.exe", L"electron.exe",

        // Store Clients & Launchers
        L"steam.exe", L"steamservice.exe", L"steamwebhelper.exe",
        L"epicgameslauncher.exe", L"epicwebhelper.exe",
        L"galaxyclient.exe", L"galaxyclienthelper.exe",
        L"battlenet.exe", L"battle.net.exe", L"agent.exe",
        L"eadesktop.exe", L"eawebhost.exe", L"eabackgroundservice.exe",
        L"rockstar-games-launcher.exe", L"launcher.exe",
        L"rockstarservice.exe", L"socialclubhelper.exe",
        L"riotclientservices.exe", L"riotclientux.exe",
        L"upc.exe", L"ubisoftconnect.exe", L"ubisoftgamelauncher.exe",

        // Developer Tools & IDEs
        L"devenv.exe", L"code.exe", L"antigravity.exe", L"git.exe", L"msbuild.exe", L"vctip.exe",

        // Driver & Hardware Utilities
        L"radeonsoftware.exe", L"amdrssrcx.exe", L"amdrssrcext.exe", L"amdrssrc.exe",
        L"nvcontainer.exe", L"nvidia share.exe", L"geforceexperience.exe", L"nvspcaps64.exe",
        L"discord.exe", L"spotify.exe", L"obs64.exe", L"obs32.exe", L"rtss.exe", L"rtsshnd.exe",
        L"afterburner.exe"
    };

    if (kExclusions.count(exe) > 0) return true;

    // Filter out common CEF, Chromium, and Crashpad helper child processes
    if (exe.find(L"crashpad") != std::wstring::npos ||
        exe.find(L"cefhost") != std::wstring::npos ||
        exe.find(L"webhelper") != std::wstring::npos ||
        exe.find(L"gameoverlay") != std::wstring::npos) {
        return true;
    }

    return false;
}

struct Tracked {
    std::wstring exe;
    pacer::ShmBox box;
    std::uint32_t applied_state = pacer::PacerState_Unlimited;
    bool has_gfx = false;
};

DWORD WINAPI watchdog_thread(LPVOID param) {
    wchar_t dir[MAX_PATH]{};
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) *(slash + 1) = L'\0';

    std::wstring core_dll = std::wstring(dir) + L"PacerCore.dll";
    if (GetFileAttributesW(core_dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        emit(L"[svc] ERROR: PacerCore.dll not found");
        return 2;
    }
    emit(L"[svc] watching (privileged injection, foreground-sticky election)");

    std::map<DWORD, std::wstring> injected;  // pid -> exe (dedup + reap)
    DWORD active = 0;
    const DWORD self = GetCurrentProcessId();
    DWORD loop = 0;

    auto read_force = [&]() -> DWORD {
        DWORD pid = 0;
        if (g_ctl) {
            ctl_lock(&g_ctl->spin);
            pid = (DWORD)g_ctl->force_pid;
            ctl_unlock(&g_ctl->spin);
        }
        return pid;
    };
    auto read_candidates = [&](std::vector<DWORD>& out) {
        out.clear();
        if (!g_ctl) return;
        ctl_lock(&g_ctl->spin);
        DWORD n = (DWORD)g_ctl->candidate_count;
        if (n > 64) n = 64;
        for (DWORD i = 0; i < n; ++i) out.push_back(g_ctl->candidates[i]);
        ctl_unlock(&g_ctl->spin);
    };
    auto try_inject = [&](DWORD pid, const wchar_t* tag) -> bool {
        if (pid == 0 || pid == self) return false;
        if (injected.count(pid)) return true;
        if (g_inject_fail_logged.count(pid)) {
            auto it = g_inject_retry.find(pid);
            if (it != g_inject_retry.end() && it->second > loop) return false;
        }
        std::wstring fexe = exe_of_pid(pid);
        if (fexe.empty() || is_excluded_system_app(fexe)) return false;
        if (inject_candidate(pid, dir)) {
            wchar_t buf[192];
            swprintf_s(buf, L"[inject] %s (pid %lu) [%s] [observe] %s", fexe.c_str(), pid,
                       is_x64(pid) ? L"x64" : L"x86", tag);
            emit(buf);
            g_inject_fail_logged.erase(pid);
            std::set<std::wstring> mods;
            enum_modules(pid, mods);
            warn_if_anticheat(pid, mods);
            injected.emplace(pid, fexe);
            return true;
        }
        if (!g_inject_fail_logged.count(pid)) {
            wchar_t buf[192];
            swprintf_s(buf,
                       L"[inject-fail] %s (pid %lu) [%s] %s -- injection refused "
                       L"(protected/launcher/access). Will retry.",
                       fexe.c_str(), pid, is_x64(pid) ? L"x64" : L"x86", tag);
            emit(buf);
        }
        g_inject_fail_logged.insert(pid);
        g_inject_retry[pid] = loop + 50;  // ~10s throttle at 200ms
        return false;
    };

    while (WaitForSingleObject(g_stop_event, 0) != WAIT_OBJECT_0) {
        loop++;
        // Detection input arrives from the interactive UI's detection thread
        // via the control channel (the service runs in session 0 and cannot
        // see windows itself).
        DWORD fgpid = g_ctl ? (DWORD)InterlockedExchangeAdd(&g_ctl->foreground_pid, 0) : 0;
        DWORD forced_pid = read_force();
        std::vector<DWORD> cands;
        read_candidates(cands);

        // Inject: broad candidates, the foreground process (click-to-detect),
        // and any explicitly forced target.
        for (DWORD pid : cands) try_inject(pid, L"");
        try_inject(fgpid, L"[fg]");
        if (forced_pid) try_inject(forced_pid, L"[forced]");
        // 1. reap dead processes
        for (auto it = injected.begin(); it != injected.end();) {
            if (!process_alive(it->first)) {
                wchar_t buf[160];
                swprintf_s(buf, L"[svc] %s (pid %lu) exited", it->second.c_str(),
                           it->first);
                emit(buf);
                if (active == it->first) active = 0;
                it = injected.erase(it);
            } else {
                ++it;
            }
        }

        // Per-game shm is owned by the UI (same session as the game); the
        // service only injects + elects. Reaping is handled above.

        // 4. wait for next tick (detection runs in the UI; no WinEventHook here).
        if (WaitForSingleObject(g_stop_event, g_scan_ms) == WAIT_OBJECT_0) break;

        // 5. elect by foreground (sticky), or forced target. fgpid/forced_pid
        //    were read from the control channel at the top of the loop (the
        //    interactive UI's detection thread produces them).
        if (active && !injected.count(active)) active = 0;

        DWORD pick = active;
        if (forced_pid && injected.count(forced_pid)) {
            pick = forced_pid;
        } else if (fgpid && injected.count(fgpid)) {
            pick = fgpid;
        } else if (active && injected.count(active)) {
            pick = active;
        } else if (!active && injected.size() == 1) {
            pick = injected.begin()->first;
        }

        // Per-game state (Limited / fps / mode / bias) is written by the UI,
        // which shares the game's session and can open its Local\ shm. The
        // service (session 0) only injects + elects + reports the elected pid.

        if (pick && pick != active) {
            active = pick;
            set_election(pick, injected[pick]);
            wchar_t buf[160];
            swprintf_s(buf, L"[svc] limiting: %s (pid %lu)", injected[pick].c_str(),
                       pick);
            emit(buf);
        } else if (!pick && active) {
            active = 0;
            set_election(0, L"");
            emit(L"[svc] no game focused -> all unlimited");
        }
    }

    // Graceful release: the UI owns the per-game shm; nothing to undo here.
    emit(L"[svc] stopped");
    return 0;
}

}  // namespace

bool enable_debug_privilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    LUID luid{};
    if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        CloseHandle(token);
        return false;
    }
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(token);
    return ok && (GetLastError() == ERROR_SUCCESS);
}

bool force_target(DWORD pid, const std::wstring& exe) {
    control_set_force(pid, exe);
    return true;
}

bool start(const Options& opt) {
    if (g_thread) return true;
    enable_debug_privilege();
    g_opt = opt;
    g_scan_ms = opt.scan_ms ? opt.scan_ms : 200;
    control_open();  // ensure the control channel exists (UI may have made it)
    g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
    if (!g_thread) {
        CloseHandle(g_stop_event);
        g_stop_event = nullptr;
        return false;
    }
    return true;
}

void stop() {
    if (!g_thread && !g_det_thread) return;
    if (g_stop_event) SetEvent(g_stop_event);
    if (g_stop_det) SetEvent(g_stop_det);
    if (g_fg_event_det) SetEvent(g_fg_event_det);
    if (g_thread) {
        WaitForSingleObject(g_thread, 5000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_det_thread) {
        WaitForSingleObject(g_det_thread, 5000);
        CloseHandle(g_det_thread);
        g_det_thread = nullptr;
    }
    if (g_stop_event) { CloseHandle(g_stop_event); g_stop_event = nullptr; }
    if (g_stop_det) { CloseHandle(g_stop_det); g_stop_det = nullptr; }
    if (g_fg_event_det) { CloseHandle(g_fg_event_det); g_fg_event_det = nullptr; }
    if (g_fg_hook_det) { UnhookWinEvent(g_fg_hook_det); g_fg_hook_det = nullptr; }
    set_election(0, L"");
}

DWORD WINAPI detection_thread(LPVOID) {
    while (WaitForSingleObject(g_stop_det, 0) != WAIT_OBJECT_0) {
        // Foreground process (click-to-detect source). Only meaningful in an
        // interactive session, which is why detection runs in the UI process.
        DWORD fgpid = 0;
        HWND fgh = GetForegroundWindow();
        if (fgh) GetWindowThreadProcessId(fgh, &fgpid);
        control_set_foreground(fgpid);

        // Broad auto-detect: enumerate processes, keep those with a game-like
        // window. EnumWindows only works in the interactive session.
        std::vector<DWORD> cands;
        WTS_PROCESS_INFOW* pinfo = nullptr;
        DWORD count = 0;
        if (WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pinfo, &count)) {
            for (DWORD i = 0; i < count; ++i) {
                DWORD pid = pinfo[i].ProcessId;
                if (pid <= 4) continue;
                if (!pinfo[i].pProcessName) continue;
                std::wstring exe = lower(pinfo[i].pProcessName);
                if (is_excluded_system_app(exe)) continue;
                if (!game_like_window(pid)) continue;
                cands.push_back(pid);
            }
            WTSFreeMemory(pinfo);
        }
        control_set_candidates(cands);

        WaitForSingleObject(g_fg_event_det, g_scan_ms);
        ResetEvent(g_fg_event_det);
    }
    return 0;
}

bool start_detection() {
    if (g_det_thread) return true;
    control_open();
    g_stop_det = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_fg_event_det = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_fg_hook_det = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
                                    nullptr, on_foreground_change, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_det_thread = CreateThread(nullptr, 0, detection_thread, nullptr, 0, nullptr);
    return g_det_thread != nullptr;
}

void stop_detection() {
    if (g_stop_det) SetEvent(g_stop_det);
    if (g_fg_event_det) SetEvent(g_fg_event_det);
    if (g_det_thread) {
        WaitForSingleObject(g_det_thread, 5000);
        CloseHandle(g_det_thread);
        g_det_thread = nullptr;
    }
    if (g_stop_det) { CloseHandle(g_stop_det); g_stop_det = nullptr; }
    if (g_fg_event_det) { CloseHandle(g_fg_event_det); g_fg_event_det = nullptr; }
    if (g_fg_hook_det) { UnhookWinEvent(g_fg_hook_det); g_fg_hook_det = nullptr; }
}

void set_target_fps(double fps) {
    g_opt.fps = fps;
    if (g_ctl) pacer::ctl_write_double(&g_ctl->target_fps_bits, fps);
}

void set_mode(std::uint32_t mode) {
    // Clamp to a valid PacerMode value.
    if (mode > 3) mode = 0;
    g_opt.mode = mode;
    if (g_ctl) InterlockedExchange(&g_ctl->mode_val, (LONG)mode);
}

void set_delay_bias(double bias) {
    if (bias < 0.0) bias = 0.0;
    if (bias > 1.0) bias = 1.0;
    g_opt.delay_bias = bias;
    if (g_ctl) pacer::ctl_write_double(&g_ctl->delay_bias_bits, bias);
}

DWORD elected_pid() {
    return control_get_election_pid();
}

std::wstring elected_exe() {
    return control_get_election_exe();
}

void set_event_sink(std::function<void(const std::wstring&)> sink) {
    AcquireSRWLockExclusive(&g_lock);
    g_sink = std::move(sink);
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace svc
