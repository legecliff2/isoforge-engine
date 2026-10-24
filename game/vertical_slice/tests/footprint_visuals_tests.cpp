// Footprint presentation: anchor, centre (odd and even), the NxN base ring, and depth.
// Ground rings sort on the front tile. The body is drawn there and sorted at the centre.
#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "footprint_visuals.hpp"
#include "iso2d/iso.hpp"
#include "iso2d/sprite_batch.hpp"
#include "render_null/null_device.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
constexpr float kW = 64.f, kH = 32.f;

Vec2f screen(const iso2d::IsoProjection& p, float x, float y) { return p.to_screen(x, y); }

// The pawn's historical anchor pixel lands here. Feet sit half a tile below that pixel.
Vec2f anchor_pixel(const iso2d::IsoProjection& p, const FootprintGeom& g) {
    Vec2f s = screen(p, g.sort_iso.x, g.sort_iso.y);
    s.y -= g.centre_anchor_y;
    return s;
}
} // namespace

TEST_CASE("footprint geom: 1x1 is the anchor tile, with no slide and no scale") {
    const FootprintGeom g = footprint_geom({4, 7}, 1, kW, kH);
    CHECK(g.n == 1);
    CHECK(g.sort_iso.x == 4.f);
    CHECK(g.sort_iso.y == 7.f);
    CHECK(g.centre_iso.x == 4.f);
    CHECK(g.centre_iso.y == 7.f);
    CHECK(g.centre_anchor_y == 0.f);
    CHECK(g.centre_tile == Vec2i{4, 7});
    CHECK(g.pawn_scale == 1.f);
    CHECK(g.ring_w == kW);
    CHECK(g.ring_h == kH);
    CHECK(g.ring_anchor_x == kW / 2.f);
    CHECK(g.ring_anchor_y == 0.f);
    CHECK(g.sort_iso.x + g.sort_iso.y == 11.f);
    CHECK(actor_sort_iso(g).x == g.sort_iso.x);
    CHECK(actor_sort_iso(g).y == g.sort_iso.y);

    float w = 28.f, h = 44.f, ax = 14.f, ay = 24.f;
    place_on_footprint(w, h, ax, ay, g, true);
    CHECK(w == 28.f);
    CHECK(h == 44.f);
    CHECK(ax == 14.f);
    CHECK(ay == 24.f);
}

TEST_CASE("footprint centre and ring: 1x1, 2x2, 3x3 and 4x4, odd and even") {
    const iso2d::IsoProjection proj;   // 64 x 32, the slice's tile
    REQUIRE(proj.tile_w == kW);
    REQUIRE(proj.tile_h == kH);
    const Vec2i anchors[] = {{0, 0}, {3, 5}, {-4, 2}};
    for (Vec2i anchor : anchors) {
        for (int n : {1, 2, 3, 4}) {
            CAPTURE(anchor.x);
            CAPTURE(anchor.y);
            CAPTURE(n);
            const FootprintGeom g = footprint_geom(anchor, n, kW, kH);
            CHECK(g.n == n);
            CHECK(g.pawn_scale == float(n));
            CHECK(g.sort_iso.x == float(anchor.x + n - 1));
            CHECK(g.sort_iso.y == float(anchor.y + n - 1));
            CHECK(g.centre_iso.x == float(anchor.x) + float(n - 1) * 0.5f);
            CHECK(g.centre_iso.y == float(anchor.y) + float(n - 1) * 0.5f);
            CHECK(g.centre_tile == Vec2i{anchor.x + (n - 1) / 2, anchor.y + (n - 1) / 2});

            float max_depth = -1.e9f;
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    max_depth = std::max(max_depth, float(anchor.x + x + anchor.y + y));
            CHECK(g.sort_iso.x + g.sort_iso.y == max_depth);
            if (n > 1) CHECK(g.centre_iso.x + g.centre_iso.y < g.sort_iso.x + g.sort_iso.y);
            CHECK(actor_sort_iso(g).x == g.centre_iso.x);
            CHECK(actor_sort_iso(g).y == g.centre_iso.y);

            // The historical anchor pixel, drawn at the front tile with centre_anchor_y, lands on
            // centre_iso. Feet (half a tile below that pixel) are the footprint centre: the centre
            // of the middle tile when n is odd, the shared corner when n is even.
            const Vec2f pixel = anchor_pixel(proj, g);
            const Vec2f centre_top = screen(proj, g.centre_iso.x, g.centre_iso.y);
            CHECK(pixel.x == centre_top.x);
            CHECK(pixel.y == centre_top.y);
            const Vec2f feet{pixel.x, pixel.y + kH * 0.5f};
            const Vec2f expect = screen(proj, float(anchor.x) + float(n) * 0.5f, float(anchor.y) + float(n) * 0.5f);
            CHECK(feet.x == expect.x);
            CHECK(feet.y == expect.y);
            // Scaled pawn: the pixel that sat on the 1x1 tile centre stays on the footprint
            // centre, and the body grows up from those feet (grow_lift).
            float pw = 28.f, ph = 44.f, pax = 14.f, pay = 24.f;
            place_on_footprint(pw, ph, pax, pay, g, true);
            const float feet_local = (24.f + kH * 0.5f) * float(n);
            const Vec2f placed = screen(proj, g.sort_iso.x, g.sort_iso.y);
            CHECK(placed.x == expect.x);
            CHECK(placed.y + feet_local - pay == expect.y);
            CHECK(g.grow_lift == kH * 0.5f * float(n - 1));
            if ((n % 2) == 1) {
                const Vec2f mid = screen(proj, float(g.centre_tile.x) + 0.5f, float(g.centre_tile.y) + 0.5f);
                CHECK(feet.x == mid.x);
                CHECK(feet.y == mid.y);
            } else {
                const Vec2f corner = screen(proj, float(anchor.x + n / 2), float(anchor.y + n / 2));
                CHECK(feet.x == corner.x);
                CHECK(feet.y == corner.y);
            }

            // The base ring's diamond is exactly the NxN footprint.
            const Vec2f top = screen(proj, g.sort_iso.x, g.sort_iso.y);
            CHECK(top.y - g.ring_anchor_y == screen(proj, float(anchor.x), float(anchor.y)).y);
            CHECK(top.x == screen(proj, float(anchor.x), float(anchor.y)).x);
            CHECK(g.ring_w == kW * float(n));
            CHECK(g.ring_h == kH * float(n));
            const float top_y = top.y - g.ring_anchor_y;
            CHECK(top_y + g.ring_h == screen(proj, float(anchor.x + n), float(anchor.y + n)).y);
            CHECK(top.x - g.ring_w * 0.5f == screen(proj, float(anchor.x), float(anchor.y + n)).x);
            CHECK(top.x + g.ring_w * 0.5f == screen(proj, float(anchor.x + n), float(anchor.y)).x);
        }
    }
}

TEST_CASE("footprint side: a non-positive side is one tile") {
    CHECK(footprint_geom({1, 1}, 0, kW, kH).n == 1);
    CHECK(footprint_geom({1, 1}, -4, kW, kH).sort_iso.x == 1.f);
}

TEST_CASE("footprint sort: front tile draws after its own floors and before the tile past the front") {
    render::NullDevice dev;
    iso2d::SpriteBatch batch(dev, 32);
    iso2d::Sprite s;
    s.w = 1.f;
    s.h = 1.f;
    // 3x3 at (4, 5). Front tile (6, 7), depth 13. Centre is (5, 6), depth 11, and would lose to the front floor.
    const FootprintGeom g = footprint_geom({4, 5}, 3, kW, kH);
    CHECK(g.centre_iso.x + g.centre_iso.y == 11.f);
    CHECK(g.sort_iso.x + g.sort_iso.y == 13.f);
    batch.begin();
    batch.draw(s, g.sort_iso, 0, 1, 9);    // 0 ring, layer 0, front depth, above floors
    batch.draw(s, {4.f, 5.f}, 0, 2, 0);    // 1 anchor floor, depth 9
    batch.draw(s, {6.f, 7.f}, 0, 3, 0);    // 2 front floor, same depth as the ring, lower z
    batch.draw(s, {7.f, 7.f}, 0, 4, 0);    // 3 the tile past the front, depth 14
    batch.draw(s, {6.f, 6.f}, 1, 5, 4);    // 4 neighbour between centre (11) and the front corner (13)
    batch.draw(s, g.sort_iso, actor_sort_iso(g), 1, 6, 4);   // 5 body drawn on the front anchor, sorted at the centre
    batch.draw(s, {7.f, 8.f}, 1, 7, 0);    // 6 wall strictly in front of the corner, depth 15
    iso2d::Camera cam;
    cam.viewport_w = 64;
    cam.viewport_h = 64;
    batch.prepare(cam);
    // The ring still covers the footprint floors. The neighbour in front of the centre paints over the body.
    CHECK(batch.order() == std::vector<uint32_t>{1, 2, 0, 3, 5, 4, 6});
}

TEST_CASE("footprint actor sort: 1x1 matches the tile, and 2x2 3x3 4x4 front, back and ties") {
    render::NullDevice dev;
    iso2d::Sprite s;
    s.w = 1.f;
    s.h = 1.f;
    const Vec2i anchor{4, 5};
    for (int n : {1, 2, 3, 4}) {
        CAPTURE(n);
        const FootprintGeom g = footprint_geom(anchor, n, kW, kH);
        const Vec2f back{float(anchor.x - 1), float(anchor.y)};
        const Vec2f front_n{float(anchor.x + n), float(anchor.y)};
        const Vec2f tie{float(anchor.x + n), float(anchor.y - 1)};
        REQUIRE(back.x + back.y < actor_sort_iso(g).x + actor_sort_iso(g).y);
        REQUIRE(front_n.x + front_n.y > actor_sort_iso(g).x + actor_sort_iso(g).y);
        if (n > 2) REQUIRE(front_n.x + front_n.y < g.sort_iso.x + g.sort_iso.y);
        CHECK(tie.x + tie.y == doctest::Approx(actor_sort_iso(g).x + actor_sort_iso(g).y));

        iso2d::SpriteBatch batch(dev, 16);
        batch.begin();
        batch.draw(s, g.sort_iso, actor_sort_iso(g), 1, 10, 4);   // 0 body
        batch.draw(s, front_n, 1, 30, 4);                         // 1 in front of the centre
        batch.draw(s, back, 1, 20, 4);                            // 2 behind the centre
        batch.draw(s, tie, 1, 5, 4);                              // 3 tie, lower id
        batch.draw(s, tie, 1, 40, 4);                             // 4 tie, higher id
        batch.prepare({});
        CHECK(batch.order() == std::vector<uint32_t>{2, 3, 0, 4, 1});

        if (n == 1) {
            iso2d::SpriteBatch historical(dev, 16);
            historical.begin();
            historical.draw(s, g.sort_iso, 1, 10, 4);
            historical.draw(s, front_n, 1, 30, 4);
            historical.draw(s, back, 1, 20, 4);
            historical.draw(s, tie, 1, 5, 4);
            historical.draw(s, tie, 1, 40, 4);
            historical.prepare({});
            CHECK(historical.order() == batch.order());
        }
    }
}

TEST_CASE("slice view: a multi-tile base ring is one extra sprite; side 1 is unchanged and stable") {
    auto actor = [](int side, bool current, bool outlined) {
        ViewModel vm;
        ActorView a;
        a.id = 7;
        a.name = "OGRE";
        a.tile = {4, 6};
        a.footprint = side;
        a.hp = 20;
        a.hp_max = 30;
        a.current = current;
        a.outlined = outlined;
        vm.actors.push_back(a);
        TileView t;
        t.tile = {4, 6};
        t.kind = TileKind::Floor;
        vm.tiles.push_back(t);
        return vm;
    };
    auto sprites = [](const ViewModel& vm) {
        render::NullDevice dev;
        dev.set_hash_uploads(true);
        SliceView view(dev);
        iso2d::Camera cam;
        cam.viewport_w = 640;
        cam.viewport_h = 360;
        REQUIRE(dev.begin_frame());
        view.prepare(vm, cam, 640, 360);
        auto& cl = dev.command_list();
        cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
        view.submit(cl);
        cl.end_pass();
        dev.end_frame();
        CHECK(dev.errors() == 0);
        return std::pair<uint32_t, uint64_t>{view.world_sprites(), dev.last_frame().upload_hash};
    };
    const auto one = sprites(actor(1, false, false));
    const auto one_again = sprites(actor(1, false, false));
    CHECK(one == one_again);
    CHECK(one.first == 2);   // floor + pawn. No ring on a 1x1 that is not selected.

    const auto selected = sprites(actor(1, true, false));
    CHECK(selected.first == one.first + 1);   // current-turn ring
    const auto outlined = sprites(actor(1, false, true));
    CHECK(outlined.first == one.first + 1);

    for (int n : {2, 3, 4}) {
        CAPTURE(n);
        const auto base = sprites(actor(n, false, false));
        const auto base2 = sprites(actor(n, false, false));
        CHECK(base == base2);                  // no RNG in the footprint draw
        CHECK(base.first == one.first + 1);    // dim base ring covering the footprint
        CHECK(base.second != one.second);
        const auto cur = sprites(actor(n, true, false));
        CHECK(cur.first == selected.first);    // gold ring replaces the dim one: still one ring
        CHECK(cur.second != selected.second);
        const auto both = sprites(actor(n, false, true));
        CHECK(both.first == outlined.first + 1);   // dim base ring plus the outline
    }

    render::NullDevice dev;
    SliceView view(dev);
    ViewModel vm = actor(2, false, false);
    const Vec2f focus = view.actors_focus_px(vm);
    const Vec2f centre = view.tile_center_px(4.5f, 6.5f);   // even centre, between the four tiles
    CHECK(focus.x == doctest::Approx(centre.x));
    vm.actors[0].footprint = 4;
    const Vec2f huge = view.actors_focus_px(vm);
    const Vec2f huge_centre = view.tile_center_px(5.5f, 7.5f);
    CHECK(huge.x == doctest::Approx(huge_centre.x));
    vm.actors[0].footprint = 3;
    const Vec2f mid = view.actors_focus_px(vm);
    CHECK(mid.x == doctest::Approx(view.tile_center_px(5.f, 7.f).x));   // odd: middle tile
    vm.actors[0].footprint = 1;
    CHECK(view.actors_focus_px(vm).x == doctest::Approx(view.tile_center_px(4.f, 6.f).x));
}
