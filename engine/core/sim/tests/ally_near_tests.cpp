// ActionRequest::ally_near_target (Pack Tactics / Sneak Attack fact) supplied by sim.
#include <doctest/doctest.h>

#include <string>

#include "sample_set.hpp"
#include "sim/driver.hpp"
#include "world/chunk_codec.hpp"

using namespace eng;
using namespace eng::sim;
using rules5e::ActionKind;

namespace {
const char* kFighter = "srd51.character.fighter_1";
const char* kGoblin = "srd51.monster.goblin";

world::Chunk arena(std::vector<std::tuple<std::string, int, int>> spawns) {
    world::Chunk c{};
    c.coord = {5, 5, 0};
    c.origin = world::Chunk::Origin::Handcrafted;
    c.palette = {"", "floor"};
    for (auto& cell : c.layers[0]) cell.tile = 1;
    for (std::size_t i = 0; i < spawns.size(); ++i) {
        auto& [a, x, y] = spawns[i];
        c.spawns.push_back({a, Vec2i{x, y}, world::spawn_stable_id(c.coord, uint32_t(i)), ""});
    }
    c.content_hash = world::compute_content_hash(c);
    return c;
}
ContentLibrary lib() {
    ContentLibrary l;
    REQUIRE(l.load(sample_set()).empty());
    return l;
}
} // namespace

TEST_CASE("ally_near_target: same-team creature within 5 ft of the target, not the attacker, not an enemy") {
    auto l = lib();
    auto ch = arena({{kGoblin, 6, 5}, {kFighter, 5, 5}, {kGoblin, 4, 5}, {kGoblin, 9, 9}, {kGoblin, 5, 6}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    auto id = [&](std::size_t i) { return ch.spawns[i].stable_id; };
    const CombatantId g1 = id(0), f = id(1), g2 = id(2), g3 = id(3);
    CHECK(c->ally_near_target(g1, f));     // g2 and the one at (5,6) flank the fighter
    CHECK(c->ally_near_target(g2, f));     // g1 does
    CHECK(c->ally_near_target(g3, f));     // far attacker, allies are next to the target
    CHECK_FALSE(c->ally_near_target(f, g3));   // g3 is alone; the fighter has no friend there
    CHECK_FALSE(c->ally_near_target(f, g1));   // goblins near g1 are its allies, not the fighter's
}

TEST_CASE("ally_near_target: a dead or incapacitated ally does not count") {
    auto l = lib();
    auto ch = arena({{kGoblin, 6, 5}, {kFighter, 5, 5}, {kGoblin, 4, 5}});
    ecs::Registry reg;
    auto c = Combat::create(reg, ch, l, 3);
    REQUIRE(c);
    const CombatantId g1 = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id, g2 = ch.spawns[2].stable_id;
    REQUIRE(c->ally_near_target(g1, f));
    auto* ally = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), g2));   // test-only state poke
    rules5e::add_condition(*ally, rules5e::Condition::Paralyzed);
    CHECK_FALSE(c->ally_near_target(g1, f));
    rules5e::remove_condition(*ally, rules5e::Condition::Paralyzed);
    CHECK(c->ally_near_target(g1, f));
    ally->dead = true;
    CHECK_FALSE(c->ally_near_target(g1, f));
}

TEST_CASE("ally_near_target: Pack Tactics advantage reaches the attack roll through sim") {
    auto l = lib();
    auto hits = [&](int ally_x) {
        int n = 0;
        for (uint64_t seed = 1; seed <= 300; ++seed) {
            auto ch = arena({{kGoblin, 6, 5}, {kFighter, 5, 5}, {kGoblin, ally_x, 5}});
            ecs::Registry reg;
            auto c = Combat::create(reg, ch, l, seed);
            REQUIRE(c);
            const CombatantId g1 = ch.spawns[0].stable_id, f = ch.spawns[1].stable_id;
            auto* g = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), g1));
            g->attacks[0].to_hit = 0;
            rules5e::RollTrait t{};
            t.roll = rules5e::TraitRoll::Attack;
            t.mode = rules5e::RollMode::Advantage;
            t.when = rules5e::kWhenAllyNearTarget;
            g->roll_traits.push_back(t);
            auto* fi = const_cast<rules5e::Combatant*>(rules5e::find(c->state(), f));
            fi->ac = 21;
            fi->hp = fi->hp_max = 5000;
            for (int i = 0; i < 4 && rules5e::current_actor(c->state()) != g1; ++i) {
                Command e;
                e.request = {rules5e::current_actor(c->state()), ActionKind::EndTurn};
                REQUIRE(c->submit(e).ok);
            }
            Command a;
            a.request = {g1, ActionKind::Attack, f, 0};
            REQUIRE(c->submit(a).ok);
            n += rules5e::find(c->state(), f)->hp < 5000;
        }
        return n;
    };
    const int flanked = hits(4), alone = hits(20);   // ally adjacent to the fighter vs 15 tiles away
    CHECK(flanked > alone + 8);   // only a natural 20 hits: ~9.75% with advantage vs 5%
}
