// Front-Pacing and Input Polling Alignment Hook.
// Intercepts PeekMessageW / GetMessageW to align input event sampling with the next frame's VBI deadline.
#pragma once

#include <windows.h>
#include <cstdint>

namespace pacer {

class FrontPacer {
public:
    static bool install();
    static void uninstall();

    static void set_enabled(bool enabled);
    static bool is_enabled();

    // Invoked by PeekMessageW / GetMessageW interceptors
    static void on_message_polled();

private:
    static bool enabled_;
    static std::uint64_t last_input_aligned_qpc_;
};

}  // namespace pacer
