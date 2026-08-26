// Pacer UI - High-Precision Anti-Aliased Live Frametime Graph
#pragma once

#include <windows.h>
#include <cstdint>
#include <vector>

#include "shared/shm_client.h"

namespace ui {

struct GraphStats {
    double current_ms = 0.0;
    double fps = 0.0;
    double mean_ms = 0.0;
    double stddev_ms = 0.0;
    double min_ms = 0.0;
    double max_ms = 0.0;
    double target_fps = 60.0;
    double measured_hz = 0.0;
    bool is_limited = false;
    std::uint32_t api = 0;
};

class FrametimeGraph {
public:
    FrametimeGraph() = default;
    ~FrametimeGraph() = default;

    void paint(HDC hdc, const RECT& client_rect, pacer::ShmBox& shm_box);

    const GraphStats& last_stats() const { return stats_; }

private:
    GraphStats stats_;
    std::vector<double> sample_cache_;
};

// Window procedure for the graph custom control
LRESULT CALLBACK graph_control_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

}  // namespace ui
