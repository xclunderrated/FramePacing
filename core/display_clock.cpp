#include <algorithm>
#include "display_clock.h"

#include <algorithm>
#include <cstring>

#include "log.h"

namespace pacer {

void DisplayClock::reset() {
    hist_n_ = 0;
    prev_qpc_ = 0;
    prev_sync_count_ = 0;
    have_prev_ = false;
    sample_ = DisplaySample{};
}

void DisplayClock::on_frame_presented(IDXGISwapChain* sc) {
    if (!sc) return;

    DXGI_FRAME_STATISTICS st{};
    HRESULT hr = E_FAIL;

    __try {
        hr = sc->GetFrameStatistics(&st);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        hr = E_FAIL;
    }

    if (FAILED(hr) || st.SyncQPCTime.QuadPart == 0) {
        // Disjoint or unsupported under current windowing mode
        return;
    }

    std::uint64_t t = (std::uint64_t)st.SyncQPCTime.QuadPart;
    UINT cnt = st.SyncRefreshCount;

    if (have_prev_) {
        if (cnt < prev_sync_count_) {
            hist_n_ = 0;
        } else if (cnt > prev_sync_count_) {
            double period = (double)(t - prev_qpc_) / (double)(cnt - prev_sync_count_);
            double ms = period / (double)qpc_freq() * 1000.0;
            if (ms >= 1.0 && ms <= 100.0) {  // 10..1000 Hz plausible
                if (hist_n_ < kHistCap) {
                    period_hist_[hist_n_++] = period;
                }
            } else {
                hist_n_ = 0;
            }
        }
    }
    prev_qpc_ = t;
    prev_sync_count_ = cnt;
    have_prev_ = true;

    if (hist_n_ >= 15) {
        double sorted[kHistCap];
        memcpy(sorted, period_hist_, sizeof(double) * hist_n_);
        std::sort(sorted, sorted + hist_n_);
        double median = sorted[hist_n_ / 2];

        sample_.valid = true;
        sample_.period_ticks = median;
        sample_.last_vbi_qpc = t;

        // Slide the window by half for continuous updates
        size_t keep = hist_n_ / 2;
        memmove(period_hist_, period_hist_ + (hist_n_ - keep), sizeof(double) * keep);
        hist_n_ = keep;
    }
}

}  // namespace pacer

