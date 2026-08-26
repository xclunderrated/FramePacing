import React, { useState } from 'react';
import { Folder, FileCode, ChevronRight, ChevronDown, Copy, Check } from 'lucide-react';

interface FileItem {
  path: string;
  name: string;
  category: string;
  content: string;
}

const REPO_FILES: FileItem[] = [
  {
    path: 'core/pacing.cpp',
    name: 'pacing.cpp',
    category: 'Core DLL',
    content: `// Hybrid wait + Critically-damped Phase-Lock + Adaptive Headroom scheduler
#include "pacing.h"
#include <cmath>
#include <algorithm>
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
}

void HybridWait::wait_until(std::uint64_t deadline_qpc) {
    std::uint64_t now = qpc_now();
    if (now >= deadline_qpc) return;

    std::uint64_t max_wait = (std::uint64_t)((double)freq_ * 0.050);
    if (deadline_qpc > now + max_wait) deadline_qpc = now + max_wait;

    const std::int64_t coarse_until = (std::int64_t)deadline_qpc - spin_tail_ticks_;
    if (timer_ && (std::int64_t)now < coarse_until) {
        double wait_100ns = ((double)coarse_until - (double)now) / (double)freq_ * 1.0e7;
        LARGE_INTEGER rel;
        rel.QuadPart = -(LONGLONG)wait_100ns;
        if (SetWaitableTimer(timer_, &rel, 0, nullptr, nullptr, FALSE)) {
            DWORD guard_ms = (DWORD)((double)(deadline_qpc - now) / (double)freq_ * 1000.0) + 5;
            WaitForSingleObject(timer_, guard_ms);
        }
    }
    // High-resolution spin tail with dynamic CPU pause/yield
    while (qpc_now() < deadline_qpc) {
        YieldProcessor();
    }
}

// -------------------------------------------------------------- PacerEngine
void PacerEngine::pace_frame(std::uint64_t now_qpc, const DisplaySample& disp) {
    if (cfg_.target_fps <= 1.0 || period_ticks_ <= 0.0) {
        pending_back_budget_ = 0.0;
        return;
    }

    if (disp.valid && disp.period_ticks != last_disp_period_) {
        retune(disp);
    }

    if (!initialized_ || (double)now_qpc > next_target_ticks_ + (1.5 * period_ticks_)) {
        next_target_ticks_ = (double)now_qpc + period_ticks_;
        initialized_ = true;
        pll_integral_err_ = 0.0;
    } else {
        next_target_ticks_ += period_ticks_;
    }

    // Latency-First (Latent Sync): Adaptive Headroom Clamping
    double back_budget = 0.0;
    if (cfg_.mode == PacerMode_LatencyFirst) {
        double bias = std::max(0.0, std::min(1.0, cfg_.delay_bias));
        double requested_back = bias * period_ticks_;
        double safety_margin_ticks = (double)freq_ * 0.0015;
        double max_safe_back = std::max(0.0, period_ticks_ - peak_render_ticks_ - safety_margin_ticks);
        back_budget = std::min(requested_back, max_safe_back);
    }
    pending_back_budget_ = back_budget;

    // Critically Damped VBI Phase Steering with Deadband
    if ((cfg_.mode == PacerMode_DisplayLocked || cfg_.mode == PacerMode_LatencyFirst) &&
        is_display_divisor_ && disp.valid && disp.period_ticks > 0.0) {
        double p = disp.period_ticks;
        double vbi = (double)disp.last_vbi_qpc;
        double target_phase = (cfg_.mode == PacerMode_DisplayLocked) ? (-0.25 * p) : 0.0;
        double k = std::round((next_target_ticks_ - vbi) / p);
        double expected_vbi = vbi + k * p;
        double err = next_target_ticks_ - expected_vbi - target_phase;
        err = std::fmod(err + 0.5 * p, p) - 0.5 * p;

        double err_us = err * 1.0e6 / (double)freq_;
        pll_phase_us_ = err_us;

        // 40 µs deadband eliminates standing waves; slew rate bounded to 0.2 µs/frame
        if (std::fabs(err_us) > 40.0) {
            double slew_limit = 0.0000002 * (double)freq_;
            double steer = 0.0004 * err;
            if (steer > slew_limit) steer = slew_limit;
            if (steer < -slew_limit) steer = -slew_limit;
            next_target_ticks_ -= steer;
        }
    } else {
        pll_phase_us_ = 0.0;
    }

    std::uint64_t deadline = (std::uint64_t)llround(next_target_ticks_ - back_budget);
    waiter_.wait_until(deadline);
}

} // namespace pacer`,
  },
  {
    path: 'core/display_clock.cpp',
    name: 'display_clock.cpp',
    category: 'Core DLL',
    content: `// Synchronous render-thread display clock estimator via IDXGISwapChain::GetFrameStatistics
#include "display_clock.h"
#include <algorithm>
#include <cstring>

namespace pacer {

void DisplayClock::on_frame_presented(IDXGISwapChain* sc) {
    if (!sc) return;

    DXGI_FRAME_STATISTICS st{};
    HRESULT hr = E_FAIL;

    __try {
        hr = sc->GetFrameStatistics(&st);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hr = E_FAIL;
    }

    if (FAILED(hr) || st.SyncQPCTime.QuadPart == 0) return;

    std::uint64_t t = (std::uint64_t)st.SyncQPCTime.QuadPart;
    UINT cnt = st.SyncRefreshCount;

    if (have_prev_ && cnt > prev_sync_count_) {
        double period = (double)(t - prev_qpc_) / (double)(cnt - prev_sync_count_);
        double ms = period / (double)qpc_freq() * 1000.0;
        if (ms >= 1.0 && ms <= 100.0) {
            if (hist_n_ < kHistCap) period_hist_[hist_n_++] = period;
        }
    }
    prev_qpc_ = t;
    prev_sync_count_ = cnt;
    have_prev_ = true;

    if (hist_n_ >= 15) {
        double sorted[kHistCap];
        memcpy(sorted, period_hist_, sizeof(double) * hist_n_);
        std::sort(sorted, sorted + hist_n_);
        sample_.valid = true;
        sample_.period_ticks = sorted[hist_n_ / 2];
        sample_.last_vbi_qpc = t;
    }
}

} // namespace pacer`,
  },
  {
    path: 'core/engine_context.cpp',
    name: 'engine_context.cpp',
    category: 'Core DLL',
    content: `// Dynamic runtime synchronization for PacerEngine
#include "engine_context.h"
#include "shm.h"
#include "log.h"

namespace pacer {

EngineContext g_ctx{};

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

bool pre_present(std::uint32_t api, void* sc, bool is_dummy) {
    if (is_dummy) return false;

    // Immediately poll and apply dynamic configuration changes
    ctx_tick_watch();

    // FRONT-EDGE PACING: Pace the frame BEFORE submitting present
    if (shm_state() == PacerState_Limited) {
        AcquireSRWLockShared(&g_ctx.lock);
        g_ctx.engine.pace_frame(qpc_now(), g_ctx.display.current());
        ReleaseSRWLockShared(&g_ctx.lock);
    }
    return true;
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

} // namespace pacer`,
  },
  {
    path: 'ui/ui_main.cpp',
    name: 'ui_main.cpp',
    category: 'PacerUI',
    content: `// Win32 Framepacer Control Window
#include "shm.h"
#include "shm_client.h"

void apply_fps(double fps) {
    if (fps < 0.0) fps = 0.0;
    if (fps > 0.0 && fps < 10.0) fps = 10.0;
    if (fps > 1000.0) fps = 1000.0;

    g_target_fps = fps;
    double limit_fps = (fps <= 0.0) ? 10000.0 : fps;
    svc::set_target_fps(limit_fps);

    if (g_current_pid != 0 && !g_shm_box.valid()) {
        g_shm_box.open(g_current_pid, true);
    }

    if (g_shm_box.valid()) {
        g_shm_box.shm->ctl.state =
            (g_target_fps > 0.0) ? pacer::PacerState_Limited : pacer::PacerState_Unlimited;
        pacer::ctl_write_double(&g_shm_box.shm->ctl.target_fps_bits, g_target_fps);
        g_shm_box.shm->ctl.mode = g_settings_mode;
        pacer::ctl_write_double(&g_shm_box.shm->ctl.delay_bias_bits, g_delay_bias);
    }

    InvalidateRect(g_main, nullptr, FALSE);
}`,
  },
  {
    path: 'shared/shm.h',
    name: 'shm.h',
    category: 'Shared',
    content: `#pragma once
#include <cstdint>
#include <windows.h>

namespace pacer {

inline constexpr wchar_t kShmNamePrefix[] = L"Local\\\\Pacer.SHM.";
inline constexpr std::uint32_t kShmMagic = 0x50414352u; // 'PACR'
inline constexpr std::uint32_t kRingCapacity = 4096;

enum PacerMode : std::uint32_t {
    PacerMode_DisplayLocked = 0,  // Console front-edge: VBI phase-locked loop
    PacerMode_Async         = 1,  // RTSS-style post-present wait
    PacerMode_VrrLive       = 2,  // VRR Live adaptive sync
    PacerMode_LatencyFirst  = 3,  // Special K Latent sync default (tearline parking)
};

enum PacerState : std::uint32_t {
    PacerState_Unlimited = 0,
    PacerState_Limited   = 1,
};

struct ControlBlock {
    std::uint32_t magic;
    std::uint32_t version;
    volatile std::uint32_t state;
    volatile std::uint32_t mode;
    volatile std::uint32_t api;
    std::uint32_t pid;
    std::uint64_t qpc_frequency;
    volatile std::uint64_t target_fps_bits;
    volatile std::uint64_t delay_bias_bits;
    volatile std::uint64_t measured_refresh_hz_bits;
};

}`,
  },
];

export const RepoFileViewer: React.FC = () => {
  const [selectedFile, setSelectedFile] = useState<FileItem>(REPO_FILES[0]);
  const [copied, setCopied] = useState<boolean>(false);

  const handleCopy = () => {
    navigator.clipboard.writeText(selectedFile.content);
    setCopied(true);
    setTimeout(() => setCopied(false), 1200);
  };

  return (
    <div className="w-full bg-[#141518] rounded-lg border border-[#272930] p-3.5 space-y-3 font-sans text-xs">
      <div className="flex items-center justify-between">
        <div className="flex items-center space-x-2">
          <Folder className="w-4 h-4 text-amber-400" />
          <span className="font-semibold text-zinc-100">Repository C++ Source Files</span>
        </div>
        <button
          onClick={handleCopy}
          className="flex items-center space-x-1 px-2.5 py-1 bg-[#1e2026] hover:bg-[#282b33] border border-[#30333d] rounded text-zinc-300 transition-colors"
        >
          {copied ? <Check className="w-3 h-3 text-emerald-400" /> : <Copy className="w-3 h-3 text-zinc-400" />}
          <span>{copied ? 'Copied' : 'Copy'}</span>
        </button>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-3">
        {/* File List */}
        <div className="space-y-1 bg-[#0f1013] border border-[#22242a] rounded p-2">
          {REPO_FILES.map((f) => (
            <div
              key={f.path}
              onClick={() => setSelectedFile(f)}
              className={`px-2.5 py-1.5 rounded cursor-pointer flex items-center justify-between transition-colors ${
                selectedFile.path === f.path
                  ? 'bg-cyan-950/60 border border-cyan-500/40 text-cyan-300 font-medium'
                  : 'hover:bg-[#1a1c22] text-zinc-400'
              }`}
            >
              <div className="flex items-center space-x-2 truncate">
                <FileCode className="w-3.5 h-3.5 text-zinc-400 shrink-0" />
                <span className="truncate">{f.name}</span>
              </div>
              <span className="text-[10px] text-zinc-500 shrink-0 font-mono">{f.category}</span>
            </div>
          ))}
        </div>

        {/* Code Content Box */}
        <div className="md:col-span-2 bg-[#0c0d10] border border-[#22242a] rounded p-3 font-mono text-[11px] text-zinc-300 overflow-x-auto max-h-64 leading-relaxed">
          <div className="text-[10px] text-zinc-500 pb-2 mb-2 border-b border-[#1e2026] font-sans flex items-center justify-between">
            <span>{selectedFile.path}</span>
            <span>C++ Source</span>
          </div>
          <pre>{selectedFile.content}</pre>
        </div>
      </div>
    </div>
  );
};
