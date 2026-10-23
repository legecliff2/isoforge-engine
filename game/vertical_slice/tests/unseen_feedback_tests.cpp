// Unseen target / unseen attacker (presentation only). ActorView flags come from Combat sight
// once EncounterHooks::sight_facts is installed; an empty hook or no Combat leaves the defaults
// (everyone sees everyone). SliceView draws a slate-cyan ground diamond and a coral pip. Null-backend
// frame hashes are deterministic, and clearing the flags restores the uncued frame.
#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

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
    return {frame_hash(dev.last_frame()), view.world_sprites()};
}

ActorView pawn(uint32_t id, const char* name, Vec2i tile, uint8_t team) {
    ActorView a;
    a.id = id;
    a.name = name;
    a.tile = tile;
    a.hp = a.hp_max = 12;
    a.team = team;
    a.is_pc = team == 0;
    return a;
}

// One chunk, fog on, team 0. Tile (5, 4) is seen now; everything else is unexplored.
ViewModel fog_scene() {
    ViewModel vm;
    vm.sim_live = true;
    vm.light.present = true;
    vm.light.frame = 1;
    vm.light.fog = true;
    vm.light.fog_team = 0;
    LightChunkView ch;
    ch.cx = 0;
    ch.cy = 0;
    ch.has_fog = true;
    const int bit = 4 * 32 + 5;   // tile (5, 4)
    ch.seen_now[size_t(bit / 64)] |= 1ull << (bit % 64);
    ch.explored = ch.seen_now;
    vm.light.chunks.push_back(ch);
    return vm;
}
} // namespace

TEST_CASE("unseen cues: on/off changes the frame hash by one sprite each, and repeats") {
    ViewModel base;
    base.actors = {pawn(1, "FIGHTER", {3, 4}, 0), pawn(2, "GOBLIN", {5, 4}, 1)};
    // No LightFrame: both tokens draw (fog cannot hide them).
    ViewModel target = base;
    target.actors[1].seen_by_viewer = false;
    ViewModel attacker = target;
    attacker.actors[1].unseen_attacker = true;
    ViewModel cleared = attacker;
    cleared.actors[1].seen_by_viewer = true;
    cleared.actors[1].unseen_attacker = false;

    const Frame fb = render_frame(base), ft = render_frame(target), fa = render_frame(attacker);
    const Frame fc = render_frame(cleared), fa2 = render_frame(attacker);
    CHECK(fa.hash == fa2.hash);
    CHECK(fb.hash == fc.hash);                              // cues off == never set
    CHECK(ft.hash != fb.hash);
    CHECK(fa.hash != ft.hash);
    CHECK(ft.world_sprites == fb.world_sprites + 1);        // slate-cyan diamond
    CHECK(fa.world_sprites == ft.world_sprites + 1);        // coral pip
}

TEST_CASE("unseen cues: fog still draws a same-team token and a seen-tile foe, and hides the rest") {
    ViewModel ally_only = fog_scene();
    ally_only.actors = {pawn(1, "FIGHTER", {3, 4}, 0)};   // own team, unexplored tile: still drawn
    const Frame plain = render_frame(ally_only);

    ViewModel unseen_ally = ally_only;
    unseen_ally.actors[0].seen_by_viewer = false;
    const Frame marked = render_frame(unseen_ally);
    CHECK(marked.hash != plain.hash);
    CHECK(marked.world_sprites == plain.world_sprites + 1);   // ring, token was not dropped

    // Foe on a seen tile: fog draws them, and both cues attach.
    ViewModel foe_seen = unseen_ally;
    ActorView gob = pawn(2, "GOBLIN", {5, 4}, 1);
    gob.seen_by_viewer = false;
    gob.unseen_attacker = true;
    foe_seen.actors.push_back(gob);
    const Frame with_foe = render_frame(foe_seen);
    CHECK(with_foe.world_sprites == marked.world_sprites + 1 /*pawn*/ + 2 /*ring + pip*/);

    // Foe on an unexplored tile: fog hides the token and both cues. No invented marker.
    // Compare cues on vs off for the same actor list (the roster panel grows with actor count,
    // so this is not compared to the ally-only frame).
    ViewModel foe_dark = unseen_ally;
    ActorView hidden = gob;
    hidden.tile = {6, 4};
    foe_dark.actors.push_back(hidden);
    ViewModel foe_dark_off = foe_dark;
    foe_dark_off.actors[1].seen_by_viewer = true;
    foe_dark_off.actors[1].unseen_attacker = false;
    const Frame dark = render_frame(foe_dark), dark_off = render_frame(foe_dark_off);
    CHECK(dark.world_sprites == marked.world_sprites);
    CHECK(dark.world_sprites == dark_off.world_sprites);
    CHECK(dark.hash == dark_off.hash);
}

TEST_CASE("unseen cues: no combat and an empty sight hook leave everyone seen") {
    std::string err;
    auto stat = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE_MESSAGE(stat, err);
    REQUIRE_FALSE(stat->view().actors.empty());
    for (const ActorView& a : stat->view().actors) {
        CHECK(a.seen_by_viewer);
        CHECK(a.sees_viewer);
        CHECK_FALSE(a.unseen_attacker);
    }

    // Dark map (no torches). Facts would already say the fighter cannot see the goblin, but the hook
    // is empty until the first submit, so the snapshot must stay at the Sight{} default.
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.pc_torches = false;
    auto live = Session::open(o, &err);
    REQUIRE_MESSAGE(live, err);
    const ViewModel before = live->view();
    REQUIRE(before.actors.size() == 2);
    for (const ActorView& a : before.actors) {
        CHECK(a.seen_by_viewer);
        CHECK(a.sees_viewer);
        CHECK_FALSE(a.unseen_attacker);
    }
}

// One EndTurn, never an attack: binds sight_facts (install_hooks runs on submit) and leaves both
// creatures where they spawned. step() would let the fighter shoot, and a goblin dies in one hit.
void bind_sight_without_attacking(Session& s) {
    const uint32_t cur = s.current_actor();
    uint32_t other = 0;
    for (const ActorView& a : s.view().actors)
        if (a.id != cur) other = a.id;
    REQUIRE(other != 0);
    REQUIRE(s.end_turns_until(other));
}

void expect_dark_goblin(const ViewModel& vm) {
    REQUIRE(vm.actors.size() == 2);
    const ActorView* pc = nullptr;
    const ActorView* gob = nullptr;
    for (const ActorView& a : vm.actors) (a.is_pc ? pc : gob) = &a;
    REQUIRE(pc);
    REQUIRE(gob);
    CHECK_FALSE(pc->dead);
    CHECK_FALSE(gob->dead);
    // Camera is the conscious party character. They see themselves. The hook is installed, so the
    // human fighter in underground Dark cannot see the goblin. The goblin (darkvision 60, open floor,
    // about 50 ft) can see the fighter and can hear them, so they are an unseen attacker.
    CHECK(pc->seen_by_viewer);
    CHECK(pc->sees_viewer);
    CHECK_FALSE(pc->unseen_attacker);
    CHECK_FALSE(gob->seen_by_viewer);
    CHECK(gob->sees_viewer);
    CHECK(gob->unseen_attacker);
}

TEST_CASE("unseen cues: live sight_facts marks a dark-map goblin, fog does not grow a marker") {
    std::string err;
    Session::Options hidden{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    hidden.pc_torches = false;   // underground Dark; fog on (the default)
    auto fogged = Session::open(hidden, &err);
    REQUIRE_MESSAGE(fogged, err);
    bind_sight_without_attacking(*fogged);
    CHECK(fogged->error().empty());
    const ViewModel vm = fogged->view();
    const ViewModel again = fogged->view();
    expect_dark_goblin(vm);
    for (size_t i = 0; i < vm.actors.size(); ++i) {
        CHECK(again.actors[i].seen_by_viewer == vm.actors[i].seen_by_viewer);
        CHECK(again.actors[i].sees_viewer == vm.actors[i].sees_viewer);
        CHECK(again.actors[i].unseen_attacker == vm.actors[i].unseen_attacker);
    }
    // The goblin's tile is unseen by the party, so fog does not draw the token. Cues must not invent
    // a marker there: clearing the flags does not change the frame.
    ViewModel cleared = vm;
    for (ActorView& a : cleared.actors) {
        a.seen_by_viewer = true;
        a.sees_viewer = true;
        a.unseen_attacker = false;
    }
    const Frame hid = render_frame(vm), hid2 = render_frame(vm), hid_off = render_frame(cleared);
    CHECK(hid.hash == hid2.hash);
    CHECK(hid.hash == hid_off.hash);
    CHECK(hid.world_sprites == hid_off.world_sprites);

    // Fog off: the token is drawn anyway (same path as a same-team / seen-tile unseen creature).
    // The diamond and the pip change the hash; a second frame matches; clearing the flags removes both.
    Session::Options shown = hidden;
    shown.fog = false;
    auto open = Session::open(shown, &err);
    REQUIRE_MESSAGE(open, err);
    bind_sight_without_attacking(*open);
    CHECK(open->error().empty());
    const ViewModel live = open->view();
    expect_dark_goblin(live);
    ViewModel live_off = live;
    for (ActorView& a : live_off.actors) {
        a.seen_by_viewer = true;
        a.sees_viewer = true;
        a.unseen_attacker = false;
    }
    const Frame on = render_frame(live), on2 = render_frame(live), off = render_frame(live_off);
    CHECK(on.hash == on2.hash);
    CHECK(on.hash != off.hash);
    CHECK(on.world_sprites == off.world_sprites + 2);   // slate-cyan diamond + coral pip
}
