#pragma once
#include <cstdint>

namespace eng {

struct Vec2i { int32_t x = 0, y = 0; friend constexpr bool operator==(Vec2i, Vec2i) = default; };
struct Vec2f { float x = 0.f, y = 0.f; };

struct RectI {
    int32_t x = 0, y = 0, w = 0, h = 0;
    friend constexpr bool operator==(RectI, RectI) = default;
};

// Linear RGBA, presentation only (floats are allowed here, §2.2 rule 8).
struct Color {
    float r = 0.f, g = 0.f, b = 0.f, a = 1.f;
    static constexpr Color rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
    }
};

} // namespace eng

namespace eng::render {
using eng::Color;
using eng::RectI;
using eng::Vec2f;
using eng::Vec2i;
}
