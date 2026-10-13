#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

TEST_CASE("Medicine check DC 10 stabilizes a dying creature") {
    int successes = 0, failures = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        auto s = begin_encounter({pc(1), pc(4), monster(2)}, seed);
        make_dying(mut(s, 4));
        end_turns_until(s, 1);
        if (current_actor(s) != 1) continue;
        CHECK_FALSE(step(s, {1, ActionKind::Stabilize, 4}).ok);            // not adjacent
        CHECK_FALSE(step(s, {1, ActionKind::Stabilize, 2, 0, 0, false, true}).ok);  // not dying
        auto r = step(s, {1, ActionKind::Stabilize, 4, 0, 0, false, true});
        REQUIRE(r.ok);
        REQUIRE(count(r.events, EventKind::MedicineCheck) == 1);
        const Event* ep = nullptr;
        for (const auto& x : r.events) if (x.kind == EventKind::MedicineCheck) ep = &x;
        REQUIRE(ep);
        const Event& e = *ep;
        bool ok = e.c != 0;
        CHECK(ok == (e.b >= 10));
        CHECK(e.b == e.a + skill_modifier(*find(s, 1), Skill::Medicine));
        CHECK(find(s, 4)->death.stable == ok);
        CHECK(pc_state(*find(s, 4)) == (ok ? PcState::Stable : PcState::Dying));
        ok ? ++successes : ++failures;
        CHECK_FALSE(s.budget.action);
        CHECK_FALSE(step(s, {1, ActionKind::Stabilize, 4, 0, 0, false, true}).ok);  // action spent
    }
    CHECK(successes > 0); CHECK(failures > 0);
}

TEST_CASE("Medicine proficiency adds the proficiency bonus") {
    auto c = pc(1);
    CHECK(skill_modifier(c, Skill::Medicine) == 0);
    c.skill_proficiencies = bit(Skill::Medicine);
    CHECK(skill_modifier(c, Skill::Medicine) == 2);
    CHECK(skill_ability(Skill::Medicine) == Ability::Wis);
    CHECK(skill_ability(Skill::Stealth) == Ability::Dex);
    CHECK(skill_ability(Skill::Athletics) == Ability::Str);
    CHECK(skill_ability(Skill::Persuasion) == Ability::Cha);
    CHECK(skill_ability(Skill::Arcana) == Ability::Int);
}

TEST_CASE("Spare the Dying hook: unavailable by default; stabilizes automatically when allowed") {
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 2);
    make_dying(mut(s, 4));
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    CHECK_FALSE(step(s, {1, ActionKind::SpareTheDying, 4}).ok);
    s.hooks.spare_the_dying = [](const EncounterState&, const Combatant& caster, const Combatant&) {
        return caster.id == 1;
    };
    auto rng_before = s.rng.save();
    auto r = step(s, {1, ActionKind::SpareTheDying, 4});
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::Stabilized) == 1);
    CHECK(find(s, 4)->death.stable);
    CHECK(s.rng.save().state == rng_before.state);   // no roll
    CHECK_FALSE(s.budget.action);
}

TEST_CASE("stable creature: no death saves; damage restarts them") {
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 6);
    make_dying(mut(s, 4));
    mut(s, 4).death.stable = true;
    std::vector<Event> all;
    for (int i = 0; i < 6; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), r.events.begin(), r.events.end());
    }
    for (const auto& e : all) CHECK_FALSE((e.kind == EventKind::DeathSaveRolled && e.actor == 4));
    end_turns_until(s, 2);
    mut(s, 4).ac = -20;
    for (int i = 0; i < 30; ++i) {
        auto copy = s;
        REQUIRE(step(copy, {2, ActionKind::Attack, 4}).ok);
        if (find(copy, 4)->death.failures > 0) { s = copy; break; }
        s.rng = rng::Rng(uint64_t(i) + 90, 0);
    }
    CHECK_FALSE(find(s, 4)->death.stable);
    CHECK(find(s, 4)->death.failures == 1);   // ranged (not within 5 ft): no crit
}

TEST_CASE("Extra Attack: attacks_per_action attacks for one action") {
    auto s = begin_encounter({pc(1), monster(2), monster(3)}, 8);
    mut(s, 1).attacks_per_action = 2;
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    REQUIRE(step(s, {1, ActionKind::Attack, 2}).ok);
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.attacks_remaining == 1);
    REQUIRE(step(s, {1, ActionKind::Move, 0, 0, 5}).ok);   // movement between attacks
    REQUIRE(step(s, {1, ActionKind::Attack, 3}).ok);        // different target is fine
    CHECK(s.budget.attacks_remaining == 0);
    CHECK_FALSE(step(s, {1, ActionKind::Attack, 2}).ok);

    auto t = begin_encounter({pc(1), monster(2)}, 8);
    end_turns_until(t, 1);
    REQUIRE(step(t, {1, ActionKind::Attack, 2}).ok);        // default: one attack
    CHECK_FALSE(step(t, {1, ActionKind::Attack, 2}).ok);
    auto u = begin_encounter({pc(1), monster(2)}, 8);
    mut(u, 1).attacks_per_action = 2;
    end_turns_until(u, 1);
    REQUIRE(step(u, {1, ActionKind::Dash}).ok);             // action spent elsewhere
    CHECK_FALSE(step(u, {1, ActionKind::Attack, 2}).ok);
}

TEST_CASE("Help: ally's next attack against the target has advantage, then is consumed") {
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 12);
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    CHECK_FALSE(step(s, {1, ActionKind::Help, 2, 0, 0, false, false, 4}).ok);   // not within 5 ft
    CHECK_FALSE(step(s, {1, ActionKind::Help, 2, 0, 0, false, true, 1}).ok);    // self as ally
    REQUIRE(step(s, {1, ActionKind::Help, 2, 0, 0, false, true, 4}).ok);
    CHECK(find(s, 4)->helped_vs == 2);
    end_turns_until(s, 4);
    REQUIRE(current_actor(s) == 4);
    auto r = step(s, {4, ActionKind::Attack, 2});
    REQUIRE(r.ok);
    // AttackResolved uses Advantage: verify through the d20 roll mode by re-running.
    CHECK(find(s, 4)->helped_vs == 0);

    // Expires at the start of the helper's next turn if unused.
    auto t = begin_encounter({pc(1), pc(4), monster(2)}, 12);
    end_turns_until(t, 1);
    REQUIRE(step(t, {1, ActionKind::Help, 2, 0, 0, false, true, 4}).ok);
    REQUIRE(step(t, {1, ActionKind::EndTurn}).ok);
    end_turns_until(t, 1);
    CHECK(find(t, 4)->helped_vs == 0);
}

TEST_CASE("Help advantage is applied to the roll") {
    // Same rng state, with and without Help: the helped attack draws two d20s.
    auto base = begin_encounter({pc(1), pc(4), monster(2)}, 12);
    mut(base, 2).hp = mut(base, 2).hp_max = 200;
    end_turns_until(base, 4);
    REQUIRE(current_actor(base) == 4);
    auto helped = base;
    mut(helped, 4).helped_vs = 2; mut(helped, 4).helped_by = 1;
    REQUIRE(step(base, {4, ActionKind::Attack, 2}).ok);
    REQUIRE(step(helped, {4, ActionKind::Attack, 2}).ok);
    rng::Rng a = base.rng, b = helped.rng;
    CHECK(a.save().state != b.save().state);   // one extra d20 drawn under advantage
}

TEST_CASE("Hide: Stealth total recorded; hidden attacker gets advantage and is revealed") {
    auto s = begin_encounter({pc(1), monster(2), monster(3)}, 21);
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    auto r = step(s, {2, ActionKind::Hide, 0, 0, 0, true});   // Nimble Escape bonus action
    REQUIRE(r.ok);
    REQUIRE(count(r.events, EventKind::Hid) == 1);
    const auto* g = find(s, 2);
    CHECK(g->stealth_total >= 1);
    const Event* hid = nullptr;
    for (const auto& e : r.events) if (e.kind == EventKind::Hid) hid = &e;
    REQUIRE(hid);
    CHECK(g->stealth_total == std::max(1, hid->a));
    CHECK(s.budget.action);
    REQUIRE(step(s, {2, ActionKind::Attack, 1}).ok);
    CHECK(find(s, 2)->stealth_total == 0);
    CHECK_FALSE(step(s, {2, ActionKind::Hide, 0, 0, 0, true}).ok);   // bonus already used
    REQUIRE(step(s, {2, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK_FALSE(step(s, {1, ActionKind::Hide, 0, 0, 0, true}).ok);   // PC has no grant
    CHECK(step(s, {1, ActionKind::Hide}).ok);                         // as an action
}

TEST_CASE("Ready: trigger callback fires the response once as a reaction") {
    EncounterHooks h;
    int offered = 0;
    // Trigger 7: "when an enemy moves". Filled in by sim in the real engine.
    h.ready_trigger = [&offered](const EncounterState& st, const ReadiedAction& r, const Event& e) {
        ++offered;
        if (r.trigger_id != 7 || e.kind != EventKind::Moved) return false;
        const Combatant* mover = find(st, e.actor);
        const Combatant* me = find(st, r.actor);
        return mover && me && mover->team != me->team;
    };
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, h);
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    ActionRequest ready{1, ActionKind::Ready, 2};
    ready.ready_kind = ActionKind::Attack; ready.trigger_id = 7; ready.target_within_5ft = true;
    CHECK_FALSE(step(s, ActionRequest{1, ActionKind::Ready, 2}).ok);   // ready_kind EndTurn: invalid
    REQUIRE(step(s, ready).ok);
    CHECK(find(s, 1)->has_readied);
    CHECK_FALSE(s.budget.action);
    REQUIRE(step(s, {1, ActionKind::Move, 0, 0, 5}).ok);   // own move: trigger says no (same team)
    CHECK(find(s, 1)->has_readied);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    auto r = step(s, {2, ActionKind::Move, 0, 0, 5});
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ReadyTriggered) == 1);
    CHECK(count(r.events, EventKind::AttackResolved) == 1);
    CHECK(find(s, 1)->reaction_used);
    CHECK_FALSE(find(s, 1)->has_readied);
    auto r2 = step(s, {2, ActionKind::Move, 0, 0, 5});
    CHECK(count(r2.events, EventKind::ReadyTriggered) == 0);   // used up
    CHECK(offered > 0);
}

TEST_CASE("Ready: expires at the readier's next turn; no hook means it never fires") {
    auto s = begin_encounter({pc(1), monster(2)}, 5);
    end_turns_until(s, 1);
    ActionRequest ready{1, ActionKind::Ready};
    ready.ready_kind = ActionKind::Move; ready.move_ft = 10;
    REQUIRE(step(s, ready).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    auto r = step(s, {2, ActionKind::Move, 0, 0, 5});
    CHECK(count(r.events, EventKind::ReadyTriggered) == 0);
    std::vector<Event> all;
    for (int i = 0; i < 4 && current_actor(s) != 1; ++i) {
        auto e = step(s, {current_actor(s), ActionKind::EndTurn});
        all.insert(all.end(), e.events.begin(), e.events.end());
    }
    CHECK(count(all, EventKind::ReadyExpired) == 1);
    CHECK_FALSE(find(s, 1)->has_readied);
    CHECK(s.readied.empty());
}

TEST_CASE("Help on ability checks: advantage on the ally's next check, consumed once") {
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 12);
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    CHECK_FALSE(step(s, {1, ActionKind::Help, 0, 0, 0, false, false, 1}).ok);   // self
    REQUIRE(step(s, {1, ActionKind::Help, 0, 0, 0, false, false, 4}).ok);       // target 0 = check
    CHECK(find(s, 4)->check_helped_by == 1);
    CHECK(find(s, 4)->helped_vs == 0);
    auto r = encounter_check(s, 4, Skill::Athletics, 10);
    REQUIRE(r.ok);
    REQUIRE(r.events.size() == 1);
    CHECK(r.events[0].kind == EventKind::CheckRolled);
    CHECK((r.events[0].c & 2) != 0);                          // helped
    CHECK(find(s, 4)->check_helped_by == 0);
    auto r2 = encounter_check(s, 4, Skill::Athletics, 10);
    CHECK((r2.events[0].c & 2) == 0);                         // consumed
}

TEST_CASE("Help on checks draws two d20s (advantage) and cancels with disadvantage") {
    auto base = begin_encounter({pc(1), pc(4), monster(2)}, 12);
    auto helped = base;
    mut(helped, 4).check_helped_by = 1;
    REQUIRE(encounter_check(base, 4, Skill::Medicine, 10).ok);
    REQUIRE(encounter_check(helped, 4, Skill::Medicine, 10).ok);
    rng::Rng extra = base.rng;   // helped drew exactly one more d20
    (void)extra.d(20);
    CHECK(extra.save().state == helped.rng.save().state);
    auto pois = base;
    mut(pois, 4).check_helped_by = 1;
    add_condition(mut(pois, 4), Condition::Poisoned);         // adv + dis -> one die
    auto before = pois.rng;
    REQUIRE(encounter_check(pois, 4, Skill::Medicine, 10).ok);
    (void)before.d(20);
    CHECK(before.save().state == pois.rng.save().state);
}

TEST_CASE("Help on checks applies to Stabilize's Medicine check and expires like attack Help") {
    auto s = begin_encounter({pc(1), pc(4), pc(5), monster(2)}, 3);
    make_dying(mut(s, 5));
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    REQUIRE(step(s, {1, ActionKind::Help, 0, 0, 0, false, false, 4}).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 4);
    if (current_actor(s) == 4) {
        auto r = step(s, {4, ActionKind::Stabilize, 5, 0, 0, false, true});
        REQUIRE(r.ok);
        bool helped = false;
        for (const auto& e : r.events) if (e.kind == EventKind::CheckRolled) helped = (e.c & 2) != 0;
        CHECK(helped);
    }
    auto t = begin_encounter({pc(1), pc(4), monster(2)}, 3);
    end_turns_until(t, 1);
    REQUIRE(step(t, {1, ActionKind::Help, 0, 0, 0, false, false, 4}).ok);
    REQUIRE(step(t, {1, ActionKind::EndTurn}).ok);
    end_turns_until(t, 1);                                    // helper's next turn: expired
    CHECK(find(t, 4)->check_helped_by == 0);
}
