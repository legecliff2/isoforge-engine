#include <doctest/doctest.h>

#include "light_standin.hpp"
#include "render_null/null_device.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
ViewModel arena() {
    ViewModel vm;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) vm.tiles.push_back({{x, y}, 0, 0, TileKind::Floor});
    ActorView pc;
    pc.id = 1; pc.name = "FIGHTER"; pc.tile = {4, 4}; pc.hp = pc.hp_max = 10; pc.is_pc = true;
    ActorView near_foe;
    near_foe.id = 2; near_foe.name = "GOBLIN"; near_foe.tile = {6, 5}; near_foe.hp = near_foe.hp_max = 7; near_foe.team = 1;
    ActorView far_foe = near_foe;
    far_foe.id = 3; far_foe.tile = {28, 28};
    vm.actors = {pc, near_foe, far_foe};
    return vm;
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

TEST_CASE("light view: no LightFrame = unchanged unlit view, no light maps") {
    render::NullDevice dev;
    SliceView v(dev);
    ViewModel vm = arena();
    frame(dev, v, vm);
    CHECK(v.light_maps().chunks() == 0);
    CHECK(v.world_sprites() == 32 * 32 + 3);   // every actor drawn
    CHECK(dev.errors() == 0);
}

TEST_CASE("light view: per-chunk R8 map from the frame's StreamDelta; dirty-rect uploads; fog hides unseen foes") {
    render::NullDevice dev;
    SliceView v(dev);
    LightStandinState st;
    ViewModel vm = arena();
    fill_standin_light(vm, st);
    REQUIRE(vm.light.present);
    REQUIRE(vm.light.dirty.size() == 1);
    CHECK(vm.light.dirty[0].rect == RectI{0, 0, 32, 32});   // first frame: whole chunk
    frame(dev, v, vm);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(v.light_maps().uploads() == 1);
    CHECK(v.light_maps().bytes_uploaded() == 1024);
    CHECK(v.light_maps().sample({0, 0, 0}, {4, 4}) == uint8_t(iso2d::LightLevel::Bright));
    CHECK(v.light_maps().sample({0, 0, 0}, {20, 20}) == uint8_t(iso2d::LightLevel::Dark));
    // Tiles + 1 torch glow + PC + near goblin; the far goblin is outside what the team sees.
    CHECK(v.world_sprites() == 32 * 32 + 1 + 2);

    // Same frame content again: nothing dirty, no upload.
    ViewModel vm2 = arena();
    fill_standin_light(vm2, st);
    CHECK(vm2.light.dirty.empty());
    frame(dev, v, vm2);
    CHECK(v.light_maps().uploads() == 1);

    // PC steps one tile: only the changed ring is uploaded, not the chunk.
    ViewModel vm3 = arena();
    vm3.actors[0].tile = {5, 4};
    fill_standin_light(vm3, st);
    REQUIRE(vm3.light.dirty.size() == 1);
    const RectI r = vm3.light.dirty[0].rect;
    CHECK(r.w * r.h < 1024);
    frame(dev, v, vm3);
    CHECK(v.light_maps().uploads() == 2);
    CHECK(v.light_maps().bytes_uploaded() == 1024 + uint64_t(r.w * r.h));
    CHECK(v.light_maps().sample({0, 0, 0}, {13, 4}) == uint8_t(iso2d::LightLevel::Dim));
    CHECK(dev.errors() == 0);
}

TEST_CASE("light view: remembered tiles show no current light; unexplored are black") {
    LightStandinState st;
    ViewModel vm = arena();
    fill_standin_light(vm, st);
    vm = arena();
    vm.actors[0].tile = {20, 4};   // walk away: the start area is explored but not seen
    fill_standin_light(vm, st);
    const LightChunkView& c = vm.light.chunks[0];
    CHECK_FALSE(c.seen(0, 0));
    CHECK(c.known(0, 0));
    CHECK_FALSE(c.known(0, 31));
    CHECK(iso2d::fog_of(c.seen(0, 0), c.known(0, 0)) == iso2d::Fog::Remembered);
    CHECK(iso2d::light_tint(c.samples[0], iso2d::Fog::Remembered).r ==
          iso2d::light_tint(uint8_t(iso2d::LightLevel::Bright), iso2d::Fog::Remembered).r);
}
