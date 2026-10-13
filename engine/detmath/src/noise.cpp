#include "detmath/noise.hpp"

#include <array>
#include <cassert>

#include "detmath/stable_hash.hpp"

namespace detmath::noise {

namespace {
// Lattice hash over the index bit patterns (uint64), so wrapped neighbours are well-defined.
Fx lattice_u(uint64_t seed, uint64_t ix, uint64_t iy, uint32_t octave) {
    // Canonical little-endian bytes: u64 seed, i64 ix, i64 iy, u32 octave (28 bytes, §4.5).
    std::array<uint8_t, 28> b{};
    std::size_t n = 0;
    auto put = [&](uint64_t v, int bytes) { for (int i = 0; i < bytes; ++i) b[n++] = uint8_t(v >> (8 * i)); };
    put(seed, 8);
    put(ix, 8);
    put(iy, 8);
    put(octave, 4);
    return Fx::from_raw(int32_t(stable_hash(b) >> 48));   // [0, 65535] -> [0, 1)
}
} // namespace

Fx lattice(uint64_t seed, int64_t ix, int64_t iy, uint32_t octave) {
    return lattice_u(seed, uint64_t(ix), uint64_t(iy), octave);   // same bytes as before
}

namespace {
// Floor division / modulo by 2^k for signed values (arithmetic shift is defined in C++20).
constexpr int64_t floor_shift(int64_t v, int k) { return v >> k; }
constexpr int64_t low_bits(int64_t v, int k) { return v & ((int64_t(1) << k) - 1); }

// Smoothstep 3t^2 - 2t^3 on raw Q16.16, t in [0, 1).
constexpr int64_t fade(int64_t t) {
    const int64_t one = Fx::kOne;
    const int64_t t2 = (t * t) >> 16;
    return (t2 * (3 * one - 2 * t)) >> 16;
}
constexpr int64_t lerp(int64_t a, int64_t b, int64_t s) { return a + (((b - a) * s) >> 16); }
} // namespace

Fx value2(uint64_t seed, int64_t x, int64_t y, int log2_cell, uint32_t octave) {
    if (!value2_params_valid(log2_cell)) {
        assert(!"detmath::noise::value2: log2_cell must be in [0, 16]");
        return Fx::from_raw(0);
    }
    const uint64_t ix = uint64_t(floor_shift(x, log2_cell)), iy = uint64_t(floor_shift(y, log2_cell));
    const uint64_t ix1 = ix + 1, iy1 = iy + 1;   // wrapping, never signed overflow
    // Fraction inside the cell as Q16.16: low bits scaled to 16 fractional bits.
    const int64_t fx = low_bits(x, log2_cell) << (16 - log2_cell);
    const int64_t fy = low_bits(y, log2_cell) << (16 - log2_cell);
    const int64_t v00 = lattice_u(seed, ix, iy, octave).raw, v10 = lattice_u(seed, ix1, iy, octave).raw;
    const int64_t v01 = lattice_u(seed, ix, iy1, octave).raw, v11 = lattice_u(seed, ix1, iy1, octave).raw;
    const int64_t sx = fade(fx), sy = fade(fy);
    const int64_t r = lerp(lerp(v00, v10, sx), lerp(v01, v11, sx), sy);
    return Fx::from_raw(int32_t(r));
}

Fx fbm2(uint64_t seed, int64_t x, int64_t y, int log2_cell, int octaves) {
    if (!fbm2_params_valid(log2_cell, octaves)) {
        assert(!"detmath::noise::fbm2: need log2_cell in [0, 16] and 1 <= octaves <= log2_cell + 1");
        return Fx::from_raw(0);
    }
    int64_t sum = 0, norm = 0;
    for (int o = 0; o < octaves; ++o) {
        const int64_t amp = int64_t(1) << (octaves - 1 - o);   // 2^-o, scaled to integers
        sum += amp * value2(seed, x, y, log2_cell - o, uint32_t(o)).raw;
        norm += amp;
    }
    return Fx::from_raw(int32_t(sum / norm));   // non-negative, so truncation == floor
}

} // namespace detmath::noise
