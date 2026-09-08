#pragma once

#include <cstddef>
#include <cstdint>

namespace dkr::runtime::telemetry {

struct Metrics {
    double presented_fps = 0.0;
    double frame_time_ms = 0.0;
    double simulation_hz = 0.0;
    double graphics_hz = 0.0;
    double vi_hz = 0.0;
    double interpolated_hz = 0.0;
    double audio_frames_per_second = 0.0;
};

void record_simulation_tick();
// Process-monotonic host counter, including frontend/menu ticks.
std::uint64_t simulation_tick_sequence();
void record_audio_buffer(std::size_t interleaved_sample_count);
void record_graphics_task();
void record_vi_present();
void record_interpolated_present();
void record_interpolated_presents(std::uint64_t count);
void record_presented_frames(std::uint64_t count);
Metrics metrics();

} // namespace dkr::runtime::telemetry
