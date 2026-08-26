// pacer-eject: cleanup tool to signal injected PacerCore instances to
// unhook, close shared memory, and unload cleanly from target processes.
//
// Usage:
//   pacer-eject <pid>        -- Eject PacerCore from a specific PID
//   pacer-eject --all        -- Scan all running processes and eject PacerCore from all
//   pacer-eject              -- (Same as --all)

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>
#include <vector>

#include "shared/shm.h"

namespace {

bool eject_pid(DWORD pid, const std::wstring& exe_hint = L"") {
    std::wstring name = pacer::shm_name(pid);
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (mapping) {
        auto* shm = static_cast<pacer::SharedMemLayout*>(
            MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(pacer::SharedMemLayout)));
        if (shm) {
            if (shm->ctl.magic == pacer::kShmMagic) {
                wprintf(L"[pacer-eject] Signalling exit to pid %lu%s%s...\n",
                        pid, exe_hint.empty() ? L"" : L" (", exe_hint.empty() ? L"" : (exe_hint + L")").c_str());
                shm->ctl.exit_requested = 1;
            }
            UnmapViewOfFile(shm);
        }
        CloseHandle(mapping);

        // Wait up to 1 second for self-eject
        for (int i = 0; i < 10; ++i) {
            Sleep(100);
            HANDLE check = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
            if (!check) break;
            CloseHandle(check);
        }
    }

    // Wait up to 3 seconds for the target's worker thread to self-eject and close the mapping
    bool ejected = false;
    for (int i = 0; i < 30; ++i) {
        Sleep(100);
        HANDLE check = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
        if (!check) {
            ejected = true;
            break;
        }
        CloseHandle(check);
    }

    if (ejected) {
        wprintf(L"[pacer-eject] pid %lu successfully ejected PacerCore.\n", pid);
    } else {
        wprintf(L"[pacer-eject] pid %lu exit signaled, checking module status...\n", pid);
    }

    // Direct Remote FreeLibrary fallback to guarantee module release
    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION, FALSE, pid);
    if (hp) {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me{sizeof(me)};
            if (Module32FirstW(snap, &me)) {
                do {
                    if (_wcsicmp(me.szModule, L"PacerCore.dll") == 0 ||
                        _wcsicmp(me.szModule, L"PacerCore86.dll") == 0) {
                        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
                        FARPROC pFree = GetProcAddress(k32, "FreeLibrary");
                        if (pFree) {
                            HANDLE t = CreateRemoteThread(hp, nullptr, 0, (LPTHREAD_START_ROUTINE)pFree, me.hModule, 0, nullptr);
                            if (t) {
                                WaitForSingleObject(t, 1000);
                                CloseHandle(t);
                                wprintf(L"[pacer-eject] pid %lu module freed directly.\n", pid);
                            }
                        }
                        break;
                    }
                } while (Module32NextW(snap, &me));
            }
            CloseHandle(snap);
        }
        CloseHandle(hp);
    }

    return true;
}

void eject_all() {
    wprintf(L"[pacer-eject] Scanning system for active PacerCore instances...\n");
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"Failed to create process snapshot: %lu\n", GetLastError());
        return;
    }

    DWORD self_pid = GetCurrentProcessId();
    int found_count = 0;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == 4 || pe.th32ProcessID == self_pid)
                continue;

            if (eject_pid(pe.th32ProcessID, pe.szExeFile)) {
                found_count++;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (found_count == 0) {
        wprintf(L"[pacer-eject] No active PacerCore shared memory mappings found.\n");
    } else {
        wprintf(L"[pacer-eject] Ejection signal sent to %d process(es).\n", found_count);
    }
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2 || wcscmp(argv[1], L"--all") == 0 || wcscmp(argv[1], L"-a") == 0) {
        eject_all();
        return 0;
    }

    if (wcscmp(argv[1], L"--help") == 0 || wcscmp(argv[1], L"-h") == 0) {
        wprintf(L"Usage: pacer-eject [pid | --all]\n");
        return 0;
    }

    DWORD pid = wcstoul(argv[1], nullptr, 10);
    if (pid == 0) {
        fwprintf(stderr, L"Invalid PID: %s\n", argv[1]);
        return 1;
    }

    if (!eject_pid(pid)) {
        fwprintf(stderr, L"[pacer-eject] No active PacerCore shared memory found for pid %lu\n", pid);
        return 2;
    }

    return 0;
}
