#pragma once
// StableHash (ARCHITECTURE.md §4.5, §4.8): xxHash3-64, seed 0, over canonical bytes.
// Identical output to eng::stable_hash in base (both XXH3_64bits, seed 0).
#include <cstdint>
#include <span>

namespace detmath {

uint64_t stable_hash(std::span<const uint8_t> bytes) noexcept;

} // namespace detmath
