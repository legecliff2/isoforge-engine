// Freeze (CONTENT_CONVENTIONS §42): cold in types_dealt applies one Speed modifier, speed_sub 20, until the end of
// the creature's next turn. A second hit refreshes that row. It does not stack. No new hash tag (the row is 0xA2).
#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

namespace {
const uint32_t kFreeze = spell_id_of("test.water#freeze#freeze");

Combatant water(CombatantId id) {
    auto c = monster(id);
    c.hp = c.hp_max = 114;
    c.speed_ft = 30;
    c.swim_ft = 90;
    c.freeze = {bit(DamageType::Cold), 20, kFreeze};
    return c;
}
void arm(EncounterState& s, DamageType type, int16_t bonus) {
    auto& at = mut(s, 1).attacks[0];
    at.to_hit = 100;
    at.type = type;
    at.damage = {0, 0, bonus};
    at.extra_damage.clear();
}
const ActiveModifier* row(const EncounterState& s) {
    for (const auto& m : s.modifiers)
        if (m.target == 2 && m.spec.id == kFreeze) return &m;
    return nullptr;
}
int speed_rows(const EncounterState& s) {
    int n = 0;
    for (const auto& m : s.modifiers)
        if (m.target == 2 && m.spec.stat == ModStat::Speed) ++n;
    return n;
}
StepResult strike(EncounterState& s) {
    end_turns_until(s, 1);
    auto r = step(s, ActionRequest{1, ActionKind::Attack, 2, 0});
    REQUIRE_MESSAGE(r.ok, r.error);
    return r;
}
}  // namespace

TEST_CASE("freeze: cold damage reduces every speed by 20 until the end of its next turn") {
    auto s = begin_encounter({pc(1, 500), water(2)}, 5);
    arm(s, DamageType::Cold, 6);
    auto hit = strike(s);
    const ActiveModifier* m = row(s);
    REQUIRE(m);
    CHECK(m->spec.speed_sub == 20);
    CHECK(m->spec.speed_num == 1);
    CHECK(m->spec.speed_den == 1);
    CHECK(m->spec.stat == ModStat::Speed);
    CHECK(m->timed.kind == Expiry::EndOfTargetNextTurn);
    CHECK(m->timed.rounds_left == 1);   // the elemental is not the actor
    CHECK(speed_rows(s) == 1);
    CHECK(modified_speed(s, mut(s, 2)) == 10);
    CHECK(mode_speed(s, mut(s, 2), 2) == 70);   // swim 90 - 20
    bool applied = false;
    for (const auto& e : hit.events)
        if (e.kind == EventKind::ModifierApplied && e.target == 2 && e.a == int32_t(kFreeze) &&
            e.b == int32_t(ModStat::Speed) && e.c == 20)
            applied = true;
    CHECK(applied);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 10);
    REQUIRE(step(s, {2, ActionKind::Dash}).ok);
    CHECK(s.budget.movement_ft == 20);   // Dash adds the modified speed
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    CHECK(row(s) == nullptr);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 30);
    CHECK(modified_speed(s, mut(s, 2)) == 30);
}

TEST_CASE("freeze: a second cold hit refreshes the same row and does not stack") {
    auto s = begin_encounter({pc(1, 500), water(2)}, 5);
    mut(s, 1).attacks_per_action = 2;
    arm(s, DamageType::Cold, 6);
    strike(s);
    strike(s);
    CHECK(speed_rows(s) == 1);
    CHECK(modified_speed(s, mut(s, 2)) == 10);   // 30 - 20, not 30 - 40
    CHECK(row(s)->timed.rounds_left == 1);
}

TEST_CASE("freeze: slashing, immunity, and a zeroed cold component do not slow; a killing blow applies nothing") {
    {
        auto s = begin_encounter({pc(1, 500), water(2)}, 5);
        arm(s, DamageType::Slashing, 6);
        strike(s);
        CHECK(row(s) == nullptr);
        CHECK(modified_speed(s, mut(s, 2)) == 30);
        CHECK(mut(s, 2).hp == 108);
    }
    {
        auto s = begin_encounter({pc(1, 500), water(2)}, 5);
        mut(s, 2).immunities |= bit(DamageType::Cold);
        arm(s, DamageType::Cold, 6);
        strike(s);
        CHECK(row(s) == nullptr);
        CHECK(mut(s, 2).hp == 114);
    }
    {
        auto s = begin_encounter({pc(1, 500), water(2)}, 5);
        mut(s, 1).attacks_per_action = 2;
        mut(s, 2).resistances |= bit(DamageType::Cold);
        arm(s, DamageType::Cold, 1);   // 1 / 2 = 0: types_dealt excludes cold
        strike(s);
        CHECK(row(s) == nullptr);
        CHECK(mut(s, 2).hp == 114);
        arm(s, DamageType::Cold, 2);   // 1 cold remains
        strike(s);
        CHECK(row(s) != nullptr);
        CHECK(mut(s, 2).hp == 113);
    }
    {
        auto s = begin_encounter({pc(1, 500), water(2)}, 5);
        mut(s, 2).temp_hp = 10;
        arm(s, DamageType::Cold, 6);
        strike(s);
        CHECK(row(s) != nullptr);   // temp HP absorbed it; the type was still dealt
        CHECK(mut(s, 2).hp == 114);
        CHECK(mut(s, 2).temp_hp == 4);
    }
    {
        auto s = begin_encounter({pc(1, 500), water(2)}, 5);
        mut(s, 2).hp = 1;
        arm(s, DamageType::Cold, 6);
        strike(s);
        CHECK(mut(s, 2).dead);
        CHECK(row(s) == nullptr);
    }
    {
        auto s = begin_encounter({pc(1, 500), monster(2)}, 9);
        mut(s, 2).hp = mut(s, 2).hp_max = 40;
        arm(s, DamageType::Cold, 6);
        strike(s);
        CHECK(s.modifiers.empty());
    }
}

TEST_CASE("freeze: a slashing hit that also deals cold slows once") {
    auto s = begin_encounter({pc(1, 500), water(2)}, 5);
    arm(s, DamageType::Slashing, 4);
    DamagePart cold;
    cold.dice = {0, 0, 3};
    cold.type = DamageType::Cold;
    mut(s, 1).attacks[0].extra_damage.push_back(cold);
    strike(s);
    CHECK(speed_rows(s) == 1);
    CHECK(mut(s, 2).hp == 107);   // 4 + 3
    CHECK(modified_speed(s, mut(s, 2)) == 10);
}

TEST_CASE("freeze: cold during its own turn does not rewrite the banked budget; the next turn is slowed") {
    auto s = begin_encounter({pc(1, 500), water(2)}, 5);
    s.hooks.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    mut(s, 1).reaction_policy[std::size_t(ReactionTrigger::LeavingReach)] = ReactionPolicy::Auto;
    arm(s, DamageType::Cold, 6);
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    CHECK(s.budget.movement_ft == 30);
    ActionRequest mv{2, ActionKind::Move};
    mv.move_ft = 5;
    mv.leaves_reach = true;
    auto moved = step(s, mv);
    REQUIRE(moved.ok);
    REQUIRE(row(s));
    CHECK(row(s)->timed.rounds_left == 2);   // this turn does not count
    CHECK(s.budget.movement_ft == 25);       // 30 - 5; the banked feet are not rewritten
    CHECK(modified_speed(s, mut(s, 2)) == 10);
    CHECK(mut(s, 2).hp == 108);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    REQUIRE(row(s));
    CHECK(row(s)->timed.rounds_left == 1);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 10);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    CHECK(row(s) == nullptr);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 30);
}

TEST_CASE("freeze: cold again during the slowed turn extends through the following turn") {
    auto s = begin_encounter({pc(1, 500), water(2)}, 5);
    s.hooks.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    mut(s, 1).reaction_policy[std::size_t(ReactionTrigger::LeavingReach)] = ReactionPolicy::Auto;
    arm(s, DamageType::Cold, 6);
    strike(s);
    CHECK(row(s)->timed.rounds_left == 1);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 10);
    ActionRequest mv{2, ActionKind::Move};
    mv.move_ft = 5;
    mv.leaves_reach = true;
    REQUIRE(step(s, mv).ok);
    REQUIRE(row(s));
    CHECK(row(s)->timed.rounds_left == 2);   // refreshed from this turn
    CHECK(speed_rows(s) == 1);
    CHECK(s.budget.movement_ft == 5);        // the 10 ft budget, minus the step; not rewritten to 10 - 20
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    REQUIRE(row(s));
    CHECK(row(s)->timed.rounds_left == 1);
    end_turns_until(s, 2);
    CHECK(s.budget.movement_ft == 10);
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    CHECK(row(s) == nullptr);
}
