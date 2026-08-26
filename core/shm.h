// Shared-memory producer side (runs inside the game process).
#pragma once

#include "shared/shm.h"

namespace pacer {

bool shm_create(std::uint32_t pid);
void shm_close();

void shm_push_interval(std::uint64_t delta_qpc_ticks);

double shm_target_fps();
double shm_delay_bias();
std::uint32_t shm_state();
std::uint32_t shm_mode();

void shm_set_api(std::uint32_t api);
void shm_publish_display(double measured_refresh_hz,
                         std::uint64_t last_vbi_qpc,
                         double pll_phase_us);
bool shm_exit_requested();

bool shm_stats_reset_requested();
void shm_clear_stats_reset();
void shm_reset_ring();

}  // namespace pacer
