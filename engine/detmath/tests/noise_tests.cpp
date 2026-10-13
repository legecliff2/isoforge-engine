#include <doctest/doctest.h>

#include <array>
#include <cstdlib>

#include "detmath/noise.hpp"
#include "detmath/stable_hash.hpp"

using detmath::Fx;
namespace noise = detmath::noise;

TEST_CASE("stable_hash: XXH3-64 seed 0 known answers") {
    CHECK(detmath::stable_hash({}) == 0x2D06800538D394C2ull);   // XXH3_64bits("")
    const std::array<uint8_t, 3> abc{'a', 'b', 'c'};
    CHECK(detmath::stable_hash(abc) == 0x78AF5F94892F3950ull);
}

// Goldens identical to the values pinned when this lived in procgen (PR #16).
TEST_CASE("noise: range, lattice exactness, continuity, golden") {
    const uint64_t s = 0x1234;
    int64_t sum = 0;
    for (int64_t y = -64; y < 64; ++y)
        for (int64_t x = -64; x < 64; ++x) {
            const int32_t v = noise::value2(s, x, y, 3).raw;
            CHECK((v >= 0 && v < Fx::kOne));
            const int32_t f = noise::fbm2(s, x, y, 3, 3).raw;
            CHECK((f >= 0 && f < Fx::kOne));
            sum += f;
            if ((x & 7) == 0 && (y & 7) == 0) CHECK(v == noise::lattice(s, x >> 3, y >> 3, 0).raw);
            CHECK(std::abs(noise::value2(s, x + 1, y, 3).raw - v) <= 65536 * 3 / 8 / 2 + 1);
        }
    CHECK(sum / (128 * 128) > 20000);
    CHECK(sum / (128 * 128) < 45000);
    CHECK(noise::fbm2(s, 0, 0, 3, 3).raw == 49016);
    CHECK(noise::fbm2(s, -1000000007, 99999999999, 3, 3).raw == 34028);
}
