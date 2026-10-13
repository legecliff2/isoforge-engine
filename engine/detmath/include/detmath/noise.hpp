#pragma once
// detmath::noise: integer value noise (ARCHITECTURE.md §4.4). Lattice values from
// detmath::stable_hash (xxHash3-64, seed 0) of
// canonical bytes (u64 seed, i64 ix, i64 iy, u32 octave), smoothstep-interpolated in
// detmath::Fx (Q16.16) with int64 intermediates. No floats.
//
// Inputs are integer world-tile coordinates (int64, so any chunk coord works without Fx
// overflow). Each octave has a lattice cell of 2^log2_cell tiles; the fractional position
// inside a cell is computed exactly with shifts, never by dividing an Fx coordinate.
//
// Moved from procgen (PR #16) unchanged: outputs are pinned by goldens in detmath noise_tests
// and by procgen's chunk content-hash goldens. The §4.8 sketch takes Fx coordinates; this
// keeps 64-bit integer tile coordinates so world-scale positions never overflow Q16.16.
#include <cstdint>

#include "detmath/fx.hpp"

namespace detmath::noise {

// Lattice value in [0, 1) as Fx: top 16 bits of StableHash(seed, ix, iy, octave).
detmath::Fx lattice(uint64_t seed, int64_t ix, int64_t iy, uint32_t octave);

// Parameter limits. Out-of-range parameters are a caller bug: debug builds (NDEBUG not
// defined) assert; release builds return Fx{0} deterministically, identical on every platform.
inline constexpr int kMaxLog2Cell = 16;
constexpr bool value2_params_valid(int log2_cell) { return log2_cell >= 0 && log2_cell <= kMaxLog2Cell; }
constexpr bool fbm2_params_valid(int log2_cell, int octaves) {
    return value2_params_valid(log2_cell) && octaves >= 1 && octaves <= log2_cell + 1;
}

// Value noise in [0, 1). Requires log2_cell in [0, 16] (else assert / Fx{0}, see above).
// Defined for every int64 x, y: the +1 lattice neighbour is computed in uint64 (wrapping), so
// at x = INT64_MAX with log2_cell 0 the neighbour index wraps to INT64_MIN. Its weight is
// zero there (fraction 0), so the result is exactly lattice(seed, x, y, octave).
detmath::Fx value2(uint64_t seed, int64_t x, int64_t y, int log2_cell, uint32_t octave = 0);

// Fractal sum: octave o uses cell 2^(log2_cell - o) and amplitude 2^-o, normalised to [0, 1).
// Requires log2_cell in [0, 16] and 1 <= octaves <= log2_cell + 1 (else assert / Fx{0}).
detmath::Fx fbm2(uint64_t seed, int64_t x, int64_t y, int log2_cell, int octaves);

} // namespace detmath::noise
