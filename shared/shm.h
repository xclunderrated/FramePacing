// Pacer shared-memory specification (single source of truth).
// Producer: PacerCore.dll (inside the game process).
// Consumers: tools/pacer-stats.exe (M0), PacerUI.exe (M3).
//
// Layout: one control block followed by a power-of-two ring of per-frame
// QPC intervals. Single writer (the game's render thread), multiple readers.
// 8-byte aligned stores are atomic on x86/x64; control doubles go through
// InterlockedExchange64 helpers below.
#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

namespace pacer {

inline constexpr wchar_t      kShmNamePrefix[] = L"Local\\Pacer.SHM.";
inline constexpr std::uint32_t kShmMagic    = 0x50414352u;  // 'PACR'
inline constexpr std::uint32_t kShmVersion  = 1;
inline constexpr std::uint32_t kRingCapacity = 4096;        // must be power of two

enum PacerState : std::uint32_t {
    PacerState_Unlimited = 0,
    PacerState_Limited   = 1,
};

enum PacerMode : std::uint32_t {
    PacerMode_DisplayLocked = 0,  // default: VBI phase-locked loop on measured refresh
    PacerMode_Async         = 1,  // RTSS-style post-present wait (compat fallback)
    PacerMode_VrrLive       = 2,
    PacerMode_LatencyFirst  = 3,
};

enum PacerApi : std::uint32_t {
    PacerApi_Unknown = 0,
    PacerApi_Dxgi    = 1,
    PacerApi_Vulkan  = 2,
    PacerApi_OpenGL  = 3,
    PacerApi_D3D9    = 4,
    PacerApi_DDraw   = 5,
};

struct ControlBlock {
    std::uint32_t magic;
    std::uint32_t version;
    volatile std::uint32_t state;      // PacerState
    volatile std::uint32_t mode;       // PacerMode
    volatile std::uint32_t api;        // PacerApi (set when a primary swapchain is found)
    std::uint32_t pid;
    std::uint64_t qpc_frequency;

    volatile std::uint64_t target_fps_bits;           // double, written by UI/tools
    volatile std::uint64_t delay_bias_bits;           // double, Latency-First split (0=front-heavy/tear-stable .. 1=back-heavy/lowest latency)
    volatile std::uint64_t measured_refresh_hz_bits;  // double, written by core
    volatile std::uint64_t last_vbi_qpc;              // display clock sample
    volatile std::uint64_t pll_phase_us_bits;         // double, signed steering error

    // Hygiene (M4): when set to 1, the core unhooks and frees itself.
    volatile std::uint32_t exit_requested;
    std::uint32_t _reserved3;
};

struct SharedMemLayout {
    ControlBlock ctl;
    alignas(64) volatile LONG write_idx;
    std::uint64_t ring[kRingCapacity];  // per-frame QPC interval deltas
};

inline std::wstring shm_name(std::uint32_t pid) {
    return std::wstring(kShmNamePrefix) + std::to_wstring(pid);
}

// ---- atomic double helpers over the volatile 64-bit fields ----
inline void ctl_write_double(volatile std::uint64_t* field, double value) {
    std::int64_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    memcpy(&bits, &value, sizeof(bits));
    InterlockedExchange64(reinterpret_cast<volatile LONGLONG*>(field), bits);
}
inline double ctl_read_double(volatile std::uint64_t* field) {
    // NOTE: must be a plain load -- Interlocked* ops WRITE the destination and
    // would fault on a read-only (FILE_MAP_READ) mapped view. An aligned
    // 8-byte load is atomic on x86/x64.
    std::int64_t bits = static_cast<std::int64_t>(*field);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

}  // namespace pacer
