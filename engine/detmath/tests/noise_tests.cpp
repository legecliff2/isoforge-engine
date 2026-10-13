#include <doctest/doctest.h>

#include <array>
#include <cstdlib>
#include <limits>

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

TEST_CASE("noise: defined at INT64 extremes (wrapping neighbour index)") {
    constexpr int64_t kMax = std::numeric_limits<int64_t>::max(), kMin = std::numeric_limits<int64_t>::min();
    const uint64_t s = 0x1234;
    for (int k = 0; k <= noise::kMaxLog2Cell; ++k)
        for (int64_t x : {kMin, kMin + 1, int64_t(-1), int64_t(0), kMax - 1, kMax})
            for (int64_t y : {kMin, kMax}) {
                const int32_t v = noise::value2(s, x, y, k).raw;
                CHECK((v >= 0 && v < Fx::kOne));
            }
    // log2_cell 0: fraction is 0, so the (wrapped) neighbours have zero weight.
    CHECK(noise::value2(s, kMax, kMax, 0).raw == noise::lattice(s, kMax, kMax, 0).raw);
    CHECK(noise::value2(s, kMin, kMax, 0).raw == noise::lattice(s, kMin, kMax, 0).raw);
    // Coarser cells near the top: deterministic and in range for full-depth fbm.
    for (int k = 0; k <= noise::kMaxLog2Cell; ++k) {
        const int32_t f = noise::fbm2(s, kMax, kMin, k, k + 1).raw;
        CHECK((f >= 0 && f < Fx::kOne));
        CHECK(f == noise::fbm2(s, kMax, kMin, k, k + 1).raw);
    }
}

TEST_CASE("noise: parameter preconditions") {
    CHECK(noise::value2_params_valid(0));
    CHECK(noise::value2_params_valid(16));
    CHECK_FALSE(noise::value2_params_valid(-1));
    CHECK_FALSE(noise::value2_params_valid(17));
    CHECK(noise::fbm2_params_valid(0, 1));
    CHECK(noise::fbm2_params_valid(16, 17));
    CHECK(noise::fbm2_params_valid(3, 4));
    CHECK_FALSE(noise::fbm2_params_valid(3, 5));
    CHECK_FALSE(noise::fbm2_params_valid(3, 0));
    CHECK_FALSE(noise::fbm2_params_valid(-1, 1));
    CHECK_FALSE(noise::fbm2_params_valid(17, 1));
    // Valid edges compute normally.
    CHECK(noise::value2(1, 12345, -678, 0).raw == noise::lattice(1, 12345, -678, 0).raw);
    CHECK(noise::value2(1, 65536 * 3, 0, 16).raw == noise::lattice(1, 3, 0, 0).raw);
    CHECK(noise::fbm2(1, 7, 7, 0, 1).raw == noise::value2(1, 7, 7, 0, 0).raw);
#ifdef NDEBUG
    // Release: out-of-range parameters return 0 deterministically (debug builds assert).
    CHECK(noise::value2(1, 5, 5, -1).raw == 0);
    CHECK(noise::value2(1, 5, 5, 17).raw == 0);
    CHECK(noise::fbm2(1, 5, 5, 3, 5).raw == 0);
    CHECK(noise::fbm2(1, 5, 5, 3, 0).raw == 0);
    CHECK(noise::fbm2(1, 5, 5, 17, 1).raw == 0);
#endif
}
