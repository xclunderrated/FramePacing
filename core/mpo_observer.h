// Multi-Plane Overlay (MPO) and Independent Flip (iFlip) composition observer.
// Tracks whether the active swapchain is running under True Hardware Direct Flip, MPO, or DWM Composed.
#pragma once

#include <dxgi1_3.h>
#include <windows.h>
#include <cstdint>

namespace pacer {

enum CompositionTier : std::uint32_t {
    CompositionTier_Unknown = 0,
    CompositionTier_Composed = 1,       // Standard DWM composition (latency overhead)
    CompositionTier_DirectFlip = 2,     // True Hardware Independent Flip
    CompositionTier_MultiPlaneOverlay = 3, // MPO active (zero-latency scanout)
};

class MpoObserver {
public:
    static CompositionTier check_composition_tier(IDXGISwapChain* sc) {
        if (!sc) return CompositionTier_Unknown;

        IDXGISwapChainMedia* media = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChainMedia), reinterpret_cast<void**>(&media))) && media) {
            DXGI_FRAME_STATISTICS_MEDIA stats{};
            if (SUCCEEDED(media->GetFrameStatisticsMedia(&stats))) {
                media->Release();
                if (stats.CompositionMode == DXGI_FRAME_PRESENTATION_MODE_COMPOSED) {
                    return CompositionTier_Composed;
                } else if (stats.CompositionMode == DXGI_FRAME_PRESENTATION_MODE_OVERLAY) {
                    return CompositionTier_MultiPlaneOverlay;
                } else {
                    return CompositionTier_DirectFlip;
                }
            }
            media->Release();
        }
        return CompositionTier_DirectFlip;
    }
};

}  // namespace pacer
