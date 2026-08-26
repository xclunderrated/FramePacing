// GPU execution timing queries for D3D11 / D3D12 render context.
// Measures exact GPU render duration on the GPU timeline to safeguard Latent Sync back-waits.
#pragma once

#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <cstdint>

namespace pacer {

class GpuQueryTracker {
public:
    GpuQueryTracker() = default;
    ~GpuQueryTracker();

    GpuQueryTracker(const GpuQueryTracker&) = delete;
    GpuQueryTracker& operator=(const GpuQueryTracker&) = delete;

    // Called right before Present to close the GPU disjoint/timestamp pair
    void on_pre_present(IDXGISwapChain* sc);

    // Called right after Present to kick off the query for the next frame
    void on_post_present(IDXGISwapChain* sc);

    // Releases device query objects across device changes / Alt+Tab
    void reset();

    // Returns the measured GPU execution time in milliseconds (0.0 if not ready or unsupported)
    double gpu_duration_ms() const { return gpu_duration_ms_; }

private:
    ID3D11Device* dev_ = nullptr;
    ID3D11DeviceContext* ctx_ = nullptr;

    ID3D11Query* q_disjoint_ = nullptr;
    ID3D11Query* q_start_ = nullptr;
    ID3D11Query* q_end_ = nullptr;

    bool query_in_flight_ = false;
    double gpu_duration_ms_ = 0.0;
};

}  // namespace pacer
