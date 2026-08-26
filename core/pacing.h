// The pacemaker: high-precision rigid sequence frame scheduling + hybrid wait.
#pragma once

#include <cstdint>
#include <windows.h>

namespace pacer {

std::uint64_t qpc_now();
std::uint64_t qpc_freq();

// Display truth sample produced by DisplayClock.
struct DisplaySample {
    bool valid = false;
    double period_ticks = 0.0;       // measured display period in QPC ticks
    std::uint64_t last_vbi_qpc = 0;  // timestamp of most recent observed VBlank
};

// High-precision hybrid wait: waitable timer for bulk sleep + tight sub-millisecond spin tail.
class HybridWait {
public:
    HybridWait();
    ~HybridWait();
    HybridWait(const HybridWait&) = delete;
    HybridWait& operator=(const HybridWait&) = delete;

    void wait_until(std::uint64_t deadline_qpc);

private:
    HANDLE timer_ = nullptr;
    std::uint64_t freq_ = 0;
    std::int64_t spin_tail_ticks_ = 0;
};

struct PacerConfig {
    std::uint32_t mode = 0;  // PacerMode_DisplayLocked
    double target_fps = 60.0;
    double delay_bias = 0.0; // Latency-First: 0 = front-heavy (tear-stable) .. 1 = back-heavy (lowest latency)
};

class PacerEngine {
public:
    void init(std::uint64_t qpc_frequency_hz);
    void configure(const PacerConfig& cfg);

    // Front-edge pacing: blocks the render thread until the exact frame presentation deadline.
    void pace_frame(std::uint64_t now_qpc, const DisplaySample& disp);

    // Back-edge pacing (Latency-First / Latent Sync only): holds the render
    // thread after Present to delay the next frame's start, lowering input latency.
    void pace_after(std::uint64_t now_qpc, const DisplaySample& disp);

    double effective_fps() const {
        return (period_ticks_ > 0.0) ? ((double)freq_ / period_ticks_) : cfg_.target_fps;
    }
    double pll_phase_us() const { return pll_phase_us_; }

    void reanchor(std::uint64_t now);

private:
    void retune(const DisplaySample& disp);

    std::uint64_t freq_ = 10000000;
    PacerConfig cfg_;

    double period_ticks_ = 0.0;
    double next_target_ticks_ = 0.0;
    double last_disp_period_ = 0.0;
    bool initialized_ = false;
    bool is_display_divisor_ = false;
    double pll_phase_us_ = 0.0;
    double pending_back_budget_ = 0.0;  // back-edge idle reserved by the last pace_frame

    HybridWait waiter_;
};

}  // namespace pacer
