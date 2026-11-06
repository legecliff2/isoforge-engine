// SRD 5.1 concentration: "You can end concentration at any time (no action required)."
// Admission is Heal / AbilityCheck: off the current turn, not blocked by gaze or appearance
// pending, refused while a reaction decision is pending. No action, bonus action, or reaction.
// Invisible Passage is not this action.
#include <doctest/doctest.h>

#include <memory>

#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kConc = spell_id_of("test.conc.self");

SpellDef conc_spell() {
    SpellDef d;
    d.id = kConc; d.content_id = "test.conc.self"; d.level = 1; d.range_ft = 5;
    d.kind = SpellEffectKind::ApplyCondition; d.applies_condition = true;
    d.condition = Condition::Invisible; d.concentration = true;
    d.duration = SpellDuration::Hours; d.duration_amount = 1; d.expiry = 1; d.duration_rounds = 600;
    return d;
}
EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(conc_spell());
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id) {
    Combatant c = wizard(id, 0);
    c.spells_known.push_back(kConc);
    never_react(c);
    return c;
}
EncounterState ready() {
    Combatant foe = monster(3);
    foe.team = 1;
    never_react(foe);
    auto s = begin_encounter({caster(1), caster(2), foe}, 1, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
bool concentrating(const EncounterState& s, CombatantId id) {
    return find(s, id)->concentration_spell != 0;
}
}  // namespace

TEST_CASE("concentration end: no action or bonus action, and the other caster keeps theirs") {
    auto s = ready();
    REQUIRE(step(s, cast(1, kConc, 1, 1)).ok);
    CHECK(concentrating(s, 1));
    CHECK(find(s, 1)->has(Condition::Invisible));
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.bonus_action);
    mut(s, 2).concentration_spell = kConc;
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    const bool action = s.budget.action;
    const bool bonus = s.budget.bonus_action;
    REQUIRE(step(s, {1, ActionKind::EndConcentration}).ok);
    CHECK_FALSE(concentrating(s, 1));
    CHECK_FALSE(find(s, 1)->has(Condition::Invisible));
    CHECK(find(s, 2)->concentration_spell == kConc);
    CHECK(s.budget.action == action);
    CHECK(s.budget.bonus_action == bonus);
    CHECK(current_actor(s) == 2);
}

TEST_CASE("concentration end: not concentrating is refused and spends nothing") {
    auto s = ready();
    CHECK(s.budget.action);
    CHECK(s.budget.bonus_action);
    auto r = step(s, {1, ActionKind::EndConcentration});
    CHECK_FALSE(r.ok);
    CHECK(r.error == "not concentrating");
    CHECK(s.budget.action);
    CHECK(s.budget.bonus_action);
}

TEST_CASE("concentration end: gaze, appearance, and surprise do not block it") {
    auto s = ready();
    REQUIRE(step(s, cast(1, kConc, 1, 1)).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(s.budget.action);
    s.budget.gaze_pending = true;
    s.budget.appearance_pending = true;
    mut(s, 1).surprised = true;
    REQUIRE(step(s, {1, ActionKind::EndConcentration}).ok);
    CHECK_FALSE(concentrating(s, 1));
    CHECK(s.budget.action);
    CHECK(s.budget.bonus_action);
    CHECK(s.budget.gaze_pending);
    CHECK(s.budget.appearance_pending);
    CHECK(mut(s, 1).surprised);
}

TEST_CASE("concentration end: a pending reaction decision refuses it") {
    auto s = ready();
    REQUIRE(step(s, cast(1, kConc, 1, 1)).ok);
    s.pending = PendingDecision{};
    auto r = step(s, {1, ActionKind::EndConcentration});
    CHECK_FALSE(r.ok);
    CHECK(r.error == "awaiting a reaction decision");
    CHECK(concentrating(s, 1));
}

TEST_CASE("concentration end: replay clears the same spell") {
    auto run = [] {
        Combatant a = caster(1);
        Combatant foe = monster(2);
        foe.team = 1;
        never_react(foe);
        auto s = begin_encounter({a, foe}, 9, nullptr, hooks());
        end_turns_until(s, 1);
        ActionLog log;
        REQUIRE(step_logged(s, cast(1, kConc, 1, 1), log).status == StepStatus::Done);
        REQUIRE(step_logged(s, {1, ActionKind::EndConcentration}, log).status == StepStatus::Done);
        return std::make_pair(std::move(s), log);
    };
    const auto first = run();
    Combatant a = caster(1);
    Combatant foe = monster(2);
    foe.team = 1;
    never_react(foe);
    auto again = begin_encounter({a, foe}, 9, nullptr, hooks());
    end_turns_until(again, 1);
    for (const auto& e : first.second) REQUIRE(replay_step(again, e).status == StepStatus::Done);
    CHECK_FALSE(concentrating(again, 1));
    CHECK_FALSE(concentrating(first.first, 1));
    CHECK(again.effects.size() == first.first.effects.size());
}
