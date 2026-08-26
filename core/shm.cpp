#include <algorithm>
#include "shm.h"

#include "log.h"

namespace pacer {

static SharedMemLayout* g_shm = nullptr;
static HANDLE g_mapping = nullptr;

static SECURITY_ATTRIBUTES* permissive_sa() {
    static SECURITY_DESCRIPTOR sd;
    static SECURITY_ATTRIBUTES sa{sizeof(sa)};
    if (InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
        SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE)) {
        sa.lpSecurityDescriptor = &sd;
    }
    return &sa;
}

bool shm_create(std::uint32_t pid) {
    const std::wstring name = shm_name(pid);
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, permissive_sa(), PAGE_READWRITE, 0,
                                   (DWORD)sizeof(SharedMemLayout), name.c_str());
    if (!g_mapping) {
        PLOG("shm: CreateFileMapping failed, err=%lu", GetLastError());
        return false;
    }
    g_shm = static_cast<SharedMemLayout*>(
        MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemLayout)));
    if (!g_shm) {
        PLOG("shm: MapViewOfFile failed, err=%lu", GetLastError());
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return false;
    }
    if (g_shm->ctl.magic != kShmMagic) {
        g_shm->ctl.magic = kShmMagic;
        g_shm->ctl.version = kShmVersion;
        // Fresh injections boot in OBSERVE mode (collect timings, no pacing).
        // The service flips the elected target to Limited; observe-mode cores
        // that never present self-eject (see dllmain.cpp).
        g_shm->ctl.state = PacerState_Unlimited;
        g_shm->ctl.mode = PacerMode_LatencyFirst;  // Latent Sync default: smooth + flat frametime, lower latency
        g_shm->ctl.api = PacerApi_Unknown;
        g_shm->ctl.pid = pid;
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_shm->ctl.qpc_frequency = (std::uint64_t)f.QuadPart;
        ctl_write_double(&g_shm->ctl.target_fps_bits, 60.0);
        ctl_write_double(&g_shm->ctl.delay_bias_bits, 0.0);
        ctl_write_double(&g_shm->ctl.measured_refresh_hz_bits, 0.0);
        g_shm->ctl.last_vbi_qpc = 0;
        ctl_write_double(&g_shm->ctl.pll_phase_us_bits, 0.0);
        g_shm->ctl.exit_requested = 0;
        g_shm->ctl._reserved3 = 0;
        g_shm->write_idx = 0;
    }
    return true;
}

void shm_close() {
    if (g_shm) UnmapViewOfFile(g_shm);
    if (g_mapping) CloseHandle(g_mapping);
    g_shm = nullptr;
    g_mapping = nullptr;
}

void shm_push_interval(std::uint64_t delta_qpc_ticks) {
    if (!g_shm) return;
    LONG i = InterlockedIncrement(&g_shm->write_idx);
    g_shm->ring[(std::uint32_t)(i - 1) & (kRingCapacity - 1)] = delta_qpc_ticks;
}

double shm_target_fps() {
    return g_shm ? ctl_read_double(&g_shm->ctl.target_fps_bits) : 60.0;
}

double shm_delay_bias() {
    return g_shm ? ctl_read_double(&g_shm->ctl.delay_bias_bits) : 0.0;
}

std::uint32_t shm_state() {
    return g_shm ? g_shm->ctl.state : PacerState_Unlimited;
}

std::uint32_t shm_mode() {
    return g_shm ? g_shm->ctl.mode : PacerMode_DisplayLocked;
}

void shm_set_api(std::uint32_t api) {
    if (g_shm) g_shm->ctl.api = api;
}

void shm_publish_display(double measured_refresh_hz,
                         std::uint64_t last_vbi_qpc,
                         double pll_phase_us) {
    if (!g_shm) return;
    ctl_write_double(&g_shm->ctl.measured_refresh_hz_bits, measured_refresh_hz);
    g_shm->ctl.last_vbi_qpc = last_vbi_qpc;
    ctl_write_double(&g_shm->ctl.pll_phase_us_bits, pll_phase_us);
}

bool shm_exit_requested() {
    return g_shm && g_shm->ctl.exit_requested != 0;
}

}  // namespace pacer

