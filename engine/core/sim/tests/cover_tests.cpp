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
const char* kFireball = "test.spell.fireball";

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
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    Json big = load_json("content/packs/srd51.core/monsters/goblin.json");
    big["$schema"] = "monster@4";
    big["id"] = "t.large";
    big["size"] = "large";
    REQUIRE_FALSE(l.add(big, "monster"));
    REQUIRE_FALSE(l.add_spell(fireball()));
    Json w = load_json("content/packs/sample.vertical_slice/characters/fighter_1.json");
    w["id"] = kWizard;
    w["abilities"]["int"] = 16;
    w["hit_points"]["average"] = 30;
    w["spellcasting"] = {{"ability", "int"}, {"caster_level", 5}, {"spells", Json::array({kFireball})}};
    REQUIRE_FALSE(l.add(w, "character"));
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
