#include <doctest/doctest.h>

#include "render_null/null_device.hpp"

using namespace eng::render;

static TextureDesc rgba(uint32_t w, uint32_t h) {
    TextureDesc d{};
    d.w = w; d.h = h; d.fmt = Format::RGBA8_UNORM;
    return d;
}

TEST_CASE("null device handles are generation checked") {
    NullDevice dev;
    auto t = dev.create_texture(rgba(4, 4));
    CHECK(t.valid());
    dev.destroy(t);
    CHECK(dev.errors() == 0);
    dev.destroy(t);  // double free
    CHECK(dev.errors() == 1);
    auto t2 = dev.create_texture(rgba(4, 4));
    CHECK(t2.index == t.index);
    CHECK(t2.generation != t.generation);
}

TEST_CASE("null device records frame") {
    auto dev = create_null_device();
    CHECK(dev->caps().backend_name == "null");
    auto p = dev->create_pipeline({"sprite"});
    REQUIRE(dev->begin_frame());
    auto& cl = dev->command_list();
    cl.begin_pass(TargetId{}, {0, 0, 0, 1});
    cl.set_pipeline(p);
    cl.draw_indexed(6, 0, 0);
    cl.end_pass();
    dev->end_frame();
    auto& nd = static_cast<NullDevice&>(*dev);
    CHECK(nd.errors() == 0);
    CHECK(nd.last_frame().draw_calls == 1);
    CHECK(nd.last_frame().indices == 6);
}
