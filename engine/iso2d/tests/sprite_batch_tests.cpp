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
