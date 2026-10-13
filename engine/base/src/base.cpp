#include "base/handle.hpp"
#include "base/math.hpp"

namespace eng {
static_assert(sizeof(Handle<struct AnyTag>) == 8);
static_assert(sizeof(RectI) == 16);
} // namespace eng
