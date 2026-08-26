// EngineContext: owns the single pacing engine, the display clock, the
// primary-target selection, and is the single entry every API hook calls
// before and after real present execution.
#pragma once

#include <cstdint>
#include <dxgi.h>
#include <windows.h>

#include "display_clock.h"
#include "pacing.h"
#include "shm.h"

namespace pacer {

struct EngineContext {
    PacerEngine engine;
    DisplayClock display;               // only meaningful for DXGI primaries
    void* primary_swapchain = nullptr;  // active rendering swapchain
    std::uint64_t last_present_qpc = 0;
    double applied_fps = -1.0;
    std::uint32_t applied_mode = 0xFFFFFFFFu;
    volatile std::uint32_t path_api = 0;
    SRWLOCK lock = SRWLOCK_INIT;
};

EngineContext& ctx();

void ctx_init();

// Front-edge pacing: called immediately BEFORE the real present function executes.
bool pre_present(std::uint32_t api, void* sc, bool is_dummy);

// Telemetry & interval tracking: called immediately AFTER the real present function returns.
void post_present(std::uint32_t api, void* sc, bool is_dummy);

// Legacy unified entry point
void on_present(std::uint32_t api, void* sc, bool is_dummy);

// Applies control-block changes (FPS/mode) to the engine
void ctx_tick_watch();

std::uint64_t last_present_qpc();

}  // namespace pacer
