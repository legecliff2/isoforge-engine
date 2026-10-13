#include "render/device.hpp"
#include "render/sprite.hpp"

// render_api is header-first; this TU keeps it a real static lib (§2.1 "header + lib")
// and checks that the public headers are self-contained.
namespace eng::render {
static_assert(!TargetId{}.valid(), "TargetId{} must denote the swapchain");
} // namespace eng::render
