#pragma once
// Integer math value types (ARCHITECTURE.md §2.1 base). No floats: these feed state.
#include <compare>
#include <cstdint>

namespace eng {

struct Vec2i {
    int32_t x = 0, y = 0;
    auto operator<=>(const Vec2i&) const = default;
};

struct RectI {
    int32_t x = 0, y = 0, w = 0, h = 0;
    auto operator<=>(const RectI&) const = default;
};

} // namespace eng

namespace eng::render {
using eng::RectI;
using eng::Vec2i;
}
