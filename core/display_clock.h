// DisplayClock: Synchronous, render-thread-safe display clock estimator.
// Learns the real refresh rate and VBI phase from IDXGISwapChain::GetFrameStatistics
// directly during present handoff without spawning background threads or holding COM references.
#pragma once

#include <dxgi.h>
#include <windows.h>
#include <cstdint>

#include "pacing.h"

namespace pacer {

class DisplayClock {
public:
    DisplayClock() = default;
    ~DisplayClock() = default;

    DisplayClock(const DisplayClock&) = delete;
    DisplayClock& operator=(const DisplayClock&) = delete;

    // Called synchronously on the render thread right after a successful present.
    void on_frame_presented(IDXGISwapChain* sc);

    // Resets clock history across swapchain reconfigurations or alt-tabs.
    void reset();

    DisplaySample current() const { return sample_; }

private:
    static constexpr size_t kHistCap = 61;

    double period_hist_[kHistCap]{};
    size_t hist_n_ = 0;
    std::uint64_t prev_qpc_ = 0;
    UINT prev_sync_count_ = 0;
    bool have_prev_ = false;

    DisplaySample sample_{};
};

}  // namespace pacer
