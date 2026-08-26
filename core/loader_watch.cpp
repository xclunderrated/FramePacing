#include "loader_watch.h"

#include <windows.h>
#include <winternl.h>

#include "hook_ddraw.h"
#include "hook_dx9.h"
#include "hook_exports.h"
#include "log.h"

namespace pacer {

namespace {

typedef struct PACER_LDR_DLL_NOTIFICATION_DATA {
    ULONG Flags;
    PCUNICODE_STRING FullDllName;
    PCUNICODE_STRING BaseDllName;
    PVOID DllBase;
    ULONG SizeOfCode;
} PACLDR_DATA, *PPACLDR_DATA;

constexpr ULONG LDR_DLL_NOTIFICATION_REASON_LOADED = 1;

using FDllNotify = NTSTATUS(NTAPI*)(ULONG flags, void* cb, void* ctx, void** cookie);

volatile LONG g_pending_vulkan = 0;
volatile LONG g_pending_opengl = 0;
volatile LONG g_pending_dx9 = 0;
volatile LONG g_pending_ddraw = 0;

// Loader-lock context: keep it allocation-free and CRT-free.
bool base_name_is(PCUNICODE_STRING s, const wchar_t* lit) {
    if (!s || !s->Buffer) return false;
    size_t want = wcslen(lit);
    USHORT have_chars = s->Length / sizeof(wchar_t);
    if (have_chars != want) return false;
    for (USHORT i = 0; i < have_chars; ++i) {
        wchar_t a = s->Buffer[i];
        wchar_t b = lit[i];
        if (a >= L'A' && a <= L'Z') a |= 0x20;
        if (b >= L'A' && b <= L'Z') b |= 0x20;
        if (a != b) return false;
    }
    return true;
}

void CALLBACK on_dll_notify(ULONG reason, PPACLDR_DATA data, PVOID) {
    if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED) return;
    if (base_name_is(data->BaseDllName, L"opengl32.dll"))
        InterlockedExchange(&g_pending_opengl, 1);
    else if (base_name_is(data->BaseDllName, L"vulkan-1.dll"))
        InterlockedExchange(&g_pending_vulkan, 1);
    else if (base_name_is(data->BaseDllName, L"d3d9.dll"))
        InterlockedExchange(&g_pending_dx9, 1);
    else if (base_name_is(data->BaseDllName, L"ddraw.dll"))
        InterlockedExchange(&g_pending_ddraw, 1);
}

void* g_cookie = nullptr;

}  // namespace

bool loader_watch_install() {
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    auto reg = reinterpret_cast<FDllNotify>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!reg) return false;
    NTSTATUS st = reg(0, reinterpret_cast<void*>(&on_dll_notify), nullptr, &g_cookie);
    if (st != 0) {
        PLOG("LdrRegisterDllNotification failed: 0x%08lx", (unsigned long)st);
        return false;
    }
    PLOG("loader notifications armed");
    return true;
}

void loader_watch_poll() {
    if (InterlockedExchange(&g_pending_vulkan, 0)) hook_exports_arm_vulkan();
    if (InterlockedExchange(&g_pending_opengl, 0)) hook_exports_arm_opengl();
    if (InterlockedExchange(&g_pending_dx9, 0)) hook_dx9_install();
    if (InterlockedExchange(&g_pending_ddraw, 0)) hook_ddraw_install();
}

}  // namespace pacer
