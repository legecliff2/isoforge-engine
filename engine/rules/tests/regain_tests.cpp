// Shield Guardian Regeneration (CONTENT_CONVENTIONS §55). Start-of-turn healing while hp >= 1.
// Not troll regen: no regen_down, and 0 hit points is an ordinary death.
#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

namespace {
Combatant guardian(CombatantId id, int hp, int hp_max, int16_t amount = 10) {
    auto c = monster(id);
    c.team = 1;
    c.hp = int16_t(hp);
    c.hp_max = int16_t(hp_max);
    c.regain = amount;
    return c;
}
void next_turn_of(EncounterState& s, CombatantId id) {
    (void)step(s, {current_actor(s), ActionKind::EndTurn});
    end_turns_until(s, id);
}
int healed_on(const std::vector<Event>& ev, CombatantId id) {
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == EventKind::Healed && e.target == id) n += e.a;
    return n;
}
}  // namespace

TEST_CASE("regain: heals at the start of its turn, including from 1, and caps at maximum") {
    auto ally = monster(3);
    ally.team = 1;
    auto s = begin_encounter({pc(1, 500), guardian(2, 80, 80), ally}, 5);
    end_turns_until(s, 1);
    mut(s, 2).hp = 20;
    next_turn_of(s, 2);
    CHECK(mut(s, 2).hp == 30);
    mut(s, 2).hp = 1;
    next_turn_of(s, 1);
    next_turn_of(s, 2);
    CHECK(mut(s, 2).hp == 11);
    mut(s, 2).hp = 75;
    next_turn_of(s, 1);
    next_turn_of(s, 2);
    CHECK(mut(s, 2).hp == 80);
    mut(s, 2).hp = 80;
    next_turn_of(s, 1);
    next_turn_of(s, 2);
    CHECK(mut(s, 2).hp == 80);
    CHECK_FALSE(mut(s, 2).regen_down);
    CHECK_FALSE(mut(s, 2).dead);
}

TEST_CASE("regain: another creature's turn does not heal it") {
    auto ally = monster(3);
    ally.team = 1;
    // Dex is 14 on all three, so the d20 decides the order. Pin ally, then the PC, then the guardian.
    EncounterState s;
    bool pinned = false;
    for (uint64_t seed = 1; seed < 200 && !pinned; ++seed) {
        s = begin_encounter({pc(1, 500), guardian(2, 80, 80), ally}, seed);
        pinned = s.order.size() == 3 && s.order[0].id == 3 && s.order[1].id == 1 && s.order[2].id == 2;
    }
    REQUIRE(pinned);
    REQUIRE(current_actor(s) == 3);
    mut(s, 2).hp = 30;
    REQUIRE(step(s, {3, ActionKind::EndTurn}).ok);
    REQUIRE(current_actor(s) == 1);
    REQUIRE(current_actor(s) != 2);   // the PC's turn, not the guardian's
    CHECK(mut(s, 2).hp == 30);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    REQUIRE(current_actor(s) == 2);
    CHECK(mut(s, 2).hp == 40);
}

TEST_CASE("regain: 0 hit points is an ordinary death, and a troll still waits") {
    auto ally = monster(3);
    ally.team = 1;
    auto s = begin_encounter({pc(1, 500), guardian(2, 5, 80), ally}, 5);
    end_turns_until(s, 1);
    mut(s, 2).hp = 5;   // a turn that already started may have healed the opening 5
    mut(s, 1).attacks[0].to_hit = 100;
    mut(s, 1).attacks[0].damage = {0, 0, 5};
    auto hit = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    REQUIRE(hit.ok);
    CHECK(mut(s, 2).hp == 0);
    CHECK(mut(s, 2).dead);
    CHECK_FALSE(mut(s, 2).regen_down);
    CHECK(count(hit.events, EventKind::CombatantDied) == 1);
    CHECK(count(hit.events, EventKind::CombatantDowned) == 0);
    next_turn_of(s, 2);
    CHECK(mut(s, 2).hp == 0);
    CHECK(mut(s, 2).dead);
    CHECK(healed_on(hit.events, 2) == 0);

    mut(s, 2).dead = true;
    mut(s, 2).hp = 0;
    mut(s, 2).regain = 10;
    next_turn_of(s, 1);
    next_turn_of(s, 2);
    CHECK(mut(s, 2).hp == 0);
    CHECK(mut(s, 2).dead);

    auto t = monster(4);
    t.team = 1;
    t.hp = t.hp_max = 40;
    t.regeneration = {10, bit(DamageType::Acid) | bit(DamageType::Fire)};
    auto live = begin_encounter({pc(1, 500), t, ally}, 5);
    end_turns_until(live, 1);
    mut(live, 4).hp = 1;
    mut(live, 1).attacks[0].to_hit = 100;
    mut(live, 1).attacks[0].damage = {0, 0, 5};
    auto down = step(live, ActionRequest{1, ActionKind::Attack, 4, 0});
    REQUIRE(down.ok);
    CHECK(mut(live, 4).hp == 0);
    CHECK_FALSE(mut(live, 4).dead);
    CHECK(mut(live, 4).regen_down);
    CHECK(count(down.events, EventKind::CombatantDowned) == 1);
    next_turn_of(live, 4);
    CHECK_FALSE(mut(live, 4).dead);
    CHECK_FALSE(mut(live, 4).regen_down);
    CHECK(mut(live, 4).hp == 10);
}
