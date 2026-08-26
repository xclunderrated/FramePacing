#pragma once

#include <cstdint>

namespace pacer {

// Hooks DXGI Present/Present1 (+ Resize* as pass-through observers) for the
// entire process. vtable slots are resolved at runtime from a dummy
// swapchain -- no hardcoded code offsets, robust across driver/OS updates.
bool hooks_install();
void hooks_uninstall();

// QPC of the most recent present on the primary swapchain; 0 = none yet.
std::uint64_t last_present_qpc();

}  // namespace pacer
