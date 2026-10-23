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
