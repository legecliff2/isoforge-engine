#pragma once
// Integer math value types (ARCHITECTURE.md §2.1 base). No floats: these feed state.
#include <compare>
#include <cstdint>

namespace eng {

struct Vec2i {
    int32_t x = 0, y = 0;
    auto operator<=>(const Vec2i&) const = default;
};

} // namespace eng
