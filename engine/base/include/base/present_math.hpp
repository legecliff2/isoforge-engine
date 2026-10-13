#pragma once
// Float value types for PRESENTATION ONLY (render_*, iso2d, camera, UI; §2.2 rule 8).
// Never use these in sim/rules/world state; base/math.hpp stays integer-only.
#include <cstdint>

namespace eng {

struct Vec2f { float x = 0.f, y = 0.f; };

// Linear RGBA.
struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    static constexpr Color rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
    }
};

} // namespace eng

namespace eng::render {
using eng::Color;
using eng::Vec2f;
}
