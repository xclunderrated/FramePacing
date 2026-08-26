// The watchdog core: shared by pacer-svc (console/headless) and pacer-ui
// (embedded background thread). Behavior contract:
//   - behavioral election by sustained present rate (>= 8 fps) with hysteresis
//   - observe injections for structural candidates; elected target gets
//     state=Limited + the fps target
//   - anti-cheat module markers = warn-only advisory line (RTSS-style
//     always-limit policy; the user owns the risk)
//   - no name blocklists of any kind
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <windows.h>

#include "../shared/shm.h"

namespace svc {

// Opens (or creates) the cross-session control channel shared between the
// interactive UI and the privileged injection service. Must be called by both
// sides before any other control function. Uses the Global\ namespace so a
// session-0 LocalSystem service and the interactive UI can share it.
bool control_open();
void control_close();

// Config: UI -> service (applied to the elected target each tick).
void control_set_config(double fps, std::uint32_t mode, double delay_bias);

// Detection input: UI -> service.
void control_set_candidates(const std::vector<DWORD>& pids);
void control_set_foreground(DWORD pid);
void control_set_force(DWORD pid, const std::wstring& exe);

// Election output: service -> UI.
void control_set_election(DWORD pid, const std::wstring& exe);
DWORD control_get_election_pid();
std::wstring control_get_election_exe();

// Log ring: service -> UI (UI drains via control_log_drain in its tick).
void control_log(const std::wstring& line);
void control_log_drain(void (*cb)(const std::wstring&));

// Runs the interactive-session detection loop (WTS scan + foreground tracking).
// Only call from the UI process (it can see windows). The watchdog consumes the
// results from the control channel.
bool start_detection();
void stop_detection();

struct Options {
    double fps = 60.0;
    DWORD scan_ms = 1000;
    std::uint32_t mode = pacer::PacerMode_LatencyFirst;  // Latent Sync default: smooth + flat frametime, lower latency
    double delay_bias = 0.0; // Latency-First only: 0 = front-heavy/tear-stable .. 1 = back-heavy/low-latency
};

// Enables SeDebugPrivilege on the process token to allow inspecting/injecting protected games.
bool enable_debug_privilege();

bool start(const Options& opt);  // spawns watchdog thread; false if DLL missing
void stop();

void set_target_fps(double fps);  // applies to elected target on next tick
void set_mode(std::uint32_t mode);  // applies to elected target on next tick
void set_delay_bias(double bias);   // Latency-First only; clamps 0..1

// Forces immediate injection and election of a specific target PID.
bool force_target(DWORD pid, const std::wstring& exe);

// Current election (0 / empty when nothing is being limited).
DWORD elected_pid();
std::wstring elected_exe();

// Optional line-based log/status sink (console prints if unset).
void set_event_sink(std::function<void(const std::wstring&)> sink);

}  // namespace svc
