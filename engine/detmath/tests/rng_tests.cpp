#include <doctest/doctest.h>

#include <array>
#include <vector>
#include "golden_hash.hpp"
#include "rng/rng.hpp"

using rng::Pcg32;

TEST_CASE("Pcg32 matches the PCG reference implementation (seed 42, stream 54)") {
    // Expected values from pcg32-demo in the reference pcg-c-basic distribution.
    Pcg32 r(42u, 54u);
    const uint32_t expected[] = {0xa15c02b7, 0x7b47f409, 0xba1d3330, 0x83d2f293, 0xbfa4784b, 0xcbed606e};
    for (uint32_t e : expected) CHECK(r.next_u32() == e);
}

TEST_CASE("splitmix64 matches the reference sequence (seed 0)") {
    uint64_t x = 0;
    CHECK(rng::splitmix64(x) == 0xE220A8397B1DCDAFull);
    CHECK(rng::splitmix64(x) == 0x6E789E6AA1B965F4ull);
    CHECK(rng::splitmix64(x) == 0x06C45D188009454Full);
}

TEST_CASE("splitmix64 is constexpr") {
    constexpr uint64_t v = [] { uint64_t x = 0; return rng::splitmix64(x); }();
    static_assert(v == 0xE220A8397B1DCDAFull);
}

TEST_CASE("golden: Pcg32 output stream hash") {
    Pcg32 r(0x1234'5678'9ABC'DEF0ull, 7u);
    GoldenHash g;
    for (int i = 0; i < 10000; ++i) g.u32(r.next_u32());
    CHECK(g.h == 0x149F2459EB556576ull);
}

TEST_CASE("golden: bounded draws hash (below/range/chance/d/shuffle)") {
    Pcg32 r(2026u, 1u);
    GoldenHash g;
    for (int i = 0; i < 2000; ++i) g.u32(r.below(uint32_t(i % 97 + 1)));
    for (int i = 0; i < 2000; ++i) g.i32(r.range(-1000, 1000));
    for (int i = 0; i < 2000; ++i) g.byte(r.chance(1, 3) ? 1 : 0);
    for (int i = 0; i < 2000; ++i) g.i32(r.d(20));
    std::array<int32_t, 52> deck{};
    for (int i = 0; i < 52; ++i) deck[std::size_t(i)] = i;
    r.shuffle(std::span<int32_t>(deck));
    for (int32_t c : deck) g.i32(c);
    CHECK(g.h == 0x27581D3361C25B15ull);
}

TEST_CASE("golden: splitmix64 and derive_seed hash") {
    GoldenHash g;
    uint64_t x = 0xDEADBEEFull;
    for (int i = 0; i < 1000; ++i) g.u64(rng::splitmix64(x));
    for (int64_t cx = -8; cx <= 8; ++cx)
        for (int64_t cy = -8; cy <= 8; ++cy) {
            const int64_t keys[] = {cx, cy, 0};
            g.u64(rng::derive_seed(42u, keys));
        }
    CHECK(g.h == 0xBC89EA4EE8C841E7ull);
}

TEST_CASE("derive_seed is order-sensitive and key-sensitive") {
    const int64_t ab[] = {1, 2}, ba[] = {2, 1}, a[] = {1};
    CHECK(rng::derive_seed(7, ab) != rng::derive_seed(7, ba));
    CHECK(rng::derive_seed(7, ab) != rng::derive_seed(7, a));
    CHECK(rng::derive_seed(7, ab) == rng::derive_seed(7, ab));
    CHECK(rng::derive_seed(7, ab) != rng::derive_seed(8, ab));
}

TEST_CASE("different streams are independent") {
    Pcg32 a(1, 1), b(1, 2);
    int same = 0;
    for (int i = 0; i < 100; ++i) same += a.next_u32() == b.next_u32();
    CHECK(same < 3);
}

TEST_CASE("save/load round-trips state") {
    Pcg32 r(99, 3);
    for (int i = 0; i < 10; ++i) r.next_u32();
    auto s = r.save();
    std::vector<uint32_t> first;
    for (int i = 0; i < 20; ++i) first.push_back(r.next_u32());
    Pcg32 other(0, 0);
    other.load(s);
    for (uint32_t v : first) CHECK(other.next_u32() == v);
}

TEST_CASE("bounded helpers stay in range") {
    Pcg32 r(5, 5);
    for (int i = 0; i < 5000; ++i) {
        CHECK(r.below(6) < 6u);
        int32_t v = r.range(-3, 3);
        CHECK(v >= -3); CHECK(v <= 3);
        int d = r.d(12);
        CHECK(d >= 1); CHECK(d <= 12);
    }
    CHECK(r.below(0) == 0u);
    CHECK(r.below(1) == 0u);
    CHECK(r.range(5, 5) == 5);
    int32_t full = r.range(INT32_MIN, INT32_MAX);  // full span path
    (void)full;
    CHECK_FALSE(r.chance(0, 10));
    CHECK(r.chance(10, 10));
    CHECK_FALSE(r.chance(1, 0));
}

TEST_CASE("d(20) hits every face with roughly uniform frequency") {
    Pcg32 r(123, 0);
    std::array<int, 21> hist{};
    for (int i = 0; i < 20000; ++i) ++hist[std::size_t(r.d(20))];
    CHECK(hist[0] == 0);
    for (int f = 1; f <= 20; ++f) { CHECK(hist[std::size_t(f)] > 850); CHECK(hist[std::size_t(f)] < 1150); }
}
