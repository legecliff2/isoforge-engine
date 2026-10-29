#include <doctest/doctest.h>

#include "iso2d/sprite_batch.hpp"
#include "render_null/null_device.hpp"

#include <algorithm>

using namespace eng;
using namespace eng::iso2d;
using namespace eng::render;

static TextureDesc rgba(uint32_t w, uint32_t h) {
    TextureDesc d{};
    d.w = w; d.h = h; d.fmt = Format::RGBA8_UNORM;
    return d;
}

TEST_CASE("sprite uv: swapping u0 and u1 H-flips the quad") {
    float u[4], v[4];
    sprite_corner_uv(0.f, 0.f, 1.f, 1.f, u, v);
    CHECK(u[0] == doctest::Approx(0.f));
    CHECK(u[1] == doctest::Approx(1.f));
    CHECK(v[0] == doctest::Approx(0.f));
    CHECK(v[2] == doctest::Approx(1.f));
    // Left edge reads the former right edge. The batch emits these corners; no extra flag.
    sprite_corner_uv(1.f, 0.25f, 0.f, 0.75f, u, v);
    CHECK(u[0] == doctest::Approx(1.f));
    CHECK(u[1] == doctest::Approx(0.f));
    CHECK(u[2] == doctest::Approx(0.f));
    CHECK(u[3] == doctest::Approx(1.f));
    CHECK(v[1] == doctest::Approx(0.25f));
    CHECK(v[3] == doctest::Approx(0.75f));
}

TEST_CASE("sprite batch sorts by depth and batches by texture") {
    NullDevice dev;
    auto ta = dev.create_texture(rgba(2, 2));
    auto tb = dev.create_texture(rgba(2, 2));
    SpriteBatch batch(dev, 64);
    batch.begin();
    Sprite a{ta, 64, 32}, b{tb, 64, 64};
    batch.draw(b, {2, 2}, 1, 7);  // 0: pillar, layer 1
    batch.draw(a, {1, 1}, 0, 2);  // 1
    batch.draw(a, {0, 0}, 0, 1);  // 2
    batch.draw(a, {1, 0}, 0, 3);  // 3
    batch.draw(b, {0, 1}, 1, 5);  // 4: pillar, layer 1, shallower than #0
    batch.prepare({{0, 0}, 640, 480});
    CHECK(batch.order() == std::vector<uint32_t>{2, 3, 1, 4, 0});
    CHECK(batch.batch_count() == 2);  // all ground (ta) then all pillars (tb)

    REQUIRE(dev.begin_frame());
    auto& cl = dev.command_list();
    cl.begin_pass(TargetId{}, {});
    batch.submit(cl);
    cl.end_pass();
    dev.end_frame();
    CHECK(dev.errors() == 0);
    REQUIRE(dev.last_frame().draw_calls == 2);
    CHECK(dev.last_frame().draws[0].texture == ta);
    CHECK(dev.last_frame().draws[0].index_count == 18);
    CHECK(dev.last_frame().draws[1].first_index == 18);
}

namespace {
// Centre of an NxN footprint anchored at `anchor`: the 1x1 sort reference (its tile when n is 1).
// The front corner is anchor + (n-1, n-1). Depth is iso_x + iso_y.
Vec2f footprint_centre(Vec2i anchor, int n) {
    const float back = float(n - 1) * 0.5f;
    return {float(anchor.x) + back, float(anchor.y) + back};
}
Vec2f footprint_front(Vec2i anchor, int n) {
    const float back = float(n - 1);
    return {float(anchor.x) + back, float(anchor.y) + back};
}
} // namespace

TEST_CASE("sort position, not the projected anchor, decides order") {
    NullDevice dev;
    Sprite s{TextureId{}, 8, 8};
    SpriteBatch batch(dev, 8);
    batch.begin();
    batch.draw(s, {0, 0}, {5, 5}, 0, 1, 0);   // 0 drawn at the back, sorted at the front
    batch.draw(s, {5, 5}, {0, 0}, 0, 2, 0);   // 1 drawn at the front, sorted at the back
    batch.prepare({});
    CHECK(batch.order() == std::vector<uint32_t>{1, 0});
    // The key is the historical one of the sort sample.
    CHECK_FALSE(make_sort_key({5, 5}, 0, 0, 1) < make_sort_key({0, 0}, 0, 0, 2));
    CHECK(make_sort_key({0, 0}, 0, 0, 2) < make_sort_key({5, 5}, 0, 0, 1));
}

TEST_CASE("1x1 sort is unchanged when the sort point is the draw point") {
    NullDevice dev;
    Sprite s{TextureId{}, 8, 8};
    auto run = [&](bool split) {
        SpriteBatch batch(dev, 64);
        batch.begin();
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x) {
                const Vec2f p{float(x), float(y)};
                const uint32_t id = uint32_t(y * 4 + x);
                if (split) batch.draw(s, p, p, 0, id, 0);
                else batch.draw(s, p, 0, id, 0);
                if ((x + y) % 3 == 0) {
                    if (split) batch.draw(s, p, p, 1, 100 + id, 4);
                    else batch.draw(s, p, 1, 100 + id, 4);
                }
            }
        batch.prepare({});
        return batch.order();
    };
    const auto historical = run(false);
    CHECK(run(true) == historical);
    // layer, then x+y, then z, then id. Re-running the split form must not reshuffle ties.
    CHECK(run(true) == historical);
}

TEST_CASE("multi-tile sort: 1x1, front and back neighbours, ties") {
    NullDevice dev;
    Sprite s{TextureId{}, 8, 8};
    const Vec2i anchor{4, 5};
    for (int n : {1, 2, 3, 4}) {
        CAPTURE(n);
        const Vec2f centre = footprint_centre(anchor, n);
        const Vec2f front = footprint_front(anchor, n);
        const float centre_d = centre.x + centre.y;
        const float front_d = front.x + front.y;
        if (n == 1) {
            CHECK(centre_d == front_d);
            CHECK(centre.x == float(anchor.x));
            CHECK(centre.y == float(anchor.y));
        } else {
            CHECK(centre_d < front_d);
        }

        // Back: west of the anchor. Strictly behind the centre for every side.
        const Vec2f back_n{float(anchor.x - 1), float(anchor.y)};
        REQUIRE(back_n.x + back_n.y < centre_d);

        // Front: strictly in front of the centre. 3x3 and 4x4 have a whole tile between the
        // centre and the front corner; that is the neighbour the corner sort put under the body.
        // 2x2 has no integer depth in between, so the front neighbour is the corner diagonal
        // itself (in front of the centre, tied with a corner-sorted body). 1x1's front neighbour
        // is the next tile, the historical order.
        const Vec2f front_n{float(anchor.x + n), float(anchor.y)};
        REQUIRE(front_n.x + front_n.y > centre_d);
        if (n > 2) REQUIRE(front_n.x + front_n.y < front_d);
        if (n == 2) CHECK(front_n.x + front_n.y == front_d);
        if (n == 1) CHECK(front_n.x + front_n.y == centre_d + 1.f);

        // Same depth as the centre, off to the side. Lower id draws behind the body, higher in front.
        const Vec2f tie_n{float(anchor.x + n), float(anchor.y - 1)};
        CHECK(tie_n.x + tie_n.y == doctest::Approx(centre_d));

        SpriteBatch batch(dev, 16);
        batch.begin();
        // Submitted out of order. Draw anchors are deliberately not the sort samples.
        batch.draw(s, front, centre, 1, /*id*/ 10, 4);   // 0 body
        batch.draw(s, {9, 9}, front_n, 1, 30, 4);        // 1 front neighbour
        batch.draw(s, {0, 0}, back_n, 1, 20, 4);         // 2 back neighbour
        batch.draw(s, tie_n, tie_n, 1, 5, 4);            // 3 tie, lower id
        batch.draw(s, tie_n, tie_n, 1, 40, 4);           // 4 tie, higher id
        batch.draw(s, centre, centre, 1, 10, 4);         // 5 same key as the body; stable, after it
        batch.draw(s, tie_n, 1, 1, 0);                   // 6 same depth, lower z (a wall on that diagonal)
        batch.prepare({});
        // back, then the centre diagonal by (z, id, submission), then the front neighbour.
        CHECK(batch.order() == std::vector<uint32_t>{2, 6, 3, 0, 5, 4, 1});

        if (n == 1) {
            SpriteBatch historical(dev, 16);
            historical.begin();
            historical.draw(s, centre, 1, 10, 4);
            historical.draw(s, front_n, 1, 30, 4);
            historical.draw(s, back_n, 1, 20, 4);
            historical.draw(s, tie_n, 1, 5, 4);
            historical.draw(s, tie_n, 1, 40, 4);
            historical.draw(s, centre, 1, 10, 4);
            historical.draw(s, tie_n, 1, 1, 0);
            historical.prepare({});
            CHECK(historical.order() == batch.order());
            CHECK_FALSE(make_sort_key(centre, 1, 4, 10) < make_sort_key(front, 1, 4, 10));
            CHECK_FALSE(make_sort_key(front, 1, 4, 10) < make_sort_key(centre, 1, 4, 10));
        }
    }
}

TEST_CASE("sort is deterministic regardless of submission order") {
    NullDevice dev;
    auto t = dev.create_texture(rgba(1, 1));
    auto run = [&](bool reversed) {
        SpriteBatch batch(dev, 256);
        batch.begin();
        std::vector<std::pair<int, int>> cells;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) cells.push_back({x, y});
        if (reversed) std::reverse(cells.begin(), cells.end());
        for (auto [x, y] : cells) batch.draw({t, 64, 32}, {float(x), float(y)}, 0, uint32_t(y * 8 + x));
        batch.prepare({});
        std::vector<int> ids;
        for (uint32_t i : batch.order()) ids.push_back(reversed ? 63 - int(i) : int(i));
        return ids;
    };
    CHECK(run(false) == run(true));
}

namespace {
struct Drawn { std::vector<uint32_t> order; std::vector<std::pair<uint32_t, uint32_t>> runs; uint32_t quads = 0, pages = 0; };
// Same deterministic sprite stream into a batch with `page` quads per vertex page.
Drawn draw_stream(uint32_t page, uint32_t n) {
    NullDevice dev;
    const TextureId t[3] = {dev.create_texture(rgba(2, 2)), dev.create_texture(rgba(2, 2)), dev.create_texture(rgba(2, 2))};
    SpriteBatch batch(dev, page);
    Drawn out;
    for (int frame = 0; frame < 2; ++frame) {   // second frame reuses the pages
        batch.begin();
        for (uint32_t i = 0; i < n; ++i) {
            Sprite s{t[(i * 7 / 5) % 3], 64, 32};
            const float x = float((i * 37) % 23), y = float((i * 11) % 19);
            batch.draw(s, {x, y}, int32_t(i % 3 == 0), (i * 13) % 50, int32_t(i % 4));
        }
        batch.prepare({{0, 0}, 640, 480});
        REQUIRE(dev.begin_frame());
        auto& cl = dev.command_list();
        cl.begin_pass(TargetId{}, {});
        batch.submit(cl);
        cl.end_pass();
        dev.end_frame();
    }
    CHECK(dev.errors() == 0);
    out.order = batch.order();
    out.pages = batch.page_count();
    for (const auto& d : dev.last_frame().draws) {   // coalesce page splits back into texture runs
        out.quads += d.index_count / 6;
        if (!out.runs.empty() && out.runs.back().first == d.texture.index) out.runs.back().second += d.index_count / 6;
        else out.runs.push_back({d.texture.index, d.index_count / 6});
    }
    return out;
}
} // namespace

TEST_CASE("sprite batch never drops: overflow splits into pages and keeps the unsplit draw order") {
    const uint32_t n = 1000;
    const Drawn big = draw_stream(4096, n);   // fits one page
    CHECK(big.pages == 1);
    CHECK(big.quads == n);
    for (uint32_t page : {64u, 100u, 999u}) {
        CAPTURE(page);
        const Drawn split = draw_stream(page, n);
        CHECK(split.pages == (n + page - 1) / page);
        CHECK(split.quads == n);            // every sprite drawn
        CHECK(split.order == big.order);    // same global (layer, x + y, z, id) order
        CHECK(split.runs == big.runs);      // same texture sequence once page splits are merged back
        CHECK(draw_stream(page, n).runs == split.runs);   // deterministic
    }
    // order() is a permutation sorted by the documented key
    std::vector<uint32_t> sorted = big.order;
    std::sort(sorted.begin(), sorted.end());
    for (uint32_t i = 0; i < n; ++i) CHECK(sorted[i] == i);
}
