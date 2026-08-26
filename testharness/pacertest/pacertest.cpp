// pacer-pacertest: standalone frame-limiter precision harness.
// Compiles the real PacerEngine (core/pacing.cpp) so it exercises the actual
// hybrid wait + rigid-sequence scheduler. No injection, no D3D, no display
// needed: it drives pace_frame() in a tight loop and reports the achieved
// interval distribution so you can verify the limiter's precision offline.
//
// Usage:
//   pacertest.exe [target_fps] [frames] [mode]
//     target_fps  default 60.0
//     frames      default 3000
//     mode        0=DisplayLocked 1=Async 2=VrrLive 3=LatencyFirst (default 1)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <windows.h>

#include "core/pacing.h"      // the real frame-limiter engine (compiled via CMake)

namespace {

double to_ms(std::uint64_t ticks, std::uint64_t freq) {
    return (double)ticks / (double)freq * 1000.0;
}

}  // namespace

int main(int argc, char** argv) {
    double target_fps = (argc > 1) ? atof(argv[1]) : 60.0;
    std::uint64_t frames = (argc > 2) ? (std::uint64_t)atoll(argv[2]) : 3000;
    std::uint32_t mode = (argc > 3) ? (std::uint32_t)atoi(argv[3]) : 1u;  // Async default

    std::uint64_t freq = pacer::qpc_freq();

    pacer::PacerEngine engine;
    engine.init(freq);
    pacer::PacerConfig cfg{};
    cfg.mode = mode;
    cfg.target_fps = target_fps;
    cfg.delay_bias = 0.5;
    engine.configure(cfg);

    printf("pacer-pacertest: target=%.3f fps mode=%u frames=%llu qpc=%llu Hz\n",
           target_fps, mode, (unsigned long long)frames, (unsigned long long)freq);
    printf("press Ctrl+C to abort\n");

    std::vector<std::uint64_t> deltas;
    deltas.reserve((size_t)frames);

    const pacer::DisplaySample no_disp{};  // no display clock -> pure software limiter

    std::uint64_t prev = pacer::qpc_now();
    for (std::uint64_t i = 0; i < frames; ++i) {
        std::uint64_t now = pacer::qpc_now();
        engine.pace_frame(now, no_disp);
        std::uint64_t after = pacer::qpc_now();
        if (i > 0) deltas.push_back(after - prev);
        prev = after;

        // Rolling print every 500 frames.
        if ((i + 1) % 500 == 0) {
            double mean = 0.0;
            for (auto d : deltas) mean += (double)d;
            mean /= (double)deltas.size();
            double var = 0.0;
            for (auto d : deltas) var += ((double)d - mean) * ((double)d - mean);
            var /= (double)deltas.size();
            printf("  [%5llu] mean=%.4f ms  stddev=%.4f ms  eff=%.3f fps\n",
                   (unsigned long long)(i + 1), to_ms((std::uint64_t)mean, freq),
                   to_ms((std::uint64_t)std::sqrt(var), freq),
                   (double)freq / mean);
        }
    }

    // Final summary statistics over the whole run.
    std::vector<std::uint64_t> sorted = deltas;
    std::sort(sorted.begin(), sorted.end());
    double mean = 0.0;
    for (auto d : sorted) mean += (double)d;
    mean /= (double)sorted.size();
    double var = 0.0;
    for (auto d : sorted) var += ((double)d - mean) * ((double)d - mean);
    var /= (double)sorted.size();

    auto pct = [&](double p) -> std::uint64_t {
        size_t idx = (size_t)(p * (double)(sorted.size() - 1) + 0.5);
        return sorted[idx];
    };

    printf("==== summary (n=%zu) ====\n", sorted.size());
    printf("  target   : %.4f ms (%.3f fps)\n", to_ms((std::uint64_t)(freq / target_fps), freq), target_fps);
    printf("  mean     : %.4f ms  stddev=%.4f ms\n", to_ms((std::uint64_t)mean, freq),
           to_ms((std::uint64_t)std::sqrt(var), freq));
    printf("  min      : %.4f ms\n", to_ms(sorted.front(), freq));
    printf("  p50      : %.4f ms\n", to_ms(pct(0.50), freq));
    printf("  p99      : %.4f ms\n", to_ms(pct(0.99), freq));
    printf("  p99.9    : %.4f ms\n", to_ms(pct(0.999), freq));
    printf("  max      : %.4f ms\n", to_ms(sorted.back(), freq));
    printf("  achieved : %.3f fps  (phase err %.3f us)\n",
           (double)freq / mean, engine.pll_phase_us());
    return 0;
}
