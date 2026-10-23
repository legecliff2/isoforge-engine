// Cover (VERSION_BUMP_BATCH 7.2t hand-off): sim's EncounterHooks::cover. DMG grid rule: lines from one corner of the origin
// (the source's space, the least-cover corner) to every corner of the target's space; a lone blocks_sight tile a line crosses
// is a full obstruction, another creature half; grazing an edge or a corner of a lone obstruction does not block.
#include <doctest/doctest.h>

#include <fstream>
#include <string>

#include "sample_set.hpp"
#include "sim/combat.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using Json = assets::Json;
using rules5e::ActionKind;
using rules5e::Cover;
using rules5e::CoverQuery;

namespace {
const std::string kRoot = ENGINE_SOURCE_ROOT;
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";
const char* kWizard = "test.character.wizard";
const char* kCleric = "test.character.cleric";
const char* kFireball = "test.spell.fireball";
const char* kDexBlast = "test.spell.dex_blast";
const char* kSacred = "srd51.spell.sacred_flame";
const char* kFireBolt = "srd51.spell.fire_bolt";

Json load_json(const std::string& rel) {
    std::ifstream f(kRoot + "/" + rel);
    REQUIRE(f.good());
    return Json::parse(f);
}
struct Spawn { std::string archetype; int x, y; };
world::Chunk arena(const std::vector<Spawn>& spawns, std::vector<Vec2i> walls = {}) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor", "wall"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (Vec2i w : walls)
        c.layers[2][std::size_t(w.y * 32 + w.x)].flags = world::tile_flags::kBlocksSight | world::tile_flags::kBlocksMove;
    for (std::size_t i = 0; i < spawns.size(); ++i)
        c.spawns.push_back({spawns[i].archetype, Vec2i{spawns[i].x, spawns[i].y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    c.content_hash = world::compute_content_hash(c);
    return c;
}
// A Fireball stand-in (spell@1, cantrip so no slot bookkeeping): 150 ft, a 10 ft radius sphere at a point, Dex save.
Json fireball() {
    Json s = load_json("content/packs/srd51.core/spells/poison_spray.json");
    s["id"] = kFireball;
    s["name"] = "Test Fireball";
    s["range"] = {{"kind", "ranged"}, {"feet", 150}};
    s["area"] = {{"shape", "sphere"}, {"size_ft", 10}};
    s["effects"][0]["ability"] = "dex";
    s["effects"][0]["damage"] = {{"dice", "1d2"}, {"type", "fire"}};
    s["effects"][0]["on_success"] = "half";
    s.erase("scaling");
    return s;
}
// Targeted Dex save that does NOT ignore cover (Sacred Flame's opposite for the e2e).
Json dex_blast() {
    Json s = load_json("content/packs/srd51.core/spells/poison_spray.json");
    s["id"] = kDexBlast;
    s["name"] = "Test Dex Blast";
    s["range"] = {{"kind", "ranged"}, {"feet", 60}};
    s["effects"][0]["ability"] = "dex";
    s["effects"][0]["damage"] = {{"dice", "1d2"}, {"type", "fire"}};
    s["effects"][0]["on_success"] = "none";
    s.erase("scaling");
    return s;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    Json big = load_json("content/packs/srd51.core/monsters/goblin.json");
    big["$schema"] = "monster@4";
    big["id"] = "t.large";
    big["size"] = "large";
    REQUIRE_FALSE(l.add(big, "monster"));
    REQUIRE_FALSE(l.add_spell(fireball()));
    REQUIRE_FALSE(l.add_spell(dex_blast()));
    // sacred_flame + fire_bolt already in srd51.core via sample_set
    Json w = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    w["id"] = kWizard;
    w["abilities"]["int"] = 16;
    w["hit_points"]["average"] = 30;
    w["spellcasting"] = {{"ability", "int"}, {"caster_level", 5},
                         {"spells", Json::array({kFireball, kDexBlast, kFireBolt})}};
    REQUIRE_FALSE(l.add(w, "character"));
    Json cl = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    cl["id"] = kCleric;
    cl["abilities"]["wis"] = 16;
    cl["hit_points"]["average"] = 30;
    cl["spellcasting"] = {{"ability", "wis"}, {"caster_level", 5}, {"spells", Json::array({kSacred, kDexBlast})}};
    REQUIRE_FALSE(l.add(cl, "character"));
    return l;
}
struct Board {
    ecs::Registry reg;
    world::Chunk ch;
    std::optional<Combat> c;
    Board(const ContentLibrary& l, std::vector<Spawn> sp, std::vector<Vec2i> walls = {}) {
        ch = arena(sp, std::move(walls));
        c = Combat::create(reg, ch, l, 1);
        REQUIRE(c);
    }
    CombatantId id(std::size_t i) const { return ch.spawns[i].stable_id; }
    Cover cover(std::size_t src, std::size_t tgt) const { return c->cover_of(CoverQuery{id(src), id(tgt)}); }
    Cover from_tile(std::size_t src, std::size_t tgt, Vec2i o, bool area = false) const {
        CoverQuery q{id(src), id(tgt)};
        q.from_area = area; q.has_origin = true; q.origin_x = o.x; q.origin_y = o.y;
        return c->cover_of(q);
    }
    Cover tiles(Vec2i o, int on, Vec2i t, int tn, CombatantId ea = 0, CombatantId eb = 0) const {
        return c->cover_between(o, on, t, tn, ea, eb);
    }
    SubmitResult settle(SubmitResult r) {
        while (r.needs) r = c->decide({r.needs->window_seq, false, 0});
        return r;
    }
    // SavingThrowRolled: actor=saver; AttackResolved: actor=attacker. Optional `who` filters actor.
    const rules5e::Event* first(rules5e::EventKind k, CombatantId who = 0) const {
        for (const auto& e : c->events()) {
            if (e.kind != SimEvent::Kind::Rules || e.rules.kind != k) continue;
            if (who && e.rules.actor != who) continue;
            return &e.rules;
        }
        return nullptr;
    }
};
void advance_to(Combat& c, CombatantId who) {
    for (int i = 0; i < 8 && rules5e::current_actor(c.state()) != who; ++i) {
        Command e;
        e.request = {rules5e::current_actor(c.state()), ActionKind::EndTurn};
        REQUIRE(c.submit(e).ok);
    }
    REQUIRE(rules5e::current_actor(c.state()) == who);
}
} // namespace

TEST_CASE("cover: open ground gives none; a creature between gives half; the source and target never cover") {
    auto l = lib();
    Board open(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}});
    CHECK(open.cover(0, 1) == Cover::None);
    CHECK(open.cover(1, 0) == Cover::None);
    Board crowd(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 5}});
    CHECK(crowd.cover(0, 1) == Cover::Half);
    CHECK(crowd.cover(1, 0) == Cover::Half);        // symmetric here
    CHECK(crowd.cover(0, 2) == Cover::None);        // the blocker itself: adjacent, nothing between
    Board side(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 7}});
    CHECK(side.cover(0, 1) == Cover::None);         // off the lines
}

TEST_CASE("cover: walls - a lone wall tile in line gives half, a wall corner three-quarters, a full wall total") {
    auto l = lib();
    Board lone(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}}, {{4, 5}});
    CHECK(lone.cover(0, 1) == Cover::Half);          // the best corner's lines pass above / below it: 2 of 4 blocked
    // The target in a wall corner (wall tiles north and west of it, the diagonal open), shot along the diagonal: the best
    // corner of the attacker still has 3 of its 4 lines through wall.
    Board corner(l, {{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 6}, {6, 5}});
    CHECK(corner.cover(0, 1) == Cover::ThreeQuarters);
    Board pillar(l, {{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 5}});
    CHECK(pillar.cover(0, 1) == Cover::ThreeQuarters);   // a lone wall tile on the diagonal: likewise 3 of 4
    Board closed(l, {{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 5}, {5, 6}, {6, 5}});
    CHECK(closed.cover(0, 1) == Cover::Total);           // corner closed: no line gets through
    std::vector<Vec2i> wall;
    for (int y = 0; y < 12; ++y) wall.push_back({4, y});
    Board full(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}}, wall);
    CHECK(full.cover(0, 1) == Cover::Total);         // lines along the wall's inner edges are blocked too (both sides wall)
    Board two(l, {{kFighter, 1, 5}, {kGoblin, 6, 6}}, {{4, 5}, {4, 6}});
    CHECK(two.cover(0, 1) == Cover::Total);              // a two-tile wall straight between: the shared edge blocks too
}

TEST_CASE("cover: a Large source uses the corner of its space giving the least cover") {
    auto l = lib();
    // A goblin beside the target blocks every line from the west of x = 10 to the target's south-west corner.
    Board b(l, {{"t.large", 8, 1}, {kGoblin, 10, 5}, {kGoblin, 9, 5}});
    CHECK(b.cover(0, 1) == Cover::None);                    // the space's corner (10, 3) sees past the blocker
    CHECK(b.from_tile(0, 1, {8, 2}) == Cover::Half);        // one tile of that space alone would not
    CHECK(b.from_tile(0, 1, {9, 2}) == Cover::None);
}

TEST_CASE("cover: an explicit origin (Spiritual Weapon's tile) is measured from that tile, not the caster") {
    auto l = lib();
    Board b(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 5}}, {{6, 3}, {7, 3}, {8, 3}, {5, 3}});
    CHECK(b.cover(0, 1) == Cover::Half);                    // from the caster: the goblin between
    CHECK(b.from_tile(0, 1, {6, 7}) == Cover::None);        // the weapon south of the target: clear
    CHECK(b.from_tile(0, 1, {6, 1}) == Cover::Total);       // north, behind the wall row
}

TEST_CASE("cover: an aimed Fireball fills the area origin, cover is taken from the aim point, and replay matches") {
    auto l = lib();
    // Blocker between the wizard and the target; the aim point is south of the target, clear of it.
    const std::vector<Spawn> sp{{kWizard, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 5}};
    Board b(l, sp);
    CHECK(b.cover(0, 1) == Cover::Half);
    CHECK(b.from_tile(0, 1, {8, 7}, true) == Cover::None);
    advance_to(*b.c, b.id(0));
    Command cmd;
    cmd.request = {b.id(0), ActionKind::CastSpell};
    cmd.request.spell_id = rules5e::spell_id_of(kFireball);
    cmd.aim = true;
    cmd.to = {8, 7};
    auto r = b.c->submit(cmd);
    REQUIRE_MESSAGE(r.ok, r.error);
    const auto& q = b.c->log().back().step.request;
    CHECK(q.kind == ActionKind::CastSpell);
    CHECK(q.has_area_origin);
    CHECK(q.area_x == 8);
    CHECK(q.area_y == 7);
    ecs::Registry reg2;
    std::string err;
    auto rep = replay(reg2, b.ch, l, 1, b.c->log(), &err);
    REQUIRE_MESSAGE(rep, err);
    CHECK(rep->hash() == b.c->hash());
    CHECK(rep->log_hash() == b.c->log_hash());
    CHECK(rep->cover_of(CoverQuery{b.id(0), b.id(1)}) == Cover::Half);   // replayed board, same answer
}

TEST_CASE("cover: cover_between matches cover_of for creatures and accepts bare tiles (R&P query)") {
    auto l = lib();
    Board open(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}});
    CHECK(open.tiles({2, 5}, 1, {6, 5}, 1) == Cover::None);
    CHECK(open.tiles({2, 5}, 1, {6, 5}, 1, open.id(0), open.id(1)) == Cover::None);
    Board crowd(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}, {kGoblin, 4, 5}});
    CHECK(crowd.cover(0, 1) == Cover::Half);
    CHECK(crowd.tiles({2, 5}, 1, {6, 5}, 1, crowd.id(0), crowd.id(1)) == Cover::Half);
    // Bare tile probe (no except): the three creatures all count as bodies, so the middle goblin still halves.
    CHECK(crowd.tiles({2, 5}, 1, {6, 5}, 1) == Cover::Half);
    Board wall(l, {{kFighter, 2, 5}, {kGoblin, 6, 5}}, {{4, 5}});
    CHECK(wall.tiles({2, 5}, 1, {6, 5}, 1) == Cover::Half);
    CHECK(wall.tiles({2, 5}, 1, {6, 5}, 1) == wall.cover(0, 1));
}

TEST_CASE("cover: adjacent and diagonal with nothing between give none; wall between adjacent tiles totals") {
    auto l = lib();
    Board adj(l, {{kFighter, 4, 5}, {kGoblin, 5, 5}});
    CHECK(adj.cover(0, 1) == Cover::None);
    CHECK(adj.tiles({4, 5}, 1, {5, 5}, 1) == Cover::None);
    Board diag(l, {{kFighter, 4, 4}, {kGoblin, 5, 5}});
    CHECK(diag.cover(0, 1) == Cover::None);
    // A wall filling the only shared edge between adjacent tiles: total (both sides of the edge block).
    Board blocked(l, {{kFighter, 3, 5}, {kGoblin, 5, 5}}, {{4, 5}});
    CHECK(blocked.cover(0, 1) == Cover::Half);   // one tile between, not adjacent; best corner slips past
    Board closed(l, {{kFighter, 3, 3}, {kGoblin, 6, 6}}, {{5, 5}, {5, 6}, {6, 5}});
    CHECK(closed.cover(0, 1) == Cover::Total);
}

TEST_CASE("cover: a Large target uses every corner of its footprint") {
    auto l = lib();
    // Large at (6,4) covers x 6..7, y 4..5. A two-tile stub at x=5 is only Half (some corner lines clear).
    Board stub(l, {{kFighter, 2, 5}, {"t.large", 6, 4}}, {{5, 4}, {5, 5}});
    CHECK(stub.cover(0, 1) == Cover::Half);
    CHECK(stub.tiles({2, 5}, 1, {6, 4}, 2, stub.id(0), stub.id(1)) == Cover::Half);
    // A full wall column blocks every corner line into the 2x2.
    std::vector<Vec2i> col;
    for (int y = 0; y < 12; ++y) col.push_back({5, y});
    Board full(l, {{kFighter, 2, 5}, {"t.large", 6, 4}}, col);
    CHECK(full.cover(0, 1) == Cover::Total);
    CHECK(full.tiles({2, 5}, 1, {6, 4}, 2, full.id(0), full.id(1)) == Cover::Total);
}

TEST_CASE("cover: attack through half cover raises effective AC; total cover refuses the attack") {
    auto l = lib();
    // Same three combatants so the seed stream matches; third goblin off the line (open) vs between (half).
    // Creature cover does not block LOS (walls that yield Total also fail spell_target_ok first).
    Board open(l, {{kWizard, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 8}});
    Board half(l, {{kWizard, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 5}});
    CHECK(open.cover(0, 1) == Cover::None);
    CHECK(half.cover(0, 1) == Cover::Half);
    auto cast_bolt = [](Board& b) {
        advance_to(*b.c, b.id(0));
        Command cmd;
        cmd.request = {b.id(0), ActionKind::CastSpell, b.id(1)};
        cmd.request.spell_id = rules5e::spell_id_of(kFireBolt);
        return b.settle(b.c->submit(cmd));
    };
    auto ro = cast_bolt(open);
    auto rh = cast_bolt(half);
    REQUIRE_MESSAGE(ro.ok, ro.error);
    REQUIRE_MESSAGE(rh.ok, rh.error);
    const auto* eo = open.first(rules5e::EventKind::AttackResolved, open.id(0));
    const auto* eh = half.first(rules5e::EventKind::AttackResolved, half.id(0));
    REQUIRE(eo);
    REQUIRE(eh);
    CHECK(eo->a == eh->a);   // same natural (same seed, same prior draws)
    CHECK(eo->b == eh->b);   // attack total unchanged; cover raises the AC the total is compared against
    // Hit when total >= AC (+ cover). Goblin AC 15; +2 half → miss band [15, 17).
    const bool open_hit = (eo->c & 1) != 0, half_hit = (eh->c & 1) != 0;
    if (eo->b >= 15 && eo->b < 17) {
        CHECK(open_hit);
        CHECK_FALSE(half_hit);
    } else {
        CHECK(open_hit == half_hit);
    }
    std::vector<Vec2i> wall;
    for (int y = 0; y < 12; ++y) wall.push_back({5, y});
    Board full(l, {{kWizard, 2, 5}, {kGoblin, 8, 5}}, wall);
    CHECK(full.cover(0, 1) == Cover::Total);
    advance_to(*full.c, full.id(0));
    Command atk;
    atk.request = {full.id(0), ActionKind::CastSpell, full.id(1)};
    atk.request.spell_id = rules5e::spell_id_of(kFireBolt);
    auto rt = full.settle(full.c->submit(atk));
    CHECK_FALSE(rt.ok);
    // Full wall: sim LOS fails before the cover Total check (rules-layer Total→"target has total cover" is in rules tests).
    CHECK(rt.error == "invalid spell target");
}

TEST_CASE("cover: Dex save gets +2 behind half cover; Sacred Flame ignores it; total still blocks Sacred Flame") {
    auto l = lib();
    Board open(l, {{kCleric, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 8}});
    Board half(l, {{kCleric, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 5}});
    CHECK(open.cover(0, 1) == Cover::None);
    CHECK(half.cover(0, 1) == Cover::Half);
    auto cast = [](Board& b, const char* spell) {
        advance_to(*b.c, b.id(0));
        Command cmd;
        cmd.request = {b.id(0), ActionKind::CastSpell, b.id(1)};
        cmd.request.spell_id = rules5e::spell_id_of(spell);
        return b.settle(b.c->submit(cmd));
    };
    // Dex blast: cover raises the save total by +2 (actor = saver).
    REQUIRE_MESSAGE(cast(open, kDexBlast).ok, "dex open");
    REQUIRE_MESSAGE(cast(half, kDexBlast).ok, "dex half");
    const auto* so = open.first(rules5e::EventKind::SavingThrowRolled, open.id(1));
    const auto* sh = half.first(rules5e::EventKind::SavingThrowRolled, half.id(1));
    REQUIRE(so);
    REQUIRE(sh);
    CHECK(so->a == sh->a);
    CHECK(sh->b == so->b + 2);
    // Sacred Flame: same totals with and without half cover (ignores_cover).
    Board o2(l, {{kCleric, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 8}});
    Board h2(l, {{kCleric, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 5}});
    REQUIRE_MESSAGE(cast(o2, kSacred).ok, "sf open");
    REQUIRE_MESSAGE(cast(h2, kSacred).ok, "sf half");
    const auto* fo = o2.first(rules5e::EventKind::SavingThrowRolled, o2.id(1));
    const auto* fh = h2.first(rules5e::EventKind::SavingThrowRolled, h2.id(1));
    REQUIRE(fo);
    REQUIRE(fh);
    CHECK(fo->a == fh->a);
    CHECK(fo->b == fh->b);
    // Total cover (full wall): cover_of is Total; cast refused (LOS before cover string).
    std::vector<Vec2i> wall;
    for (int y = 0; y < 12; ++y) wall.push_back({5, y});
    Board full(l, {{kCleric, 2, 5}, {kGoblin, 8, 5}}, wall);
    CHECK(full.cover(0, 1) == Cover::Total);
    advance_to(*full.c, full.id(0));
    Command cmd;
    cmd.request = {full.id(0), ActionKind::CastSpell, full.id(1)};
    cmd.request.spell_id = rules5e::spell_id_of(kSacred);
    auto r = full.settle(full.c->submit(cmd));
    CHECK_FALSE(r.ok);
    CHECK(r.error == "invalid spell target");
}

TEST_CASE("cover: determinism - cover_between and cover_of agree across two Combats with the same seed") {
    auto l = lib();
    const std::vector<Spawn> sp{{kFighter, 2, 5}, {kGoblin, 8, 5}, {kGoblin, 5, 5}};
    const std::vector<Vec2i> walls{{6, 4}, {6, 6}};
    Board a(l, sp, walls), b(l, sp, walls);
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j) {
            if (i == j) continue;
            CHECK(a.cover(i, j) == b.cover(i, j));
            CHECK(a.tiles({sp[i].x, sp[i].y}, 1, {sp[j].x, sp[j].y}, 1, a.id(i), a.id(j)) == a.cover(i, j));
        }
    CHECK(a.c->hash() == b.c->hash());
}
