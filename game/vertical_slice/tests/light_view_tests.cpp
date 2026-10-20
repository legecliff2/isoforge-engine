#include <doctest/doctest.h>

#include <memory>

#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
std::unique_ptr<Session> open_sim(bool torches = true) {
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.pc_torches = torches;
    std::string err;
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    return s;
}
uint8_t at(const ViewModel& vm, Vec2i t) {
    const LightChunkView* c = vm.light.chunk_of_tile(t);
    REQUIRE(c);
    return c->samples[size_t(t.y * 32 + t.x)];
}
const ActorView& pc(const ViewModel& vm) {
    for (const auto& a : vm.actors) if (a.is_pc) return a;
    FAIL("no PC");
    return vm.actors.front();
}
void frame(render::NullDevice& dev, SliceView& v, const ViewModel& vm) {
    iso2d::Camera cam;
    REQUIRE(dev.begin_frame());
    v.prepare(vm, cam, 1280, 720);
    auto& cl = dev.command_list();
    cl.begin_pass(render::TargetId{}, Color{0, 0, 0, 1});
    v.submit(cl);
    cl.end_pass();
    dev.end_frame();
}
} // namespace

TEST_CASE("session copies the sim LightFrame: samples, dirty set, sources with stable ids, carrier, preset") {
    auto s = open_sim();
    const ViewModel vm = s->view();
    REQUIRE(vm.light.present);
    REQUIRE(vm.light.chunks.size() == 1);
    CHECK(vm.light.added.size() == 1);
    REQUIRE(vm.light.dirty.size() == 1);   // first frame: the chunk is new
    CHECK(vm.light.dirty[0].rect == RectI{0, 0, 32, 32});
    CHECK_FALSE(vm.light.chunks[0].has_fog);   // LightFrame has no visibility masks yet
    const ActorView& p = pc(vm);
    REQUIRE(vm.light.sources.size() == 1);
    const LightSourceView& src = vm.light.sources[0];
    CHECK(src.attached);
    CHECK(src.index == 1);
    CHECK(src.carrier == 1);
    CHECK(src.carrier_id == p.id);
    CHECK(src.tile == p.tile);
    CHECK(src.lit);
    CHECK(src.preset == "srd51.core.light.torch");
    CHECK((at(vm, p.tile) & 3) == uint8_t(iso2d::LightLevel::Bright));
    // Underground ambient: far corners are Dark, the 20-40 ft ring is Dim (where nothing blocks).
    bool any_dark = false;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) any_dark |= (at(vm, {x, y}) & 3) == 0;
    CHECK(any_dark);

    // Steps without movement keep samples: no dirty chunk; a move makes the chunk dirty.
    s->step();
    const ViewModel vm2 = s->view();
    CHECK(vm2.light.frame > vm.light.frame);
    CHECK(vm2.light.added.empty());
    if (pc(vm2).tile == p.tile) CHECK(vm2.light.chunks[0].samples == vm.light.chunks[0].samples);
}

TEST_CASE("no torches: ambient only (underground = Dark everywhere); render path stays valid") {
    auto s = open_sim(false);
    const ViewModel vm = s->view();
    REQUIRE(vm.light.present);
    CHECK(vm.light.sources.empty());
    for (uint8_t b : vm.light.chunks[0].samples) CHECK((b & 3) == 0);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(dev.errors() == 0);
}

TEST_CASE("render mirrors the real frame: one R8 map, full upload once, minimal rects after, none when unchanged") {
    auto s = open_sim();
    render::NullDevice dev;
    SliceView v(dev);
    ViewModel vm = s->view();
    frame(dev, v, vm);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(v.light_maps().uploads() == 1);
    CHECK(v.light_maps().bytes_uploaded() == 1024);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) CHECK(v.light_maps().sample({0, 0, 0}, {x, y}) == at(vm, {x, y}));
    frame(dev, v, vm);   // same frame again: nothing
    CHECK(v.light_maps().uploads() == 1);
    uint64_t bytes = v.light_maps().bytes_uploaded();
    for (int i = 0; i < 40 && !s->ended(); ++i) {
        s->step();
        vm = s->view();
        frame(dev, v, vm);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) REQUIRE(v.light_maps().sample({0, 0, 0}, {x, y}) == at(vm, {x, y}));
    }
    CHECK(v.light_maps().bytes_uploaded() - bytes < uint64_t(1024) * 40);   // shrunk rects, not a chunk per step
    CHECK(dev.errors() == 0);
}

TEST_CASE("render without a LightFrame and fog masks (when present) still behave") {
    // Static view: no sim, no light.
    std::string err;
    auto st = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE(st);
    CHECK_FALSE(st->view().light.present);
    // Hand-built masks (the contract shape for when the sim publishes them): unseen foes are hidden.
    ViewModel vm = open_sim()->view();
    LightChunkView& c = vm.light.chunks[0];
    c.has_fog = true;
    const Vec2i p = pc(vm).tile;
    for (int y = p.y - 2; y <= p.y + 2; ++y)
        for (int x = p.x - 2; x <= p.x + 2; ++x)
            if (x >= 0 && y >= 0 && x < 32 && y < 32) {
                c.seen_now[size_t((y * 32 + x) / 64)] |= uint64_t(1) << ((y * 32 + x) % 64);
                c.explored[size_t((y * 32 + x) / 64)] |= uint64_t(1) << ((y * 32 + x) % 64);
            }
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    int far_foes = 0;
    for (const auto& a : vm.actors)
        if (!a.is_pc && std::max(std::abs(a.tile.x - p.x), std::abs(a.tile.y - p.y)) > 2) ++far_foes;
    CHECK(v.world_sprites() == vm.tiles.size() + vm.light.sources.size() + vm.actors.size() - size_t(far_foes) + 1);   // +1 current-turn ring
    CHECK(iso2d::light_tint(0xFF, iso2d::Fog::Unexplored).r == 0.f);
}
