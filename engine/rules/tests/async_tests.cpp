#include <doctest/doctest.h>

#include <memory>
#include "encounter_util.hpp"
#include "golden_hash.hpp"

using namespace enc;

namespace {

ActionRequest leave_move(CombatantId id, int16_t ft = 5) {
    ActionRequest r{id, ActionKind::Move};
    r.move_ft = ft; r.leaves_reach = true;
    return r;
}

EncounterHooks in_range_hooks() {
    EncounterHooks h;
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    return h;
}

uint64_t hash_state(const EncounterState& s) {
    GoldenHash g;
    for (const auto& c : s.combatants) {
        g.u32(c.id); g.i32(c.hp); g.i32(c.temp_hp); g.u32(c.conditions);
        g.byte(c.death.successes); g.byte(c.death.failures); g.byte(c.death.stable); g.byte(c.dead);
        g.byte(c.reaction_used); g.byte(uint8_t(c.reaction_ac_bonus)); g.byte(c.has_readied);
        g.byte(c.dodging); g.byte(c.disengaged); g.u32(c.check_helped_by);
    }
    for (const auto& e : s.order) { g.u32(e.id); g.i32(e.total); }
    g.u32(s.round); g.u32(s.turn_index); g.i32(s.budget.movement_ft); g.byte(s.budget.action);
    g.byte(s.budget.bonus_action); g.byte(s.budget.attacks_remaining);
    auto st = s.rng.save(); g.u64(st.state); g.u64(st.inc);
    g.u32(s.next_window_seq); g.byte(s.ended); g.byte(s.pending.has_value());
    g.u32(uint32_t(s.readied.size()));
    return g.h;
}

uint64_t hash_log(const ActionLog& log) {
    GoldenHash g;
    for (const auto& e : log) {
        g.byte(uint8_t(e.request.kind)); g.u32(e.request.actor); g.u32(e.request.target);
        g.u32(uint32_t(e.reactions.size()));
        for (const auto& r : e.reactions) {
            g.u32(r.window_seq); g.byte(uint8_t(r.trigger)); g.u32(r.reactor);
            g.byte(r.decision.react); g.byte(r.decision.option);
        }
    }
    return g.h;
}

} // namespace

TEST_CASE("async: NeedsDecision leaves state untouched; only a matching decision resumes") {
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, in_range_hooks());
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    auto before = hash_state(s);
    auto r = step(s, leave_move(2));
    REQUIRE(r.ok);
    REQUIRE(r.status == StepStatus::NeedsDecision);
    REQUIRE(r.needs);
    CHECK(r.needs->reactor == 1);
    CHECK(r.needs->trigger == ReactionTrigger::LeavingReach);
    CHECK(r.needs->trigger_actor == 2);
    CHECK(r.needs->window_seq == s.next_window_seq);   // state is pre-action; this window is the next seq
    REQUIRE(pending_decision(s));
    CHECK(pending_decision(s)->window_seq == r.needs->window_seq);
    CHECK(r.needs->window.trigger == ReactionTrigger::LeavingReach);
    CHECK(r.needs->options.size() == 1);
    CHECK(count(r.events, EventKind::ReactionWindowOpened) == 1);   // provisional
    CHECK(s.pending.has_value());
    {
        auto copy = s; copy.pending.reset();
        CHECK(hash_state(copy) == before);   // state is exactly as before the action
    }
    auto paused = hash_state(s);

    CHECK_FALSE(step(s, {2, ActionKind::EndTurn}).ok);                         // awaiting decision
    auto wrong = decision_request(*r.needs, true, 0); wrong.decision.window_seq += 1;
    CHECK_FALSE(step(s, wrong).ok);
    auto wrong2 = decision_request(*r.needs, true, 0); wrong2.actor = 2;
    CHECK_FALSE(step(s, wrong2).ok);
    CHECK_FALSE(step(s, decision_request(*r.needs, true, 9)).ok);            // bad option
    CHECK(hash_state(s) == paused);

    auto d = step(s, decision_request(*r.needs, true, 0));
    REQUIRE(d.status == StepStatus::Done);
    CHECK_FALSE(s.pending.has_value());
    CHECK(count(d.events, EventKind::AttackResolved) == 1);
    CHECK(count(d.events, EventKind::Moved) == 1);
    CHECK(find(s, 1)->reaction_used);
    REQUIRE(d.logged);
    CHECK(d.logged->request.kind == ActionKind::Move);
    CHECK(d.logged->reactions.size() == 1);
    CHECK_FALSE(step(s, decision_request(*r.needs, true, 0)).ok);           // nothing pending
}

TEST_CASE("async: several reactors are asked one at a time, in polling order") {
    auto s = begin_encounter({pc(1), pc(4), pc(5), monster(2)}, 9, nullptr, in_range_hooks());
    mut(s, 2).hp = mut(s, 2).hp_max = 300;
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    std::vector<CombatantId> asked;
    ActionLog log;
    auto r = step_logged(s, leave_move(2), log);
    while (r.status == StepStatus::NeedsDecision) {
        asked.push_back(r.needs->reactor);
        CHECK(log.empty());
        r = step_logged(s, decision_request(*r.needs, r.needs->reactor != 4, 0), log);
    }
    REQUIRE(r.status == StepStatus::Done);
    std::vector<CombatantId> expected;
    for (CombatantId id : default_poll_order(s, {1, 4, 5})) if (id != 2) expected.push_back(id);
    CHECK(asked == expected);
    REQUIRE(log.size() == 1);
    CHECK(log[0].reactions.size() == 3);
    CHECK(count(r.events, EventKind::ReactionTaken) == 2);
    CHECK_FALSE(find(s, 4)->reaction_used);
}

TEST_CASE("polling order is a policy hook") {
    EncounterHooks h = in_range_hooks();
    h.poll_order = [](const EncounterState&, const std::vector<CombatantId>& eligible) {
        return std::vector<CombatantId>(eligible.rbegin(), eligible.rend());   // reverse initiative order
    };
    auto s = begin_encounter({pc(1), pc(4), pc(5), monster(2)}, 9, nullptr, h);
    end_turns_until(s, 2);
    std::vector<CombatantId> asked;
    auto r = step(s, leave_move(2));
    while (r.status == StepStatus::NeedsDecision) {
        asked.push_back(r.needs->reactor);
        r = step(s, decision_request(*r.needs, false, 0));
    }
    std::vector<CombatantId> init;
    for (const auto& e : s.order) if (e.id != 2) init.push_back(e.id);
    CHECK(asked == std::vector<CombatantId>(init.rbegin(), init.rend()));
}

TEST_CASE("heal and ability checks are logged requests; their Ready triggers are logged too") {
    EncounterHooks h;
    h.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) {
        return e.kind == EventKind::Healed || e.kind == EventKind::CheckRolled;
    };
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 5, nullptr, h);
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 1);
    ActionRequest ready{1, ActionKind::Ready, 2};
    ready.ready_kind = ActionKind::Attack; ready.trigger_id = 1; ready.target_within_5ft = true;
    ActionLog log;
    REQUIRE(step_logged(s, ready, log).status == StepStatus::Done);
    REQUIRE(step_logged(s, {1, ActionKind::EndTurn}, log).status == StepStatus::Done);
    mut(s, 4).hp = 3;
    auto r = step_logged(s, heal_request(4, 5), log);
    REQUIRE(r.status == StepStatus::NeedsDecision);              // PC 1's readied attack is offered
    CHECK(r.needs->window.trigger == ReactionTrigger::ReadiedTrigger);
    r = step_logged(s, decision_request(*r.needs, false, 0), log);   // ignore it
    REQUIRE(r.status == StepStatus::Done);
    r = step_logged(s, check_request(4, Skill::Athletics, 12), log);
    REQUIRE(r.status == StepStatus::NeedsDecision);
    r = step_logged(s, decision_request(*r.needs, true, 0), log);    // fire on the check
    REQUIRE(r.status == StepStatus::Done);
    CHECK(count(r.events, EventKind::ReadyTriggered) == 1);
    CHECK_FALSE(step(s, heal_request(77, 1)).ok);
    CHECK_FALSE(step(s, check_request(77, Skill::Athletics, 10)).ok);

    REQUIRE(log.size() == 4);
    CHECK(log[2].request.kind == ActionKind::Heal);
    CHECK(log[2].reactions.size() == 1);
    CHECK(log[3].request.kind == ActionKind::AbilityCheck);

    // The log alone replays it (hp tweak is not part of the log, so apply it at the same point).
    auto t = begin_encounter({pc(1), pc(4), monster(2)}, 5, nullptr, h);
    mut(t, 2).hp = mut(t, 2).hp_max = 200;
    end_turns_until(t, 1);
    bool desync = false;
    for (std::size_t i = 0; i < log.size(); ++i) {
        if (i == 2) mut(t, 4).hp = 3;
        desync |= replay_step(t, log[i]).desync;
    }
    CHECK_FALSE(desync);
    CHECK(hash_state(t) == hash_state(s));
}

TEST_CASE("a readied Move that fires opens LeavingReach, but its events don't fire other readies") {
    EncounterHooks h = in_range_hooks();
    h.ready_trigger = [](const EncounterState&, const ReadiedAction& r, const Event& e) {
        if (r.trigger_id == 1) return e.kind == EventKind::Dodged;    // PC 1: move away when goblin dodges
        return e.kind == EventKind::Moved;                            // PC 4: attack anything that moves
    };
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&,
                           std::span<const ReactionOption>) { return ReactionDecision{0, true, 0}; };
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 5, nullptr, h);
    mut(s, 1).hp = mut(s, 1).hp_max = 200;
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 1);
    ActionRequest move_away{1, ActionKind::Ready};
    move_away.ready_kind = ActionKind::Move; move_away.move_ft = 10; move_away.leaves_reach = true;
    move_away.trigger_id = 1;
    REQUIRE(step(s, move_away).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 4);
    ActionRequest attack_mover{4, ActionKind::Ready, 2};
    attack_mover.ready_kind = ActionKind::Attack; attack_mover.trigger_id = 2;
    REQUIRE(step(s, attack_mover).ok);
    REQUIRE(step(s, {4, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    auto r = step(s, {2, ActionKind::Dodge});
    REQUIRE(r.status == StepStatus::Done);
    CHECK(count(r.events, EventKind::ReadyTriggered) == 1);                 // PC 1 only
    int leaving = 0;
    for (const auto& e : r.events)
        if (e.kind == EventKind::ReactionWindowOpened && e.a == int32_t(ReactionTrigger::LeavingReach)) ++leaving;
    CHECK(leaving == 1);                                                    // goblin's OA window
    CHECK(find(s, 2)->reaction_used);                                       // goblin took the OA
    CHECK(find(s, 4)->has_readied);                                         // PC 1's Moved didn't fire PC 4
    for (const auto& e : r.events) if (e.kind == EventKind::Moved) CHECK(e.reaction_depth >= 1);
}

namespace {

std::vector<Combatant> golden_party() {
    auto g2 = monster(2), g3 = monster(3);
    g2.hp = g2.hp_max = 30; g3.hp = g3.hp_max = 30;
    return {pc(1, 40), pc(4, 40), g2, g3};
}

EncounterHooks golden_hooks() {
    EncounterHooks h = in_range_hooks();
    h.end_condition = end_when_no_pc_or_no_opponent;
    h.being_hit_options = [](const EncounterState&, const ReactionWindow&, const Combatant& c) {
        std::vector<ReactionOption> o;
        if (c.is_pc) o.push_back({0, ReactionOptionKind::AcBonus, 5, 9001});
        return o;
    };
    h.spell_cast_options = [](const EncounterState&, const ReactionWindow&, const Combatant& c) {
        std::vector<ReactionOption> o;
        if (!c.is_pc) o.push_back({0, ReactionOptionKind::Counter, 0, 4242});
        return o;
    };
    return h;
}

// Deterministic "AI": depends only on the window and the reactor.
ReactionDecision golden_decide(const ReactionWindow& w, CombatantId reactor) {
    return ReactionDecision{0, ((w.seq * 7u + reactor) % 3u) != 0, 0};
}

ActionRequest golden_policy(const EncounterState& s, int i) {
    CombatantId a = current_actor(s);
    const Combatant* me = find(s, a);
    CombatantId foe = 0;
    for (const auto& c : s.combatants) if (!c.dead && c.hp > 0 && c.team != me->team) { foe = c.id; break; }
    if (me->hp <= 0) return {a, ActionKind::EndTurn};
    if (i % 11 == 5) return heal_request(me->id, 3);
    if (s.budget.action && me->is_pc && i % 7 == 3) {
        ActionRequest c{a, ActionKind::CastSpell, foe}; c.spell_id = 77; c.spell_level = 1; return c;
    }
    if (s.budget.movement_ft >= 10 && i % 3 == 0) return leave_move(a);
    if (s.budget.action && foe) { ActionRequest r{a, ActionKind::Attack, foe}; r.target_within_5ft = true; return r; }
    return {a, ActionKind::EndTurn};
}

constexpr uint64_t kSeed = 0x60D5EEDull;

} // namespace

TEST_CASE("golden: replay with reaction windows; sync adapter, async and log replay agree") {
    // Sync adapter path.
    EncounterHooks hs = golden_hooks();
    hs.decide_reaction = [](const EncounterState&, const ReactionWindow& w, const Combatant& r,
                            std::span<const ReactionOption>) { return golden_decide(w, r.id); };
    auto s = begin_encounter(golden_party(), kSeed, nullptr, hs);
    ActionLog sync_log;
    int windows = 0;
    for (int i = 0; i < 400 && !s.ended; ++i) {
        auto r = step_logged(s, golden_policy(s, i), sync_log);
        REQUIRE(r.status == StepStatus::Done);
        windows += count(r.events, EventKind::ReactionWindowOpened);
    }
    REQUIRE(windows > 10);

    // Async path: same requests; decisions supplied through NeedsDecision / ReactionDecision.
    auto t = begin_encounter(golden_party(), kSeed, nullptr, golden_hooks());
    ActionLog async_log;
    int pauses = 0;
    for (int i = 0; i < 400 && !t.ended; ++i) {
        auto r = step_logged(t, golden_policy(t, i), async_log);
        while (r.status == StepStatus::NeedsDecision) {
            ++pauses;
            r = step_logged(t, decision_request(*r.needs, golden_decide(r.needs->window, r.needs->reactor)), async_log);
        }
        REQUIRE(r.status == StepStatus::Done);
    }
    REQUIRE(pauses > 5);
    CHECK(hash_state(t) == hash_state(s));
    CHECK(hash_log(async_log) == hash_log(sync_log));

    // Log replay (no decider).
    auto u = begin_encounter(golden_party(), kSeed, nullptr, golden_hooks());
    bool desync = false;
    for (const auto& e : sync_log) desync |= replay_step(u, e).desync;
    CHECK_FALSE(desync);
    CHECK(hash_state(u) == hash_state(s));

    int heals = 0, casts = 0;
    for (const auto& e : sync_log) { heals += e.request.kind == ActionKind::Heal; casts += e.request.kind == ActionKind::CastSpell; }
    CHECK(heals > 0); CHECK(casts > 0);
    CHECK(s.ended);
    CHECK(sync_log.size() == 41u);
    CHECK(hash_state(s) == 0x844F15B1A57C8722ull);
    CHECK(hash_log(sync_log) == 0x8302FB2D965649C3ull);
}
