// Cover debug overlay (presentation only). The level is Combat::cover_between's answer for the
// attacker's footprint (or a 1-tile aim point) and the target's footprint. SliceView draws a
// screen-space line and badge; the iso world batch is unchanged. F4 / Tab / N / M go through
// DebugCoverView::handle, the same path main.cpp uses.
#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "assets/pack_set.hpp"
#include "debug_cover.hpp"
#include "ecs/registry.hpp"
#include "render_null/null_device.hpp"
#include "rules5e/encounter.hpp"
#include "session.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "slice_view.hpp"
#include "world/chunk.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace slice;
using Cmd = DebugCoverView::Cmd;

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
    uint32_t ui_sprites = 0;
};
Frame render_frame(const ViewModel& vm) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    iso2d::Camera cam;
    const Vec2f c = view.tile_center_px(4.f, 4.f);
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
    return {frame_hash(dev.last_frame()), view.world_sprites(), view.ui_sprites()};
}

ViewModel pair_scene() {
    ViewModel vm;
    vm.sim_live = true;
    vm.turn = "FIGHTER";
    ActorView a;
    a.id = 1;
    a.name = "FIGHTER";
    a.tile = {2, 5};
    a.hp = a.hp_max = 12;
    a.is_pc = true;
    a.current = true;
    ActorView b;
    b.id = 2;
    b.name = "GOBLIN";
    b.tile = {6, 5};
    b.hp = b.hp_max = 7;
    b.team = 1;
    vm.actors = {a, b};
    return vm;
}

CoverDebugView probe_of(CoverLevel level, bool aim = false) {
    CoverDebugView c;
    c.present = true;
    c.aim = aim;
    c.attacker = 1;
    c.target = 2;
    c.origin = aim ? Vec2i{6, 8} : Vec2i{2, 5};
    c.origin_n = 1;
    c.target_tile = {6, 5};
    c.target_n = 1;
    c.level = level;
    return c;
}

sim::ContentLibrary& library() {
    static sim::ContentLibrary lib = [] {
        const std::filesystem::path roots[] = {std::filesystem::path(SLICE_PACK_ROOT)};
        const std::string req[] = {SLICE_PACK};
        const assets::PackSet set = assets::PackSet::resolve(roots, req);
        REQUIRE(set.ok());
        sim::ContentLibrary loaded;
        REQUIRE(loaded.load(set).empty());
        return loaded;
    }();
    return lib;
}

struct Spawn { const char* arch; int x, y; };
struct Board {
    ecs::Registry reg;
    world::Chunk ch{};   // value-init: TileCell flags are otherwise uninitialized and read as walls
    std::optional<sim::Combat> c;
    Board(std::vector<Spawn> sp, std::vector<Vec2i> walls = {}) {
        ch.coord = {1, 2, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor", "wall"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
        for (Vec2i w : walls)
            ch.layers[2][std::size_t(w.y * 32 + w.x)].flags =
                world::tile_flags::kBlocksSight | world::tile_flags::kBlocksMove;
        for (std::size_t i = 0; i < sp.size(); ++i)
            ch.spawns.push_back({sp[i].arch, {sp[i].x, sp[i].y}, world::spawn_stable_id(ch.coord, uint32_t(i)), ""});
        ch.content_hash = world::compute_content_hash(ch);
        std::string err;
        c = sim::Combat::create(reg, ch, library(), 1, &err);
        REQUIRE_MESSAGE(c, err);
    }
    uint32_t id(std::size_t i) const { return ch.spawns[i].stable_id; }
};

const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";

CoverLevel level_of(rules5e::Cover c) {
    switch (c) {
    case rules5e::Cover::Half: return CoverLevel::Half;
    case rules5e::Cover::ThreeQuarters: return CoverLevel::ThreeQuarters;
    case rules5e::Cover::Total: return CoverLevel::Total;
    case rules5e::Cover::None: break;
    }
    return CoverLevel::None;
}
} // namespace

TEST_CASE("cover overlay: each level changes the screen batch only, and repeats") {
    ViewModel base = pair_scene();
    ViewModel none = base;
    none.cover = probe_of(CoverLevel::None);
    ViewModel half = base;
    half.cover = probe_of(CoverLevel::Half);
    ViewModel three = base;
    three.cover = probe_of(CoverLevel::ThreeQuarters);
    ViewModel total = base;
    total.cover = probe_of(CoverLevel::Total);
    ViewModel aim = total;
    aim.cover = probe_of(CoverLevel::Total, true);
    ViewModel cleared = total;
    cleared.cover = {};

    const Frame fb = render_frame(base), fn = render_frame(none), fh = render_frame(half);
    const Frame f3 = render_frame(three), ft = render_frame(total), fa = render_frame(aim);
    const Frame fc = render_frame(cleared), fh2 = render_frame(half);
    CHECK(fh.hash == fh2.hash);
    CHECK(fb.hash == fc.hash);                 // overlay off == never set
    CHECK(fn.hash != fb.hash);
    CHECK(fh.hash != fn.hash);
    CHECK(f3.hash != fh.hash);
    CHECK(ft.hash != f3.hash);
    CHECK(fa.hash != ft.hash);                 // aim origin moves the line
    CHECK(fn.world_sprites == fb.world_sprites);
    CHECK(fh.world_sprites == fb.world_sprites);
    CHECK(f3.world_sprites == fb.world_sprites);
    CHECK(ft.world_sprites == fb.world_sprites);
    CHECK(fn.ui_sprites > fb.ui_sprites);
    CHECK(ft.ui_sprites > fh.ui_sprites);      // total draws the extra bar and cross
}

void expect_pair(const Board& b, std::size_t src, std::size_t tgt, CoverLevel want) {
    const uint32_t a = b.id(src), t = b.id(tgt);
    const CoverDebugView p = cover_probe(*b.c, a, t, false, {});
    REQUIRE(p.present);
    CHECK_FALSE(p.aim);
    CHECK(p.attacker == a);
    CHECK(p.target == t);
    CHECK(p.origin == b.c->pos(a));
    CHECK(p.origin_n == b.c->footprint_size(a));
    CHECK(p.target_tile == b.c->pos(t));
    CHECK(p.target_n == b.c->footprint_size(t));
    const rules5e::Cover api = b.c->cover_between(b.c->pos(a), b.c->footprint_size(a), b.c->pos(t), b.c->footprint_size(t), a, t);
    CHECK(p.level == level_of(api));
    CHECK(p.level == want);
}

TEST_CASE("cover probe: cover_between for footprints, a body between, walls, and an aim tile") {
    Board open({{kFighter, 2, 5}, {kGoblin, 6, 5}});
    expect_pair(open, 0, 1, CoverLevel::None);
    CHECK_FALSE(cover_probe(*open.c, open.id(0), open.id(0), false, {}).present);
    CHECK_FALSE(cover_probe(*open.c, 0, open.id(1), false, {}).present);

    Board crowd({{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 5}});
    expect_pair(crowd, 0, 1, CoverLevel::Half);
    // Aim south of the target: the body at (4, 5) is not on that line. Origin side is 1, not the caster's footprint.
    const Vec2i aim{6, 8};
    const CoverDebugView aimed = cover_probe(*crowd.c, crowd.id(0), crowd.id(1), true, aim);
    REQUIRE(aimed.present);
    CHECK(aimed.aim);
    CHECK(aimed.origin == aim);
    CHECK(aimed.origin_n == 1);
    CHECK(aimed.target_tile == crowd.c->pos(crowd.id(1)));
    const rules5e::Cover aim_api = crowd.c->cover_between(aim, 1, crowd.c->pos(crowd.id(1)), crowd.c->footprint_size(crowd.id(1)),
                                                          crowd.id(0), crowd.id(1));
    CHECK(aimed.level == level_of(aim_api));
    CHECK(aimed.level == CoverLevel::None);
    CHECK(aimed.level != cover_probe(*crowd.c, crowd.id(0), crowd.id(1), false, {}).level);

    Board corner({{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 6}, {6, 5}});
    expect_pair(corner, 0, 1, CoverLevel::ThreeQuarters);

    Board closed({{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 5}, {5, 6}, {6, 5}});
    expect_pair(closed, 0, 1, CoverLevel::Total);
}

TEST_CASE("cover overlay: F4 follows the current actor and the driver's target; M needs an aim") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true}, &err);
    REQUIRE_MESSAGE(s, err);
    DebugCoverView v;
    ViewModel closed = s->view();
    v.decorate(*s, closed);
    CHECK_FALSE(closed.cover.present);

    v.handle(Cmd::Toggle, *s);
    CHECK(v.open());
    ViewModel vm = s->view();
    v.decorate(*s, vm);
    REQUIRE(vm.cover.present);
    CHECK_FALSE(vm.cover.aim);
    CHECK(vm.cover.attacker == s->current_actor());
    CHECK(vm.cover.target == s->suggested_target(vm.cover.attacker));
    CHECK(vm.cover.target != vm.cover.attacker);
    CHECK(vm.cover.level == s->query_cover(vm.cover.attacker, vm.cover.target, false, {}).level);
    CHECK(vm.cover.origin_n >= 1);
    CHECK(vm.cover.target_n >= 1);
    // Sample room: fighter (10, 15) and goblin (20, 15), open floor. The query is still cover_between.
    CHECK(vm.cover.level == CoverLevel::None);

    const uint32_t first = vm.cover.attacker;
    v.handle(Cmd::NextAttacker, *s);
    vm = s->view();
    v.decorate(*s, vm);
    CHECK(vm.cover.attacker != first);
    CHECK(vm.cover.target == first);

    v.handle(Cmd::NextTarget, *s);
    vm = s->view();
    v.decorate(*s, vm);
    CHECK(vm.cover.target != vm.cover.attacker);
    CHECK(vm.cover.note.empty());

    CHECK_FALSE(s->last_aim_tile(vm.cover.attacker, nullptr));
    v.handle(Cmd::ToggleAim, *s);
    CHECK(v.message() == "NO AIM");
    vm = s->view();
    v.decorate(*s, vm);
    CHECK(vm.cover.present);
    CHECK_FALSE(vm.cover.aim);
    CHECK(vm.cover.note == "NO AIM");

    v.handle(Cmd::Toggle, *s);
    CHECK_FALSE(v.open());
    vm = s->view();
    v.decorate(*s, vm);
    CHECK_FALSE(vm.cover.present);
    CHECK(vm.cover.note.empty());
}

TEST_CASE("cover overlay: static view has no pair to measure") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE_MESSAGE(s, err);
    DebugCoverView v;
    v.handle(Cmd::Toggle, *s);
    ViewModel vm = s->view();
    v.decorate(*s, vm);
    CHECK_FALSE(vm.cover.present);
    CHECK(vm.cover.note == "NO PAIR");
    CHECK_FALSE(s->query_cover(1, 2, false, {}).present);
}
