#include <doctest/doctest.h>

#include "render_null/null_device.hpp"
#include "session.hpp"
#include "slice_view.hpp"

#include <algorithm>

using namespace slice;

TEST_CASE("palette classification") {
    CHECK(classify_tile("tiles.dungeon.wall_01") == TileKind::Wall);
    CHECK(classify_tile("tiles.dungeon.floor_01") == TileKind::Floor);
    CHECK(classify_tile("tiles.dungeon.floor_raised_01") == TileKind::Raised);
    CHECK(classify_tile("tiles.dungeon.door_open_01") == TileKind::Door);
    CHECK(classify_tile("tiles.dungeon.rubble_01") == TileKind::Rubble);
}

TEST_CASE("static view: chunk tiles, walls and both spawns through the world module") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE_MESSAGE(s, err);
    ViewModel v = s->view();
    CHECK_FALSE(v.sim_live);
    CHECK(v.tiles.size() > 100);
    size_t walls = 0;
    for (auto& t : v.tiles) walls += t.kind == TileKind::Wall;
    CHECK(walls > 0);
    REQUIRE(v.actors.size() == 2);
    bool fighter = false, goblin = false;
    for (auto& a : v.actors) {
        fighter |= a.name == "FIGHTER" && a.is_pc && a.tile == eng::Vec2i{10, 15} && a.hp_max > 0;
        goblin |= a.name == "GOBLIN" && !a.is_pc && a.tile == eng::Vec2i{20, 15} && a.hp_max > 0;
    }
    CHECK(fighter);
    CHECK(goblin);
    CHECK_FALSE(s->step());   // static: nothing to drive
}

TEST_CASE("sim view: stepping the headless fight to the end updates the snapshot") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 2, true}, &err);
    REQUIRE_MESSAGE(s, err);
    ViewModel v0 = s->view();
    CHECK(v0.sim_live);
    CHECK_FALSE(v0.turn.empty());
    int steps = 0;
    while (!s->ended() && steps < 5000 && s->step()) ++steps;
    CHECK_MESSAGE(s->error().empty(), s->error());
    CHECK(s->ended());
    ViewModel v1 = s->view();
    CHECK(v1.ended);
    bool someone_hurt = false;
    for (auto& a : v1.actors) someone_hurt |= a.hp < a.hp_max;
    CHECK(someone_hurt);
    CHECK_FALSE(v1.log.empty());
}

TEST_CASE("view renders a snapshot without touching game state (null device)") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true}, &err);
    REQUIRE(s);
    eng::render::NullDevice dev;
    {
        SliceView view(dev);
        const ViewModel vm = s->view();
        REQUIRE(dev.begin_frame());
        view.prepare(vm, eng::iso2d::Camera{{0, 0}, 1280, 720, 1.5f}, 1280, 720);
        auto& cl = dev.command_list();
        cl.begin_pass(eng::render::TargetId{}, {});
        view.submit(cl);
        cl.end_pass();
        dev.end_frame();
        CHECK(dev.errors() == 0);
        CHECK(view.world_sprites() >= vm.tiles.size() + vm.actors.size());
        CHECK(view.ui_sprites() > 20);   // panel text + bars
        CHECK(dev.last_frame().draw_calls == view.batches());
    }
    CHECK(dev.live_textures() == 0);
}

TEST_CASE("camera focus: centre of the combatants' bounding box") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE(s);
    eng::render::NullDevice dev;
    SliceView view(dev);
    ViewModel vm = s->view();   // fighter (10,15), goblin (20,15)
    const eng::Vec2f f = view.actors_focus_px(vm);
    const eng::Vec2f a = view.tile_center_px(10, 15), b = view.tile_center_px(20, 15);
    CHECK(f.x == doctest::Approx((a.x + b.x) / 2));
    CHECK(f.y < (a.y + b.y) / 2 + 16.f);
    CHECK(f.y > std::min(a.y, b.y) - 44.f);
    vm.actors.resize(1);
    vm.actors[0].tile = {3, 4};
    const eng::Vec2f one = view.actors_focus_px(vm), c = view.tile_center_px(3, 4);
    CHECK(one.x == doctest::Approx(c.x));
}

TEST_CASE("session takes a pack set: errors surface, config shape parses") {
    std::string err;
    CHECK_FALSE(Session::open({{SLICE_PACK_ROOT}, {"no.such_pack"}, 1, false}, &err));
    CHECK(err == "pack set: missing_dep: requested pack 'no.such_pack' is not in the pack set");
    CHECK_FALSE(Session::open({{SLICE_PACK_ROOT}, {}, 1, false}, &err));
    // srd51.core alone resolves but has no chunk.
    CHECK_FALSE(Session::open({{SLICE_PACK_ROOT}, {"srd51.core"}, 1, false}, &err));
    Session::Options o;
    REQUIRE(Session::apply_pack_config(R"({"pack_roots": [")" SLICE_PACK_ROOT R"("], "packs": ["sample.vertical_slice"]})", o, &err));
    CHECK(o.packs == std::vector<std::string>{"sample.vertical_slice"});
    CHECK(Session::open(o, &err));
    CHECK_FALSE(Session::apply_pack_config(R"({"packs": "x"})", o, &err));
    CHECK_FALSE(Session::apply_pack_config("[", o, &err));
}
