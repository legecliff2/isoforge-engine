#include "detmath/stable_hash.hpp"

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace detmath {

uint64_t stable_hash(std::span<const uint8_t> bytes) noexcept {
    return uint64_t(XXH3_64bits(bytes.data(), bytes.size()));
}

} // namespace detmath
