#include <doctest/doctest.h>

#include "base/canonical_hash.hpp"
#include "base/handle.hpp"
#include "base/math.hpp"
#include "base/present_math.hpp"

TEST_CASE("stable_hash is XXH3-64 (seed 0): reference vectors") {
    eng::CanonicalHasher empty;
    CHECK(empty.value() == 0x2D06800538D394C2ull);    // XXH3_64bits("")
    const uint8_t abc[] = {'a', 'b', 'c'};
    CHECK(eng::stable_hash(abc) == 0x78AF5F94892F3950ull);   // XXH3_64bits("abc")
}

TEST_CASE("CanonicalHasher: integers are little-endian, strings length-prefixed") {
    eng::CanonicalHasher x, y;
    x.u32(0x04030201u);
    for (uint8_t b : {1, 2, 3, 4}) y.byte(b);
    CHECK(x.value() == y.value());
    eng::CanonicalHasher s1, s2;
    s1.str("ab"); s1.str("c");
    s2.str("a"); s2.str("bc");
    CHECK(s1.value() != s2.value());
}

TEST_CASE("Vec2i is totally ordered") {
    CHECK(eng::Vec2i{1, 2} < eng::Vec2i{1, 3});
    CHECK(eng::Vec2i{0, 9} < eng::Vec2i{1, 0});
    CHECK(eng::Vec2i{4, 4} == eng::Vec2i{4, 4});
}

struct TagA;

TEST_CASE("Handle default is null") {
    eng::Handle<TagA> h{};
    CHECK_FALSE(h.valid());
    eng::Handle<TagA> g{3, 1};
    CHECK(g.valid());
    CHECK(g == eng::Handle<TagA>{3, 1});
    CHECK_FALSE(g == eng::Handle<TagA>{3, 2});
}

TEST_CASE("RectI equality and Color rgba8") {
    CHECK(eng::RectI{1, 2, 3, 4} == eng::RectI{1, 2, 3, 4});
    constexpr auto c = eng::Color::rgba8(255, 0, 51, 255);
    CHECK(c.r == doctest::Approx(1.0));
    CHECK(c.b == doctest::Approx(0.2));
}
