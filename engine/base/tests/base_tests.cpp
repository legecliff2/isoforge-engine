#include <doctest/doctest.h>

#include "base/handle.hpp"
#include "base/math.hpp"

struct TagA;

TEST_CASE("Handle default is null") {
    eng::Handle<TagA> h{};
    CHECK_FALSE(h.valid());
    eng::Handle<TagA> g{3, 1};
    CHECK(g.valid());
    CHECK(g == eng::Handle<TagA>{3, 1});
    CHECK_FALSE(g == eng::Handle<TagA>{3, 2});
}

TEST_CASE("Color rgba8") {
    constexpr auto c = eng::Color::rgba8(255, 0, 51, 255);
    CHECK(c.r == doctest::Approx(1.0));
    CHECK(c.b == doctest::Approx(0.2));
}
