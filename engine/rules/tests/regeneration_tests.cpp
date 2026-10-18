// Regeneration (T&C-G8): start-of-turn healing, suppression by damage type, dies-at-turn-start.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
Combatant troll(CombatantId id, int hp = 84) {
    auto c = monster(id);
    c.hp = c.hp_max = int16_t(hp);
    c.regeneration = {10, bit(DamageType::Acid) | bit(DamageType::Fire)};
    return c;
}
// pc(1) acts first (team 0), troll 2 (team 1); returns state at the troll's turn start +1 round control.
void next_turn_of(EncounterState& s, CombatantId id) {
    (void)step(s, {current_actor(s), ActionKind::EndTurn});
    end_turns_until(s, id);
}
int hp_of(EncounterState& s, CombatantId id) { return mut(s, id).hp; }
} // namespace

TEST_CASE("regeneration: heals at the start of its turn, capped at max") {
    auto s = begin_encounter({pc(1, 500), troll(2)}, 5);
    end_turns_until(s, 1);
    mut(s, 2).hp = 70;
    next_turn_of(s, 2);
    CHECK(hp_of(s, 2) == 80);
    mut(s, 2).hp = 80;
    next_turn_of(s, 1); next_turn_of(s, 2);
    CHECK(hp_of(s, 2) == 84);                       // capped
    mut(s, 2).regeneration.amount = 0;              // no regeneration: no healing
    mut(s, 2).hp = 10;
    next_turn_of(s, 1); next_turn_of(s, 2);
    CHECK(hp_of(s, 2) == 10);
}

TEST_CASE("regeneration: acid or fire damage suppresses it for one turn") {
    auto s = begin_encounter({pc(1, 500), troll(2)}, 5);
    end_turns_until(s, 1);
    auto& t = mut(s, 2);
    t.hp = 50;
    auto d = apply_damage(t, 5, DamageType::Fire, 0);
    CHECK(d.types_dealt == bit(DamageType::Fire));
    // route through the encounter: a hit of fire damage via a readied-free path is exercised in the sim; here use the flag contract
    t.regen_suppressed = true;
    next_turn_of(s, 2);
    CHECK(hp_of(s, 2) == 50);                       // suppressed this turn
    CHECK_FALSE(mut(s, 2).regen_suppressed);
    next_turn_of(s, 1); next_turn_of(s, 2);
    CHECK(hp_of(s, 2) == 60);                       // back to normal
}

TEST_CASE("regeneration: damage types dealt are reported after mitigation") {
    Combatant t = troll(2);
    t.resistances = bit(DamageType::Fire); 
    CHECK(apply_damage(t, 1, DamageType::Fire, 0).types_dealt == 0u);      // 1 / 2 = 0: nothing dealt
    CHECK(apply_damage(t, 6, DamageType::Fire, 0).types_dealt == bit(DamageType::Fire));
    t.immunities = bit(DamageType::Acid);
    CHECK(apply_damage(t, 9, DamageType::Acid, 0).types_dealt == 0u);
    const DamagePiece parts[] = {{4, DamageType::Slashing}, {3, DamageType::Acid}, {0, DamageType::Cold}};
    CHECK(apply_damage_parts(t, parts, 0).types_dealt == bit(DamageType::Slashing));   // acid immune here
    CHECK(apply_damage(t, 0, DamageType::Cold, 0).types_dealt == 0u);
}

TEST_CASE("regeneration: a troll at 0 HP lives until its turn starts, then regenerates or dies") {
    auto drop = [](bool suppressing) {
        auto s = begin_encounter({pc(1, 500), troll(2)}, 5);
        end_turns_until(s, 1);
        ActionRequest r{1, ActionKind::Attack, 2, 0};
        mut(s, 2).hp = 1;
        mut(s, 1).attacks[0].to_hit = 100;
        mut(s, 1).attacks[0].type = suppressing ? DamageType::Fire : DamageType::Slashing;
        auto res = step(s, r);
        REQUIRE(res.ok);
        return std::make_pair(std::move(s), count(res.events, EventKind::CombatantDowned));
    };
    {
        auto [s, downed] = drop(false);
        CHECK(downed == 1);
        CHECK(mut(s, 2).hp == 0); CHECK_FALSE(mut(s, 2).dead); CHECK(mut(s, 2).regen_down);
        CHECK_FALSE(s.ended);                                        // still in the fight
        next_turn_of(s, 2);
        CHECK_FALSE(mut(s, 2).dead); CHECK_FALSE(mut(s, 2).regen_down); CHECK(hp_of(s, 2) == 10);
    }
    {
        auto [s, downed] = drop(true);
        CHECK(mut(s, 2).regen_suppressed); CHECK_FALSE(mut(s, 2).dead);
        next_turn_of(s, 2);
        CHECK(mut(s, 2).dead);
        CHECK(s.ended);
    }
}

TEST_CASE("regeneration: massive damage still kills; creatures without regeneration die at once") {
    auto s = begin_encounter({pc(1, 500), troll(2, 10)}, 5);
    end_turns_until(s, 1);
    mut(s, 2).hp = 1; mut(s, 1).attacks[0].to_hit = 100; mut(s, 1).attacks[0].damage = {1, 4, 40};   // leftover >= hp max
    REQUIRE(step(s, ActionRequest{1, ActionKind::Attack, 2, 0}).ok);
    CHECK(mut(s, 2).dead);
    auto u = begin_encounter({pc(1, 500), monster(2)}, 5);
    end_turns_until(u, 1);
    mut(u, 2).hp = 1; mut(u, 1).attacks[0].to_hit = 100;
    REQUIRE(step(u, ActionRequest{1, ActionKind::Attack, 2, 0}).ok);
    CHECK(mut(u, 2).dead);
}

TEST_CASE("regeneration: a fire hit through the encounter suppresses; a slashing hit does not") {
    for (bool fire : {true, false}) {
        auto s = begin_encounter({pc(1, 500), troll(2)}, 5);
        end_turns_until(s, 1);
        mut(s, 1).attacks[0].to_hit = 100;
        mut(s, 1).attacks[0].type = fire ? DamageType::Fire : DamageType::Slashing;
        mut(s, 2).hp = 40;
        REQUIRE(step(s, ActionRequest{1, ActionKind::Attack, 2, 0}).ok);
        CHECK(mut(s, 2).regen_suppressed == fire);
        const int after = hp_of(s, 2);
        next_turn_of(s, 2);
        CHECK(hp_of(s, 2) == (fire ? after : after + 10));
    }
}
