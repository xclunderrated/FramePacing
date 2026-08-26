// Common hook synchronization and in-flight execution reference counting.
// Guarantees zero crashes during runtime DLL ejection and unhooking.
#pragma once

#include <windows.h>
#include <cstdint>

namespace pacer {

extern volatile LONG g_in_flight_hooks;
extern volatile bool g_hooks_ejecting;

inline bool hooks_is_ejecting() {
    return g_hooks_ejecting;
}

// RAII guard to track in-flight hook executions across all game rendering & message threads.
class HookGuard {
public:
    HookGuard() {
        InterlockedIncrement(&g_in_flight_hooks);
    }
    ~HookGuard() {
        InterlockedDecrement(&g_in_flight_hooks);
    }

    HookGuard(const HookGuard&) = delete;
    HookGuard& operator=(const HookGuard&) = delete;
};

}  // namespace pacer
