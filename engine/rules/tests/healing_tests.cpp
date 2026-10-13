#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

TEST_CASE("apply_healing is pure and caps at max HP") {
    auto c = pc(1, 20); c.hp = 5;
    auto h = apply_healing(c, 30);
    CHECK(c.hp == 5);
    CHECK(h.healed == 15); CHECK(h.hp_after == 20); CHECK_FALSE(h.from_zero);
    CHECK(apply_healing(c, 0).healed == 0);
    CHECK(apply_healing(c, -3).healed == 0);
    c.exhaustion_level = 4;                         // max HP halved -> 10
    CHECK(apply_healing(c, 30).hp_after == 10);
    c.hp = 15;                                      // above the reduced cap: never lowered
    CHECK(apply_healing(c, 5).hp_after == 15);
    c.dead = true;
    CHECK(apply_healing(c, 5).healed == 0);
}

TEST_CASE("healing from 0 HP resets death saves and removes Unconscious, not Prone") {
    auto c = pc(1, 20);
    make_dying(c);
    c.death = {2, 2, false};
    auto h = apply_healing(c, 4);
    CHECK(h.from_zero);
    commit_healing(c, h);
    CHECK(c.hp == 4);
    CHECK(c.death.successes == 0); CHECK(c.death.failures == 0); CHECK_FALSE(c.death.stable);
    CHECK_FALSE(c.has(Condition::Unconscious));
    CHECK(c.has(Condition::Prone));
}

TEST_CASE("encounter heal(): events, no-ops on invalid, dying PC returns to the fight") {
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 3);
    make_dying(mut(s, 1));
    mut(s, 1).death = {1, 2, false};
    CHECK_FALSE(heal(s, 77, 5).ok);
    CHECK_FALSE(heal(s, 1, 0).ok);
    auto r = heal(s, 1, 6);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::Healed) == 1);
    CHECK(count(r.events, EventKind::ConditionRemoved) == 1);
    const auto* p = find(s, 1);
    CHECK(p->hp == 6);
    CHECK(is_conscious(*p));
    CHECK(pc_state(*p) == PcState::Conscious);
    CHECK(p->death.failures == 0);
    mut(s, 2).dead = true;
    CHECK_FALSE(heal(s, 2, 3).ok);                  // the dead stay dead
}

TEST_CASE("default end condition: ends when no PC is conscious; reports each PC's state") {
    auto s = begin_encounter({pc(1), pc(4), pc(5), monster(2)}, 11);
    CHECK_FALSE(s.ended);
    make_dying(mut(s, 1));
    make_dying(mut(s, 4)); mut(s, 4).death.stable = true;
    mut(s, 5).hp = 1; mut(s, 5).ac = -20;
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    for (int i = 0; i < 40 && !s.ended; ++i) {      // until goblin drops PC 5
        auto copy = s;
        auto r = step(copy, {2, ActionKind::Attack, 5, 0, 0, false, false});
        REQUIRE(r.ok);
        if (copy.ended) { s = copy; break; }
        s.rng = rng::Rng(uint64_t(i) + 50, 0);
    }
    REQUIRE(s.ended);
    REQUIRE(s.outcome.has_value());
    CHECK(s.outcome->reason == EndReason::NoPcConscious);
    REQUIRE(s.outcome->pcs.size() == 3);
    CHECK(s.outcome->pcs[0].id == 1); CHECK(s.outcome->pcs[0].state == PcState::Dying);
    CHECK(s.outcome->pcs[1].id == 4); CHECK(s.outcome->pcs[1].state == PcState::Stable);
    CHECK(s.outcome->pcs[2].id == 5);
    CHECK(s.outcome->pcs[2].state == PcState::Dying);
}

TEST_CASE("default end condition does not end when every monster is down") {
    auto s = begin_encounter({pc(1), monster(2)}, 4);
    mut(s, 2).dead = true;
    REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK_FALSE(s.ended);                           // rules don't call a "win"
}

TEST_CASE("end-condition hook overrides the default") {
    EncounterHooks h;
    int calls = 0;
    h.end_condition = [&calls](const EncounterState& st) -> std::optional<EncounterOutcome> {
        ++calls;
        if (st.round >= 3) return EncounterOutcome{EndReason::Custom, pc_statuses(st)};
        return std::nullopt;
    };
    auto s = begin_encounter({pc(1), monster(2)}, 9, nullptr, h);
    for (int i = 0; i < 20 && !s.ended; ++i) REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK(s.ended);
    CHECK(s.round == 3);
    CHECK(s.outcome->reason == EndReason::Custom);
    CHECK(calls > 0);

    EncounterHooks never;   // a policy that never ends overrides "no PC conscious"
    never.end_condition = [](const EncounterState&) { return std::optional<EncounterOutcome>{}; };
    auto t = begin_encounter({pc(1), monster(2)}, 9, nullptr, never);
    make_dying(mut(t, 1));
    REQUIRE(step(t, {current_actor(t), ActionKind::EndTurn}).ok);
    CHECK_FALSE(t.ended);
}

TEST_CASE("pc_state covers every case") {
    auto c = pc(1);
    CHECK(pc_state(c) == PcState::Conscious);
    add_condition(c, Condition::Unconscious);
    CHECK(pc_state(c) == PcState::Unconscious);
    c.hp = 0;
    CHECK(pc_state(c) == PcState::Dying);
    c.death.stable = true;
    CHECK(pc_state(c) == PcState::Stable);
    c.dead = true;
    CHECK(pc_state(c) == PcState::Dead);
}
