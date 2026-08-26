#include <algorithm>
#include "engine_context.h"

#include "log.h"
#include "shm.h"

namespace pacer {

static EngineContext g_ctx;

EngineContext& ctx() { return g_ctx; }

void ctx_init() {
    AcquireSRWLockExclusive(&g_ctx.lock);
    g_ctx.engine.init(qpc_freq());
    PacerConfig cfg{};
    cfg.mode = shm_mode();
    cfg.target_fps = shm_target_fps();
    cfg.delay_bias = shm_delay_bias();
    g_ctx.engine.configure(cfg);
    g_ctx.applied_fps = cfg.target_fps;
    g_ctx.applied_mode = cfg.mode;
    g_ctx.applied_bias = cfg.delay_bias;
    ReleaseSRWLockExclusive(&g_ctx.lock);
}

void ctx_shutdown() {
    AcquireSRWLockExclusive(&g_ctx.lock);
    g_ctx.primary_swapchain = nullptr;
    g_ctx.gpu_tracker.reset();
    g_ctx.display.reset();
    g_ctx.path_api = 0;
    ReleaseSRWLockExclusive(&g_ctx.lock);
}

bool pre_present(std::uint32_t api, void* sc, bool is_dummy) {
    if (is_dummy) return false;

    // Immediately poll and apply any dynamic configuration changes (FPS limit, mode, bias)
    ctx_tick_watch();

    // Stats reset: clear the frametime ring so 1% Low / 99th percentile start
    // from a fresh window. Cleared before any new interval is recorded.
    if (shm_stats_reset_requested()) {
        shm_reset_ring();
        g_ctx.last_present_qpc = 0;  // avoid a spurious first-delta after reset
        shm_clear_stats_reset();
    }

    // Dynamic active swapchain & API tracking:
    // Seamlessly track resolution changes, splash->game transitions, and DLSS 3 Frame Gen swapchains.
    if (api == PacerApi_Dxgi && sc != nullptr) {
        if (sc != g_ctx.primary_swapchain) {
            g_ctx.primary_swapchain = sc;
            shm_set_api(PacerApi_Dxgi);
            g_ctx.path_api = PacerApi_Dxgi;
        }
    } else {
        if (g_ctx.path_api != api) {
            g_ctx.path_api = api;
            shm_set_api(api);
        }
    }

    // FRONT-EDGE PACING: Pace the frame BEFORE submitting present to the driver/GPU.
    if (shm_state() == PacerState_Limited) {
        double measured_gpu = 0.0;
        if (api == PacerApi_Dxgi && sc != nullptr) {
            g_ctx.gpu_tracker.on_pre_present(static_cast<IDXGISwapChain*>(sc));
            measured_gpu = g_ctx.gpu_tracker.gpu_duration_ms();
        }

        AcquireSRWLockShared(&g_ctx.lock);
        g_ctx.engine.pace_frame(qpc_now(), g_ctx.display.current(), measured_gpu);
        ReleaseSRWLockShared(&g_ctx.lock);
    }

    std::uint64_t t = qpc_now();
    if (g_ctx.last_present_qpc != 0) {
        shm_push_interval(t - g_ctx.last_present_qpc);
    }
    g_ctx.last_present_qpc = t;
    return true;
}

void post_present(std::uint32_t api, void* sc, bool is_dummy) {
    if (is_dummy) return;

    if (api == PacerApi_Dxgi && sc) {
        IDXGISwapChain* dxgi_sc = static_cast<IDXGISwapChain*>(sc);
        g_ctx.display.on_frame_presented(dxgi_sc);
        g_ctx.gpu_tracker.on_post_present(dxgi_sc);
    }

    DisplaySample s = g_ctx.display.current();
    double hz = (s.valid && s.period_ticks > 0.0) ? (double)qpc_freq() / s.period_ticks : 0.0;
    shm_publish_display(hz, s.last_vbi_qpc, g_ctx.engine.pll_phase_us());

    // Latency-First / Latent Sync: back-edge wait after Present to delay the
    // next frame's start (lower input latency). No-op for other modes.
    if (shm_state() == PacerState_Limited && shm_mode() == PacerMode_LatencyFirst) {
        g_ctx.engine.pace_after(qpc_now(), s);
    }
}

void on_present(std::uint32_t api, void* sc, bool is_dummy) {
    pre_present(api, sc, is_dummy);
    post_present(api, sc, is_dummy);
}

void ctx_tick_watch() {
    double fps = shm_target_fps();
    std::uint32_t mode = shm_mode();
    double bias = shm_delay_bias();
    if (fps != g_ctx.applied_fps || mode != g_ctx.applied_mode || bias != g_ctx.applied_bias) {
        AcquireSRWLockExclusive(&g_ctx.lock);
        PacerConfig cfg{};
        cfg.mode = mode;
        cfg.target_fps = fps;
        cfg.delay_bias = bias;
        g_ctx.engine.configure(cfg);
        g_ctx.applied_fps = fps;
        g_ctx.applied_mode = mode;
        g_ctx.applied_bias = bias;
        ReleaseSRWLockExclusive(&g_ctx.lock);
    }
}

std::uint64_t last_present_qpc() { return g_ctx.last_present_qpc; }

}  // namespace pacer

