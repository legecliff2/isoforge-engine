// Footprint-origin area resolution (docs/proposals/AOE_MONSTER_ABILITIES.md, PR 1).
// Tests only: Combat::area_targets for cones, lines, cubes and spheres/cylinders cast by and
// hitting 2x2, 3x3 and 4x4 creatures. No behaviour change. Cylinder height and the Chebyshev
// "square sphere" are pinned as they work today; both are follow-ups, not SRD-correct geometry.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::AreaShape;
using rules5e::EventKind;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";
const Vec2i kAnchor{10, 10};

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json drake(const std::string& id, Json action, const char* size = "medium") {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["size"] = size;
    m["hit_points"]["average"] = 200;
    if (action.is_object()) m["actions"].push_back(std::move(action));
    return m;
}
Json cone_breath(int dc, const char* dice, int size_ft = 15) {
    return {{"kind", "save_effect"}, {"id", "breath"}, {"name", "Breath"},
            {"save", {{"ability", "dex"}, {"dc", dc}}},
            {"area", {{"shape", "cone"}, {"size_ft", size_ft}}}, {"origin", "self"},
            {"damage", Json::array({{{"dice", dice}, {"type", "fire"}}})}, {"on_success", "half"}};
}
struct Spawn { std::string archetype; int x, y; };
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> sight_walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : sight_walls) c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
ContentLibrary footprint_lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(drake("t.large", Json(), "large"), "monster"));
    REQUIRE_FALSE(l.add(drake("t.huge", Json(), "huge"), "monster"));
    REQUIRE_FALSE(l.add(drake("t.gargantuan", Json(), "gargantuan"), "monster"));
    return l;
}
const char* caster_id(int n) { return n == 2 ? "t.large" : n == 3 ? "t.huge" : "t.gargantuan"; }
rules5e::SpellDef tmpl(AreaShape s, int range_ft, int size_ft, int width_ft = 0, int height_ft = 0) {
    rules5e::SpellDef d;
    d.area = s;
    d.range_ft = uint16_t(range_ft);
    d.area_size_ft = uint16_t(size_ft);
    d.area_width_ft = uint16_t(width_ft);
    d.area_height_ft = uint16_t(height_ft);
    return d;
}
bool on_footprint(Vec2i t, Vec2i anchor, int n) {
    return t.x >= anchor.x && t.x < anchor.x + n && t.y >= anchor.y && t.y < anchor.y + n;
}
std::string show(std::vector<Vec2i> v) {
    std::sort(v.begin(), v.end());
    std::string o;
    for (Vec2i t : v) o += "(" + std::to_string(t.x) + "," + std::to_string(t.y) + ")";
    return o;
}
struct Placed { std::string archetype; Vec2i at; };
// Caster first. Probes are the other spawns, in order. `hit_tiles` is their anchors, sorted.
struct Board {
    ecs::Registry reg;
    world::Chunk ch;
    std::optional<Combat> c;
    std::vector<Vec2i> probe_at;
    Board(const ContentLibrary& lib, const char* caster, Vec2i at, std::vector<Placed> probes,
          std::vector<Vec2i> walls = {}, uint64_t seed = 1) {
        std::vector<Spawn> sp{{caster, at.x, at.y}};
        for (const auto& p : probes) {
            sp.push_back({p.archetype, p.at.x, p.at.y});
            probe_at.push_back(p.at);
        }
        ch = arena(std::move(sp), std::move(walls));
        std::string err;
        c = Combat::create(reg, ch, lib, seed, &err);
        REQUIRE_MESSAGE(c, err);
    }
    CombatantId caster() const { return ch.spawns[0].stable_id; }
    CombatantId probe(std::size_t i) const { return ch.spawns[i + 1].stable_id; }
    std::vector<CombatantId> ids(const rules5e::SpellDef& d, Vec2i aim, std::string* err = nullptr) const {
        return c->area_targets(caster(), d, aim, err);
    }
    std::vector<Vec2i> hit_tiles(const rules5e::SpellDef& d, Vec2i aim) const {
        std::vector<Vec2i> out;
        for (CombatantId id : ids(d, aim))
            for (std::size_t i = 0; i < probe_at.size(); ++i)
                if (probe(i) == id) out.push_back(probe_at[i]);
        std::sort(out.begin(), out.end());
        return out;
    }
};
void expect_tiles(const std::string& what, const std::vector<Vec2i>& got, std::vector<Vec2i> exp) {
    std::sort(exp.begin(), exp.end());
    CHECK_MESSAGE(got == exp, what << "\n got " << show(got) << "\n exp " << show(exp));
}
struct Golden {
    int n;
    const char* dir;
    Vec2i aim;
    std::vector<Vec2i> tiles;
};
// Neighbours of the golden tiles that are not themselves hits and not on the caster. A template
// that grows by one tile fails here; one that drops a listed tile fails the equality.
std::vector<Placed> with_border(int n, const std::vector<Vec2i>& tiles) {
    std::vector<Vec2i> all = tiles;
    std::sort(all.begin(), all.end());
    auto known = [&](Vec2i t) { return std::binary_search(all.begin(), all.end(), t); };
    std::vector<Placed> probes;
    for (Vec2i t : tiles) probes.push_back({kGoblin, t});
    for (Vec2i t : tiles) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                Vec2i q{t.x + dx, t.y + dy};
                if (q.x < 0 || q.y < 0 || q.x > 31 || q.y > 31) continue;
                if (known(q) || on_footprint(q, kAnchor, n)) continue;
                all.push_back(q);
                std::sort(all.begin(), all.end());
                probes.push_back({kGoblin, q});
            }
    }
    return probes;
}
void check_goldens(const ContentLibrary& lib, AreaShape shape, int size_ft, int width_ft, const std::vector<Golden>& rows) {
    const auto def = tmpl(shape, 0, size_ft, width_ft);
    for (const auto& row : rows) {
        Board b(lib, caster_id(row.n), kAnchor, with_border(row.n, row.tiles));
        const std::string what = std::string(shape == AreaShape::Cone ? "cone " : "line ") + row.dir + " " + std::to_string(row.n);
        expect_tiles(what, b.hit_tiles(def, row.aim), row.tiles);
        const auto once = b.ids(def, row.aim);
        CHECK(once.size() == row.tiles.size());   // one id per probe, even where the border crowds the edge
        auto again = b.ids(def, row.aim);
        CHECK(once == again);   // same board, same order
    }
}
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        auto r = c.submit(e);
        while (r.needs) r = c.decide({r.needs->window_seq, false, 0});
        REQUIRE(r.ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
struct SaveCast {
    std::vector<CombatantId> savers;
    std::vector<int> success;
    std::vector<int> natural;
    std::vector<std::pair<CombatantId, int>> damage;
    uint64_t state_hash = 0, log_hash = 0, event_hash = 0;
};
SaveCast cast_breath(ContentLibrary& lib, const char* caster_arch, Vec2i caster_at, std::vector<Placed> probes,
                     Vec2i aim, uint64_t seed) {
    Board b(lib, caster_arch, caster_at, std::move(probes), {}, seed);
    advance_to(*b.c, b.caster());
    const std::size_t from = b.c->events().size();
    Command cmd;
    cmd.aim = true;
    cmd.to = aim;
    cmd.request = {b.caster(), ActionKind::CastSpell};
    cmd.request.spell_id = rules5e::spell_id_of(std::string(caster_arch) + "#breath");
    auto r = b.c->submit(cmd);
    while (r.needs) r = b.c->decide({r.needs->window_seq, false, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    SaveCast out;
    for (std::size_t i = from; i < b.c->events().size(); ++i) {
        const auto& e = b.c->events()[i];
        if (e.kind != SimEvent::Kind::Rules) continue;
        if (e.rules.kind == EventKind::SavingThrowRolled) {
            out.savers.push_back(e.rules.actor);
            out.natural.push_back(e.rules.a);
            out.success.push_back(e.rules.c & 1);
        }
        if (e.rules.kind == EventKind::DamageDealt) out.damage.push_back({e.rules.target, e.rules.a});
    }
    out.state_hash = b.c->hash();
    out.log_hash = b.c->log_hash();
    out.event_hash = event_stream_hash(b.c->events());
    return out;
}
} // namespace

TEST_CASE("area geometry: self-cube touches the footprint edge, 8 directions, 2x2 3x3 and 4x4") {
    auto lib = footprint_lib();
    // 15 ft cube. Inclusive box. A diagonal aim ties to the horizontal cube (the sim prefers x).
    // The cross axis is centred on the centre tile, the lower of an even pair: n=4's mid row is 11, not 12.
    struct Box { int n; const char* dir; Vec2i aim; int x0, x1, y0, y1; };
    const Box boxes[] = {
        {2, "E", {12, 10}, 12, 14, 9, 11},  {2, "NE", {12, 9}, 12, 14, 9, 11}, {2, "SE", {12, 11}, 12, 14, 9, 11},
        {2, "W", {9, 10}, 7, 9, 9, 11},     {2, "NW", {9, 9}, 7, 9, 9, 11},    {2, "SW", {9, 11}, 7, 9, 9, 11},
        {2, "N", {10, 9}, 9, 11, 7, 9},     {2, "S", {10, 12}, 9, 11, 12, 14},
        {3, "E", {13, 11}, 13, 15, 10, 12}, {3, "NE", {13, 10}, 13, 15, 10, 12}, {3, "SE", {13, 12}, 13, 15, 10, 12},
        {3, "W", {9, 11}, 7, 9, 10, 12},    {3, "NW", {9, 10}, 7, 9, 10, 12},   {3, "SW", {9, 12}, 7, 9, 10, 12},
        {3, "N", {11, 9}, 10, 12, 7, 9},    {3, "S", {11, 13}, 10, 12, 13, 15},
        {4, "E", {14, 11}, 14, 16, 10, 12}, {4, "NE", {14, 10}, 14, 16, 10, 12}, {4, "SE", {14, 12}, 14, 16, 10, 12},
        {4, "W", {9, 11}, 7, 9, 10, 12},    {4, "NW", {9, 10}, 7, 9, 10, 12},   {4, "SW", {9, 12}, 7, 9, 10, 12},
        {4, "N", {11, 9}, 10, 12, 7, 9},    {4, "S", {11, 14}, 10, 12, 14, 16},
    };
    const auto cube = tmpl(AreaShape::Cube, 0, 15);
    for (const Box& box : boxes) {
        std::vector<Vec2i> inside;
        std::vector<Placed> probes;
        for (int y = box.y0; y <= box.y1; ++y)
            for (int x = box.x0; x <= box.x1; ++x) {
                inside.push_back({x, y});
                probes.push_back({kGoblin, {x, y}});
            }
        auto add_out = [&](int x, int y) {
            Vec2i t{x, y};
            if (t.x < 0 || t.y < 0 || t.x > 31 || t.y > 31) return;
            if (on_footprint(t, kAnchor, box.n)) return;
            if (t.x >= box.x0 && t.x <= box.x1 && t.y >= box.y0 && t.y <= box.y1) return;
            probes.push_back({kGoblin, t});
        };
        add_out(box.x0 - 1, box.y0);
        add_out(box.x1 + 1, box.y0);
        add_out(box.x0, box.y0 - 1);
        add_out(box.x1, box.y1 + 1);
        Board b(lib, caster_id(box.n), kAnchor, probes);
        expect_tiles(std::string("cube ") + box.dir + " " + std::to_string(box.n), b.hit_tiles(cube, box.aim), inside);
    }
}

TEST_CASE("area geometry: sphere and cylinder use the footprint, and a point origin, at 2x2 3x3 and 4x4") {
    auto lib = footprint_lib();
    // Self origin: Chebyshev distance from the nearest footprint tile (rect_distance), not from the anchor.
    // Radius 5 ft is the adjacent ring, corners included. Radius 10 ft takes the next ring. A cylinder is the
    // same 2D test; area_height_ft does not change membership (follow-up: cylinder height is ignored).
    struct Probe { Vec2i at; int dist; };
    const Probe rings[][4] = {
        {{{12, 10}, 1}, {{12, 12}, 1}, {{13, 10}, 2}, {{14, 11}, 3}},   // 2x2, footprint x 10..11
        {{{13, 11}, 1}, {{13, 13}, 1}, {{14, 11}, 2}, {{15, 12}, 3}},   // 3x3, footprint x 10..12
        {{{14, 12}, 1}, {{14, 14}, 1}, {{15, 12}, 2}, {{16, 13}, 3}},   // 4x4, footprint x 10..13
    };
    const int ns[] = {2, 3, 4};
    for (int i = 0; i < 3; ++i) {
        std::vector<Placed> probes;
        for (const Probe& p : rings[i]) probes.push_back({kGoblin, p.at});
        Board b(lib, caster_id(ns[i]), kAnchor, probes);
        auto members = [&](int radius) {
            std::vector<Vec2i> exp;
            for (const Probe& p : rings[i])
                if (p.dist <= radius) exp.push_back(p.at);
            return exp;
        };
        const Vec2i aim{kAnchor.x + ns[i], kAnchor.y};   // self sphere ignores the aim point
        expect_tiles("sphere r1", b.hit_tiles(tmpl(AreaShape::Sphere, 0, 5), aim), members(1));
        expect_tiles("sphere r2", b.hit_tiles(tmpl(AreaShape::Sphere, 0, 10), aim), members(2));
        expect_tiles("cylinder r1", b.hit_tiles(tmpl(AreaShape::Cylinder, 0, 5, 0, 100), aim), members(1));
        expect_tiles("cylinder r2 height 5", b.hit_tiles(tmpl(AreaShape::Cylinder, 0, 10, 0, 5), aim), members(2));
        CHECK(b.hit_tiles(tmpl(AreaShape::Sphere, 0, 5), aim) == b.hit_tiles(tmpl(AreaShape::Cylinder, 0, 5), aim));
    }
    // Point origin: Chebyshev about the aim tile. Range and line of sight are to the aim, from any caster tile.
    {
        Board b(lib, kFighter, {10, 10}, {{kGoblin, {18, 10}}, {kGoblin, {20, 10}}, {kGoblin, {20, 12}}, {kGoblin, {21, 10}}});
        const auto ball = tmpl(AreaShape::Sphere, 60, 10);   // radius 2 tiles about (18,10)
        expect_tiles("point sphere", b.hit_tiles(ball, {18, 10}), {{18, 10}, {20, 10}, {20, 12}});
        expect_tiles("point cylinder", b.hit_tiles(tmpl(AreaShape::Cylinder, 60, 10, 0, 30), {18, 10}),
                     {{18, 10}, {20, 10}, {20, 12}});
        std::string err;
        CHECK(b.ids(tmpl(AreaShape::Sphere, 20, 10), {18, 10}, &err).empty());   // 8 tiles = 40 ft > 20
        CHECK(err == "aim beyond range");
    }
    // A 4x4, 3x3 and 2x2 partly inside a point sphere are each one target.
    {
        Board b(lib, kFighter, {10, 10}, {{"t.gargantuan", {20, 10}}});   // (20,10) is at radius 2 of aim (18,10); (21,10) is not
        const auto ids = b.ids(tmpl(AreaShape::Sphere, 120, 10), {18, 10});
        CHECK(ids == std::vector<CombatantId>{b.probe(0)});
        CHECK(b.ids(tmpl(AreaShape::Cylinder, 120, 10, 0, 5), {18, 10}) == ids);
    }
    {
        Board b(lib, kFighter, {10, 10}, {{"t.huge", {19, 9}}});   // 3x3 x 19..21, y 9..11: six tiles are inside radius 2 of (18,10), the x=21 column is not
        CHECK(b.ids(tmpl(AreaShape::Sphere, 120, 10), {18, 10}).size() == 1);
    }
    {
        Board b(lib, kFighter, {10, 10}, {{"t.large", {20, 11}}});   // 2x2: (20,11) dist 2, (21,12) dist 3
        CHECK(b.ids(tmpl(AreaShape::Sphere, 120, 10), {18, 10}).size() == 1);
        Board miss(lib, kFighter, {10, 10}, {{"t.large", {21, 10}}});   // nearest tile is 3 away
        std::string err;
        CHECK(miss.ids(tmpl(AreaShape::Sphere, 120, 10), {18, 10}, &err).empty());
        CHECK(err == "no creature in the area");
    }
}

TEST_CASE("area geometry: point-origin line of sight uses any caster tile, 2x2 3x3 and 4x4") {
    auto lib = footprint_lib();
    const auto ball = tmpl(AreaShape::Sphere, 90, 5);
    // The wall blocks the anchor's straight line to the aim. Another tile of the footprint still sees it,
    // so the aim is legal and the empty template reports no creature. A 1x1 on the anchor cannot see it.
    struct Case { int n; Vec2i wall; Vec2i aim; };
    const Case cases[] = {{2, {12, 10}, {16, 10}}, {3, {13, 10}, {16, 10}}, {4, {14, 11}, {18, 11}}};
    for (const Case& cs : cases) {
        Board big(lib, caster_id(cs.n), kAnchor, {}, {cs.wall});
        std::string err = "unset";
        CHECK(big.ids(ball, cs.aim, &err).empty());
        CHECK(err == "no creature in the area");
        Board small(lib, kFighter, kAnchor, {}, {cs.wall});
        err = "unset";
        CHECK(small.ids(ball, cs.aim, &err).empty());
        CHECK(err == "aim not in line of sight");
    }
}

TEST_CASE("area geometry: cone and line from the footprint centre, 8 directions, odd and even") {
    auto lib = footprint_lib();
    // Hand check, not taken from the tables below. 3x3 anchor (10,10), centre tile (11,11), cone 30 ft aimed
    // due east at (13,11). Length is 6 tiles from the centre: (17,11) is in, (18,11) is out. The half-angle
    // boundary holds (17,8) and rejects (17,7). (9,11) is behind the centre.
    {
        Board b(lib, "t.huge", kAnchor,
                {{kGoblin, {13, 11}}, {kGoblin, {17, 11}}, {kGoblin, {17, 8}}, {kGoblin, {18, 11}}, {kGoblin, {17, 7}}, {kGoblin, {9, 11}}});
        expect_tiles("huge cone east", b.hit_tiles(tmpl(AreaShape::Cone, 0, 30), {13, 11}), {{13, 11}, {17, 11}, {17, 8}});
    }
    // 4x4 anchor (10,10). The centre sits between tiles (doubled centre 23), so an "east" aim at the lower
    // middle row (14,11) is one doubled-unit north of the centre. The 30x5 line is those four tiles, not the
    // row of a 1x1 standing on the anchor. (17,11) is off that width; (18,10) is past the length.
    {
        Board b(lib, "t.gargantuan", kAnchor,
                {{kGoblin, {14, 11}}, {kGoblin, {15, 11}}, {kGoblin, {16, 11}}, {kGoblin, {17, 10}}, {kGoblin, {17, 11}}, {kGoblin, {18, 10}}});
        expect_tiles("gargantuan line east", b.hit_tiles(tmpl(AreaShape::Line, 0, 30, 5), {14, 11}),
                     {{14, 11}, {15, 11}, {16, 11}, {17, 10}});
    }
    // Full templates, every direction. Even centres (2 and 4) have no on-axis tile, so cardinal aims sit a
    // half-tile off the centre; the 3x3 aims are on the centre row or column. Each listed tile is spawned,
    // plus every in-bounds neighbour that is not a hit and not the caster, so a one-tile drift fails.
    const std::vector<Golden> lines = {
    {2,"E",{12,10},{{14,9},{15,9},{16,9},{12,10},{13,10}}},
    {2,"NE",{12,9},{{14,7},{13,8},{12,9}}},
    {2,"N",{10,9},{{9,5},{9,6},{9,7},{10,8},{10,9}}},
    {2,"NW",{9,9},{{7,7},{8,8},{9,9}}},
    {2,"W",{9,10},{{5,9},{6,9},{7,9},{8,10},{9,10}}},
    {2,"SW",{9,11},{{8,11},{9,11},{5,12},{6,12},{7,12}}},
    {2,"S",{10,12},{{10,12},{10,13},{9,14},{9,15},{9,16}}},
    {2,"SE",{12,11},{{12,11},{13,11},{14,12},{15,12},{16,12}}},
    {3,"E",{13,11},{{13,11},{14,11},{15,11},{16,11},{17,11}}},
    {3,"NE",{13,10},{{16,8},{14,9},{15,9},{16,9},{13,10},{14,10}}},
    {3,"N",{11,9},{{11,5},{11,6},{11,7},{11,8},{11,9}}},
    {3,"NW",{9,10},{{6,8},{6,9},{7,9},{8,9},{8,10},{9,10}}},
    {3,"W",{9,11},{{5,11},{6,11},{7,11},{8,11},{9,11}}},
    {3,"SW",{9,12},{{8,12},{9,12},{6,13},{7,13},{8,13},{6,14}}},
    {3,"S",{11,13},{{11,13},{11,14},{11,15},{11,16},{11,17}}},
    {3,"SE",{13,12},{{13,12},{14,12},{14,13},{15,13},{16,13},{16,14}}},
    {4,"E",{14,11},{{17,10},{14,11},{15,11},{16,11}}},
    {4,"NE",{14,10},{{15,9},{16,9},{14,10}}},
    {4,"N",{11,9},{{10,6},{11,7},{11,8},{11,9}}},
    {4,"NW",{9,10},{{7,9},{8,9},{9,10}}},
    {4,"W",{9,11},{{6,10},{7,11},{8,11},{9,11}}},
    {4,"SW",{9,12},{{7,12},{8,12},{9,12},{6,13}}},
    {4,"S",{11,14},{{11,14},{11,15},{11,16},{10,17}}},
    {4,"SE",{14,12},{{14,12},{15,12},{16,12},{17,13}}},
    };
    const std::vector<Golden> cones = {
    {2,"E",{12,10},{{16,5},{15,6},{16,6},{14,7},{15,7},{16,7},{13,8},{14,8},{15,8},{16,8},{12,9},{13,9},{14,9},{15,9},{16,9},{12,10},{13,10},{14,10},{15,10},{16,10},{14,11},{15,11},{16,11}}},
    {2,"NE",{12,9},{{13,5},{14,5},{15,5},{16,5},{12,6},{13,6},{14,6},{15,6},{16,6},{12,7},{13,7},{14,7},{15,7},{16,7},{12,8},{13,8},{14,8},{15,8},{16,8},{11,9},{12,9},{13,9},{14,9},{15,9},{12,10}}},
    {2,"N",{10,9},{{5,5},{6,5},{7,5},{8,5},{9,5},{10,5},{11,5},{6,6},{7,6},{8,6},{9,6},{10,6},{11,6},{7,7},{8,7},{9,7},{10,7},{11,7},{8,8},{9,8},{10,8},{9,9},{10,9}}},
    {2,"NW",{9,9},{{5,5},{6,5},{7,5},{8,5},{5,6},{6,6},{7,6},{8,6},{9,6},{5,7},{6,7},{7,7},{8,7},{9,7},{5,8},{6,8},{7,8},{8,8},{9,8},{6,9},{7,9},{8,9},{9,9},{10,9},{9,10}}},
    {2,"W",{9,10},{{5,5},{5,6},{6,6},{5,7},{6,7},{7,7},{5,8},{6,8},{7,8},{8,8},{5,9},{6,9},{7,9},{8,9},{9,9},{5,10},{6,10},{7,10},{8,10},{9,10},{5,11},{6,11},{7,11}}},
    {2,"SW",{9,11},{{5,10},{6,10},{7,10},{5,11},{6,11},{7,11},{8,11},{9,11},{5,12},{6,12},{7,12},{8,12},{9,12},{5,13},{6,13},{7,13},{8,13},{5,14},{6,14},{7,14},{5,15},{6,15},{5,16}}},
    {2,"S",{10,12},{{9,12},{10,12},{8,13},{9,13},{10,13},{7,14},{8,14},{9,14},{10,14},{11,14},{6,15},{7,15},{8,15},{9,15},{10,15},{11,15},{5,16},{6,16},{7,16},{8,16},{9,16},{10,16},{11,16}}},
    {2,"SE",{12,11},{{14,10},{15,10},{16,10},{12,11},{13,11},{14,11},{15,11},{16,11},{12,12},{13,12},{14,12},{15,12},{16,12},{13,13},{14,13},{15,13},{16,13},{14,14},{15,14},{16,14},{15,15},{16,15},{16,16}}},
    {3,"E",{13,11},{{17,8},{15,9},{16,9},{17,9},{13,10},{14,10},{15,10},{16,10},{17,10},{13,11},{14,11},{15,11},{16,11},{17,11},{13,12},{14,12},{15,12},{16,12},{17,12},{15,13},{16,13},{17,13},{17,14}}},
    {3,"NE",{13,10},{{16,5},{17,5},{15,6},{16,6},{17,6},{14,7},{15,7},{16,7},{17,7},{14,8},{15,8},{16,8},{17,8},{13,9},{14,9},{15,9},{16,9},{17,9},{13,10},{14,10},{15,10},{16,10},{17,10},{13,11},{14,11},{15,11},{16,11},{17,11}}},
    {3,"N",{11,9},{{8,5},{9,5},{10,5},{11,5},{12,5},{13,5},{14,5},{9,6},{10,6},{11,6},{12,6},{13,6},{9,7},{10,7},{11,7},{12,7},{13,7},{10,8},{11,8},{12,8},{10,9},{11,9},{12,9}}},
    {3,"NW",{9,10},{{5,5},{6,5},{5,6},{6,6},{7,6},{5,7},{6,7},{7,7},{8,7},{5,8},{6,8},{7,8},{8,8},{5,9},{6,9},{7,9},{8,9},{9,9},{5,10},{6,10},{7,10},{8,10},{9,10},{5,11},{6,11},{7,11},{8,11},{9,11}}},
    {3,"W",{9,11},{{5,8},{5,9},{6,9},{7,9},{5,10},{6,10},{7,10},{8,10},{9,10},{5,11},{6,11},{7,11},{8,11},{9,11},{5,12},{6,12},{7,12},{8,12},{9,12},{5,13},{6,13},{7,13},{5,14}}},
    {3,"SW",{9,12},{{5,11},{6,11},{7,11},{8,11},{9,11},{5,12},{6,12},{7,12},{8,12},{9,12},{5,13},{6,13},{7,13},{8,13},{9,13},{5,14},{6,14},{7,14},{8,14},{5,15},{6,15},{7,15},{8,15},{5,16},{6,16},{7,16},{5,17},{6,17}}},
    {3,"S",{11,13},{{10,13},{11,13},{12,13},{10,14},{11,14},{12,14},{9,15},{10,15},{11,15},{12,15},{13,15},{9,16},{10,16},{11,16},{12,16},{13,16},{8,17},{9,17},{10,17},{11,17},{12,17},{13,17},{14,17}}},
    {3,"SE",{13,12},{{13,11},{14,11},{15,11},{16,11},{17,11},{13,12},{14,12},{15,12},{16,12},{17,12},{13,13},{14,13},{15,13},{16,13},{17,13},{14,14},{15,14},{16,14},{17,14},{14,15},{15,15},{16,15},{17,15},{15,16},{16,16},{17,16},{16,17},{17,17}}},
    {4,"E",{14,11},{{16,8},{17,8},{15,9},{16,9},{17,9},{14,10},{15,10},{16,10},{17,10},{14,11},{15,11},{16,11},{17,11},{14,12},{15,12},{16,12},{17,12},{17,13}}},
    {4,"NE",{14,10},{{15,6},{16,6},{17,6},{15,7},{16,7},{17,7},{14,8},{15,8},{16,8},{17,8},{14,9},{15,9},{16,9},{17,9},{14,10},{15,10},{16,10},{17,10},{14,11},{15,11},{16,11},{17,11}}},
    {4,"N",{11,9},{{8,6},{9,6},{10,6},{11,6},{12,6},{13,6},{8,7},{9,7},{10,7},{11,7},{12,7},{9,8},{10,8},{11,8},{12,8},{10,9},{11,9},{12,9}}},
    {4,"NW",{9,10},{{6,6},{7,6},{8,6},{6,7},{7,7},{8,7},{6,8},{7,8},{8,8},{9,8},{6,9},{7,9},{8,9},{9,9},{6,10},{7,10},{8,10},{9,10},{6,11},{7,11},{8,11},{9,11}}},
    {4,"W",{9,11},{{6,8},{7,8},{6,9},{7,9},{8,9},{6,10},{7,10},{8,10},{9,10},{6,11},{7,11},{8,11},{9,11},{6,12},{7,12},{8,12},{9,12},{6,13}}},
    {4,"SW",{9,12},{{6,10},{6,11},{7,11},{8,11},{9,11},{6,12},{7,12},{8,12},{9,12},{6,13},{7,13},{8,13},{9,13},{6,14},{7,14},{8,14},{6,15},{7,15}}},
    {4,"S",{11,14},{{10,14},{11,14},{12,14},{9,15},{10,15},{11,15},{12,15},{8,16},{9,16},{10,16},{11,16},{12,16},{8,17},{9,17},{10,17},{11,17},{12,17},{13,17}}},
    {4,"SE",{14,12},{{17,10},{14,11},{15,11},{16,11},{17,11},{14,12},{15,12},{16,12},{17,12},{14,13},{15,13},{16,13},{17,13},{15,14},{16,14},{17,14},{16,15},{17,15}}},
    };
    check_goldens(lib, AreaShape::Line, 30, 5, lines);
    check_goldens(lib, AreaShape::Cone, 30, 0, cones);
}

TEST_CASE("area hits a creature once, in slot order, including a partly covered 4x4") {
    auto lib = footprint_lib();
    const auto cone = tmpl(AreaShape::Cone, 0, 15);
    const auto cube = tmpl(AreaShape::Cube, 0, 15);
    const auto line = tmpl(AreaShape::Line, 0, 30, 5);
    const Vec2i east{11, 10};
    // Any covered tile is enough. A footprint that only overlaps the template is still one id.
    {
        Board hit(lib, kFighter, {10, 10}, {{"t.gargantuan", {12, 9}}, {kGoblin, {16, 12}}});
        const auto ids = hit.ids(cube, east);
        CHECK(ids.size() == 1);
        CHECK(ids[0] == hit.probe(0));   // the goblin past the cube is not a second hit
        CHECK(hit.ids(cone, east) == ids);
        Board miss(lib, kFighter, {10, 10}, {{"t.gargantuan", {14, 10}}});   // the whole 4x4 is past the 15 ft cone and cube
        std::string err;
        CHECK(miss.ids(cone, east, &err).empty());
        CHECK(err == "no creature in the area");
    }
    {
        Board b(lib, kFighter, {10, 10}, {{"t.huge", {12, 10}}});   // 3x3: the near four tiles are in the cube, the far column is not
        CHECK(b.ids(cube, east).size() == 1);
        CHECK(b.ids(cone, east).size() == 1);
    }
    {
        Board b(lib, kFighter, {10, 10}, {{"t.large", {13, 10}}});   // 2x2: (13,10) and (13,11) are in the cone, x=14 is not
        CHECK(b.ids(cone, east).size() == 1);
        CHECK(b.ids(line, east).size() == 1);
    }
    // Two creatures inside: slot order (spawn order), not distance. The far goblin is spawned first.
    {
        Board b(lib, kFighter, {10, 10}, {{kGoblin, {16, 10}}, {kGoblin, {12, 10}}});
        const auto def = tmpl(AreaShape::Cone, 0, 30);
        const auto ids = b.ids(def, east);
        CHECK(ids == std::vector<CombatantId>{b.probe(0), b.probe(1)});
        CHECK(b.ids(def, east) == ids);
        Board swapped(lib, kFighter, {10, 10}, {{kGoblin, {12, 10}}, {kGoblin, {16, 10}}});
        CHECK(swapped.ids(def, east) == std::vector<CombatantId>{swapped.probe(0), swapped.probe(1)});
    }
}

TEST_CASE("area geometry: line of sight is per tile of the target footprint") {
    auto lib = footprint_lib();
    const Vec2i aim{13, 10};
    const auto cone = tmpl(AreaShape::Cone, 0, 15);
    const auto line = tmpl(AreaShape::Line, 0, 20, 5);   // 4 tiles long, one tile wide, along y=10
    // Wall at (12,10) hides (13,10) and leaves (13,9) visible. The cone contains both, so the creature is hit
    // on the seen tile alone. The line contains only the hidden row, so the visible off-line tiles do not count.
    struct Target { const char* id; Vec2i at; };
    const Target targets[] = {{"t.large", {13, 9}}, {"t.huge", {13, 8}}, {"t.gargantuan", {13, 7}}};
    for (const Target& t : targets) {
        Board open(lib, kFighter, {10, 10}, {{t.id, t.at}});
        CHECK(open.ids(cone, aim).size() == 1);
        CHECK(open.ids(line, aim).size() == 1);
        Board walled(lib, kFighter, {10, 10}, {{t.id, t.at}}, {{12, 10}});
        CHECK(walled.ids(cone, aim).size() == 1);    // one inside tile is seen
        std::string err;
        CHECK(walled.ids(line, aim, &err).empty());   // the only inside tiles are hidden
        CHECK(err == "no creature in the area");
        const auto ids = walled.ids(cone, aim);   // one id, not one per covered tile
        CHECK(ids.size() == 1);
    }
}

TEST_CASE("area save: half damage on a success, one roll for a multi-tile creature, deterministic") {
    // 1d1+N is a fixed total. Dex save of a fighter or a goblin-derived monster is +2 (dex 14, not proficient):
    // DC 1 always succeeds, DC 30 always fails. Half is integer division (7 -> 3).
    auto run = [](int dc, const char* dice, const char* caster, Vec2i at, std::vector<Placed> probes, Vec2i aim, uint64_t seed) {
        ContentLibrary lib;
        REQUIRE(lib.load(sample_set()).empty());
        REQUIRE_FALSE(lib.add(drake(caster, cone_breath(dc, dice), caster == std::string("t.large") ? "large" : "medium"), "monster"));
        if (std::string(caster) != "t.large") {
            REQUIRE_FALSE(lib.add(drake("t.large", Json(), "large"), "monster"));
            REQUIRE_FALSE(lib.add(drake("t.huge", Json(), "huge"), "monster"));
            REQUIRE_FALSE(lib.add(drake("t.gargantuan", Json(), "gargantuan"), "monster"));
        }
        return cast_breath(lib, caster, at, std::move(probes), aim, seed);
    };
    const int full = 8, half = 4;
    {
        const auto fail = run(30, "1d1+7", "t.breath", {5, 5}, {{kFighter, {7, 5}}}, {9, 5}, 42);
        const auto fail2 = run(30, "1d1+7", "t.breath", {5, 5}, {{kFighter, {7, 5}}}, {9, 5}, 42);
        const auto save = run(1, "1d1+7", "t.breath", {5, 5}, {{kFighter, {7, 5}}}, {9, 5}, 42);
        REQUIRE(fail.damage.size() == 1);
        CHECK(fail.damage[0].second == full);
        REQUIRE(save.damage.size() == 1);
        CHECK(save.damage[0].second == half);
        CHECK(fail.success == std::vector<int>{0});
        CHECK(save.success == std::vector<int>{1});
        CHECK(fail.natural == save.natural);   // the d20 is the same roll; only the DC changed the result
        CHECK(fail.event_hash == fail2.event_hash);
        CHECK(fail.state_hash == fail2.state_hash);
        CHECK(fail.log_hash == fail2.log_hash);
        CHECK(fail.event_hash != save.event_hash);
    }
    {
        const auto odd = run(1, "1d1+6", "t.breath", {5, 5}, {{kFighter, {7, 5}}}, {9, 5}, 7);
        const auto odd_fail = run(30, "1d1+6", "t.breath", {5, 5}, {{kFighter, {7, 5}}}, {9, 5}, 7);
        REQUIRE(odd.damage.size() == 1);
        CHECK(odd.damage[0].second == 3);   // 7 / 2, round down
        CHECK(odd_fail.damage[0].second == 7);
    }
    // Two fighters in the cone: one save and one damage each, in spawn order (the farther one is spawned first).
    {
        const auto both = run(30, "1d1+7", "t.breath", {5, 5}, {{kFighter, {8, 6}}, {kFighter, {7, 5}}}, {9, 5}, 3);
        REQUIRE(both.savers.size() == 2);
        REQUIRE(both.damage.size() == 2);
        CHECK(both.damage[0].second == full);
        CHECK(both.damage[1].second == full);
        CHECK(both.savers[0] == both.damage[0].first);
        CHECK(both.savers[1] == both.damage[1].first);
        // Spawn order on chunk (5,5,0): index 0 is the caster, 1 is the farther fighter, 2 is the nearer one.
        CHECK(both.savers[0] == world::spawn_stable_id({5, 5, 0}, 1));
        CHECK(both.savers[1] == world::spawn_stable_id({5, 5, 0}, 2));
    }
    // A footprint that several cone tiles cover still saves once and takes the damage once.
    // The fighter stands north of the cone so the two teams stay in the fight; the cone does not reach that tile.
    for (const char* size_id : {"t.large", "t.huge", "t.gargantuan"}) {
        const Vec2i at = std::string(size_id) == "t.large" ? Vec2i{7, 5} : Vec2i{6, 4};
        const std::vector<Placed> probes{{size_id, at}, {kFighter, {5, 8}}};
        const auto fail = run(30, "1d1+7", "t.breath", {5, 5}, probes, {9, 5}, 11);
        const auto save = run(1, "1d1+7", "t.breath", {5, 5}, probes, {9, 5}, 11);
        CHECK(fail.savers.size() == 1);
        CHECK(save.savers.size() == 1);
        REQUIRE(fail.damage.size() == 1);
        REQUIRE(save.damage.size() == 1);
        CHECK(fail.damage[0].second == full);
        CHECK(save.damage[0].second == half);
        CHECK(fail.natural == save.natural);
    }
    // Cast by a Large creature: the cone still resolves through the same save, half on a success.
    {
        const auto save = run(1, "1d1+7", "t.large", {8, 8}, {{kFighter, {10, 8}}}, {12, 8}, 9);
        CHECK(save.savers.size() == 1);
        REQUIRE(save.damage.size() == 1);
        CHECK(save.damage[0].second == half);
        CHECK(save.success == std::vector<int>{1});
    }
}
