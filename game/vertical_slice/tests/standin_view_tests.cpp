#include <doctest/doctest.h>

#include <filesystem>

#include "assets/clip_registry.hpp"
#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

using namespace eng;
using namespace slice;

#ifndef SLICE_CLIP_FILE
#define SLICE_CLIP_FILE ""
#endif

TEST_CASE("Core clip registry is the only stand-in alias path") {
    assets::ClipFile file;
    const auto err = assets::load_clip_file(std::filesystem::path{SLICE_CLIP_FILE}, file);
    REQUIRE_FALSE(err);
    const assets::ClipRegistry* standin = nullptr;
    const assets::ClipRegistry* doll = nullptr;
    for (const assets::ClipRegistry& k : file.kits) {
        if (k.id == "standin.small") standin = &k;
        if (k.id == "paperdoll") doll = &k;
    }
    REQUIRE(standin);
    REQUIRE(doll);
    CHECK(standin->resolve("climb") == std::optional<std::string>{"walk"});
    CHECK(standin->resolve("crawl") == std::optional<std::string>{"walk"});
    CHECK(standin->resolve("hide") == std::optional<std::string>{"idle"});
    // melee → attack is in Core's stand-in registry (not a second table).
    CHECK(standin->resolve("melee") == std::optional<std::string>{"attack"});
    CHECK(standin->resolve("hard-land") == std::optional<std::string>{"fall_impact"});
    CHECK(standin->resolve("no-such") == std::optional<std::string>{"idle"});
    CHECK(doll->resolve("hard-land") == std::optional<std::string>{"prone_idle"});
}

TEST_CASE("session view: Core classifies the humanoid goblin and the fighter as paperdoll") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE_MESSAGE(s, err);
    const ViewModel vm = s->view();
    const ActorView* pc = nullptr;
    const ActorView* gob = nullptr;
    for (const ActorView& a : vm.actors) (a.is_pc ? pc : gob) = &a;
    REQUIRE(pc);
    REQUIRE(gob);
    CHECK(pc->visual_kind == "paperdoll");
    CHECK(pc->kit == "paperdoll");
    CHECK(pc->standin_bin.empty());
    CHECK(pc->clip.empty());
    CHECK(gob->visual_kind == "paperdoll");
    CHECK(gob->kit == "paperdoll");
    CHECK(gob->standin_bin.empty());
    CHECK_FALSE(gob->size_short);
}

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

struct Shot {
    uint64_t hash = 0;
    uint32_t sprites = 0;
};

Shot draw(const ViewModel& vm, double t) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    view.set_present_time(t);
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
    return {frame_hash(dev.last_frame()), view.world_sprites()};
}

ActorView body(uint32_t id, Vec2i tile) {
    ActorView a;
    a.id = id;
    a.name = "WOLF";
    a.tile = tile;
    a.hp = 7;
    a.hp_max = 7;
    a.visual_kind = "standin";
    a.standin_bin = "small";
    a.kit = "standin.small";
    a.clip = "idle";
    return a;
}

ViewModel scene(ActorView a) {
    ViewModel vm;
    TileView t;
    t.tile = a.tile;
    t.kind = TileKind::Floor;
    vm.tiles.push_back(t);
    vm.actors.push_back(std::move(a));
    return vm;
}

} // namespace

TEST_CASE("stand-in draw: replaces the pawn, aliases share a frame, the clock advances death") {
    const ViewModel pawn = scene([] {
        ActorView a = body(1, {4, 4});
        a.visual_kind.clear();
        a.clip.clear();
        return a;
    }());
    const ViewModel idle = scene(body(1, {4, 4}));
    const Shot pawn_shot = draw(pawn, 0);
    const Shot idle_shot = draw(idle, 0);
    const Shot idle_again = draw(idle, 0);
    CHECK(pawn_shot.sprites == 2);   // floor + body
    CHECK(idle_shot.sprites == pawn_shot.sprites);
    CHECK(idle_shot.hash != pawn_shot.hash);
    CHECK(idle_shot.hash == idle_again.hash);

    ActorView climb = body(1, {4, 4});
    climb.clip = "climb";
    ActorView walk = body(1, {4, 4});
    walk.clip = "walk";
    CHECK(draw(scene(climb), 0).hash == draw(scene(walk), 0).hash);

    ActorView landed = body(1, {4, 4});
    landed.clip = "hard-land";
    ActorView impact = body(1, {4, 4});
    impact.clip = "fall_impact";
    CHECK(draw(scene(landed), 0).hash == draw(scene(impact), 0).hash);

    ActorView dead = body(1, {4, 4});
    dead.clip = "death";
    dead.dead = true;
    const Shot d0 = draw(scene(dead), 0);
    const Shot d1 = draw(scene(dead), 10);
    CHECK(d0.hash != d1.hash);
    CHECK(d1.hash == draw(scene(dead), 10).hash);

    ActorView doll = body(1, {4, 4});
    doll.visual_kind = "paperdoll";
    doll.kit = "doll.human";
    doll.standin_bin.clear();
    doll.clip.clear();
    CHECK(draw(scene(doll), 0).hash == pawn_shot.hash);

    ActorView short_flag = body(1, {4, 4});
    short_flag.size_short = true;
    CHECK(draw(scene(short_flag), 0).hash == idle_shot.hash);
}

TEST_CASE("stand-in walk plays when the tile changes and idle holds when it does not") {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    auto shot = [&](const ViewModel& vm) {
        view.set_present_time(0);
        iso2d::Camera cam;
        cam.viewport_w = 320;
        cam.viewport_h = 240;
        REQUIRE(dev.begin_frame());
        view.prepare(vm, cam, 320, 240);
        auto& cl = dev.command_list();
        cl.begin_pass(render::TargetId{}, {});
        view.submit(cl);
        cl.end_pass();
        dev.end_frame();
        CHECK(dev.errors() == 0);
        return frame_hash(dev.last_frame());
    };
    const uint64_t stay = shot(scene(body(1, {4, 4})));
    CHECK(shot(scene(body(1, {4, 4}))) == stay);
    CHECK(shot(scene(body(1, {5, 4}))) != stay);
}
