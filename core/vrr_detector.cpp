#include "vrr_detector.h"
#include <dxgi1_6.h>
#include <cmath>
#include "log.h"

namespace pacer {

VrrDetectionResult VrrDetector::check_vrr_support(IDXGISwapChain* sc) {
    VrrDetectionResult res{};
    if (!sc) return res;

    IDXGIOutput* output = nullptr;
    if (FAILED(sc->GetContainingOutput(&output)) || !output) {
        return res;
    }

    IDXGIOutput6* output6 = nullptr;
    if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6))) && output6) {
        DXGI_OUTPUT_DESC1 desc1{};
        if (SUCCEEDED(output6->GetDesc1(&desc1))) {
            UINT flags = 0;
            if (SUCCEEDED(output6->CheckHardwareCompositionSupport(&flags))) {
                if (flags & DXGI_HARDWARE_COMPOSITION_SUPPORT_FLAG_WINDOWED) {
                    res.supported = true;
                }
            }
        }
        output6->Release();
    }

    DXGI_OUTPUT_DESC od{};
    if (SUCCEEDED(output->GetDesc(&od))) {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(od.DeviceName, ENUM_CURRENT_SETTINGS, &dm)) {
            res.max_refresh_hz = (double)dm.dmDisplayFrequency;
            if (res.max_refresh_hz >= 60.0) {
                // Apply the golden -3 FPS rule for VRR ranges to prevent queue spillover
                res.recommended_vrr_cap = std::max(30.0, res.max_refresh_hz - 3.0);
            }
        }
    }

    output->Release();
    return res;
}

}  // namespace pacer
