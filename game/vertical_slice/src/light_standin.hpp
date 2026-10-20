#pragma once
// STAND-IN producer for ViewModel::light until Engine Core publishes sim::LightFrame
// (LIGHTING.md §10, phase 6). Game-side test fixture only: it fills the same plain-data
// snapshot the real LightFrame copy will fill, so the render path can be built and tested now.
// It is NOT gameplay light (no occlusion, no zones, no rules) and is off by default
// (vertical_slice --demo-light). Delete when the real LightFrame lands.
#include <array>
#include <cstdint>

#include "view_model.hpp"

namespace slice {

struct LightStandinState {
    bool started = false;
    uint64_t frame = 0;
    std::array<uint8_t, 32 * 32> prev{};
    std::array<uint64_t, 16> explored{};
};

// Torch (bright 20 ft / dim +20 ft) on each living PC, ambient Dark; fog: seen within 40 ft of a PC,
// explored accumulates. Single chunk (0,0,0) in the slice's chunk-local frame.
void fill_standin_light(ViewModel& vm, LightStandinState& st);

} // namespace slice
