// Faerie Fire outline + Invisible ghosting (presentation): ActorView flags, SliceView ring/ghost, null-backend
// determinism; outline off removes the ring. Session::view fills from Combat (Invisible condition; ModStat::Outlined).
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "equip_scene.hpp"
#include "equip_visuals.hpp"
#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

namespace {
uint64_t frame_hash(const render::NullFrameStats& st) {
    uint64_t h = st.upload_hash;
    auto mix = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 1099511628211ull;
        }
    };
    mix(st.draw_calls);
    for (const auto& d : st.draws) {
        mix(d.texture.index);
        mix(d.index_count);
        mix(d.first_index);
        mix(uint64_t(uint32_t(d.vertex_offset)));
    }
    return h;
}
struct Frame {
    uint64_t hash = 0;
    uint32_t world_sprites = 0;
};
Frame render_frame(const ViewModel& vm) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    iso2d::Camera cam;
    const Vec2f c = view.tile_center_px(4.5f, 4.5f);
    cam.position = {c.x - 320.f, c.y - 180.f};
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
    return {frame_hash(dev.last_frame()), view.world_sprites()};
}
} // namespace

TEST_CASE("faerie fire: outline ring draws after body/equip; clearing outlined removes it") {
    ViewModel bare = equip_scene::build(false);
    REQUIRE_FALSE(bare.actors.empty());
    // Fog off so every actor draws (no LightFrame / masks needed for this presentation check).
    bare.light = {};
    bare.light.fog = false;

    ViewModel plain = bare;
    plain.actors[0].outlined = false;
    ViewModel outlined = bare;
    outlined.actors[0].outlined = true;
    ViewModel cleared = outlined;
    cleared.actors[0].outlined = false;

    const Frame fp = render_frame(plain), fo = render_frame(outlined), fc = render_frame(cleared), fo2 = render_frame(outlined);
    CHECK(fo.hash == fo2.hash);                 // byte-identical across runs
    CHECK(fp.hash == fc.hash);                  // outline off == never outlined
    CHECK(fo.hash != fp.hash);                  // ring changes the frame
    CHECK(fo.world_sprites == fp.world_sprites + 1);   // one extra ring sprite
    CHECK(kZOutline > kZLight);
}

TEST_CASE("invisible: ghost fade when still drawn; same team always visible path keeps the token") {
    ViewModel vm = equip_scene::build(false);
    vm.light = {};
    vm.light.fog = false;
    vm.actors[0].invisible = false;
    const Frame solid = render_frame(vm);
    vm.actors[0].invisible = true;
    const Frame ghost = render_frame(vm);
    CHECK(ghost.hash != solid.hash);            // alpha change reaches the hash
    CHECK(ghost.world_sprites == solid.world_sprites);
    // Outlined + invisible still draws the ring (dimmer alpha) and differs from ghost alone.
    vm.actors[0].outlined = true;
    const Frame both = render_frame(vm);
    CHECK(both.hash != ghost.hash);
    CHECK(both.world_sprites == ghost.world_sprites + 1);
}

TEST_CASE("session view: outlined and invisible default false without Faerie Fire / Invisible") {
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.pc_torches = true;
    std::string err;
    auto s = Session::open(o, &err);
    REQUIRE_MESSAGE(s, err);
    const ViewModel vm = s->view();
    REQUIRE_FALSE(vm.actors.empty());
    for (const ActorView& a : vm.actors) {
        CHECK_FALSE(a.outlined);
        CHECK_FALSE(a.invisible);
    }
}
