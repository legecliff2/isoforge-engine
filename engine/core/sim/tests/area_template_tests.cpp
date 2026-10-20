// Area templates (VERSION_BUMP_BATCH row 7): aim tile via Command::to, geometry over footprint tiles, line of sight
// from the origin, opt-in derivation, replay, and the area-action AI.
#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <string>

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

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
Json drake(const std::string& id, Json action, const char* size = "medium", bool melee_only = false) {
    Json m = load_json("content/packs/srd51.core/monsters/goblin.json");
    m["$schema"] = "monster@4";
    m["id"] = id;
    m["size"] = size;
    m["hit_points"]["average"] = 200;
    if (melee_only) m["actions"].erase(1);   // the shortbow
    if (action.is_object()) m["actions"].push_back(std::move(action));
    return m;
}
Json cone_breath(int size_ft = 30) {
    return {{"kind", "save_effect"}, {"id", "breath"}, {"name", "Breath"}, {"save", {{"ability", "dex"}, {"dc", 14}}},
            {"area", {{"shape", "cone"}, {"size_ft", size_ft}}}, {"origin", "self"},
            {"damage", Json::array({{{"dice", "1d2"}, {"type", "fire"}}})}, {"on_success", "half"}};
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
ContentLibrary sample_lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    REQUIRE_FALSE(l.add(drake("t.large", Json(), "large"), "monster"));
    return l;
}
rules5e::SpellDef tmpl(AreaShape s, int range_ft, int size_ft, int width_ft = 0) {
    rules5e::SpellDef d;
    d.area = s;
    d.range_ft = uint16_t(range_ft);
    d.area_size_ft = uint16_t(size_ft);
    d.area_width_ft = uint16_t(width_ft);
    return d;
}
// Caster (fighter) first, then the goblins; returns the goblin indices (1-based spawn order) the template catches.
struct Geo {
    ecs::Registry reg;
    world::Chunk ch;
    std::optional<Combat> c;
    Geo(const ContentLibrary& lib, std::vector<Spawn> sp, std::vector<Vec2i> walls = {}) {
        ch = arena(sp, std::move(walls));
        c = Combat::create(reg, ch, lib, 1);
        REQUIRE(c);
    }
    std::vector<std::size_t> hit(const rules5e::SpellDef& d, Vec2i aim, std::string* err = nullptr) const {
        std::vector<std::size_t> out;
        for (CombatantId id : c->area_targets(ch.spawns[0].stable_id, d, aim, err))
            for (std::size_t i = 1; i < ch.spawns.size(); ++i)
                if (ch.spawns[i].stable_id == id) out.push_back(i);
        std::sort(out.begin(), out.end());
        return out;
    }
};
using V = std::vector<std::size_t>;
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 6 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
} // namespace

TEST_CASE("area geometry: sphere and cylinder, self and point origin") {
    auto lib = sample_lib();
    Geo g(lib, {{kFighter, 10, 10}, {kGoblin, 12, 10}, {kGoblin, 13, 10}, {kGoblin, 16, 12}, {kGoblin, 18, 10}});
    CHECK(g.hit(tmpl(AreaShape::Sphere, 0, 10), {10, 11}) == V{1});            // radius 2 tiles about the caster
    CHECK(g.hit(tmpl(AreaShape::Cylinder, 0, 15), {10, 11}) == V{1, 2});       // radius 3
    const auto fb = tmpl(AreaShape::Sphere, 60, 20);                           // point origin, radius 4 tiles
    CHECK(g.hit(fb, {15, 10}) == V{1, 2, 3, 4});                               // distances 3, 2, 2, 3
    CHECK(g.hit(tmpl(AreaShape::Sphere, 60, 10), {15, 10}) == V{2, 3});        // radius 2: only the two at distance 2
    std::string err;
    CHECK(g.hit(tmpl(AreaShape::Sphere, 20, 10), {15, 10}, &err).empty());     // 5 tiles = 25 ft > 20 ft range
    CHECK(err == "aim beyond range");
    CHECK(g.hit(tmpl(AreaShape::Sphere, 60, 10), {15, 3}, &err).empty());      // nobody inside
    CHECK(err == "no creature in the area");
}

TEST_CASE("area geometry: cone is as wide as it is long and starts at the caster") {
    auto lib = sample_lib();
    Geo g(lib, {{kFighter, 10, 10}, {kGoblin, 13, 10}, {kGoblin, 13, 11}, {kGoblin, 13, 12}, {kGoblin, 14, 10}, {kGoblin, 9, 10},
                {kGoblin, 12, 8}});
    const auto cone = tmpl(AreaShape::Cone, 0, 15);   // 3 tiles
    CHECK(g.hit(cone, {11, 10}) == V{1, 2});          // (13,12) is 2 off the axis at 3 tiles: wider than half the length
    CHECK(g.hit(cone, {10, 9}) == V{});               // pointing north: nothing there ...
    CHECK(g.hit(cone, {12, 10}) == V{1, 2});          // ... aim distance does not matter, only the direction
    std::string err;
    CHECK(g.hit(cone, {10, 10}, &err).empty());
    CHECK(err == "aim must differ from the caster's tile");
    CHECK(g.hit(tmpl(AreaShape::Cone, 0, 15), {11, 9}) == V{6});   // diagonal: (12,8) is on the diagonal, 2 tiles out
}

TEST_CASE("area geometry: line has a length and a width") {
    auto lib = sample_lib();
    Geo g(lib, {{kFighter, 10, 10}, {kGoblin, 16, 10}, {kGoblin, 17, 10}, {kGoblin, 13, 11}, {kGoblin, 14, 10}, {kGoblin, 8, 10}});
    CHECK(g.hit(tmpl(AreaShape::Line, 0, 30, 5), {12, 10}) == V{1, 4});         // 30 ft = 6 tiles, 5 ft wide
    CHECK(g.hit(tmpl(AreaShape::Line, 0, 30, 15), {12, 10}) == V{1, 3, 4});     // 15 ft wide reaches (13,11)
    CHECK(g.hit(tmpl(AreaShape::Line, 0, 60, 5), {12, 10}) == V{1, 2, 4});      // longer
}

TEST_CASE("area geometry: cube from the caster and cube at a point") {
    auto lib = sample_lib();
    Geo g(lib, {{kFighter, 10, 10}, {kGoblin, 13, 11}, {kGoblin, 14, 10}, {kGoblin, 13, 12}, {kGoblin, 10, 9}, {kGoblin, 9, 10}});
    const auto cube = tmpl(AreaShape::Cube, 0, 15);   // 3 x 3 touching the caster: x 11..13, y 9..11 when aimed east
    CHECK(g.hit(cube, {11, 10}) == V{1});
    CHECK(g.hit(cube, {10, 9}) == V{4});              // aimed north: x 9..11, y 7..9, (10,9) inside
    CHECK(g.hit(cube, {9, 10}) == V{5});              // aimed west
    CHECK(g.hit(tmpl(AreaShape::Cube, 60, 10), {13, 11}) == V{1, 3});   // 2 x 2 at a point: x 13..14, y 11..12
}

TEST_CASE("area geometry: multi-tile creatures are caught by any footprint tile, Large casters measure from their footprint") {
    auto lib = sample_lib();
    // Large goblins anchor top-left: (13,9) covers x 13..14, y 9..10; (15,9) covers x 15..16.
    Geo g(lib, {{kFighter, 10, 10}, {"t.large", 13, 9}, {"t.large", 15, 9}});
    CHECK(g.hit(tmpl(AreaShape::Cube, 0, 15), {11, 10}) == V{1});   // one footprint tile (13,10) is inside x 11..13
    CHECK(g.hit(tmpl(AreaShape::Sphere, 0, 10), {10, 11}).empty());  // nearest tile (13,x) is 3 away
    CHECK(g.hit(tmpl(AreaShape::Sphere, 0, 15), {10, 11}) == V{1});
    CHECK(g.hit(tmpl(AreaShape::Sphere, 60, 5), {16, 11}) == V{2});   // (16,10) is adjacent to the aim
    // A Large caster: sphere about its footprint, cone from its centre.
    Geo h(lib, {{"t.large", 10, 10}, {kFighter, 12, 10}, {kFighter, 13, 10}});
    CHECK(h.c->area_targets(h.ch.spawns[0].stable_id, tmpl(AreaShape::Sphere, 0, 5), {12, 10}).size() == 1);   // adjacent to the footprint
}

TEST_CASE("area geometry: line of sight from the origin blocks creatures") {
    auto lib = sample_lib();
    {   // point origin: the wall at (17,10) hides (18,10) from the aim at (16,10)
        Geo g(lib, {{kFighter, 10, 10}, {kGoblin, 15, 10}, {kGoblin, 18, 10}}, {{17, 10}});
        CHECK(g.hit(tmpl(AreaShape::Sphere, 60, 15), {16, 10}) == V{1});
        std::string err;   // the aim itself must be visible from the caster
        Geo w(lib, {{kFighter, 10, 10}, {kGoblin, 15, 10}}, {{12, 10}});
        CHECK(w.hit(tmpl(AreaShape::Sphere, 60, 15), {15, 10}, &err).empty());
        CHECK(err == "aim not in line of sight");
    }
    {   // self origin: a wall in the cone's path shadows what is behind it
        Geo g(lib, {{kFighter, 10, 10}, {kGoblin, 12, 10}, {kGoblin, 14, 10}, {kGoblin, 12, 11}}, {{11, 10}});
        const auto h = g.hit(tmpl(AreaShape::Cone, 0, 30), {13, 10});
        CHECK(std::find(h.begin(), h.end(), std::size_t(2)) == h.end());
    }
}

TEST_CASE("area cast: aim derives the targets, is logged, replays, and is opt-in") {
    auto lib = sample_lib();
    REQUIRE_FALSE(lib.add(drake("t.drake", cone_breath()), "monster"));
    auto ch = arena({{kFighter, 8, 5}, {kFighter, 9, 6}, {"t.drake", 5, 5}, {kFighter, 5, 12}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, lib, 5);
    REQUIRE(c);
    const CombatantId d = ch.spawns[2].stable_id;
    const uint32_t sid = rules5e::spell_id_of("t.drake#breath");
    advance_to(*c, d);
    Command cmd;
    cmd.request = {d, ActionKind::CastSpell, ch.spawns[0].stable_id};
    cmd.request.spell_id = sid;
    // Without aim the caller's list is used: this cone is range 0, so a target outside its extent is refused.
    cmd.request.targets = {ch.spawns[3].stable_id};
    CHECK_FALSE(c->submit(cmd).ok);
    // aim with an explicit list is a caller error; so is an aim at nothing.
    cmd.aim = true;
    cmd.to = {9, 5};
    CHECK_FALSE(c->submit(cmd).ok);
    cmd.request.targets.clear();
    cmd.to = {5, 5};
    CHECK_FALSE(c->submit(cmd).ok);
    cmd.to = {5, 1};
    CHECK_FALSE(c->submit(cmd).ok);
    const uint64_t before = c->hash();
    const std::size_t log0 = c->log().size();
    cmd.to = {9, 5};
    const std::size_t from = c->events().size();
    auto r = c->submit(cmd);
    REQUIRE_MESSAGE(r.ok, r.error);
    std::vector<CombatantId> seen;
    for (std::size_t i = from; i < c->events().size(); ++i)
        if (c->events()[i].kind == SimEvent::Kind::SpellTarget) seen.push_back(c->events()[i].rules.target);
    CHECK(seen == std::vector<CombatantId>{ch.spawns[0].stable_id, ch.spawns[1].stable_id});
    CHECK(c->hash() != before);
    REQUIRE(c->log().size() == log0 + 1);
    const LogEntry& last = c->log().back();
    REQUIRE(last.to);
    CHECK(*last.to == Vec2i{9, 5});
    CHECK(last.step.request.targets == seen);   // replay carries the derived list
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, ch, lib, 5, c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == c->hash());
    CHECK(rep->log_hash() == c->log_hash());
    auto other = c->log();   // a different aim tile is a different log
    other.back().to = Vec2i{10, 5};
    ecs::Registry reg3;
    auto rep2 = replay(reg3, ch, lib, 5, other, &err);
    REQUIRE(rep2);
    CHECK(rep2->log_hash() != c->log_hash());
}

TEST_CASE("area AI: aims at two hostiles, never catches an ally, one hostile only when no attack is legal") {
    auto lib = sample_lib();
    REQUIRE_FALSE(lib.add(drake("t.drake", cone_breath(), "medium", true), "monster"));
    auto turn = [&](std::vector<Spawn> sp, CombatantId* self = nullptr) {
        struct R { ecs::Registry reg; world::Chunk ch; std::optional<Combat> c; };
        auto r = std::make_unique<R>();
        r->ch = arena(sp);
        r->c = Combat::create(r->reg, r->ch, lib, 3);
        REQUIRE(r->c);
        const CombatantId d = r->ch.spawns[0].stable_id;
        advance_to(*r->c, d);
        if (self) *self = d;
        return r;
    };
    {   // two fighters in the cone
        auto r = turn({{"t.drake", 5, 5}, {kFighter, 8, 5}, {kFighter, 8, 6}});
        const Command cmd = next_command(*r->c, r->ch.spawns[1].stable_id);
        REQUIRE(cmd.request.kind == ActionKind::CastSpell);
        CHECK(cmd.aim);
        CHECK(cmd.request.targets.empty());
        REQUIRE(r->c->submit(cmd).ok);
        CHECK(r->c->log().back().to.has_value());
    }
    {   // one fighter, out of melee reach: no attack is legal, so the single target is enough
        auto r = turn({{"t.drake", 5, 5}, {kFighter, 8, 5}});
        const Command cmd = next_command(*r->c, r->ch.spawns[1].stable_id);
        CHECK(cmd.request.kind == ActionKind::CastSpell);
    }
    {   // one fighter in melee reach: the attack is legal, one hostile is not worth the action
        auto r = turn({{"t.drake", 5, 5}, {kFighter, 6, 5}});
        CHECK(next_command(*r->c, r->ch.spawns[1].stable_id).request.kind == ActionKind::Attack);
    }
    {   // an ally (another drake) stands in the same cone: every aim that includes it is refused
        auto r = turn({{"t.drake", 5, 5}, {"t.drake", 7, 5}, {kFighter, 9, 5}, {kFighter, 9, 6}});
        const Command cmd = next_command(*r->c, r->ch.spawns[2].stable_id);
        CHECK(cmd.request.kind != ActionKind::CastSpell);
    }
}

TEST_CASE("cover 7.2t: log_hash 0xC0 folds the aimed area origin only when set, and its x / y") {
    auto lib = sample_lib();
    REQUIRE_FALSE(lib.add(drake("t.drake", cone_breath()), "monster"));
    auto ch = arena({{kFighter, 8, 5}, {kFighter, 9, 6}, {"t.drake", 5, 5}, {kFighter, 5, 12}});
    const CombatantId d = ch.spawns[2].stable_id;
    auto run = [&](int mode, int32_t x, int32_t y) {   // mode 0: no origin, 1: origin (x, y)
        ecs::Registry reg;
        auto c = Combat::create(reg, ch, lib, 5);
        REQUIRE(c);
        advance_to(*c, d);
        Command cmd;
        cmd.request = {d, ActionKind::CastSpell, ch.spawns[0].stable_id};
        cmd.request.spell_id = rules5e::spell_id_of("t.drake#breath");
        cmd.aim = true;
        cmd.to = {9, 5};
        if (mode == 1) { cmd.request.has_area_origin = true; cmd.request.area_x = x; cmd.request.area_y = y; }
        auto r = c->submit(cmd);
        REQUIRE_MESSAGE(r.ok, r.error);
        return std::pair<uint64_t, uint64_t>{c->log_hash(), c->hash()};
    };
    const auto none = run(0, 0, 0), none2 = run(0, 0, 0);
    const auto a = run(1, 9, 5), b = run(1, 9, 6), b2 = run(1, 9, 6);
    CHECK(none.first == none2.first);   // unset: nothing folded (the pre-0xC0 hash)
    CHECK(a.first != none.first);       // set: folded
    CHECK(a.first != b.first);          // x / y folded
    CHECK(b.first == b2.first);
    CHECK(a.second == none.second);     // no cover hook in sim yet: the state is the same, only the log differs
}
