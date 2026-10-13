#include "base/canonical_hash.hpp"

#define XXH_INLINE_ALL
#include <xxhash.h>

namespace eng {

uint64_t stable_hash(std::span<const uint8_t> bytes) noexcept {
    return uint64_t(XXH3_64bits(bytes.data(), bytes.size()));
}

void CanonicalHasher::str(std::string_view s) {
    u32(uint32_t(s.size()));
    for (char c : s) byte(static_cast<uint8_t>(c));
}

} // namespace eng
