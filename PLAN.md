# Pacer — Design Plan & Build Log

> A high-precision, display-locked framerate limiter for Windows.
> Goal: **perfect frame pacing** (console-class scanout flatness) with **minimal input latency**.

---

## 1. Product Definition

| Attribute | Target |
|---|---|
| Function | External per-game FPS limiter with *perfect* frame pacing |
| Pacing precision | `MsUntilDisplayed` interval stddev ≤ ±0.1 ms @ 60/120/144 FPS |
| Phase drift | `< 50 µs over 10 minutes` (locked to real display clock) |
| Added input latency | ≤ ~1 ms vs. FLW reference (smoothness-first tuning) |
| CPU overhead | < 1% (hybrid sleep + spin tail) |
| Bitness | x64 at launch; x86 DLL in M4 (legacy 32-bit games) |
| OS | Windows 10 22H2+ / Windows 11 |

## 2. Locked Decisions

| Decision | Choice | Rationale |
|---|---|---|
| Stack | **C++ everywhere** (MSVC C++20, MinHook 1.3.4, static CRT `/MT`) | No runtime deps; matches RTSS/SpecialK lineage; hook DLL must be native |
| Distribution | **Open-source (GitHub, free)** | Trust for an injector-style tool; community compatibility fixes |
| MVP APIs | **DXGI (DX11 + DX12), Vulkan, OpenGL** | One DXGI vtable hook covers DX11+DX12; ~modern titles covered |
| Legacy APIs | DX9 / DX8 / DirectDraw in **M4** | Deferred to reduce MVP risk |
| In-game overlay | **None — external graph only** | Avoids in-process render surface + anti-cheat exposure |
| Pacemaker | **Display-Locked (VBI-PLL)**, smoothness-first | Beats RTSS Async: locks to *measured scanout* clock, not nominal FPS |
| FPS entry | **Manual + auto-correct** | User types 60 → engine snaps to measured 59.94 when within ε |
| Fallback mode | **Async** (RTSS-style post-present wait) | Compat path when display stats unavailable; also M0 bootstrap |
| Anti-cheat policy | **Warn-only, always-limit (RTSS-style)** | Changed on user request (M2): no skipping; in-process AC module markers produce a printed warning; NO evasion/stealth code, ever |
| Name blocklists | **None** | Changed on user request (M2): election is behavioral (present-rate); structural guards only |

### 2.1 Why not just "Async" (the RTSS default)?

Async makes **Present()-call intervals** flat, but the player perceives **scanouts**. Three jitter sources Async cannot fix:

1. **Nominal vs. actual refresh** — a "60 Hz" panel really runs 59.94/59.95 Hz. Scheduling at exactly 60.00 causes slow phase drift (~1 ms per 16 s) → periodic dup/drop micro-hitches, forever.
2. **Flip-queue / DWM composition jitter** — FLIP-model presents queue through the display stack; flat submissions ≠ flat display times.
3. **No VBI phase lock** — pacing anchor lands at an arbitrary phase relative to VBlank; inconsistent scanout intervals.

Consoles lock presentation to the hardware VBI schedule. We reproduce exactly that on PC.

## 3. Architecture

```
PacerUI.exe  ──named pipe──► PacerService.exe ──inject──► game.exe + PacerCore.dll
     ▲  shared memory (per game process, lock-free) ◄──────────┤
     └──────────── frametime ring buffer + control block ──────┘
```

| Component | Role | Status |
|---|---|---|
| `core/` PacerCore.dll | Injected per game process. API hooks, display clock, PLL pacemaker, shm producer | **M0 in progress** |
| `service/` PacerService.exe | Game detection (graphics-DLL load heuristics), target election, injection, anti-cheat blacklist, per-game profiles | M1 |
| `ui/` PacerUI.exe | Win32/Direct2D; live frametime graph; tray; autorun; i18n | M3 |
| `shared/` | shm layout spec (single header consumed by core + tools + ui) | **M0** |
| `tools/injector/` | Console injector (dev workflow until service exists) | **M0** |
| `tools/statsreader/` | Console shm reader: mean/stddev/p99/drift of present intervals | **M0** |
| `testharness/spinner/` | Synthetic DX11 FLIP-model app, uncapped presents | **M0** |

## 4. The Pacemaker (core differentiator)

### Rule 1 — Present FIRST, wait AFTER (zero added submission latency; RTSS/Unwinder model)

```cpp
HRESULT Hooked_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    record_interval(now - last_present);          // graph data
    HRESULT hr = origPresent(sc, sync, flags);    // frame submitted immediately
    wait_until(deadline);                         // game can't start next frame early
    deadline += effective_period;                 // advance *display-derived* schedule
    return hr;
}
```

### Rule 2 — Schedule is derived from the MEASURED display clock, not nominal numbers

**Display clock acquisition** (measurement thread inside hook DLL):
- `IDXGISwapChain::GetFrameStatistics` → `SyncRefreshCount` + `SyncQPCTime` (QPC-stamped VBlanks; works windowed FLIP-model under DWM) — robust median over ~121 samples → real period (e.g. 16.638 ms) + VBI phase.
- Fallbacks: `IDXGIOutput::WaitForVBlank` (exclusive fullscreen), `DwmFlush` cadence; Win11 24H2+ `IDCompositionGetTargetStatistics` evaluated later.
- `IDXGISwapChain2::GetFrameLatencyWaitableObject` + `SetMaximumFrameLatency(1)` reserved for Latency-First mode.

**PLL steering** (smoothness-first gain):

```
vbi_period   = robust_median(vbi_deltas)             // display truth
target       = user_fps ? snap(user_fps→vbi_period×k, ε=0.5%) : vbi_period
deadline    += target
phase_err    = wrap(deadline_phase − vbi_phase, ±period/2)
deadline    -= clamp(kp·phase_err, ±50 µs)           // kp ≈ 0.05 — invisible convergence
hitch        = (now − deadline > 1.5×period) → re-anchor, NO catch-up bursts
```

**Hybrid wait**: `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` waitable timer for the bulk, spin loop (`_mm_pause` + QPC) for the last ~1.5 ms. µs-class precision at ~0% CPU.

### Modes

| Mode | Behavior | Use |
|---|---|---|
| **Display-Locked** (default) | VBI-PLL as above | Fixed-Hz/VSync; absolute scanout flatness |
| **VRR Live** | Measured-period schedule, phase lock relaxed | G-Sync/FreeSync panels |
| **Latency-First** | FLW + front-edge pacing (render as late as safely possible) | Input-lag-sensitive play |
| **Async** (fallback) | Classic post-present busy/hybrid wait, nominal period | Stats unavailable; old titles |

## 5. API Hooking Matrix

Technique: **vtable patching via MinHook**, vtable addresses resolved by creating a **dummy device+swapchain** (vtable ptr is per-class → hooking dummy's vtable hooks every instance in the process; no hardcoded offsets).

| API | Hook targets | vtable idx | Notes | Status |
|---|---|---|---|---|
| DXGI (DX11+DX12) | `Present`, `Present1`, `ResizeBuffers`, `ResizeTarget` | 8 / 22 / 13 / 14 | Same swapchain class serves D3D12 → one path covers both | **M0** |
| Vulkan | `vkCreateInstance` → chain `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` → `vkQueuePresentKHR` | — | Layer-compatible chained interception | M2 |
| OpenGL | `wglSwapBuffers`, `gdi32!SwapBuffers` | — | Inline export hooks (never IAT) | M2 |
| DX9 / DX9Ex | `IDirect3DDevice9::Present`, `Reset` | 17 / 16 | Dummy-device trick; no D3D9 display clock → free-run nominal | **M4 — written, build-pending** |
| DirectDraw7 | `IDirectDrawSurface7::Flip`, `Blt` | 11 / 5 | Dummy surface; build-validated only | **M4 — written, build-pending** |

Late-loaded graphics DLLs → `LdrRegisterDllNotification` arms hooks per module. Primary-swapchain selection ignores our own dummy (HWND marker). Detach path guarded (SRWLock, drain frames) — M2 hardening.

## 6. IPC — Shared Memory Spec (lock-free)

Named mapping `Local\Pacer.SHM.<pid>` created by PacerCore in each target process:

```
[ControlBlock] magic | version | state(unlimited/limited) | mode
               | target_fps(f64) | api | pid | qpc_freq
               | measured_refresh_hz(f64) | last_vbi_qpc | pll_phase_us(f64)
[RingBuffer]   volatile write_idx + 4096 × u64 QPC frame-interval ticks
```

UI/statsreader polls @60 Hz; control writes (FPS changes) applied **next frame — no game restart** (flagship feature).

## 7. Validation Methodology

- **Flatness:** PresentMon v2 / CapFrameX per-API captures — primary metric `MsUntilDisplayed` (display truth), secondary `MsBetweenPresents`.
- **Drift:** 10-min runs; phase-drift metric must stay < 50 µs (Async would accumulate ~10 ms @59.94).
- **Latency:** submit→display delta comparisons vs. FLW-only reference.
- **Cost:** ETW CPU sampling; RAM < 30 MB combined.
- **Stability:** alt-tab churn, resolution switches, device-reset storms, 8h soaks.

## 8. Roadmap

| Milestone | Scope | Exit gate |
|---|---|---|
| **M0 — spike (NOW)** | DXGI Present hook + hybrid wait + display clock + PLL + shm + stats validation | stddev ≤ 0.1 ms vs nominal, zero drift on measured period |
| **M1 — DONE** | Injector→service; detection/election; console controller | Live FPS switch validated |
| **M2 — DONE** | Vulkan + OpenGL hooks; multi-path dedup; behavioral election; AC warn-only; hysteresis | All harness suites green |
| **M3 — DONE** | PacerUI (graph, profiles, tray, autostart, warnings, i18n skeleton) | Dogfood-validated |
| **M4 — COMPLETE** | DX9/DDraw hooks + x86 build + bitness-aware inject + eject tool | All validation suites green |
| M5 | OSS release: CI, README/guides, public repo | Ship |

## 9. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Anti-cheat bans (EAC/BE/Vanguard/process checks) | Hard blacklist + module-load detection + user warnings; never target competitive titles |
| AV false positives | Static CRT, no packing, code signing (M4), vendor pre-submission |
| Driver/OS shifts breaking hooks | vtable addrs resolved at runtime from live class — no hardcoded offsets; per-module rearm |
| Anti-cheat-adjacent optics of GPL vs MIT | Pick license at M4 (leaning MIT) |
| Vulkan loader variance | GDPAddr-chained interception, strict unknown-proc passthrough |

## 10. Build

Prereqs: MSVC (VS 18), CMake ≥ 3.20 (VS-bundled OK), git.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Layout: `build/Release/PacerCore.dll`, `pacer-inject.exe`, `pacer-stats.exe`, `spinner.exe`.

---

# Build Log

## M0 — COMPLETE (build: VS 18 / CMake 4.3.1, git 2.55)

### What exists now (all under `build/bin/Release/`)
- `PacerCore.dll` — injected limiter core: DXGI vtable hooks (Present, Present1,
  ResizeBuffers) via MinHook + dummy-swapchain vtable resolution (no hardcoded
  offsets); PacerEngine (fractional-fps friendly scheduler, hitch-reanchor, no
  catch-up bursts); DisplayClock (measured VBI clock via DXGIFRAME_STATISTICS);
  VBI-PLL steering (kp=0.05, clamp +-50us); HybridWait (high-res waitable timer
  bulk + 1.5 ms spin tail); lock-free shm producer.
- `pacer-inject.exe` — dev injector (`pacer-inject <pid> [dll]`).
- `pacer-stats.exe` — ring reader/validator (`pacer-stats <pid> [sec] [--fps N]`,
  `--fps` performs a live control-block write).
- `spinner.exe` — DX11 FLIP_DISCARD uncapped test app.

### Validation results (165 Hz panel, measured by DisplayClock: 164.94-164.96 Hz)
| Test | Target | Measured | Verdict |
|---|---|---|---|
| Interval stddev @60 | <= 0.100 ms | 0.052-0.081 ms across 25 s | PASS |
| Mean interval @60 | 16.6667 ms | 16.6651-16.6683 ms | PASS |
| PLL phase stability | bounded | +-0.4..2.9 ms, converging, no drift | PASS |
| Live retune 60 -> 48 | no restart | 20.833 ms mean next frame, std ~0.08 ms | PASS |
| Bugs found & fixed | | (1) DisplayClock divided elapsed time by refresh-COUNT delta missing -> 20.6 Hz garbage; (2) PLL applied unplausble-clock corrections; (3) stats reader AV: InterlockedCompareExchange64 on FILE_MAP_READ view -> plain aligned load; (4) log file lock prevented tailing | fixed |

### M0 -> M1 handoff notes
- Remaining M0-class polish: present-completion sampling for `MsUntilDisplayed`
  ground truth; ETW CPU cost capture; Async fallback mode flag honored in engine
  (currently DisplayLocked codepath always).
- Service/UI not started: `service/`, `ui/` are empty placeholders.

## Next: M1 — service (watchdog), game detection/election, config IPC pipe,
real-game smoke pass on 3 titles.

---

## M1 — COMPLETE

### What exists now
- `pacer-svc.exe` — watchdog/discovery console (becomes background broker behind
  PacerUI in M3):
  - candidate detection = graphics module load (`dxgi/d3d9/11/12/vulkan-1/opengl32`)
    AND >=1 visible, titled top-level window AND not system-exe blocklisted
  - anti-cheat denylist (exe names - CS2/Valorant/Fortnite/Apex/COD/Tarkov and
    module markers - EAC/BE/Vanguard/Faceit) -- injected NEVER
  - bitness gate (x64 only until M4)
  - injected targets boot in OBSERVE (Unlimited) mode; only the elected
    (foreground) game gets state=Limited written to its control block
  - target-exit detection, applied-state caching, `--fps/--scan-ms/--secs/--dry-run`
- PacerCore self-protection vs. misinjection: observe-by-default + dormancy
  self-eject (`FreeLibraryAndExitThread` after 15 s without presents while
  unlimited). NOTE: dormancy-eject path coded but not yet validated.

### Validation results
- Hardened detector on a live desktop (~90 processes incl. GPU-using system
  apps): injected ONLY `spinner.exe`. Pre-fix run had hit ~20 innocent
  processes (explorer, shell UI, Steam helpers, ...).
- Elected + paced: std 0.065-0.088 ms @60, display measured 164.9482 Hz (stable).
- Target exit detected and logged within one scan tick.

### Lessons recorded
1. Module-presence heuristic is NOT sufficient on Windows 11 -- every modern
   desktop process carries dxgi/d3d11. Visible-window + blocklist + observe/
   elect + self-eject is the safety model.
2. C stdio under redirected pipes is fully buffered: log lines appear only at
   flush/exit. Long-running dev tools should flush per line (TODO M3: UI pipe
   protocol must not rely on stdio).
3. Injected old build may still be mapped in a handful of pre-fix system
   processes (PacerCore.m0-lock.dll) until they restart / reboot. Non-harming
   (explorer shells barely present), but must be acknowledged/claimed cleanup
   for proper release hygiene: uninstaller must remote-eject (M4 task).

## M2 — COMPLETE

### What exists now
- Core: `engine_context` (unified `on_present` per-API entry, primary-swapchain
  election, config watch); `hook_exports` (Vulkan `vkQueuePresentKHR` export
  hook + OpenGL `wglSwapBuffers`/`gdi32!SwapBuffers` export hooks with
  thread-local re-entry guard); `loader_watch` (`LdrRegisterDllNotification`
  lazy arming for late-loaded runtimes).
- **One-path rule**: the first API to present owns pacing; nested paths (modern
  WGL/Vulkan present through an internal DXGI swapchain) pass through
  untouched. Without this, GL games got double-paced (bimodal 1ms/32ms mix at
  correct 16.67ms mean -- validation caught it).
- Service reworked: NO name blocklists. Qualification = gfx module + game-like
  window (visible+titled, or >=80% of screen) + x64. Election = behavioral:
  highest sustained present rate (>=8 fps), foreground tiebreak, **hysteresis**
  (active target keeps the limiter until it stops presenting -- fixes oscillation
  where two uncapped apps ping-ponged the election every tick).
- Anti-cheat now warn-only, always-limit (user decision; markers logged).
- Test suite: `spinner` (DX11), `glspinner` (GL), `vkspinner` (Vulkan, hand-rolled
  no-SDK binding), `idle` (self-eject probe).

### Validation results
| Test | Result |
|---|---|
| DXGI via service | flat: std 0.059-0.067 ms, display 164.948 Hz |
| GL (glspinner) | flat: std 0.066-0.104 ms (paced through driver's internal DXGI path) |
| Vulkan (vkspinner) | flat: std 0.052-0.069 ms (AMD WSI presents via internal DXGI swapchain -> path-claim dedup worked) |
| Dormancy self-eject | injected into idle.exe: core unhooked + freed after ~15 s, process alive |
| Multi-process election | glspinner Limited (std ~0.065 ms), spinner observe-mode ~6,600 fps untouched; zero election flaps |
| Mid-run retune --fps 48 | still passes from M1 (20.833 ms next frame) |

### Lessons recorded
4. Vulkan `vkGetInstanceProcAddr(NULL, ...)` only serves pre-instance commands;
   must resolve device/instance procs with the live instance.
5. Driver WSI reality: OpenGL + Vulkan on Windows commonly present through a
   real (internal) DXGI swapchain -- vtable-level DXGI hooks catch them for
   free; the export hooks remain as safety net.
6. Behavioral election needs hysteresis (score functions on uncapped rates are
   noise-sensitive and will oscillate otherwise).

## Next: M3 — PacerUI **(DONE, see below)**. Then M4 legacy/x86 + signing,
M5 release.

---

## M3 — COMPLETE

### What exists now
- `service_core` refactor: watchdog logic extracted into
  `service/service_core.{h,cpp}` -- used by `pacer-svc.exe` (console) AND
  embedded as a thread inside `PacerUI.exe` (no pipe protocol needed yet).
- `shared/shm_client.h` — header-only consumer `ShmBox` (svc, UI share it).
- `PacerUI.exe` (Win32, single instance):
  - live frametime graph from the shm ring (green=limited, grey=observe,
    dotted guide at target period), ~40 ms repaint via GDI double-buffer
  - FPS edit + presets (30/40/60/120/144), mode combo (Display-Locked/Async),
    status line showing elected game + api + measured display Hz
  - per-game profiles at `%APPDATA%\Pacer\profiles.json` (auto-saved on Apply,
    auto-applied on election)
  - system tray icon (restore on click, Exit menu), minimize-to-tray
  - Start-with-Windows checkbox (HKCU Run key)
  - log list pane mirrors watchdog events incl. AC warnings
  - watchdog events cross-thread via critical-section queue drained on UI timer
- Graceful release: when the app/svc exits, every elected core's state is
  lifted to Unlimited (closing the tool never leaves a game capped).

### Validation results (M3)
| Test | Result |
|---|---|
| UI boot + election of spinner | elected in ~1 s; service events flow into log pane |
| Pacing via UI-hosted watchdog | 60 fps: std 0.057-0.069 ms, PLL coherent |
| Graceful release | UI closed -> spinner instantly back to unbounded (~6 ms/frame) -- verified post-fix with the release binary |
| Profiles file | load/save path exercised + `%APPDATA%\Pacer\profiles.json` created |

### M3.1 candidates (known gaps, deliberately deferred)
- UI: process picker override (focus a specific pid), in-window "state" of
  observe injections, dark theme, localization tables under tr()
- svc: raise ETW CPU-cost capture; per-tick rings for graphs of jitter stats
- core: Async-mode UI wiring (combo currently visual only; engine honors it
  from shm but UI does not write mode yet)

## Next: M4 — DX9/DX8/DirectDraw + x86 core build, code signing, AV
pre-submission, crash reporting, updater flow, uninstaller remote-eject
(hygiene for past injections like PacerCore.m0-lock.dll).

---

## M4 — COMPLETE

### What exists now
- **DX9 hooks** (`core/hook_dx9.{h,cpp}`): dummy-device trick with Direct3D9Ex and
  Direct3D9 fallback resolution for `IDirect3DDevice9::Present` (vtable idx 17) + `Reset` (idx 16).
  D3D9 has no DXGI display clock → engine runs on nominal/user period with high-res
  hybrid timer waitable sleep + spin tail.
- **DirectDraw hooks** (`core/hook_ddraw.{h,cpp}`): dummy `IDirectDraw7` surface created
  → hooks `IDirectDrawSurface7::Flip` (idx 11) + `Blt` (idx 5) linked with `dxguid.lib` and `ddraw.lib`.
- **x86 (32-bit) builds**: `PacerCore86.dll`, `pacer-inject86.exe`, `d9spinner86.exe`
  built via Win32 generator and co-located in `build/bin/Release/`.
- **Bitness-aware injector** (`tools/injector/injector.cpp`): detects 32-bit (WOW64) vs
  64-bit target processes via `IsWow64Process`; delegates to `pacer-inject86.exe` for
  x86 games and `PacerCore.dll` directly for x64 games.
- **Bitness-aware watchdog** (`service/service_core.cpp`): candidate discovery loop
  qualifies and injects both x64 and x86 games concurrently with behavioral election.
- **`d9spinner`** (`testharness/d9spinner/`): synthetic DX9 test app supporting Direct3D 9Ex
  and Direct3D 9 with uncapped presents (`D3DPRESENT_INTERVAL_IMMEDIATE`) for x64 and x86.
- **`pacer-eject` tool** (`tools/eject/eject.cpp`): cleans up and ejects PacerCore instances
  from a specific PID (`pacer-eject <pid>`) or across all running processes (`pacer-eject --all`)
  by setting `exit_requested = 1` in shared memory and polling for thread unhook/unload.

### Validation results (M4)
| Test | Target | Measured | Verdict |
|---|---|---|---|
| DX9 x64 pacing @60 | stddev ≤ 0.100 ms | 0.047-0.060 ms (mean 16.666-16.667 ms) | PASS |
| DX9 x64 retune @48 | no restart | 0.050-0.051 ms (mean 20.831-20.833 ms) | PASS |
| DX9 x86 pacing @60 | stddev ~0.10 ms | 0.092-0.124 ms (mean 16.663-16.669 ms) | PASS |
| DX9 x86 retune @48 | no restart | 0.092 ms (mean 20.835 ms) | PASS |
| `pacer-eject <pid>` | clean unload | Mapping closed & DLL unloaded without host crash | PASS |
| `pacer-eject --all` | multi-target eject | Scans system, signals all active cores, all eject | PASS |
| Service mixed x64+x86 | simultaneous discovery | Qualified & injected `spinner.exe` [x64] and `d9spinner86.exe` [x86] | PASS |
| Dormancy self-eject | idle probe | Injected into idle.exe, detected no presents, unhooked & self-ejected | PASS |

### Next: M5 — OSS release packaging (CI workflows, documentation, license selection).

