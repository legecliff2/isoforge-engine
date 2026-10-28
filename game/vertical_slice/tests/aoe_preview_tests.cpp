// Player area-spell preview. Creature hits come from Combat::area_targets (read-only). The tile
// list stays empty until Combat::area_preview; the draw path is tested with a view-owned list.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "aoe_overlay.hpp"
#include "aoe_preview.hpp"
#include "assets/pack_set.hpp"
#include "assets/schema_registry.hpp"
#include "ecs/registry.hpp"
#include "equip_visuals.hpp"
#include "iso2d/zone_look.hpp"
#include "render_null/null_device.hpp"
#include "rules5e/encounter.hpp"
#include "rules5e/spells.hpp"
#include "session.hpp"
#include "sim/combat.hpp"
#include "sim/content.hpp"
#include "slice_view.hpp"
#include "world/chunk.hpp"
#include "world/chunk_codec.hpp"

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
    uint32_t world = 0;
    uint32_t ui = 0;
};
Frame render_frame(const ViewModel& vm) {
    render::NullDevice dev;
    dev.set_hash_uploads(true);
    SliceView view(dev);
    iso2d::Camera cam;
    const Vec2f c = view.tile_center_px(8.f, 8.f);
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

rules5e::SpellDef tmpl(const char* id, rules5e::AreaShape shape, int size_ft, int width_ft = 0) {
    rules5e::SpellDef d;
    d.content_id = id;
    d.id = rules5e::spell_id_of(id);
    d.area = shape;
    d.range_ft = 0;
    d.area_size_ft = uint16_t(size_ft);
    d.area_width_ft = uint16_t(width_ft);
    return d;
}

sim::ContentLibrary& library() {
    static sim::ContentLibrary lib = [] {
        const std::filesystem::path roots[] = {std::filesystem::path(SLICE_PACK_ROOT)};
        const std::string req[] = {SLICE_PACK};
        const assets::PackSet set = assets::PackSet::resolve(roots, req);
        REQUIRE(set.ok());
        sim::ContentLibrary loaded;
        REQUIRE(loaded.load(set).empty());
        using rules5e::AreaShape;
        REQUIRE(loaded.spells()->add(tmpl("test.preview.cone", AreaShape::Cone, 15)));
        REQUIRE(loaded.spells()->add(tmpl("test.preview.line", AreaShape::Line, 30, 5)));
        REQUIRE(loaded.spells()->add(tmpl("test.preview.cube", AreaShape::Cube, 15)));
        REQUIRE(loaded.spells()->add(tmpl("test.preview.sphere", AreaShape::Sphere, 10)));
        REQUIRE(loaded.spells()->add(tmpl("test.preview.cylinder", AreaShape::Cylinder, 10, 0)));
        REQUIRE(loaded.spells()->add(tmpl("test.preview.sphere_tight", AreaShape::Sphere, 5)));
        return loaded;
    }();
    return lib;
}

struct Spawn { const char* arch; int x, y; };
struct Board {
    ecs::Registry reg;
    world::Chunk ch{};
    std::optional<sim::Combat> c;
    Board(std::vector<Spawn> sp) {
        ch.coord = {3, 4, 0};
        ch.origin = world::Chunk::Origin::Handcrafted;
        ch.palette = {"", "floor", "wall"};
        for (auto& cell : ch.layers[0]) cell.tile = 1;
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

ViewModel floors(std::vector<Vec2i> tiles) {
    ViewModel vm;
    for (Vec2i t : tiles) vm.tiles.push_back(TileView{t, 0, 0, TileKind::Floor});
    return vm;
}

uint32_t pc_of(const Session& s) {
    for (const ActorView& a : s.view().actors)
        if (a.is_pc) return a.id;
    return 0;
}
uint32_t foe_of(const Session& s) {
    for (const ActorView& a : s.view().actors)
        if (!a.is_pc) return a.id;
    return 0;
}
} // namespace

TEST_CASE("area overlay: five shape tints, ally and enemy, edge, and a frame-clock pulse") {
    CHECK(kAreaFillZ != iso2d::kZoneOverlayZ);
    CHECK(kAreaEdgeZ != iso2d::kZoneOverlayZ);
    CHECK(kAreaHitZ > kZOutline);

    const Color cone = area_fill_tint(AreaPreviewShape::Cone, 0);
    const Color line = area_fill_tint(AreaPreviewShape::Line, 0);
    const Color cube = area_fill_tint(AreaPreviewShape::Cube, 0);
    const Color sphere = area_fill_tint(AreaPreviewShape::Sphere, 0);
    const Color cyl = area_fill_tint(AreaPreviewShape::Cylinder, 0);
    CHECK(cone.r > cone.b);
    CHECK(line.r > 0.8f);
    CHECK(line.g > line.b);
    CHECK(cube.b > cube.r);
    CHECK(sphere.b > sphere.g);
    CHECK(cyl.g > cyl.r);
    const auto key = [](Color c) { return std::tuple{c.r, c.g, c.b}; };
    CHECK(key(cone) != key(line));
    CHECK(key(line) != key(cube));
    CHECK(key(cube) != key(sphere));
    CHECK(key(sphere) != key(cyl));
    CHECK(key(cyl) != key(cone));
    CHECK(area_fill_tint(AreaPreviewShape::None, 0).a == 0.f);

    CHECK(area_fill_tint(AreaPreviewShape::Sphere, 0).a < area_fill_tint(AreaPreviewShape::Sphere, 400).a);
    CHECK(area_fill_tint(AreaPreviewShape::Sphere, 0).a == doctest::Approx(area_fill_tint(AreaPreviewShape::Sphere, 800).a));
    CHECK(area_edge_tint(AreaPreviewShape::Cone, 0).a > area_fill_tint(AreaPreviewShape::Cone, 400).a);
    CHECK(area_hit_tint(AreaHitSide::Ally, 0).g > area_hit_tint(AreaHitSide::Ally, 0).r);
    CHECK(area_hit_tint(AreaHitSide::Enemy, 0).r > area_hit_tint(AreaHitSide::Enemy, 0).g);
    CHECK(key(area_hit_tint(AreaHitSide::Ally, 200)) != key(area_hit_tint(AreaHitSide::Enemy, 200)));

    std::vector<Vec2i> block;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) block.push_back({x, y});
    CHECK_FALSE(area_tile_is_edge(block, {1, 1}));
    CHECK(area_tile_is_edge(block, {0, 1}));
    CHECK(area_tile_is_edge(block, {2, 2}));
    CHECK(area_tile_is_edge({{4, 4}}, {4, 4}));

    CHECK(area_caption_line(ViewModel{}).empty());
    ViewModel note;
    note.area.note = "NOT YOUR TURN";
    CHECK(area_caption_line(note).find("NOT YOUR TURN") != std::string::npos);
    ViewModel pending;
    pending.area.present = true;
    pending.area.shape = AreaPreviewShape::Line;
    pending.area.tiles_pending = true;
    pending.area.aim = {3, 4};
    pending.area.spell_label = "lightning bolt";
    const std::string cap = area_caption_line(pending);
    CHECK(cap.find("LINE") != std::string::npos);
    CHECK(cap.find("TILES PENDING") != std::string::npos);
    CHECK(cap.find("3 4") != std::string::npos);
    pending.area.note = "aim beyond range";
    CHECK(area_caption_line(pending).find("TILES PENDING") == std::string::npos);
    CHECK(area_caption_line(pending).find("aim beyond range") != std::string::npos);
}

TEST_CASE("area overlay: tinted tiles and one creature ring, off the zone slot") {
    std::vector<Vec2i> block;
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) block.push_back({x, y});
    const ViewModel bare = floors(block);
    const Frame base = render_frame(bare);
    CHECK(base.world == 9);

    ViewModel filled = bare;
    filled.area.present = true;
    filled.area.shape = AreaPreviewShape::Sphere;
    filled.area.aim = {1, 1};
    filled.area.tiles = block;
    filled.area.pulse_ms = 0;
    const Frame sphere = render_frame(filled);
    CHECK(sphere.world == 9 + 9 + 8 + 1);   // floors + fills + edges + aim cursor
    CHECK(sphere.hash != base.hash);

    ViewModel dup = filled;
    dup.area.tiles.push_back({1, 1});
    CHECK(render_frame(dup).world == sphere.world);   // a repeated tile is drawn once

    ViewModel cone = filled;
    cone.area.shape = AreaPreviewShape::Cone;
    ViewModel line = filled;
    line.area.shape = AreaPreviewShape::Line;
    ViewModel cube = filled;
    cube.area.shape = AreaPreviewShape::Cube;
    ViewModel cyl = filled;
    cyl.area.shape = AreaPreviewShape::Cylinder;
    const Frame fc = render_frame(cone), fl = render_frame(line), fq = render_frame(cube), fy = render_frame(cyl);
    CHECK(fc.hash != sphere.hash);
    CHECK(fl.hash != fc.hash);
    CHECK(fq.hash != fl.hash);
    CHECK(fy.hash != fq.hash);
    CHECK(fy.hash != sphere.hash);
    CHECK(render_frame(cone).hash == fc.hash);

    ViewModel pulsed = cone;
    pulsed.area.pulse_ms = 400;
    pulsed.anim_phase = 9;
    ViewModel steady = cone;
    steady.anim_phase = 3;
    CHECK(render_frame(pulsed).hash != render_frame(steady).hash);   // frame clock, not anim_phase
    ViewModel same = steady;
    same.anim_phase = 80;
    CHECK(render_frame(same).hash == render_frame(steady).hash);
    ViewModel wrapped = steady;
    wrapped.area.pulse_ms = 800;
    CHECK(render_frame(wrapped).hash == render_frame(steady).hash);

    ViewModel aim_only = bare;
    aim_only.area.present = true;
    aim_only.area.shape = AreaPreviewShape::Cube;
    aim_only.area.aim = {1, 1};
    aim_only.area.pulse_ms = 0;
    const Frame aim0 = render_frame(aim_only);
    CHECK(aim0.world == base.world + 1);   // cursor only; tiles still pending
    aim_only.area.pulse_ms = 400;
    CHECK(render_frame(aim_only).hash == aim0.hash);   // the cursor does not pulse

    ViewModel wall;
    wall.tiles.push_back(TileView{{2, 3}, 2, 0, TileKind::Wall});
    wall.area.present = true;
    wall.area.shape = AreaPreviewShape::Sphere;
    wall.area.aim = {2, 3};
    wall.area.tiles = {{2, 3}};
    CHECK(render_frame(wall).world == 2);   // wall + aim; no floor fill on a wall

    ActorView ogre;
    ogre.id = 7;
    ogre.tile = {4, 4};
    ogre.footprint = 4;
    ogre.hp = ogre.hp_max = 40;
    ViewModel body = floors({{4, 4}});
    body.actors = {ogre};
    const uint32_t body_n = render_frame(body).world;
    body.area.present = true;
    body.area.shape = AreaPreviewShape::Cone;
    body.area.aim = {8, 8};
    body.area.hits.push_back(AreaHitView{7, AreaHitSide::Enemy});
    const uint32_t marked = render_frame(body).world;
    CHECK(marked == body_n + 2);   // aim cursor + one ring, not one per footprint tile

    ActorView pawn = ogre;
    pawn.footprint = 1;
    ViewModel small = floors({{4, 4}});
    small.actors = {pawn};
    const uint32_t small_n = render_frame(small).world;
    small.area = body.area;
    CHECK(render_frame(small).world == small_n + 2);

    ViewModel ally = body;
    ally.area.hits[0].side = AreaHitSide::Ally;
    CHECK(render_frame(ally).hash != render_frame(body).hash);

    ViewModel zone = floors({{2, 3}});
    zone.zones.push_back(ZoneOverlayView{1, 0, "srd51.spell.fog_cloud", {{2, 3}}});
    const uint32_t zone_n = render_frame(zone).world;
    zone.area.present = true;
    zone.area.shape = AreaPreviewShape::Cone;
    zone.area.aim = {2, 3};
    zone.area.tiles = {{2, 3}};
    CHECK(render_frame(zone).world == zone_n + 3);   // fill + edge + aim, zone sprites kept

    ViewModel quiet = floors({{1, 1}});
    const Frame q0 = render_frame(quiet);
    quiet.area.note = "NO AREA SPELL";
    const Frame q1 = render_frame(quiet);
    CHECK(q1.world == q0.world);
    CHECK(q1.ui > q0.ui);
    CHECK(q1.hash != q0.hash);
}

TEST_CASE("area probe: hits match area_targets, tiles wait, hashes stay put") {
    Board b({{kFighter, 10, 10}, {kFighter, 13, 11}, {kGoblin, 13, 10}, {kGoblin, 18, 10}});
    const uint32_t caster = b.id(0);
    const Vec2i aim{11, 10};
    const uint64_t h = b.c->hash();
    const uint64_t lh = b.c->log_hash();

    const auto cone_id = rules5e::spell_id_of("test.preview.cone");
    const rules5e::SpellDef* cone = b.c->spell_def(cone_id);
    REQUIRE(cone);
    const AreaPreviewView hit = area_probe(*b.c, caster, cone_id, aim, 17);
    CHECK(b.c->hash() == h);
    CHECK(b.c->log_hash() == lh);
    CHECK(hit.present);
    CHECK(hit.shape == AreaPreviewShape::Cone);
    CHECK(hit.tiles.empty());
    CHECK(hit.tiles_pending);
    CHECK(hit.pulse_ms == 17);
    CHECK(hit.note.empty());
    CHECK(hit.spell_label == "cone");
    const std::vector<uint32_t> api = b.c->area_targets(caster, *cone, aim);
    REQUIRE(hit.hits.size() == api.size());
    CHECK(hit.hits.size() == 2);
    for (std::size_t i = 0; i < api.size(); ++i) {
        CHECK(hit.hits[i].id == api[i]);
        CHECK(std::count_if(hit.hits.begin(), hit.hits.end(), [&](const AreaHitView& x) { return x.id == api[i]; }) == 1);
    }
    CHECK(hit.hits[0].id == b.id(1));
    CHECK(hit.hits[0].side == AreaHitSide::Ally);
    CHECK(hit.hits[1].id == b.id(2));
    CHECK(hit.hits[1].side == AreaHitSide::Enemy);
    const AreaPreviewView again = area_probe(*b.c, caster, cone_id, aim, 17);
    CHECK(again.hits.size() == hit.hits.size());
    CHECK(again.hits[0].id == hit.hits[0].id);
    CHECK(again.hits[1].side == hit.hits[1].side);

    struct Row { const char* id; AreaPreviewShape shape; };
    const Row rows[] = {
        {"test.preview.line", AreaPreviewShape::Line},
        {"test.preview.cube", AreaPreviewShape::Cube},
        {"test.preview.sphere", AreaPreviewShape::Sphere},
        {"test.preview.cylinder", AreaPreviewShape::Cylinder},
    };
    for (const Row& row : rows) {
        const auto id = rules5e::spell_id_of(row.id);
        const rules5e::SpellDef* def = b.c->spell_def(id);
        REQUIRE(def);
        const AreaPreviewView v = area_probe(*b.c, caster, id, aim, 0);
        const std::vector<uint32_t> ids = b.c->area_targets(caster, *def, aim);
        CHECK(v.present);
        CHECK(v.shape == row.shape);
        CHECK(v.tiles.empty());
        CHECK(v.tiles_pending);
        REQUIRE(v.hits.size() == ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) CHECK(v.hits[i].id == ids[i]);
    }
    CHECK(b.c->hash() == h);
    CHECK(b.c->log_hash() == lh);

    const AreaPreviewView tight = area_probe(*b.c, caster, rules5e::spell_id_of("test.preview.sphere_tight"), aim, 0);
    CHECK(tight.present);
    CHECK(tight.hits.empty());
    CHECK(tight.note.empty());   // nobody inside is still a preview
    CHECK(tight.tiles_pending);

    const AreaPreviewView rejected = area_probe(*b.c, caster, cone_id, b.c->pos(caster), 0);
    CHECK(rejected.present);
    CHECK(rejected.hits.empty());
    CHECK(rejected.tiles.empty());
    CHECK(rejected.note.find("differ") != std::string::npos);

    const AreaPreviewView missing = area_probe(*b.c, caster, 1, aim, 0);
    CHECK_FALSE(missing.present);
    CHECK(missing.note == "NO AREA SPELL");
    CHECK(missing.tiles.empty());
    CHECK_FALSE(missing.tiles_pending);
}

TEST_CASE("area targeting: F5 follows the PC's area spells under the cursor and does not cast") {
    std::string err;
    auto plain = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, true}, &err);
    REQUIRE_MESSAGE(plain, err);
    const uint32_t pc = pc_of(*plain);
    const uint32_t foe = foe_of(*plain);
    REQUIRE(pc != 0);
    REQUIRE(foe != 0);
    REQUIRE(plain->end_turns_until(pc));
    CHECK(plain->actor_is_pc(pc));
    CHECK_FALSE(plain->actor_is_pc(foe));
    CHECK(plain->area_spell_ids(pc).empty());

    AreaTargetView closed;
    ViewModel off = plain->view();
    closed.decorate(*plain, off, {12, 15}, 0);
    CHECK_FALSE(off.area.present);
    CHECK(off.area.note.empty());

    closed.handle(AreaTargetView::Cmd::Toggle, *plain);
    ViewModel none = plain->view();
    closed.decorate(*plain, none, {12, 15}, 3);
    CHECK_FALSE(none.area.present);
    CHECK(none.area.note == "NO AREA SPELL");
    CHECK(none.area.tiles.empty());

    const int round_before = plain->view().round;
    const AreaPreviewView q = plain->query_area(pc, rules5e::spell_id_of("srd51.spell.burning_hands"), {12, 15}, 9);
    CHECK(q.present);
    CHECK(q.shape == AreaPreviewShape::Cone);
    CHECK(q.tiles.empty());
    CHECK(q.tiles_pending);
    CHECK(q.pulse_ms == 9);
    CHECK(plain->view().round == round_before);   // the query did not step the fight

    REQUIRE(plain->end_turns_until(foe));
    ViewModel monster = plain->view();
    closed.decorate(*plain, monster, {12, 15}, 0);
    CHECK_FALSE(monster.area.present);
    CHECK(monster.area.note == "NOT YOUR TURN");

    auto teach = [](sim::ContentLibrary& lib) {
        std::ifstream in(std::filesystem::path(SLICE_PACK_ROOT) / "sample.vertical_slice/characters/fighter_1.json");
        REQUIRE(in);
        assets::Json doc = assets::Json::parse(std::string(std::istreambuf_iterator<char>(in), {}));
        assets::Json sc = assets::Json::object();
        sc["ability"] = "int";
        sc["caster_level"] = 1;
        sc["slots"] = assets::Json::object();
        sc["slots"]["1"] = 2;
        sc["spells"] = assets::Json::array({"srd51.spell.burning_hands", "srd51.spell.thunderwave", "srd51.spell.lightning_bolt"});
        doc["spellcasting"] = std::move(sc);
        const std::optional<std::string> add_err = lib.add(doc, "character");
        if (add_err) FAIL(*add_err);
    };
    Session::Options opt;
    opt.pack_roots = {SLICE_PACK_ROOT};
    opt.packs = {SLICE_PACK};
    opt.content_hook = teach;
    auto taught = Session::open(opt, &err);
    REQUIRE_MESSAGE(taught, err);
    const uint32_t mage = pc_of(*taught);
    REQUIRE(taught->end_turns_until(mage));
    const std::vector<uint32_t> spells = taught->area_spell_ids(mage);
    REQUIRE(spells.size() == 3);
    CHECK(spells[0] == rules5e::spell_id_of("srd51.spell.burning_hands"));
    CHECK(spells[1] == rules5e::spell_id_of("srd51.spell.thunderwave"));
    CHECK(spells[2] == rules5e::spell_id_of("srd51.spell.lightning_bolt"));

    AreaTargetView aim;
    aim.handle(AreaTargetView::Cmd::Toggle, *taught);
    const AreaPreviewShape want[] = {AreaPreviewShape::Cone, AreaPreviewShape::Cube, AreaPreviewShape::Line, AreaPreviewShape::Cone};
    for (AreaPreviewShape shape : want) {
        ViewModel vm = taught->view();
        aim.decorate(*taught, vm, {12, 15}, 40);
        CHECK(vm.area.present);
        CHECK(vm.area.shape == shape);
        CHECK(vm.area.aim == Vec2i{12, 15});
        CHECK(vm.area.tiles.empty());
        CHECK(vm.area.tiles_pending);
        CHECK(vm.area.caster == mage);
        aim.handle(AreaTargetView::Cmd::NextSpell, *taught);
    }
    aim.handle(AreaTargetView::Cmd::Toggle, *taught);
    ViewModel done = taught->view();
    aim.decorate(*taught, done, {12, 15}, 40);
    CHECK_FALSE(done.area.present);
    CHECK(done.area.note.empty());
}

TEST_CASE("area targeting: a static view has no actor, and the cursor picks the tile centre") {
    std::string err;
    auto s = Session::open({{SLICE_PACK_ROOT}, {SLICE_PACK}, 1, false}, &err);
    REQUIRE_MESSAGE(s, err);
    AreaTargetView v;
    v.handle(AreaTargetView::Cmd::Toggle, *s);
    ViewModel vm = s->view();
    v.decorate(*s, vm, {1, 1}, 0);
    CHECK_FALSE(vm.area.present);
    CHECK(vm.area.note == "NO ACTOR");
    const AreaPreviewView no_fight = s->query_area(1, rules5e::spell_id_of("srd51.spell.fireball"), {4, 4}, 0);
    CHECK_FALSE(no_fight.present);
    CHECK(no_fight.tiles.empty());
    CHECK(s->area_spell_ids(1).empty());

    render::NullDevice dev;
    SliceView view(dev);
    iso2d::Camera cam;
    cam.zoom = 2.f;
    cam.viewport_w = 800;
    cam.viewport_h = 600;
    cam.position = {15.f, -40.f};
    const Vec2f centre = view.tile_center_px(-2.f, 4.f);
    CHECK(view.tile_at_screen(cam.world_to_screen(centre), cam) == Vec2i{-2, 4});
    cam.zoom = 1.f;
    const Vec2f origin = view.tile_center_px(0.f, 0.f);
    CHECK(view.tile_at_screen(cam.world_to_screen(origin), cam) == Vec2i{0, 0});
}
