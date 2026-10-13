#pragma once
// Integer value noise (ARCHITECTURE.md §4.4): lattice values from StableHash (xxHash3-64) of
// canonical bytes (u64 seed, i64 ix, i64 iy, u32 octave), smoothstep-interpolated in
// detmath::Fx (Q16.16) with int64 intermediates. No floats.
//
// Inputs are integer world-tile coordinates (int64, so any chunk coord works without Fx
// overflow). Each octave has a lattice cell of 2^log2_cell tiles; the fractional position
// inside a cell is computed exactly with shifts, never by dividing an Fx coordinate.
//
// Proposed to move into detmath::noise (§4.8 sketch) once Rules wants it there; the
// algorithm and outputs would stay identical (golden-tested here).
#include <cstdint>

#include "detmath/fx.hpp"

namespace procgen::noise {

// Lattice value in [0, 1) as Fx: top 16 bits of StableHash(seed, ix, iy, octave).
detmath::Fx lattice(uint64_t seed, int64_t ix, int64_t iy, uint32_t octave);

// Value noise in [0, 1). log2_cell in [0, 16].
detmath::Fx value2(uint64_t seed, int64_t x, int64_t y, int log2_cell, uint32_t octave = 0);

// Fractal sum: octave o uses cell 2^(log2_cell - o) and amplitude 2^-o, normalised to [0, 1).
// Requires 1 <= octaves <= log2_cell + 1.
detmath::Fx fbm2(uint64_t seed, int64_t x, int64_t y, int log2_cell, int octaves);

} // namespace procgen::noise
