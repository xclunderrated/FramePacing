// pacer-stats: console validator for M0. Reads the target's shared-memory
// ring and prints present-interval statistics once per second.
// Usage: pacer-stats <pid> [seconds=30] [--fps N]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "shared/shm.h"

static double g_freq = 10.0e6;

static double ticks_to_ms(std::uint64_t t) { return (double)t / g_freq * 1000.0; }

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        fwprintf(stderr, L"usage: pacer-stats <pid> [seconds] [--fps N]\n");
        return 2;
    }
    DWORD pid = wcstoul(argv[1], nullptr, 10);
    int seconds = (argc >= 3) ? _wtoi(argv[2]) : 30;

    HANDLE mapping =
        OpenFileMappingW(FILE_MAP_READ, FALSE, pacer::shm_name(pid).c_str());
    if (!mapping) {  // give the DLL a moment to create it after injection
        Sleep(500);
        mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, pacer::shm_name(pid).c_str());
    }
    if (!mapping) {
        fwprintf(stderr, L"cannot open shm for pid %lu (err %lu)\n", pid, GetLastError());
        return 3;
    }
    auto* shm = static_cast<pacer::SharedMemLayout*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(pacer::SharedMemLayout)));
    if (!shm) {
        CloseHandle(mapping);
        return 3;
    }
    if (shm->ctl.magic != pacer::kShmMagic) {
        fwprintf(stderr, L"shm magic mismatch (core not installed yet?)\n");
        return 4;
    }
    g_freq = (double)shm->ctl.qpc_frequency;

    // Optional live control write: proves mid-game change of target FPS.
    for (int i = 3; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--fps") == 0) {
            double fps = _wtof(argv[i + 1]);
            // Need write access: remap R/W.
            UnmapViewOfFile(shm);
            CloseHandle(mapping);
            mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE,
                                       pacer::shm_name(pid).c_str());
            shm = static_cast<pacer::SharedMemLayout*>(
                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                              sizeof(pacer::SharedMemLayout)));
            if (shm) {
                pacer::ctl_write_double(&shm->ctl.target_fps_bits, fps);
                shm->ctl.state = pacer::PacerState_Limited;  // direct-tool flow
                wprintf(L"[control] target fps -> %.3f (state=limited)\n", fps);
            }
            break;
        }
    }

    wprintf(L"pid=%lu api=%u  sampling %ds\n", pid, shm->ctl.api, seconds);
    wprintf(L"--------------------------------------------------------------------------------\n");

    LONG last_idx = shm->write_idx;
    std::vector<std::uint64_t> deltas;
    deltas.reserve(pacer::kRingCapacity);

    for (int s = 0; s < seconds; ++s) {
        Sleep(1000);
        LONG idx = shm->write_idx;
        LONG avail = idx - last_idx;
        if (avail > (LONG)pacer::kRingCapacity) avail = (LONG)pacer::kRingCapacity;
        deltas.clear();
        for (LONG i = idx - avail; i < idx; ++i)
            deltas.push_back(shm->ring[(std::uint32_t)i & (pacer::kRingCapacity - 1)]);
        last_idx = idx;

        if (deltas.empty()) {
            wprintf(L"[%02ds] no presents\n", s + 1);
            continue;
        }
        double sum = 0.0;
        for (auto d : deltas) sum += ticks_to_ms(d);
        double mean = sum / (double)deltas.size();
        double var = 0.0;
        std::uint64_t mn = ~0ull, mx = 0;
        for (auto d : deltas) {
            double ms = ticks_to_ms(d);
            var += (ms - mean) * (ms - mean);
            mn = (std::min)(mn, d);
            mx = (std::max)(mx, d);
        }
        double sd = std::sqrt(var / (double)deltas.size());
        std::vector<double> ms_sorted;
        ms_sorted.reserve(deltas.size());
        for (auto d : deltas) ms_sorted.push_back(ticks_to_ms(d));
        std::sort(ms_sorted.begin(), ms_sorted.end());
        double p99 = ms_sorted[(size_t)((double)(ms_sorted.size() - 1) * 0.99)];

        double target = pacer::ctl_read_double(&shm->ctl.target_fps_bits);
        double refresh = pacer::ctl_read_double(&shm->ctl.measured_refresh_hz_bits);
        double pll = pacer::ctl_read_double(&shm->ctl.pll_phase_us_bits);
        wprintf(L"[%02ds] n=%5zu  mean=%7.4fms  std=%7.4fms  p99=%7.4f  min=%7.4f  max=%7.4f"
                L"  | target=%.2fHz display=%.4fHz pll=%+8.1fus\n",
                s + 1, deltas.size(), mean, sd, p99, ticks_to_ms(mn), ticks_to_ms(mx),
                target, refresh, pll);
    }

    UnmapViewOfFile(shm);
    CloseHandle(mapping);
    return 0;
}
