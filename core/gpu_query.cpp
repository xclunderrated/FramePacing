#include "gpu_query.h"
#include "log.h"

namespace pacer {

GpuQueryTracker::~GpuQueryTracker() {
    reset();
}

void GpuQueryTracker::reset() {
    if (q_disjoint_) { q_disjoint_->Release(); q_disjoint_ = nullptr; }
    if (q_start_) { q_start_->Release(); q_start_ = nullptr; }
    if (q_end_) { q_end_->Release(); q_end_ = nullptr; }
    if (ctx_) { ctx_->Release(); ctx_ = nullptr; }
    if (dev_) { dev_->Release(); dev_ = nullptr; }
    query_in_flight_ = false;
}

void GpuQueryTracker::on_pre_present(IDXGISwapChain* sc) {
    if (!sc) return;

    if (!dev_ || !ctx_) {
        ID3D11Device* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) && dev) {
            dev_ = dev;
            dev_->GetImmediateContext(&ctx_);

            D3D11_QUERY_DESC qd{};
            qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
            dev_->CreateQuery(&qd, &q_disjoint_);

            qd.Query = D3D11_QUERY_TIMESTAMP;
            dev_->CreateQuery(&qd, &q_start_);
            dev_->CreateQuery(&qd, &q_end_);
        }
    }

    if (!ctx_ || !q_disjoint_ || !q_end_) return;

    if (query_in_flight_) {
        ctx_->End(q_end_);
        ctx_->End(q_disjoint_);

        // Read results without blocking (D3D11_ASYNC_GETDATA_DONOTFLUSH)
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data{};
        UINT64 ts_start = 0, ts_end = 0;

        if (ctx_->GetData(q_disjoint_, &disjoint_data, sizeof(disjoint_data), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            ctx_->GetData(q_start_, &ts_start, sizeof(ts_start), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            ctx_->GetData(q_end_, &ts_end, sizeof(ts_end), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK) {
            if (!disjoint_data.Disjoint && disjoint_data.Frequency > 0 && ts_end >= ts_start) {
                double dur_sec = (double)(ts_end - ts_start) / (double)disjoint_data.Frequency;
                gpu_duration_ms_ = dur_sec * 1000.0;
            }
        }
        query_in_flight_ = false;
    }
}

void GpuQueryTracker::on_post_present(IDXGISwapChain* sc) {
    if (!ctx_ || !q_disjoint_ || !q_start_) return;

    // Start fresh frame GPU bracket immediately after Present
    ctx_->Begin(q_disjoint_);
    ctx_->End(q_start_);
    query_in_flight_ = true;
}

}  // namespace pacer
