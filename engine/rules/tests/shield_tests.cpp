// Shield Guardian Shield (CONTENT_CONVENTIONS §54). Before the attack roll, the guardian may spend its
// reaction to give its bound wearer +2 AC against that attack only, while within 5 feet. Not BeingHit.
// Not reaction_ac_bonus. A natural 20 still hits.
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <optional>

#include "encounter_util.hpp"

using namespace enc;

namespace {
Combatant striker(CombatantId id) {
    auto c = monster(id);
    c.team = 0;
    c.hp = c.hp_max = 40;
    c.attacks = {Attack{"fist", 0, {0, 0, 5}, DamageType::Bludgeoning, 5, false, false}};
    c.attacks_per_action = 2;
    for (auto& p : c.reaction_policy) p = ReactionPolicy::Never;
    return c;
}
Combatant wearer_of(CombatantId id) {
    auto c = monster(id);
    c.team = 1;
    c.hp = c.hp_max = 80;
    c.ac = 12;
    for (auto& p : c.reaction_policy) p = ReactionPolicy::Never;
    return c;
}
Reaction shield_rx() {
    Reaction r;
    r.id = 540;
    r.window = ReactionTrigger::AllyAttacked;
    r.kind = ReactionEffectKind::WearerAc;
    r.ac_bonus = 2;
    return r;
}
Combatant guardian(CombatantId id, CombatantId wear, uint8_t team = 1) {
    auto c = monster(id);
    c.team = team;
    c.hp = c.hp_max = 90;
    c.ac = 17;
    c.bound = Bound{60, wear};
    c.reactions = {shield_rx()};
    return c;
}
struct Resolved {
    bool present = false;
    int natural = 0;
    int total = 0;
    bool hit = false;
    bool crit = false;
};
Resolved resolved(const std::vector<Event>& ev) {
    Resolved o;
    for (const auto& e : ev)
        if (e.kind == EventKind::AttackResolved) {
            o.present = true;
            o.natural = e.a;
            o.total = e.b;
            o.hit = (e.c & 1) != 0;
            o.crit = (e.c & 2) != 0;
        }
    return o;
}
int ally_windows(const std::vector<Event>& ev) {
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == EventKind::ReactionWindowOpened && e.a == int32_t(ReactionTrigger::AllyAttacked)) ++n;
    return n;
}
EncounterHooks hooks_at(const std::shared_ptr<int>& dist, int pick) {
    EncounterHooks h;
    if (dist)
        h.sight_facts = [dist](const EncounterState&, const Combatant&, const Combatant&) {
            SightFacts f;
            f.distance_ft = uint16_t(*dist < 0 ? 0 : *dist);
            return f;
        };
    h.decide_reaction = [pick](const EncounterState&, const ReactionWindow& w, const Combatant&,
                               std::span<const ReactionOption>) {
        if (w.trigger != ReactionTrigger::AllyAttacked || pick < 0) return ReactionDecision{w.seq, false, 0};
        return ReactionDecision{w.seq, true, 0};
    };
    return h;
}
ActionRequest swing(CombatantId from, CombatantId to) { return ActionRequest{from, ActionKind::Attack, to, 0}; }

struct Blow {
    EncounterState state;
    StepResult step;
};
std::optional<Blow> attack(std::vector<Combatant> cs, const std::shared_ptr<int>& dist, int pick, uint64_t seed,
                           CombatantId from = 1, CombatantId to = 2) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks_at(dist, pick));
    end_turns_until(s, from);
    if (current_actor(s) != from) return std::nullopt;
    auto r = step(s, swing(from, to));
    if (!r.ok || r.status != StepStatus::Done) return std::nullopt;
    return Blow{std::move(s), std::move(r)};
}
}  // namespace

TEST_CASE("shield enum stays appended") {
    CHECK(uint8_t(ReactionEffectKind::WearerAc) == 5);
    CHECK(uint8_t(ReactionEffectKind::SubtractRoll) == 4);
    CHECK(kReactionTriggerCount == 8);
}

TEST_CASE("shield: the offer is before the roll, against the bound wearer only") {
    auto dist = std::make_shared<int>(5);
    auto s = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, 1, nullptr, hooks_at(dist, -1));
    s.hooks.decide_reaction = nullptr;
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    auto paused = step(s, swing(1, 2));
    REQUIRE(paused.status == StepStatus::NeedsDecision);
    REQUIRE(paused.needs);
    CHECK(paused.needs->trigger == ReactionTrigger::AllyAttacked);
    CHECK(paused.needs->reactor == 3);
    CHECK(paused.needs->trigger_actor == 1);
    CHECK(paused.needs->target == 2);
    CHECK(paused.needs->window.target == 2);
    CHECK(paused.needs->attack_total == 0);
    CHECK(paused.needs->window.attack_total == 0);
    CHECK(paused.needs->attack_natural == 0);
    REQUIRE(paused.needs->options.size() == 1);
    CHECK(paused.needs->options[0].kind == ReactionOptionKind::AcBonus);
    CHECK(paused.needs->options[0].ac_bonus == 2);
    CHECK(paused.needs->options[0].uses_reaction);
    CHECK_FALSE(find(s, 3)->reaction_used);   // the pause keeps the pre-request state
    auto done = step(s, decision_request(*paused.needs, true, 0));
    REQUIRE(done.ok);
    CHECK(done.status == StepStatus::Done);
    CHECK(find(s, 3)->reaction_used);
    CHECK(find(s, 2)->reaction_ac_bonus == 0);
    CHECK(find(s, 2)->ac == 12);
    CHECK(find(s, 3)->bound.wearer == 2);

    // A different creature, even at the same reported distance, is not the wearer.
    auto by = wearer_of(4);
    by.ac = 12;
    auto other = attack({striker(1), wearer_of(2), guardian(3, 2), by}, dist, 0, 1, 1, 4);
    REQUIRE(other);
    CHECK(ally_windows(other->step.events) == 0);
    CHECK_FALSE(find(other->state, 3)->reaction_used);

    // The guardian's own attack against the wearer offers Shield, including on the guardian's turn.
    auto g = guardian(3, 2);
    g.attacks = {Attack{"fist", 0, {0, 0, 5}, DamageType::Bludgeoning, 5, false, false}};
    g.team = 0;
    EncounterHooks own = hooks_at(dist, 0);
    own.decide_reaction = nullptr;
    auto own_state = begin_encounter({striker(1), wearer_of(2), g}, 1, nullptr, own);
    end_turns_until(own_state, 3);
    REQUIRE(current_actor(own_state) == 3);
    auto own_paused = step(own_state, swing(3, 2));
    REQUIRE(own_paused.status == StepStatus::NeedsDecision);
    REQUIRE(own_paused.needs);
    CHECK(own_paused.needs->reactor == 3);
    CHECK(own_paused.needs->trigger_actor == 3);
    CHECK(own_paused.needs->target == 2);
    CHECK(own_paused.needs->attack_total == 0);
    bool saw_own = false;
    for (uint64_t seed = 1; seed < 400 && !saw_own; ++seed) {
        auto took = attack({striker(1), wearer_of(2), g}, dist, 0, seed, 3, 2);
        if (!took || resolved(took->step.events).natural != 12) continue;
        saw_own = true;
        CHECK(ally_windows(took->step.events) == 1);
        CHECK(find(took->state, 3)->reaction_used);
        CHECK_FALSE(resolved(took->step.events).hit);   // 12 misses AC 14
        CHECK(find(took->state, 2)->reaction_ac_bonus == 0);
        CHECK(find(took->state, 2)->hp == 80);
        auto declined = attack({striker(1), wearer_of(2), g}, dist, -1, seed, 3, 2);
        REQUIRE(declined);
        CHECK(resolved(declined->step.events).natural == 12);
        CHECK(resolved(declined->step.events).hit);
        CHECK_FALSE(find(declined->state, 3)->reaction_used);
    }
    REQUIRE(saw_own);

    // Team is not the gate. A guardian on another team still shields its wearer.
    auto cross = attack({striker(1), wearer_of(2), guardian(3, 2, 9)}, dist, 0, 1);
    REQUIRE(cross);
    CHECK(ally_windows(cross->step.events) == 1);
    CHECK(find(cross->state, 3)->reaction_used);
    CHECK(find(cross->state, 3)->team == 9);
}

TEST_CASE("shield: +2 changes the triggering attack only, including a critical") {
    auto dist = std::make_shared<int>(5);
    std::map<int, uint64_t> seeds;
    for (uint64_t seed = 1; seed < 800 && seeds.size() < 6; ++seed) {
        auto b = attack({striker(1), wearer_of(2), guardian(3, 2)}, dist, -1, seed);
        if (!b) continue;
        const int n = resolved(b->step.events).natural;
        if ((n == 1 || n == 11 || n == 12 || n == 13 || n == 14 || n == 20) && !seeds.count(n)) seeds[n] = seed;
    }
    REQUIRE(seeds.size() == 6);
    for (int n : {1, 11, 12, 13, 14, 20}) {
        const bool bare_hit = n == 20 || (n != 1 && n >= 12);
        const bool shield_hit = n == 20 || (n != 1 && n >= 14);
        auto yes = attack({striker(1), wearer_of(2), guardian(3, 2)}, dist, 0, seeds[n]);
        auto no = attack({striker(1), wearer_of(2), guardian(3, 2)}, dist, -1, seeds[n]);
        REQUIRE(yes);
        REQUIRE(no);
        const auto y = resolved(yes->step.events);
        const auto d = resolved(no->step.events);
        CHECK(y.natural == n);
        CHECK(d.natural == n);
        CHECK(y.total == n);
        CHECK(d.total == n);
        CHECK(y.hit == shield_hit);
        CHECK(d.hit == bare_hit);
        CHECK(y.crit == (n == 20));
        CHECK(d.crit == (n == 20));
        CHECK(find(yes->state, 3)->reaction_used);
        CHECK_FALSE(find(no->state, 3)->reaction_used);
        CHECK(find(yes->state, 2)->reaction_ac_bonus == 0);
        CHECK(find(yes->state, 2)->ac == 12);
        if (!shield_hit) CHECK(find(yes->state, 2)->hp == 80);
        else CHECK(find(yes->state, 2)->hp < 80);
        if (!bare_hit) CHECK(find(no->state, 2)->hp == 80);
        else CHECK(find(no->state, 2)->hp < 80);
    }
}

TEST_CASE("shield: declining keeps the reaction; taking it blocks the next attack until the guardian's turn") {
    auto dist = std::make_shared<int>(5);
    uint64_t seed = 0;
    for (uint64_t s = 1; s < 200; ++s) {
        auto b = attack({striker(1), wearer_of(2), guardian(3, 2)}, dist, -1, s);
        if (b && resolved(b->step.events).natural == 12) { seed = s; break; }
    }
    REQUIRE(seed != 0);

    auto declined = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, seed, nullptr, hooks_at(dist, -1));
    end_turns_until(declined, 1);
    auto first = step(declined, swing(1, 2));
    REQUIRE(first.ok);
    CHECK(ally_windows(first.events) == 1);
    CHECK(count(first.events, EventKind::ReactionDeclined) == 1);
    CHECK(resolved(first.events).hit);   // 12 meets AC 12
    CHECK_FALSE(find(declined, 3)->reaction_used);
    auto second = step(declined, swing(1, 2));
    REQUIRE(second.ok);
    CHECK(ally_windows(second.events) == 1);
    CHECK_FALSE(find(declined, 3)->reaction_used);

    auto spent = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, seed, nullptr, hooks_at(dist, 0));
    end_turns_until(spent, 1);
    auto took = step(spent, swing(1, 2));
    REQUIRE(took.ok);
    CHECK_FALSE(resolved(took.events).hit);   // 12 misses AC 14
    CHECK(find(spent, 3)->reaction_used);
    CHECK(find(spent, 2)->hp == 80);
    auto follow = step(spent, swing(1, 2));
    REQUIRE(follow.ok);
    CHECK(ally_windows(follow.events) == 0);
    const auto follow_roll = resolved(follow.events);
    REQUIRE(follow_roll.present);
    if (follow_roll.natural == 20) CHECK(follow_roll.hit);
    else if (follow_roll.natural == 1) CHECK_FALSE(follow_roll.hit);
    else CHECK(follow_roll.hit == (follow_roll.total >= 12));   // AC 12, not the spent +2
    CHECK(find(spent, 2)->reaction_ac_bonus == 0);

    REQUIRE(step(spent, {1, ActionKind::EndTurn}).ok);
    end_turns_until(spent, 1);
    REQUIRE(current_actor(spent) == 1);
    CHECK_FALSE(find(spent, 3)->reaction_used);
    auto renewed = step(spent, swing(1, 2));
    REQUIRE(renewed.ok);
    CHECK(ally_windows(renewed.events) == 1);
    CHECK(find(spent, 3)->reaction_used);
}

TEST_CASE("shield: Never opens nothing; Auto takes it without a decider; 5 feet is sight_facts") {
    auto dist = std::make_shared<int>(5);
    auto g = guardian(3, 2);
    g.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Never;
    auto never = attack({striker(1), wearer_of(2), g}, dist, 0, 1);
    REQUIRE(never);
    CHECK(ally_windows(never->step.events) == 0);
    CHECK_FALSE(find(never->state, 3)->reaction_used);

    g.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
    EncounterHooks bare = hooks_at(dist, 0);
    bare.decide_reaction = nullptr;
    auto s = begin_encounter({striker(1), wearer_of(2), g}, 1, nullptr, bare);
    end_turns_until(s, 1);
    auto r = step(s, swing(1, 2));
    REQUIRE(r.status == StepStatus::Done);
    CHECK(count(r.events, EventKind::ReactionTaken) == 1);
    CHECK(find(s, 3)->reaction_used);
    CHECK(find(s, 2)->reaction_ac_bonus == 0);

    auto gate = [&](auto&& edit) {
        auto st = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, 1, nullptr, hooks_at(dist, 0));
        end_turns_until(st, 1);
        edit(mut(st, 3));
        auto stepped = step(st, swing(1, 2));
        REQUIRE(stepped.ok);
        CHECK(ally_windows(stepped.events) == 0);
    };
    gate([&](Combatant& c) { c.reaction_used = true; });
    gate([&](Combatant& c) { c.dead = true; });
    gate([&](Combatant& c) { c.hp = 0; });
    gate([&](Combatant& c) { add_condition(c, Condition::Stunned); });
    gate([&](Combatant& c) { c.surprised = true; });

    auto at = [&](int feet) {
        auto stepped = attack({striker(1), wearer_of(2), guardian(3, 2)}, std::make_shared<int>(feet), 0, 1);
        REQUIRE(stepped);
        return ally_windows(stepped->step.events);
    };
    CHECK(at(0) == 1);
    CHECK(at(5) == 1);
    CHECK(at(6) == 0);
    CHECK(at(10) == 0);
    EncounterHooks no_sight;
    no_sight.decide_reaction = hooks_at(nullptr, 0).decide_reaction;
    auto unsighted = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, 1, nullptr, no_sight);
    end_turns_until(unsighted, 1);
    auto blind = step(unsighted, swing(1, 2));
    REQUIRE(blind.ok);
    CHECK(ally_windows(blind.events) == 0);

    // The 5 feet are sight_facts, not the AllyAttacked in_range hook.
    EncounterHooks range = hooks_at(std::make_shared<int>(5), 0);
    range.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return false; };
    auto near = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, 1, nullptr, range);
    end_turns_until(near, 1);
    auto offered = step(near, swing(1, 2));
    REQUIRE(offered.ok);
    CHECK(ally_windows(offered.events) == 1);
    EncounterHooks far = hooks_at(std::make_shared<int>(10), 0);
    far.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    auto away = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, 1, nullptr, far);
    end_turns_until(away, 1);
    auto none = step(away, swing(1, 2));
    REQUIRE(none.ok);
    CHECK(ally_windows(none.events) == 0);
}

TEST_CASE("shield: one grant ends the offer, and Ask pause and replay follow that") {
    auto dist = std::make_shared<int>(5);
    uint64_t seed = 0;
    for (uint64_t s = 1; s < 400; ++s) {
        auto one = guardian(3, 2);
        one.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
        EncounterHooks bare = hooks_at(dist, 0);
        bare.decide_reaction = nullptr;
        auto st = begin_encounter({striker(1), wearer_of(2), one}, s, nullptr, bare);
        end_turns_until(st, 1);
        auto r = step(st, swing(1, 2));
        if (r.ok && resolved(r.events).natural == 14) { seed = s; break; }
    }
    REQUIRE(seed != 0);
    auto one = guardian(3, 2);
    one.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
    EncounterHooks bare = hooks_at(dist, 0);
    bare.decide_reaction = nullptr;
    auto single = begin_encounter({striker(1), wearer_of(2), one}, seed, nullptr, bare);
    end_turns_until(single, 1);
    auto hit = step(single, swing(1, 2));
    REQUIRE(hit.ok);
    CHECK(resolved(hit.events).natural == 14);
    CHECK(resolved(hit.events).hit);   // 14 meets AC 14
    CHECK_FALSE(resolved(hit.events).crit);

    // A fourth creature changes initiative, so the stacked roll is searched on its own roster.
    bool stacked = false;
    for (uint64_t s = 1; s < 400 && !stacked; ++s) {
        auto a = guardian(3, 2);
        auto b = guardian(4, 2);
        a.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
        b.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
        auto st = begin_encounter({striker(1), wearer_of(2), a, b}, s, nullptr, bare);
        end_turns_until(st, 1);
        auto r = step(st, swing(1, 2));
        if (!r.ok || resolved(r.events).natural != 14) continue;
        stacked = true;
        CHECK(resolved(r.events).hit);   // 14 meets AC 14. +4 would miss.
        CHECK_FALSE(resolved(r.events).crit);
        const int spent = int(find(st, 3)->reaction_used) + int(find(st, 4)->reaction_used);
        CHECK(spent == 1);
        CHECK(find(st, 2)->reaction_ac_bonus == 0);
    }
    REQUIRE(stacked);
    bool capped = false;
    for (uint64_t s = 1; s < 400 && !capped; ++s) {
        auto a = guardian(3, 2);
        auto b = guardian(4, 2);
        a.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
        b.reaction_policy[std::size_t(ReactionTrigger::AllyAttacked)] = ReactionPolicy::Auto;
        auto st = begin_encounter({striker(1), wearer_of(2), a, b}, s, nullptr, bare);
        end_turns_until(st, 1);
        auto r = step(st, swing(1, 2));
        if (!r.ok || resolved(r.events).natural != 12) continue;
        capped = true;
        CHECK_FALSE(resolved(r.events).hit);   // 12 misses AC 14, so the one grant is real
        CHECK(int(find(st, 3)->reaction_used) + int(find(st, 4)->reaction_used) == 1);
        CHECK(find(st, 2)->hp == 80);
    }
    REQUIRE(capped);

    EncounterHooks ask = hooks_at(dist, 0);
    ask.decide_reaction = nullptr;
    auto pause_for = [&](uint64_t seed) {
        return begin_encounter({striker(1), wearer_of(2), guardian(3, 2), guardian(4, 2)}, seed, nullptr, ask);
    };
    auto offered = pause_for(1);
    end_turns_until(offered, 1);
    auto first = step(offered, swing(1, 2));
    REQUIRE(first.status == StepStatus::NeedsDecision);
    REQUIRE(first.needs);
    CHECK(first.needs->attack_total == 0);
    CHECK(first.needs->target == 2);
    const CombatantId lead = first.needs->reactor;
    const CombatantId next = lead == 3 ? 4 : 3;
    REQUIRE((lead == 3 || lead == 4));
    auto granted = step(offered, decision_request(*first.needs, true, 0));
    REQUIRE(granted.status == StepStatus::Done);
    CHECK(count(granted.events, EventKind::ReactionTaken) == 1);
    CHECK(count(granted.events, EventKind::ReactionDeclined) == 0);
    CHECK(find(offered, lead)->reaction_used);
    CHECK_FALSE(find(offered, next)->reaction_used);
    const auto grant_roll = resolved(granted.events);
    REQUIRE(grant_roll.present);
    if (grant_roll.natural == 20) CHECK(grant_roll.hit);
    else if (grant_roll.natural == 1) CHECK_FALSE(grant_roll.hit);
    else CHECK(grant_roll.hit == (grant_roll.total >= 14));   // +2, not +4 and not +0
    auto later = step(offered, swing(1, 2));
    REQUIRE(later.status == StepStatus::NeedsDecision);
    REQUIRE(later.needs);
    CHECK(later.needs->reactor == next);
    CHECK(find(offered, lead)->reaction_used);
    CHECK_FALSE(find(offered, next)->reaction_used);
    auto later_done = step(offered, decision_request(*later.needs, false, 0));
    REQUIRE(later_done.status == StepStatus::Done);
    CHECK_FALSE(find(offered, next)->reaction_used);
    REQUIRE(granted.logged);
    CHECK(granted.logged->reactions.size() == 1);
    auto replay_grant = pause_for(1);
    end_turns_until(replay_grant, 1);
    auto replayed_grant = replay_step(replay_grant, *granted.logged);
    REQUIRE(replayed_grant.ok);
    CHECK_FALSE(replayed_grant.desync);
    CHECK(resolved(replayed_grant.events).hit == grant_roll.hit);
    CHECK(find(replay_grant, lead)->reaction_used);
    CHECK_FALSE(find(replay_grant, next)->reaction_used);

    auto declined_first = pause_for(1);
    end_turns_until(declined_first, 1);
    auto open_lead = step(declined_first, swing(1, 2));
    REQUIRE(open_lead.status == StepStatus::NeedsDecision);
    REQUIRE(open_lead.needs);
    CHECK(open_lead.needs->reactor == lead);
    auto open_next = step(declined_first, decision_request(*open_lead.needs, false, 0));
    REQUIRE(open_next.status == StepStatus::NeedsDecision);
    REQUIRE(open_next.needs);
    CHECK(open_next.needs->reactor == next);
    CHECK(open_next.needs->window_seq == open_lead.needs->window_seq);
    CHECK(open_next.needs->attack_total == 0);
    auto follow = step(declined_first, decision_request(*open_next.needs, true, 0));
    REQUIRE(follow.status == StepStatus::Done);
    CHECK_FALSE(find(declined_first, lead)->reaction_used);
    CHECK(find(declined_first, next)->reaction_used);
    CHECK(count(follow.events, EventKind::ReactionDeclined) == 1);
    CHECK(count(follow.events, EventKind::ReactionTaken) == 1);
    const auto follow_roll = resolved(follow.events);
    REQUIRE(follow_roll.present);
    if (follow_roll.natural == 20) CHECK(follow_roll.hit);
    else if (follow_roll.natural == 1) CHECK_FALSE(follow_roll.hit);
    else CHECK(follow_roll.hit == (follow_roll.total >= 14));
    REQUIRE(follow.logged);
    CHECK(follow.logged->reactions.size() == 2);
    auto replay_follow = pause_for(1);
    end_turns_until(replay_follow, 1);
    auto replayed_follow = replay_step(replay_follow, *follow.logged);
    REQUIRE(replayed_follow.ok);
    CHECK_FALSE(replayed_follow.desync);
    CHECK(resolved(replayed_follow.events).hit == follow_roll.hit);
    CHECK_FALSE(find(replay_follow, lead)->reaction_used);
    CHECK(find(replay_follow, next)->reaction_used);
    CHECK(find(replay_follow, 2)->hp == find(declined_first, 2)->hp);

    ActionLog log;
    auto live = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, seed, nullptr, hooks_at(dist, 0));
    end_turns_until(live, 1);
    auto played = step_logged(live, swing(1, 2), log);
    REQUIRE(played.status == StepStatus::Done);
    REQUIRE(log.size() == 1);
    auto again = begin_encounter({striker(1), wearer_of(2), guardian(3, 2)}, seed, nullptr, hooks_at(dist, -1));
    end_turns_until(again, 1);
    auto replayed = replay_step(again, log[0]);
    REQUIRE(replayed.ok);
    CHECK_FALSE(replayed.desync);
    CHECK(resolved(replayed.events).hit == resolved(played.events).hit);
    CHECK(resolved(replayed.events).crit == resolved(played.events).crit);
    CHECK(find(again, 3)->reaction_used);
    CHECK(find(again, 2)->hp == find(live, 2)->hp);
}

TEST_CASE("shield: a larger reaction bonus is still only +2 on the attack") {
    auto dist = std::make_shared<int>(5);
    auto over = guardian(3, 2);
    over.reactions[0].ac_bonus = 5;
    EncounterHooks bare = hooks_at(dist, 0);
    bool hit14 = false, miss12 = false;
    for (uint64_t seed = 1; seed < 400 && !(hit14 && miss12); ++seed) {
        auto st = begin_encounter({striker(1), wearer_of(2), over}, seed, nullptr, bare);
        end_turns_until(st, 1);
        auto r = step(st, swing(1, 2));
        if (!r.ok) continue;
        const auto roll = resolved(r.events);
        if (roll.natural == 14) {
            hit14 = true;
            CHECK(roll.hit);             // 14 meets AC 14. +5 would miss.
            CHECK(find(st, 3)->reaction_used);
        } else if (roll.natural == 12) {
            miss12 = true;
            CHECK_FALSE(roll.hit);       // 12 misses AC 14, so the bound is not 0.
            CHECK(find(st, 2)->hp == 80);
        }
    }
    CHECK(hit14);
    CHECK(miss12);
}
