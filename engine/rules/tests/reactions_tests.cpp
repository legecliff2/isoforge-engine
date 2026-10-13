#include <doctest/doctest.h>
#include <memory>

#include "encounter_util.hpp"
#include "golden_hash.hpp"

using namespace enc;

namespace {

constexpr auto kLeave = ReactionTrigger::LeavingReach;
constexpr auto kHit = ReactionTrigger::BeingHit;
constexpr auto kSpell = ReactionTrigger::SpellCast;
constexpr auto kReady = ReactionTrigger::ReadiedTrigger;

void set_policy(Combatant& c, ReactionTrigger t, ReactionPolicy p) { c.reaction_policy[std::size_t(t)] = p; }

ActionRequest leave_move(CombatantId id, int16_t ft = 5) {
    ActionRequest r{id, ActionKind::Move};
    r.move_ft = ft; r.leaves_reach = true;
    return r;
}

EncounterHooks everyone_in_range(bool react = true) {
    EncounterHooks h;
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    h.decide_reaction = [react](const EncounterState&, const ReactionWindow&, const Combatant&,
                                std::span<const ReactionOption>) { return ReactionDecision{0, react, 0}; };
    return h;
}

uint64_t hash_state(const EncounterState& s) {
    GoldenHash g;
    for (const auto& c : s.combatants) {
        g.u32(c.id); g.i32(c.hp); g.i32(c.temp_hp); g.u32(c.conditions);
        g.byte(c.death.successes); g.byte(c.death.failures); g.byte(c.death.stable); g.byte(c.dead);
        g.byte(c.reaction_used); g.byte(uint8_t(c.reaction_ac_bonus)); g.byte(c.has_readied);
    }
    g.u32(s.round); g.u32(s.turn_index); g.i32(s.budget.movement_ft); g.byte(s.budget.action);
    auto st = s.rng.save(); g.u64(st.state); g.u64(st.inc);
    g.u32(s.next_window_seq); g.byte(s.ended);
    return g.h;
}

} // namespace

TEST_CASE("LeavingReach: opportunity attack via window, uses the reaction") {
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, everyone_in_range());
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    auto r = step(s, leave_move(2));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ReactionWindowOpened) == 1);
    CHECK(count(r.events, EventKind::ReactionTaken) == 1);
    CHECK(count(r.events, EventKind::AttackResolved) == 1);
    CHECK(count(r.events, EventKind::Moved) == 1);
    for (const auto& e : r.events)
        if (e.kind == EventKind::AttackResolved) CHECK(e.reaction_depth == 1);
    CHECK(find(s, 1)->reaction_used);
    REQUIRE(r.reactions.size() == 1);
    CHECK(r.reactions[0].reactor == 1);
    CHECK(r.reactions[0].trigger == kLeave);
    auto r2 = step(s, leave_move(2));                       // reaction already spent
    CHECK(count(r2.events, EventKind::ReactionWindowOpened) == 0);
}

TEST_CASE("LeavingReach eligibility: disengage, Never, not in range, allies, ranged-only, incapacitated") {
    auto run = [](auto setup) {
        auto s = begin_encounter({pc(1), pc(4), monster(2)}, 5, nullptr, everyone_in_range());
        mut(s, 2).hp = mut(s, 2).hp_max = 200;   // survives both opportunity attacks
        setup(s);
        end_turns_until(s, 2);
        return step(s, leave_move(2));
    };
    CHECK(count(run([](EncounterState&) {}).events, EventKind::ReactionTaken) == 2);
    CHECK(count(run([](EncounterState& s) { mut(s, 2).disengaged = true; }).events, EventKind::ReactionWindowOpened) <= 1);
    CHECK(count(run([](EncounterState& s) { set_policy(mut(s, 1), kLeave, ReactionPolicy::Never); }).events,
                EventKind::ReactionTaken) == 1);
    CHECK(count(run([](EncounterState& s) { s.hooks.in_range = nullptr; }).events, EventKind::ReactionWindowOpened) == 0);
    CHECK(count(run([](EncounterState& s) { mut(s, 1).attacks[0].ranged = true; }).events, EventKind::ReactionTaken) == 1);
    CHECK(count(run([](EncounterState& s) { add_condition(mut(s, 4), Condition::Stunned); }).events,
                EventKind::ReactionTaken) == 1);
    // A PC leaving reach: only the hostile goblin is polled, never the ally.
    auto s = begin_encounter({pc(1), pc(4), monster(2)}, 5, nullptr, everyone_in_range());
    end_turns_until(s, 1);
    auto r = step(s, leave_move(1));
    REQUIRE(r.reactions.size() == 1);
    CHECK(r.reactions[0].reactor == 2);
    CHECK_FALSE(find(s, 4)->reaction_used);
}

TEST_CASE("LeavingReach: Disengage closes the window entirely") {
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, everyone_in_range());
    end_turns_until(s, 2);
    REQUIRE(step(s, {2, ActionKind::Disengage, 0, 0, 0, true}).ok);
    auto r = step(s, leave_move(2));
    CHECK(count(r.events, EventKind::ReactionWindowOpened) == 0);
    CHECK_FALSE(find(s, 1)->reaction_used);
}

TEST_CASE("LeavingReach: a reaction can stop the move") {
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto s = begin_encounter({pc(1), pc(4), monster(2), monster(3)}, seed, nullptr, everyone_in_range());
        mut(s, 2).hp = 1; mut(s, 2).ac = -20;
        end_turns_until(s, 2);
        if (current_actor(s) != 2) continue;
        int16_t before = s.budget.movement_ft;
        auto r = step(s, leave_move(2, 10));
        REQUIRE(r.ok);
        if (find(s, 2)->dead) {
            CHECK(count(r.events, EventKind::MoveInterrupted) == 1);
            CHECK(count(r.events, EventKind::Moved) == 0);
            CHECK(count(r.events, EventKind::ReactionTaken) == 1);   // second reactor not polled
            CHECK_FALSE((find(s, 1)->reaction_used && find(s, 4)->reaction_used));
            CHECK(s.budget.movement_ft == before);
            return;
        }
    }
    FAIL("no seed killed the mover");
}

TEST_CASE("polling order: initiative order starting after the current actor") {
    EncounterHooks h = everyone_in_range(false);
    auto order = std::make_shared<std::vector<CombatantId>>();
    h.decide_reaction = [order](const EncounterState&, const ReactionWindow&, const Combatant& r,
                                std::span<const ReactionOption>) { order->push_back(r.id); return ReactionDecision{}; };
    auto s = begin_encounter({pc(1), pc(4), pc(5), pc(6), monster(2)}, 77, nullptr, h);
    end_turns_until(s, 2);
    REQUIRE(current_actor(s) == 2);
    REQUIRE(step(s, leave_move(2)).ok);
    std::vector<CombatantId> expected;
    for (std::size_t k = 1; k < s.order.size(); ++k)
        expected.push_back(s.order[(s.turn_index + k) % s.order.size()].id);
    CHECK(*order == expected);
    // Declining keeps reactions available.
    for (CombatantId id : {1u, 4u, 5u, 6u}) CHECK_FALSE(find(s, id)->reaction_used);
}

TEST_CASE("Ask without a decider pauses (NeedsDecision); Auto takes option 0 without asking") {
    EncounterHooks h;
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    int asked = 0;
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, h);
    end_turns_until(s, 2);
    auto r = step(s, leave_move(2));
    REQUIRE(r.status == StepStatus::NeedsDecision);
    REQUIRE(r.needs.has_value());
    CHECK(r.needs->reactor == 1);
    auto d = step(s, decision_request(*r.needs, false, 0));
    REQUIRE(d.status == StepStatus::Done);
    CHECK(count(d.events, EventKind::ReactionDeclined) == 1);
    REQUIRE(d.reactions.size() == 1);
    CHECK_FALSE(d.reactions[0].decision.react);
    auto t = begin_encounter({pc(1), monster(2)}, 5, nullptr, h);
    t.hooks.decide_reaction = [&asked](const EncounterState&, const ReactionWindow&, const Combatant&,
                                       std::span<const ReactionOption>) { ++asked; return ReactionDecision{}; };
    set_policy(mut(t, 1), kLeave, ReactionPolicy::Auto);
    end_turns_until(t, 2);
    auto r2 = step(t, leave_move(2));
    CHECK(asked == 0);
    CHECK(count(r2.events, EventKind::ReactionTaken) == 1);
    CHECK(r2.reactions.empty());                            // Auto is not logged
}

TEST_CASE("BeingHit: Shield-style AC bump before damage can turn a hit into a miss") {
    EncounterHooks h;
    h.being_hit_options = [](const EncounterState&, const ReactionWindow&, const Combatant& c) {
        std::vector<ReactionOption> o;
        if (c.is_pc) o.push_back({0, ReactionOptionKind::AcBonus, 5, 9001});
        return o;
    };
    h.decide_reaction = [](const EncounterState&, const ReactionWindow&, const Combatant&,
                           std::span<const ReactionOption>) { return ReactionDecision{0, true, 0}; };
    int windows = 0, turned = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto s = begin_encounter({pc(1), pc(4), monster(2)}, seed, nullptr, h);
        end_turns_until(s, 2);
        if (current_actor(s) != 2) continue;
        int ac = find(s, 1)->ac;
        auto r = step(s, {2, ActionKind::Attack, 1});
        REQUIRE(r.ok);
        const Event* res = nullptr;
        for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) res = &e;
        REQUIRE(res);
        bool opened = count(r.events, EventKind::ReactionWindowOpened) == 1;
        if (!opened) { CHECK((res->c & 1) == 0); continue; }   // only hits open BeingHit
        ++windows;
        CHECK(find(s, 1)->reaction_used);
        CHECK(find(s, 1)->reaction_ac_bonus == 5);
        bool hit = (res->c & 1) != 0;
        bool expect_hit = res->a == 20 || res->b >= ac + 5;
        CHECK(hit == expect_hit);
        if (!hit) { ++turned; CHECK(count(r.events, EventKind::DamageDealt) == 0); }
        // AC bonus lasts until the start of the PC's next turn.
        end_turns_until(s, 1);
        CHECK(find(s, 1)->reaction_ac_bonus == 0);
    }
    CHECK(windows > 0); CHECK(turned > 0);
}

TEST_CASE("BeingHit Auto only spends the reaction when it changes the outcome") {
    EncounterHooks h;
    h.being_hit_options = [](const EncounterState&, const ReactionWindow&, const Combatant&) {
        return std::vector<ReactionOption>{{0, ReactionOptionKind::AcBonus, 5, 9001}};
    };
    int used = 0, kept = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto s = begin_encounter({pc(1), pc(4), monster(2)}, seed, nullptr, h);
        set_policy(mut(s, 1), kHit, ReactionPolicy::Auto);
        end_turns_until(s, 2);
        if (current_actor(s) != 2) continue;
        auto r = step(s, {2, ActionKind::Attack, 1});
        if (count(r.events, EventKind::ReactionWindowOpened) == 0) continue;
        const Event* res = nullptr;
        for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) res = &e;
        if (find(s, 1)->reaction_used) { ++used; CHECK((res->c & 1) == 0); }
        else { ++kept; CHECK((res->c & 1) == 1); }
    }
    CHECK(used > 0); CHECK(kept > 0);
}

TEST_CASE("SpellCast: counter stops the (stub) spell; counter-the-counter needs allow_chain") {
    auto counter_opts = [](const EncounterState&, const ReactionWindow&, const Combatant&) {
        return std::vector<ReactionOption>{{0, ReactionOptionKind::Counter, 0, 4242}};
    };
    for (bool chain : {false, true}) {
        EncounterHooks h = everyone_in_range();
        h.spell_cast_options = counter_opts;
        if (chain) h.allow_chain = [](const EncounterState&, const ReactionWindow& w) { return w.trigger == kSpell; };
        auto s = begin_encounter({pc(1), pc(4), monster(2)}, 13, nullptr, h);
        end_turns_until(s, 1);
        REQUIRE(current_actor(s) == 1);
        ActionRequest cast{1, ActionKind::CastSpell, 2};
        cast.spell_id = 77; cast.spell_level = 1;
        CHECK_FALSE(step(s, ActionRequest{1, ActionKind::CastSpell}).ok);   // needs spell_id
        auto r = step(s, cast);
        REQUIRE(r.ok);
        CHECK_FALSE(s.budget.action);
        // First reactor in order after PC 1 counters. If it's the goblin and chaining is on,
        // PC 4 counters the counter, so the original resolves.
        CombatantId first = r.reactions.empty() ? 0 : r.reactions[0].reactor;
        REQUIRE(first != 0);
        bool resolved = count(r.events, EventKind::SpellResolved) == 1;
        if (!chain) {
            CHECK_FALSE(resolved);
            CHECK(r.reactions.size() == 1);
        } else {
            CHECK(first == 2);               // only hostile creatures counter
            CHECK(resolved);                 // goblin's counter was itself countered by a PC
            CHECK(r.reactions.size() == 2);
            CHECK((find(s, 1)->reaction_used || find(s, 4)->reaction_used));
        }
        CHECK(find(s, first)->reaction_used);
    }
}

TEST_CASE("ReadiedTrigger: Ask decline keeps the readied action; accept fires it") {
    EncounterHooks h;
    h.ready_trigger = [](const EncounterState&, const ReadiedAction&, const Event& e) {
        return e.kind == EventKind::Moved && e.actor == 2;
    };
    auto accept = std::make_shared<bool>(false);
    h.decide_reaction = [accept](const EncounterState&, const ReactionWindow& w, const Combatant&,
                                 std::span<const ReactionOption> o) {
        CHECK(w.trigger == kReady);
        CHECK(o.size() == 1);
        return ReactionDecision{0, *accept, 0};
    };
    auto s = begin_encounter({pc(1), monster(2)}, 5, nullptr, h);
    mut(s, 2).hp = mut(s, 2).hp_max = 200;
    end_turns_until(s, 1);
    ActionRequest ready{1, ActionKind::Ready, 2};
    ready.ready_kind = ActionKind::Attack; ready.trigger_id = 3; ready.target_within_5ft = true;
    REQUIRE(step(s, ready).ok);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 2);
    auto r = step(s, {2, ActionKind::Move, 0, 0, 5});
    CHECK(count(r.events, EventKind::ReactionDeclined) == 1);
    CHECK(find(s, 1)->has_readied);
    *accept = true;
    auto r2 = step(s, {2, ActionKind::Move, 0, 0, 5});
    CHECK(count(r2.events, EventKind::ReadyTriggered) == 1);
    CHECK(count(r2.events, EventKind::AttackResolved) == 1);
    CHECK_FALSE(find(s, 1)->has_readied);
    CHECK(find(s, 1)->reaction_used);
}

TEST_CASE("no chaining by default: an opportunity attack's hit doesn't open BeingHit") {
    for (bool chain : {false, true}) {
        EncounterHooks h = everyone_in_range();
        h.being_hit_options = [](const EncounterState&, const ReactionWindow&, const Combatant&) {
            return std::vector<ReactionOption>{{0, ReactionOptionKind::AcBonus, 5, 1}};
        };
        if (chain) h.allow_chain = [](const EncounterState&, const ReactionWindow&) { return true; };
        int nested = 0;
        for (uint64_t seed = 0; seed < 60; ++seed) {
            auto s = begin_encounter({pc(1), monster(2)}, seed, nullptr, h);
            mut(s, 2).hp = mut(s, 2).hp_max = 200; mut(s, 2).ac = -20;   // OA always hits (except nat 1)
            end_turns_until(s, 2);
            if (current_actor(s) != 2) continue;
            auto r = step(s, leave_move(2));
            for (const auto& e : r.events)
                if (e.kind == EventKind::ReactionWindowOpened && e.a == int32_t(kHit)) ++nested;
        }
        if (chain) CHECK(nested > 0); else CHECK(nested == 0);
    }
}

TEST_CASE("replay with reactions: logged decisions reproduce the state bit for bit") {
    auto make_hooks = [](bool with_decider) {
        EncounterHooks h = everyone_in_range();
        h.end_condition = end_when_no_pc_or_no_opponent;
        h.being_hit_options = [](const EncounterState&, const ReactionWindow&, const Combatant& c) {
            std::vector<ReactionOption> o;
            if (c.is_pc) o.push_back({0, ReactionOptionKind::AcBonus, 5, 9001});
            return o;
        };
        h.decide_reaction = nullptr;
        if (with_decider)   // "AI": deterministic but state-dependent choices
            h.decide_reaction = [](const EncounterState& st, const ReactionWindow& w, const Combatant& r,
                                   std::span<const ReactionOption>) {
                return ReactionDecision{0, ((st.round + r.id + w.seq) % 3) != 0, 0};
            };
        return h;
    };
    auto party = [] { return std::vector<Combatant>{pc(1, 40), pc(4, 40), monster(2), monster(3)}; };
    const uint64_t seed = 0xACE5u;
    auto s = begin_encounter(party(), seed, nullptr, make_hooks(true));
    for (auto& c : s.combatants) if (!c.is_pc) c.hp = c.hp_max = 30;
    ActionLog log;
    int asked = 0;
    for (int i = 0; i < 300 && !s.ended; ++i) {
        CombatantId a = current_actor(s);
        const Combatant* me = find(s, a);
        CombatantId foe = 0;
        for (const auto& c : s.combatants) if (!c.dead && c.team != me->team) { foe = c.id; break; }
        ActionRequest req{a, ActionKind::EndTurn};
        if (me->hp <= 0) {}
        else if (s.budget.movement_ft >= 10 && i % 3 == 0) req = leave_move(a);
        else if (s.budget.action && foe) { req = {a, ActionKind::Attack, foe}; req.target_within_5ft = true; }
        auto r = step_logged(s, req, log);
        REQUIRE(r.ok);
        asked += int(r.reactions.size());
    }
    REQUIRE(asked > 5);
    const uint64_t final_hash = hash_state(s);

    auto replay = [&](const ActionLog& l, bool& desync) {
        auto t = begin_encounter(party(), seed, nullptr, make_hooks(false));   // no decider at replay
        for (auto& c : t.combatants) if (!c.is_pc) c.hp = c.hp_max = 30;
        desync = false;
        for (const auto& e : l) desync |= replay_step(t, e).desync;
        return hash_state(t);
    };
    bool desync = true;
    CHECK(replay(log, desync) == final_hash);
    CHECK_FALSE(desync);
    CHECK(replay(log, desync) == final_hash);   // twice, same

    auto flipped = log;
    bool done = false;
    for (auto& e : flipped)
        for (auto& rec : e.reactions)
            if (!done && rec.decision.react) { rec.decision.react = false; done = true; }
    REQUIRE(done);
    CHECK(replay(flipped, desync) != final_hash);

    auto broken = log;
    for (auto& e : broken) if (!e.reactions.empty()) { e.reactions[0].window_seq += 1000; break; }
    (void)replay(broken, desync);
    CHECK(desync);
}
