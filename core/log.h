// Tiny file logger for the injected DLL. Writes %TEMP%\pacer-core-<pid>.log.
// Dev-only aid; silent if the file cannot be opened.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <windows.h>

namespace pacer {

inline void log_write(const char* fmt, ...) {
    static FILE* f = [] {
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        wchar_t path[MAX_PATH * 2]{};
        swprintf_s(path, L"%spacer-core-%lu.log", temp, GetCurrentProcessId());
        // Shared read so logs can be tail'ed while the game is running.
        return _wfsopen(path, L"w", _SH_DENYNO);
    }();
    if (!f) return;

    static CRITICAL_SECTION cs = [] {
        CRITICAL_SECTION c;
        InitializeCriticalSection(&c);
        return c;
    }();

    EnterCriticalSection(&cs);
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fflush(f);
    LeaveCriticalSection(&cs);
}

}  // namespace pacer

#define PLOG(...) ::pacer::log_write(__VA_ARGS__)
