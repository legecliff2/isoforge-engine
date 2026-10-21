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
    CHECK(vm.light.fog);                    // fog on by default; masks come from the frame
    CHECK(vm.light.chunks[0].has_fog);
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
    c.seen_now = {};
    c.explored = {};
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

namespace {
iso2d::Fog fog_at(const SliceView& v, const ViewModel& vm, Vec2i t) {
    iso2d::Fog f{};
    v.tile_tint(vm.light, t, &f);
    return f;
}
void set_bit(std::array<uint64_t, 16>& m, int x, int y) { m[size_t((y * 32 + x) / 64)] |= uint64_t(1) << ((y * 32 + x) % 64); }
} // namespace

TEST_CASE("fog: shown team defaults to the player team; team choice and fog off are options") {
    auto s = open_sim();
    const ViewModel vm = s->view();
    const ActorView& p = pc(vm);
    CHECK(vm.light.fog_team == p.team);
    REQUIRE(vm.light.fog);
    const LightChunkView& c = vm.light.chunks[0];
    CHECK(c.seen(p.tile.x, p.tile.y));    // a viewer sees its own (torch-lit) tile
    CHECK(c.known(p.tile.x, p.tile.y));
    bool any_unseen = false;
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) any_unseen |= !c.known(x, y);
    CHECK(any_unseen);                    // underground: the far dark stays unexplored

    uint8_t foe_team = p.team;
    for (const auto& a : vm.actors) if (a.team != p.team) foe_team = a.team;
    REQUIRE(foe_team != p.team);
    Session::Options o{{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true};
    o.fog_team = foe_team;
    std::string err;
    auto sf = Session::open(o, &err);
    REQUIRE(sf);
    const ViewModel vf = sf->view();
    CHECK(vf.light.fog_team == foe_team);
    CHECK(vf.light.chunks[0].seen_now != c.seen_now);   // a different team's mask
    // (Interim normal sight: the foes stand in unlit dark, so they may see nothing; only the mask choice is checked.)

    o.fog_team.reset();
    o.fog = false;
    auto so = Session::open(o, &err);
    REQUIRE(so);
    const ViewModel vo = so->view();
    CHECK_FALSE(vo.light.fog);
    CHECK_FALSE(vo.light.chunks[0].has_fog);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vo);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) REQUIRE(fog_at(v, vo, {x, y}) == iso2d::Fog::Visible);
    // Switching the shown team / turning fog on re-uploads every fog map without a new sim frame.
    frame(dev, v, vm);
    CHECK(fog_at(v, vm, {p.tile.x, p.tile.y}) == iso2d::Fog::Visible);
    CHECK(dev.errors() == 0);
}

TEST_CASE("fog: three states render (unseen black, explored dimmed, seen normal); a missing team entry is unseen") {
    ViewModel vm = open_sim()->view();
    LightChunkView& c = vm.light.chunks[0];
    REQUIRE(c.has_fog);
    c.seen_now = {}; c.explored = {};
    set_bit(c.seen_now, 3, 3); set_bit(c.explored, 3, 3);
    set_bit(c.explored, 4, 3);
    for (auto& b : c.samples) b = iso2d::pack_light(iso2d::LightLevel::Bright, 0);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    iso2d::Fog f{};
    const Color seen = v.tile_tint(vm.light, {3, 3}, &f);
    CHECK(f == iso2d::Fog::Visible);
    CHECK(seen.r == 1.f);
    const Color expl = v.tile_tint(vm.light, {4, 3}, &f);
    CHECK(f == iso2d::Fog::Remembered);
    CHECK(expl.r > 0.f); CHECK(expl.r < seen.r);
    const Color none = v.tile_tint(vm.light, {5, 3}, &f);
    CHECK(f == iso2d::Fog::Unexplored);
    CHECK(none.r == 0.f); CHECK(none.g == 0.f); CHECK(none.b == 0.f);
    CHECK(fog_at(v, vm, {40, 3}) == iso2d::Fog::Unexplored);   // untracked chunk
    CHECK(v.fog_maps().chunks() == 1);
}

TEST_CASE("fog: a blocked chunk holds its last light and fog textures; removal frees them") {
    ViewModel vm = open_sim()->view();
    REQUIRE(vm.light.chunks.size() == 1);
    LightChunkView& c = vm.light.chunks[0];
    c.seen_now = {}; c.explored = {};
    set_bit(c.seen_now, 2, 2); set_bit(c.explored, 2, 2); set_bit(c.explored, 6, 6);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    const auto tex_l = v.light_maps().texture({0, 0, 0});
    const auto tex_f = v.fog_maps().texture({0, 0, 0});
    const uint8_t s22 = v.light_maps().sample({0, 0, 0}, {2, 2});
    const uint32_t lu = v.light_maps().uploads(), fu = v.fog_maps().uploads();

    // Next frame: the chunk's halo is pending, so it is tracked but blocked (no samples, no masks).
    ViewModel b = vm;
    b.light.frame = vm.light.frame + 1;
    b.light.chunks.clear();
    b.light.added.clear();
    b.light.dirty.clear();
    b.light.vis_dirty = {{0, 0, 0}};   // even if listed, a blocked chunk is not refreshed
    b.light.blocked = {{0, 0, 0}};
    frame(dev, v, b);
    CHECK(v.light_maps().chunks() == 1);
    CHECK(v.fog_maps().chunks() == 1);
    CHECK(v.light_maps().texture({0, 0, 0}) == tex_l);
    CHECK(v.fog_maps().texture({0, 0, 0}) == tex_f);
    CHECK(v.light_maps().uploads() == lu);
    CHECK(v.fog_maps().uploads() == fu);
    CHECK(v.light_maps().sample({0, 0, 0}, {2, 2}) == s22);
    CHECK(fog_at(v, b, {2, 2}) == iso2d::Fog::Visible);
    CHECK(fog_at(v, b, {6, 6}) == iso2d::Fog::Remembered);
    CHECK(fog_at(v, b, {9, 9}) == iso2d::Fog::Unexplored);

    // Then it leaves the tracked set: both maps are freed.
    ViewModel r = b;
    r.light.frame = b.light.frame + 1;
    r.light.blocked.clear();
    r.light.vis_dirty.clear();
    r.light.removed = {{0, 0, 0}};
    frame(dev, v, r);
    CHECK(v.light_maps().chunks() == 0);
    CHECK(v.fog_maps().chunks() == 0);
    CHECK(dev.errors() == 0);
}

TEST_CASE("fog: a chunk that leaves and returns takes the frame's explored mask whole (no merge with the old copy)") {
    ViewModel vm = open_sim()->view();
    LightChunkView& c = vm.light.chunks[0];
    c.seen_now = {}; c.explored = {};
    set_bit(c.explored, 1, 1); set_bit(c.explored, 8, 8);
    render::NullDevice dev;
    SliceView v(dev);
    frame(dev, v, vm);
    REQUIRE(fog_at(v, vm, {8, 8}) == iso2d::Fog::Remembered);

    ViewModel gone = vm;   // the chunk leaves the tracked set
    gone.light.frame = vm.light.frame + 1;
    gone.light.chunks.clear();
    gone.light.added.clear(); gone.light.dirty.clear(); gone.light.vis_dirty.clear();
    gone.light.removed = {{0, 0, 0}};
    frame(dev, v, gone);
    CHECK(v.fog_maps().chunks() == 0);

    ViewModel back = vm;   // it returns: added + dirty_vis, with the sim's stored explored (differs from ours)
    back.light.frame = gone.light.frame + 1;
    back.light.removed.clear();
    back.light.added = {{0, 0, 0}};
    back.light.vis_dirty = {{0, 0, 0}};
    LightChunkView& b = back.light.chunks[0];
    b.seen_now = {}; b.explored = {};
    set_bit(b.explored, 1, 1); set_bit(b.explored, 20, 20);
    frame(dev, v, back);
    CHECK(fog_at(v, back, {1, 1}) == iso2d::Fog::Remembered);
    CHECK(fog_at(v, back, {20, 20}) == iso2d::Fog::Remembered);
    CHECK(fog_at(v, back, {8, 8}) == iso2d::Fog::Unexplored);   // our old bit is not OR-ed back in

    ViewModel shrink = back;   // a later dirty_vis update with a smaller mask also replaces
    shrink.light.frame = back.light.frame + 1;
    shrink.light.added.clear();
    shrink.light.chunks[0].explored = {};
    set_bit(shrink.light.chunks[0].explored, 20, 20);
    frame(dev, v, shrink);
    CHECK(fog_at(v, shrink, {1, 1}) == iso2d::Fog::Unexplored);
    CHECK(fog_at(v, shrink, {20, 20}) == iso2d::Fog::Remembered);
    CHECK(dev.errors() == 0);
}
