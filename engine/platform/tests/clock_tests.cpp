#include <doctest/doctest.h>

#include "platform/clock.hpp"

using eng::platform::FixedTimestep;

TEST_CASE("fixed timestep accumulates exactly") {
    FixedTimestep ts(10, 8);
    CHECK(ts.advance(5) == 0);
    CHECK(ts.alpha() == doctest::Approx(0.5));
    CHECK(ts.advance(5) == 1);
    CHECK(ts.advance(25) == 2);
    CHECK(ts.alpha() == doctest::Approx(0.5));
    CHECK(ts.advance(5) == 1);
    CHECK(ts.steps_total() == 4);
}

TEST_CASE("fixed timestep step count is independent of frame slicing") {
    const uint64_t step = 1'000'000'000ull / 60;
    FixedTimestep a(step), b(step);
    uint64_t sa = 0, sb = 0;
    for (int i = 0; i < 1000; ++i) sa += a.advance(7'000'001);
    for (int i = 0; i < 500; ++i) sb += b.advance(14'000'002);
    CHECK(sa == sb);
    CHECK(sa == (1000ull * 7'000'001) / step);
}

TEST_CASE("fixed timestep clamps runaway frames") {
    FixedTimestep ts(10, 3);
    CHECK(ts.advance(100) == 3);
    CHECK(ts.dropped_ns() == 70);
    CHECK(ts.advance(0) == 0);
}
