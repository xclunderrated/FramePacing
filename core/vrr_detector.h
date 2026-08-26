// Windows Display Configuration & VRR (G-Sync / FreeSync) detector.
// Uses DXGI 1.6 / CCD APIs to identify VRR capability and apply the -3 FPS ceiling.
#pragma once

#include <dxgi1_6.h>
#include <windows.h>
#include <cstdint>
#include <algorithm>

namespace pacer {

struct VrrDetectionResult {
    bool supported = false;
    bool enabled = false;
    double max_refresh_hz = 0.0;
    double min_refresh_hz = 0.0;
    double recommended_vrr_cap = 0.0;
};

class VrrDetector {
public:
    static VrrDetectionResult check_vrr_support(IDXGISwapChain* sc);
};

}  // namespace pacer
