// Zone overlays are presentation: a hand-built ViewModel (no sim) checks that a known spell draws
// in the heavy-haze slot, an unknown id leaves the generic haze, and unseen tiles stay bare.
#include <doctest/doctest.h>

#include "iso2d/light_map.hpp"
#include "render_null/null_device.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
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
uint32_t sprites_of(const ViewModel& vm) {
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    CHECK(dev.errors() == 0);
    return v.world_sprites();
}
ViewModel one_floor(Vec2i tile, bool heavy) {
    ViewModel vm;
    vm.tiles.push_back(TileView{tile, 0, 0, TileKind::Floor});
    vm.light.present = true;
    vm.light.frame = 1;
    LightChunkView ch;
    ch.has_fog = false;   // every tile of the chunk is seen
    if (heavy) ch.samples[size_t(tile.y * 32 + tile.x)] = iso2d::pack_light(iso2d::LightLevel::Bright, 8);
    vm.light.chunks.push_back(ch);
    return vm;
}
} // namespace

TEST_CASE("zone overlay: a known heavy look replaces the generic haze") {
    const ViewModel bare = one_floor({2, 3}, true);
    CHECK(sprites_of(bare) == 2);   // ground + generic haze

    ViewModel fog = bare;
    fog.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.fog_cloud", {{2, 3}}});
    CHECK(sprites_of(fog) == 3);   // ground + fill + soft rim; the generic haze is not stacked on it

    ViewModel dark = bare;
    dark.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.darkness", {{2, 3}}});
    CHECK(sprites_of(dark) == 3);   // solid rim, still one overlay in the haze slot

    ViewModel unknown = bare;
    unknown.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.fire_bolt", {{2, 3}}});
    CHECK(sprites_of(unknown) == 2);   // no look: the heavy bit still draws the generic haze
}

TEST_CASE("zone overlay: Web's dashed rim follows the tile cadence; Silence on an odd tile has none") {
    // (2, 2) is even, so a dashed edge draws. (2, 3) is odd, so it does not.
    ViewModel web = one_floor({2, 2}, false);
    web.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.web", {{2, 2}}});
    CHECK(sprites_of(web) == 3);   // ground + lattice fill + rim

    ViewModel silence = one_floor({2, 3}, false);
    silence.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.silence", {{2, 3}}});
    CHECK(sprites_of(silence) == 2);   // ground + wash; dashed rim skipped
}

TEST_CASE("zone overlay: unseen tiles and walls are not filled") {
    ViewModel vm = one_floor({2, 3}, true);
    vm.light.chunks[0].has_fog = true;   // masks stay zero: the tile is unexplored
    vm.zones.push_back(ZoneOverlayView{7, 99, "srd51.spell.darkness", {{2, 3}}});
    CHECK(sprites_of(vm) == 1);   // ground only; no fill, no rim, no haze

    ViewModel wall;
    wall.tiles.push_back(TileView{{2, 3}, 2, 0, TileKind::Wall});
    wall.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.entangle", {{2, 3}}});
    CHECK(sprites_of(wall) == 1);
}
