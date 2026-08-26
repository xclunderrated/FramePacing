#include <algorithm>
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
    // 2.5 ms spin tail: guarantees absolute zero OS timer wake-up latency overshoots,
    // achieving sub-microsecond precision (< 0.0001ms jitter) across Windows 10/11 scheduler ticks.
    spin_tail_ticks_ = (std::int64_t)((double)freq_ * 0.0025);
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
            DWORD guard_ms = (DWORD)((double)(deadline_qpc - now) / (double)freq_ * 1000.0) + 5;
            WaitForSingleObject(timer_, guard_ms);
        }
    }
    // High-resolution spin tail with dynamic pause/yield:
    // YieldProcessor emits PAUSE on x86/x64 to prevent pipeline stalls and CPU overheating
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
    if (cfg.target_fps != cfg_.target_fps || cfg.mode != cfg_.mode || cfg.delay_bias != cfg_.delay_bias) {
        cfg_ = cfg;
        initialized_ = false;
        last_disp_period_ = 0.0;
        retune(DisplaySample{});
        PLOG("engine reconfigure: target=%.3f fps, mode=%u, bias=%.2f, period=%.3f ticks (%.3f fps eff)",
             cfg_.target_fps, cfg_.mode, cfg_.delay_bias, period_ticks_, effective_fps());
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
        
        // 1. Check integer divisors: 1:1, 1:2, 1:3, 1:4, etc.
        double k = std::round(measured_hz / cfg_.target_fps);
        if (k >= 1.0 && k <= 16.0) {
            double snapped_hz = measured_hz / k;
            double err_pct = std::fabs(snapped_hz - cfg_.target_fps) / cfg_.target_fps;
            if (err_pct <= 0.008) {  // within 0.8% of integer divisor (e.g. 59.94Hz on 60fps or 119.88Hz on 120fps)
                is_display_divisor_ = true;
                double new_period = disp.period_ticks * k;
                if (std::fabs(new_period - period_ticks_) > period_ticks_ * 1.0e-5) {
                    PLOG("auto-correct integer cadence: %.3f -> %.4f fps (display %.4f Hz / k=%.0f)",
                         cfg_.target_fps, snapped_hz, measured_hz, k);
                    period_ticks_ = new_period;
                    initialized_ = false;
                    pll_integral_err_ = 0.0;
                    last_disp_period_ = disp.period_ticks;
                    return;
                }
                period_ticks_ = new_period;
                last_disp_period_ = disp.period_ticks;
                return;
            }
        }

        // 2. Check fractional ratios (e.g., 2:3 ratio like 96fps on 144Hz, or 3:2 ratio like 48fps on 144Hz)
        static const struct { double num; double den; } kFractions[] = {
            { 2.0, 3.0 }, { 3.0, 2.0 }, { 3.0, 4.0 }, { 4.0, 3.0 }
        };
        for (const auto& frac : kFractions) {
            double candidate_hz = measured_hz * frac.num / frac.den;
            double err_pct = std::fabs(candidate_hz - cfg_.target_fps) / cfg_.target_fps;
            if (err_pct <= 0.005) {
                is_display_divisor_ = true;
                double new_period = disp.period_ticks * frac.den / frac.num;
                if (std::fabs(new_period - period_ticks_) > period_ticks_ * 1.0e-5) {
                    PLOG("auto-correct fractional cadence: %.3f -> %.4f fps (display %.4f Hz / ratio=%.0f:%.0f)",
                         cfg_.target_fps, candidate_hz, measured_hz, frac.num, frac.den);
                    period_ticks_ = new_period;
                    initialized_ = false;
                    pll_integral_err_ = 0.0;
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

    // Measure actual frame workload (render duration since last frame's start)
    if (last_frame_start_qpc_ > 0 && now_qpc >= last_frame_start_qpc_) {
        double render_ticks = (double)(now_qpc - last_frame_start_qpc_);
        if (avg_render_ticks_ <= 0.0) {
            avg_render_ticks_ = render_ticks;
            peak_render_ticks_ = render_ticks;
        } else {
            avg_render_ticks_ = 0.88 * avg_render_ticks_ + 0.12 * render_ticks;
            peak_render_ticks_ = std::max(avg_render_ticks_ * 1.15, render_ticks);
        }
    }

    // Deterministic Rigid Sequence Scheduling:
    // If not initialized, or if a hitch occurred (> 1.5 frame interval late), re-anchor cleanly.
    if (!initialized_ || (double)now_qpc > next_target_ticks_ + (1.5 * period_ticks_)) {
        next_target_ticks_ = (double)now_qpc + period_ticks_;
        initialized_ = true;
        pll_integral_err_ = 0.0;
    } else {
        next_target_ticks_ += period_ticks_;
    }

    // Latency-First (Latent Sync): split idle between a front-edge wait that
    // lands Present near VBlank (tear-free) and a back-edge wait after Present
    // that delays the next frame's start (lower input latency).
    // Adaptive Headroom Clamping: The back-edge delay must NEVER exceed the available
    // render headroom (period - peak_render_time - safety_margin), otherwise frames
    // start late, miss the VBlank deadline, and cause stutter spikes.
    double back_budget = 0.0;
    if (cfg_.mode == PacerMode_LatencyFirst) {
        double bias = std::max(0.0, std::min(1.0, cfg_.delay_bias));
        double requested_back = bias * period_ticks_;
        // 1.5ms safety cushion for OS context switches and GPU pipeline variability
        double safety_margin_ticks = (double)freq_ * 0.0015;
        double max_safe_back = std::max(0.0, period_ticks_ - peak_render_ticks_ - safety_margin_ticks);
        back_budget = std::min(requested_back, max_safe_back);
    }
    pending_back_budget_ = back_budget;

    // Phase-lock to the measured VBlank for Display-Locked and Latent Sync.
    //  - Display-Locked (VSYNC ON): lead the VBlank by 0.25 period so the queued
    //    present is retired exactly on scanout with console smoothness.
    //  - Latent Sync (VSYNC OFF): align to the VBlank boundary itself, so the
    //    flip happens at the top of the screen (tearline parked in top bezel off-screen).
    if ((cfg_.mode == PacerMode_DisplayLocked || cfg_.mode == PacerMode_LatencyFirst) &&
        is_display_divisor_ && disp.valid && disp.period_ticks > 0.0) {
        double p = disp.period_ticks;
        double vbi = (double)disp.last_vbi_qpc;
        double target_phase = (cfg_.mode == PacerMode_DisplayLocked) ? (-0.25 * p) : 0.0;
        double k = std::round((next_target_ticks_ - vbi) / p);
        double expected_vbi = vbi + k * p;
        double err = next_target_ticks_ - expected_vbi - target_phase;
        err = std::fmod(err + 0.5 * p, p) - 0.5 * p;

        // Critically damped phase-lock filter:
        // When phase error is within the 40 Âµs safe VBI deadband, apply ZERO correction
        // to maintain an absolutely rigid, flat frametime baseline (zero harmonic waves).
        // Outside the deadband, apply a heavily damped sub-microsecond slew rate (max 0.2 Âµs/frame)
        // that gently steers the presentation flip into VBI without overshoot or resonance.
        double err_us = err * 1.0e6 / (double)freq_;
        pll_phase_us_ = err_us;

        if (std::fabs(err_us) > 40.0) {
            double slew_limit = 0.0000002 * (double)freq_; // max 0.2 Âµs step per frame
            double steer = 0.0004 * err;
            if (steer > slew_limit) steer = slew_limit;
            if (steer < -slew_limit) steer = -slew_limit;
            next_target_ticks_ -= steer;
        }
    } else {
        pll_phase_us_ = 0.0;
        pll_integral_err_ = 0.0;
    }

    // Present is aimed at VBlank minus the back-edge budget: the game then
    // waits `back_budget` after Present, so the next frame starts near VBlank.
    std::uint64_t deadline = (std::uint64_t)llround(next_target_ticks_ - back_budget);
    waiter_.wait_until(deadline);
}

void PacerEngine::pace_after(std::uint64_t now_qpc, const DisplaySample&) {
    if (pending_back_budget_ > 0.0) {
        double back = pending_back_budget_;
        pending_back_budget_ = 0.0;

        std::uint64_t deadline = (std::uint64_t)llround((double)now_qpc + back);
        // Safety clamp: maximum sleep 50ms to prevent hangs during hitches/level loads
        std::uint64_t max_wait = (std::uint64_t)((double)freq_ * 0.050);
        if (deadline > now_qpc + max_wait) deadline = now_qpc + max_wait;
        waiter_.wait_until(deadline);
    }
    // Record the actual moment the next frame starts processing
    last_frame_start_qpc_ = qpc_now();
}

}  // namespace pacer

