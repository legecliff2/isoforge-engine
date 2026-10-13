#include <doctest/doctest.h>

#include "iso2d/iso.hpp"
#include "iso2d/sprite_batch.hpp"

using namespace eng::iso2d;

TEST_CASE("iso projection 64x32 round trip") {
    IsoProjection p;
    auto s = p.to_screen(1, 0);
    CHECK(s.x == doctest::Approx(32));
    CHECK(s.y == doctest::Approx(16));
    s = p.to_screen(0, 1);
    CHECK(s.x == doctest::Approx(-32));
    CHECK(s.y == doctest::Approx(16));
    auto i = p.to_iso(p.to_screen(3.5f, -2.f).x, p.to_screen(3.5f, -2.f).y);
    CHECK(i.x == doctest::Approx(3.5));
    CHECK(i.y == doctest::Approx(-2));
}

TEST_CASE("sort key: layer, then x+y, then z, then entity") {
    auto k = [](float x, float y, int layer, int z, uint32_t id) { return make_sort_key({x, y}, layer, z, id); };
    CHECK(k(9, 9, 0, 0, 0) < k(0, 0, 1, 0, 0));  // layer dominates
    CHECK(k(1, 2, 0, 0, 0) < k(2, 2, 0, 0, 0));  // deeper (x+y) draws later
    CHECK(k(3, 0, 0, 0, 0) < k(2, 2, 0, 0, 0));
    CHECK(k(1, 1, 0, 0, 0) < k(1, 1, 0, 1, 0));  // z
    CHECK(k(1, 1, 0, 0, 1) < k(1, 1, 0, 0, 2));  // entity tie-break
    CHECK_FALSE(k(1, 1, 0, 0, 2) < k(1, 1, 0, 0, 2));
}

TEST_CASE("camera zoom: world <-> screen round trip") {
    eng::iso2d::Camera cam{{100, 50}, 800, 600, 2.f};
    auto s = cam.world_to_screen({110, 60});
    CHECK(s.x == doctest::Approx(20));
    CHECK(s.y == doctest::Approx(20));
    auto w = cam.screen_to_world(s);
    CHECK(w.x == doctest::Approx(110));
    CHECK(w.y == doctest::Approx(60));
}
