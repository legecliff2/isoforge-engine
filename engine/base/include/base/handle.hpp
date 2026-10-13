#pragma once
#include <cstdint>

namespace eng {

// Generation-checked typed handle. A default-constructed handle is "null"
// (index 0 is reserved), which render uses for e.g. TargetId{} = swapchain.
template <class Tag>
struct Handle {
    uint32_t index = 0;
    uint32_t generation = 0;

    constexpr bool valid() const noexcept { return index != 0; }
    constexpr explicit operator bool() const noexcept { return valid(); }
    friend constexpr bool operator==(Handle, Handle) = default;
};

} // namespace eng

namespace eng::render {
using eng::Handle;
}
