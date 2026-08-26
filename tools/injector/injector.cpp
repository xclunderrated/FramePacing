// pacer-inject: dev & helper injector supporting both x64 and x86 (WOW64) targets.
// Usage: pacer-inject <pid> [path\to\PacerCore.dll]
//        Default DLL: PacerCore.dll (x64) or PacerCore86.dll (x86) beside the executable.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace {

bool get_exe_dir(wchar_t* out_dir, size_t max_len) {
    if (GetModuleFileNameW(nullptr, out_dir, (DWORD)max_len) == 0) return false;
    wchar_t* slash = wcsrchr(out_dir, L'\\');
    if (!slash) return false;
    *(slash + 1) = L'\0';
    return true;
}

bool run_helper(const std::wstring& helper_exe, DWORD pid, const wchar_t* custom_dll, DWORD* exit_code_out) {
    std::wstring cmd = L"\"" + helper_exe + L"\" " + std::to_wstring(pid);
    if (custom_dll && wcslen(custom_dll) > 0) {
        cmd += L" \"";
        cmd += custom_dll;
        cmd += L"\"";
    }

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(L'\0');

    if (!CreateProcessW(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        return false;
    }

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (exit_code_out) *exit_code_out = code;
    return true;
}

int inject_direct(DWORD pid, const wchar_t* dll_path) {
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h) {
        fwprintf(stderr, L"OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
        return 3;
    }

    SIZE_T bytes = (wcslen(dll_path) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(h, remote, dll_path, bytes, nullptr)) {
        fwprintf(stderr, L"Remote write failed: %lu\n", GetLastError());
        if (remote) VirtualFreeEx(h, remote, 0, MEM_RELEASE);
        CloseHandle(h);
        return 4;
    }

    auto load_lib = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    HANDLE th = CreateRemoteThread(h, nullptr, 0, load_lib, remote, 0, nullptr);
    if (!th) {
        fwprintf(stderr, L"CreateRemoteThread failed: %lu\n", GetLastError());
        VirtualFreeEx(h, remote, 0, MEM_RELEASE);
        CloseHandle(h);
        return 5;
    }
    WaitForSingleObject(th, 15000);

    DWORD exit_code = 0;
    GetExitCodeThread(th, &exit_code);  // LoadLibraryW returns HMODULE
    CloseHandle(th);
    VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(h);

    if (exit_code == 0) {
        fwprintf(stderr, L"LoadLibraryW returned NULL (injection failed)\n");
        return 6;
    }
    wprintf(L"Injected %s into pid %lu (module handle 0x%lx)\n", dll_path, pid, exit_code);
    return 0;
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: pacer-inject <pid> [PacerCore DLL path]\n");
        return 2;
    }
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    if (pid == 0) {
        fwprintf(stderr, L"invalid pid: %s\n", argv[1]);
        return 2;
    }

    // Determine target architecture
    HANDLE h_check = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h_check) {
        fwprintf(stderr, L"Cannot open process %lu for architecture check (err %lu)\n", pid, GetLastError());
        return 3;
    }
    BOOL is_wow64 = FALSE;
    IsWow64Process(h_check, &is_wow64);
    CloseHandle(h_check);

    wchar_t dir[MAX_PATH]{};
    get_exe_dir(dir, MAX_PATH);

#if defined(_WIN64)
    // Running as 64-bit injector
    if (is_wow64) {
        // Target is 32-bit WOW64: delegate to 32-bit helper pacer-inject86.exe
        std::wstring helper = std::wstring(dir) + L"pacer-inject86.exe";
        if (GetFileAttributesW(helper.c_str()) == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"Target pid %lu is 32-bit (x86), but %s was not found.\n", pid, helper.c_str());
            return 7;
        }
        DWORD code = 0;
        const wchar_t* custom = (argc >= 3) ? argv[2] : nullptr;
        if (!run_helper(helper, pid, custom, &code)) {
            fwprintf(stderr, L"Failed to spawn 32-bit helper: %lu\n", GetLastError());
            return 8;
        }
        return (int)code;
    } else {
        // Target is native 64-bit
        wchar_t dll[MAX_PATH]{};
        if (argc >= 3) {
            wcscpy_s(dll, argv[2]);
        } else {
            wcscpy_s(dll, dir);
            wcscat_s(dll, L"PacerCore.dll");
        }
        if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"DLL not found: %s\n", dll);
            return 2;
        }
        return inject_direct(pid, dll);
    }
#else
    // Running as 32-bit injector (pacer-inject86.exe)
    if (!is_wow64) {
        // Target is 64-bit: delegate to 64-bit injector pacer-inject.exe if available
        std::wstring helper = std::wstring(dir) + L"pacer-inject.exe";
        if (GetFileAttributesW(helper.c_str()) != INVALID_FILE_ATTRIBUTES) {
            DWORD code = 0;
            const wchar_t* custom = (argc >= 3) ? argv[2] : nullptr;
            if (run_helper(helper, pid, custom, &code)) {
                return (int)code;
            }
        }
        fwprintf(stderr, L"Cannot inject 32-bit helper into 64-bit process %lu\n", pid);
        return 7;
    } else {
        // Target is 32-bit WOW64
        wchar_t dll[MAX_PATH]{};
        if (argc >= 3) {
            wcscpy_s(dll, argv[2]);
        } else {
            wcscpy_s(dll, dir);
            wcscat_s(dll, L"PacerCore86.dll");
        }
        if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"DLL not found: %s\n", dll);
            return 2;
        }
        return inject_direct(pid, dll);
    }
#endif
}
