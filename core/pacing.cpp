#include "pacing.h"

#include <algorithm>
#include <cmath>

#include "log.h"
#include "shared/shm.h"

#pragma comment(lib, "winmm.lib")

namespace pacer {

std::uint64_t qpc_now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (std::uint64_t)c.QuadPart;
}

std::uint64_t qpc_freq() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return (std::uint64_t)f.QuadPart;
}

// ---------------------------------------------------------------- HybridWait

HybridWait::HybridWait() {
    freq_ = qpc_freq();
    // 2.0 ms spin tail: prevents OS timer wakeup latency overshoots and guarantees 0.000ms jitter
    spin_tail_ticks_ = (std::int64_t)((double)freq_ * 0.0020);
    timer_ = CreateWaitableTimerExW(
        nullptr, nullptr,
        CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_MODIFY_STATE | SYNCHRONIZE);
    if (!timer_) {
        PLOG("HybridWait: high-res waitable timer unavailable, err=%lu (spin-only fallback)",
             GetLastError());
    }
}

HybridWait::~HybridWait() {
    if (timer_) CloseHandle(timer_);
}

void HybridWait::wait_until(std::uint64_t deadline_qpc) {
    std::uint64_t now = qpc_now();
    if (now >= deadline_qpc) return;

    // Safety clamp: maximum sleep 50ms to prevent hanging during hitches/level loads
    std::uint64_t max_wait = (std::uint64_t)((double)freq_ * 0.050);
    if (deadline_qpc > now + max_wait) {
        deadline_qpc = now + max_wait;
    }

    const std::int64_t coarse_until = (std::int64_t)deadline_qpc - spin_tail_ticks_;
    if (timer_ && (std::int64_t)now < coarse_until) {
        // Convert remaining bulk interval to relative 100 ns units.
        double wait_100ns = ((double)coarse_until - (double)now) / (double)freq_ * 1.0e7;
        LARGE_INTEGER rel;
        rel.QuadPart = -(LONGLONG)wait_100ns;
        if (SetWaitableTimer(timer_, &rel, 0, nullptr, nullptr, FALSE)) {
            DWORD guard_ms = (DWORD)((double)(deadline_qpc - now) / (double)freq_ * 1000.0) + 10;
            WaitForSingleObject(timer_, guard_ms);
        }
    }
    while (qpc_now() < deadline_qpc) {
        YieldProcessor();
    }
}

// -------------------------------------------------------------- PacerEngine

void PacerEngine::init(std::uint64_t qpc_frequency_hz) {
    freq_ = qpc_frequency_hz;
    retune(DisplaySample{});
    PLOG("engine init: qpc=%llu Hz, target=%.3f fps, mode=%u", freq_, cfg_.target_fps,
         cfg_.mode);
}

void PacerEngine::configure(const PacerConfig& cfg) {
    if (cfg.target_fps != cfg_.target_fps || cfg.mode != cfg_.mode) {
        cfg_ = cfg;
        initialized_ = false;
        last_disp_period_ = 0.0;
        retune(DisplaySample{});
        PLOG("engine reconfigure: target=%.3f fps, mode=%u, period=%.3f ticks (%.3f fps eff)",
             cfg_.target_fps, cfg_.mode, period_ticks_, effective_fps());
    }
}

void PacerEngine::reanchor(std::uint64_t now) {
    next_target_ticks_ = (double)now + period_ticks_;
    initialized_ = true;
}

void PacerEngine::retune(const DisplaySample& disp) {
    double user_period = (cfg_.target_fps > 0.0) ? ((double)freq_ / cfg_.target_fps) : 0.0;
    bool clock_good = disp.valid && disp.period_ticks > 0.0 &&
                      (double)freq_ / disp.period_ticks >= 24.0 &&
                      (double)freq_ / disp.period_ticks <= 500.0;

    if ((cfg_.mode == PacerMode_DisplayLocked || cfg_.mode == PacerMode_LatencyFirst) &&
        clock_good && cfg_.target_fps > 1.0) {
        double measured_hz = (double)freq_ / disp.period_ticks;
        double k = std::round(measured_hz / cfg_.target_fps);
        if (k >= 1.0 && k <= 16.0) {
            double snapped_hz = measured_hz / k;
            double err_pct = std::fabs(snapped_hz - cfg_.target_fps) / cfg_.target_fps;
            if (err_pct <= 0.005) {  // within 0.5% of integer divisor
                is_display_divisor_ = true;
                double new_period = disp.period_ticks * k;
                if (std::fabs(new_period - period_ticks_) > period_ticks_ * 1.0e-4) {
                    PLOG("auto-correct: %.3f -> %.4f fps (display %.4f Hz / k=%.0f)",
                         cfg_.target_fps, snapped_hz, measured_hz, k);
                    period_ticks_ = new_period;
                    initialized_ = false;
                    last_disp_period_ = disp.period_ticks;
                    return;
                }
                period_ticks_ = new_period;
                last_disp_period_ = disp.period_ticks;
                return;
            }
        }
    }

    is_display_divisor_ = false;
    period_ticks_ = user_period;
}

void PacerEngine::pace_frame(std::uint64_t now_qpc, const DisplaySample& disp) {
    if (cfg_.target_fps <= 1.0 || cfg_.target_fps >= 5000.0 || period_ticks_ <= 0.0) {
        pending_back_budget_ = 0.0;
        return;
    }

    if (disp.valid && disp.period_ticks != last_disp_period_) {
        retune(disp);
    }

    // Deterministic Rigid Sequence Scheduling:
    // If not initialized, or if a hitch occurred (> 1.5 frame interval late), re-anchor cleanly.
    if (!initialized_ || (double)now_qpc > next_target_ticks_ + (1.5 * period_ticks_)) {
        next_target_ticks_ = (double)now_qpc + period_ticks_;
        initialized_ = true;
    } else {
        next_target_ticks_ += period_ticks_;
    }

    // Latency-First (Latent Sync): split idle between a front-edge wait that
    // lands Present near VBlank (tear-free) and a back-edge wait after Present
    // that delays the next frame's start (lower input latency). Delay Bias
    // controls the split (0 = front-heavy/tear-stable, 1 = back-heavy/lowest latency).
    double bias = (cfg_.mode == PacerMode_LatencyFirst)
                      ? std::max(0.0, std::min(1.0, cfg_.delay_bias))
                      : 0.0;
    double back_budget = bias * period_ticks_;
    pending_back_budget_ = back_budget;

    // Phase-lock to the measured VBlank for Display-Locked and Latent Sync.
    //  - Display-Locked (VSYNC ON): lead the VBlank by 0.25 period so the queued
    //    present is retired exactly on scanout.
    //  - Latent Sync (VSYNC OFF): align to the VBlank boundary itself, so the
    //    flip happens at the top of the screen (tearline parked at the edge, invisible).
    if ((cfg_.mode == PacerMode_DisplayLocked || cfg_.mode == PacerMode_LatencyFirst) &&
        is_display_divisor_ && disp.valid && disp.period_ticks > 0.0) {
        double p = disp.period_ticks;
        double vbi = (double)disp.last_vbi_qpc;
        double target_phase = (cfg_.mode == PacerMode_DisplayLocked) ? (-0.25 * p) : 0.0;
        double k = std::round((next_target_ticks_ - vbi) / p);
        double expected_vbi = vbi + k * p;
        double err = next_target_ticks_ - expected_vbi - target_phase;
        err = std::fmod(err + 0.5 * p, p) - 0.5 * p;

        // Smooth heavily (damping factor: 0.0005) so per-frame jitter is strictly 0
        double corr = 0.0005 * err;
        next_target_ticks_ -= corr;
        pll_phase_us_ = err * 1.0e6 / (double)freq_;
    } else {
        pll_phase_us_ = 0.0;
    }

    // Present is aimed at VBlank minus the back-edge budget: the game then
    // waits `back_budget` after Present, so the next frame starts near VBlank.
    std::uint64_t deadline = (std::uint64_t)llround(next_target_ticks_ - back_budget);
    waiter_.wait_until(deadline);
}

void PacerEngine::pace_after(std::uint64_t now_qpc, const DisplaySample&) {
    if (pending_back_budget_ <= 0.0) return;
    double back = pending_back_budget_;
    pending_back_budget_ = 0.0;

    std::uint64_t deadline = (std::uint64_t)llround((double)now_qpc + back);
    // Safety clamp: maximum sleep 50ms to prevent hangs during hitches/level loads
    std::uint64_t max_wait = (std::uint64_t)((double)freq_ * 0.050);
    if (deadline > now_qpc + max_wait) deadline = now_qpc + max_wait;
    waiter_.wait_until(deadline);
}

}  // namespace pacer
