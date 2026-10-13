#include <doctest/doctest.h>

#include "golden_hash.hpp"
#include "test_util.hpp"

using namespace rules5e;

namespace {

const Attack kLongsword{"longsword", 5, {1, 8, 3}, DamageType::Slashing, 5, false, false};
const Attack kScimitar{"scimitar", 4, {1, 6, 2}, DamageType::Slashing, 5, false, true};

Combatant fighter(CombatantId id) {
    auto c = make_combatant(id, 14, 16, 28);
    c.team = 0; c.speed_ft = 30; c.proficiency_bonus = 2;
    c.uses_death_saves = true;
    c.is_pc = true;
    c.attacks = {kLongsword};
    return c;
}
Combatant goblin(CombatantId id) {
    auto c = make_combatant(id, 14, 15, 7);
    c.team = 1; c.speed_ft = 30;
    c.bonus_action_grants = bit(ActionKind::Disengage) | bit(ActionKind::Hide);  // Nimble Escape
    c.attacks = {kScimitar};
    return c;
}

// Canonical little-endian serialisation of everything that is encounter state.
uint64_t state_hash(const EncounterState& s) {
    GoldenHash g;
    g.u32(uint32_t(s.combatants.size()));
    for (const auto& c : s.combatants) {
        g.u32(c.id); g.byte(c.team);
        g.i32(c.hp); g.i32(c.hp_max); g.i32(c.temp_hp);
        g.u32(c.conditions); g.byte(c.exhaustion_level);
        g.byte(c.death.successes); g.byte(c.death.failures); g.byte(c.death.stable);
        g.byte(c.dead); g.byte(c.reaction_used); g.byte(c.dodging); g.byte(c.disengaged);
    }
    for (const auto& e : s.order) { g.u32(e.id); g.i32(e.total); g.i32(e.dex_mod); }
    g.u32(s.round); g.u32(s.turn_index);
    g.byte(s.budget.action); g.byte(s.budget.bonus_action); g.byte(s.budget.reaction);
    g.i32(s.budget.movement_ft);
    auto st = s.rng.save(); g.u64(st.state); g.u64(st.inc);
    g.byte(s.ended);
    return g.h;
}

std::vector<Combatant> party() { return {fighter(1), goblin(2), goblin(3)}; }

// Simple deterministic policy: move 5 ft, attack the first enemy, Nimble Escape if granted, end.
// Pinned explicitly (not the default) so the replay golden is independent of the default
// end-condition policy, which became team-based after this golden was recorded.
EncounterHooks replay_hooks() {
    EncounterHooks h;
    h.end_condition = end_when_no_pc_or_no_opponent;
    return h;
}

std::vector<ActionRequest> record_log(uint64_t seed, int max_steps) {
    auto s = begin_encounter(party(), seed, nullptr, replay_hooks());
    std::vector<ActionRequest> log;
    for (int i = 0; i < max_steps && !s.ended; ++i) {
        auto legal = legal_actions(s);
        ActionRequest pick = legal.back();   // EndTurn
        for (const auto& a : legal) {
            if (a.kind == ActionKind::Move && s.budget.movement_ft >= 25) { pick = a; break; }
            if (a.kind == ActionKind::StandUp) { pick = a; break; }
            if (a.kind == ActionKind::Attack) { pick = a; pick.target_within_5ft = true; break; }
            if (a.kind == ActionKind::Disengage && a.as_bonus_action) { pick = a; break; }
        }
        log.push_back(pick);
        REQUIRE(step(s, pick).ok);
    }
    return log;
}

EncounterState replay(uint64_t seed, const std::vector<ActionRequest>& log) {
    auto s = begin_encounter(party(), seed, nullptr, replay_hooks());
    for (const auto& r : log) (void)step(s, r);   // invalid requests are no-ops by contract
    return s;
}

EncounterState at_turn_of(CombatantId id, uint64_t seed = 1, std::vector<Combatant> cs = party()) {
    auto s = begin_encounter(std::move(cs), seed);
    for (int i = 0; i < 10 && current_actor(s) != id; ++i)
        REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    REQUIRE(current_actor(s) == id);
    return s;
}

Combatant& mut(EncounterState& s, CombatantId id) {
    for (auto& c : s.combatants) if (c.id == id) return c;
    FAIL("no combatant"); return s.combatants[0];
}

} // namespace

TEST_CASE("begin_encounter rolls initiative and starts the first turn") {
    std::vector<Event> ev;
    auto s = begin_encounter(party(), 99, &ev);
    CHECK(s.order.size() == 3);
    CHECK(s.round == 1);
    CHECK(s.budget.action); CHECK(s.budget.bonus_action); CHECK(s.budget.reaction);
    CHECK(s.budget.movement_ft == 30);
    REQUIRE(ev.size() >= 4);
    CHECK(ev[3].kind == EventKind::TurnStarted);
}

TEST_CASE("action economy: one action, bonus only when granted, movement budget") {
    auto s = at_turn_of(1);
    CHECK_FALSE(step(s, {1, ActionKind::Move, 0, 0, 35}).ok);
    CHECK(step(s, {1, ActionKind::Move, 0, 0, 10}).ok);
    CHECK(s.budget.movement_ft == 20);
    CHECK(step(s, {1, ActionKind::Dash}).ok);
    CHECK(s.budget.movement_ft == 50);
    CHECK_FALSE(s.budget.action);
    CHECK_FALSE(step(s, {1, ActionKind::Attack, 2}).ok);              // action spent
    CHECK_FALSE(step(s, {1, ActionKind::Disengage, 0, 0, 0, true}).ok);  // no grant
    CHECK(s.budget.bonus_action);
    CHECK_FALSE(step(s, {2, ActionKind::EndTurn}).ok);               // not goblin's turn
    CHECK(step(s, {1, ActionKind::EndTurn}).ok);
    CHECK(current_actor(s) != 1);

    auto g = at_turn_of(2);
    CHECK(step(g, {2, ActionKind::Disengage, 0, 0, 0, true}).ok);     // Nimble Escape
    CHECK_FALSE(g.budget.bonus_action);
    CHECK(g.budget.action);
    CHECK_FALSE(step(g, {2, ActionKind::Disengage, 0, 0, 0, true}).ok);
    CHECK_FALSE(step(g, {2, ActionKind::Dash, 0, 0, 0, true}).ok);    // Dash not granted
}

TEST_CASE("invalid requests change nothing") {
    auto s = at_turn_of(1);
    auto h = state_hash(s);
    CHECK_FALSE(step(s, {1, ActionKind::Attack, 1}).ok);    // self
    CHECK_FALSE(step(s, {1, ActionKind::Attack, 77}).ok);   // missing
    CHECK_FALSE(step(s, {1, ActionKind::Attack, 2, 5}).ok); // bad attack index
    CHECK_FALSE(step(s, {1, ActionKind::Move, 0, 0, 0}).ok);
    CHECK_FALSE(step(s, {1, ActionKind::StandUp}).ok);      // not prone
    CHECK_FALSE(step(s, {1, ActionKind::Help}).ok);         // not implemented
    CHECK(state_hash(s) == h);
}

TEST_CASE("conditions shape the movement budget") {
    auto s = at_turn_of(1);
    mut(s, 1).conditions = bit(Condition::Prone);
    CHECK_FALSE(step(s, {1, ActionKind::Move, 0, 0, 20}).ok);   // crawl costs double
    CHECK(step(s, {1, ActionKind::Move, 0, 0, 5}).ok);
    CHECK(s.budget.movement_ft == 20);
    CHECK(step(s, {1, ActionKind::StandUp}).ok);                // half speed
    CHECK(s.budget.movement_ft == 5);
    CHECK_FALSE(mut(s, 1).has(Condition::Prone));

    auto g = begin_encounter(party(), 1);
    for (auto& c : g.combatants) add_condition(c, Condition::Grappled);
    // Re-start turn by cycling: grappled -> speed 0 at turn start.
    REQUIRE(step(g, {current_actor(g), ActionKind::EndTurn}).ok);
    CHECK(g.budget.movement_ft == 0);
    CHECK(g.budget.action);
    CHECK(step(g, {current_actor(g), ActionKind::Dash}).ok);
    CHECK(g.budget.movement_ft == 0);                           // Dash adds speed 0

    auto st = begin_encounter(party(), 1);
    for (auto& c : st.combatants) add_condition(c, Condition::Stunned);
    REQUIRE(step(st, {current_actor(st), ActionKind::EndTurn}).ok);
    CHECK_FALSE(st.budget.action); CHECK_FALSE(st.budget.bonus_action); CHECK_FALSE(st.budget.reaction);
    CHECK_FALSE(step(st, {current_actor(st), ActionKind::Dodge}).ok);
}

TEST_CASE("PC at 0 HP: Unconscious + Prone, death saves at turn start; damage adds failures") {
    // Second PC keeps team 0 standing so the encounter continues while PC 1 is dying.
    auto s = at_turn_of(2, 1, {fighter(1), fighter(4), goblin(2), goblin(3)});
    auto& pc = mut(s, 1);
    pc.hp = 1; pc.ac = -20;                      // goblin hits unless nat 1
    StepResult r{false, {}, {}};
    for (int tries = 0; tries < 50; ++tries) {   // retry until a hit (nat 1 misses)
        auto copy = s;
        r = step(copy, {2, ActionKind::Attack, 1, 0, 0, false, false});
        if (find(copy, 1)->hp == 0) { s = copy; break; }
        s.rng = rng::Rng(uint64_t(tries) + 1000, 0);
    }
    REQUIRE(r.ok);
    const Combatant* p = find(s, 1);
    REQUIRE(p->hp == 0);
    CHECK_FALSE(p->dead);
    CHECK(p->has(Condition::Unconscious));
    CHECK(p->has(Condition::Prone));
    CHECK(p->death.successes == 0); CHECK(p->death.failures == 0);

    // Goblin 3 hits from range (no auto-crit): +1 failure. Within 5 ft -> auto-crit, +2.
    while (current_actor(s) != 3) REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    // The PC rolled a death save at its own turn start; capture failures after that.
    p = find(s, 1);
    if (!p->dead && p->hp == 0) {
        int before = p->death.failures;
        auto copy = s;
        auto ar = step(copy, {3, ActionKind::Attack, 1, 0, 0, false, true});
        REQUIRE(ar.ok);
        const Combatant* q = find(copy, 1);
        bool hit = false;
        for (const auto& e : ar.events) if (e.kind == EventKind::AttackResolved) hit = e.c & 1;
        if (hit) {
            const bool two_failures = q->dead || q->death.failures == std::min(3, before + 2);
            CHECK(two_failures);
        }
    }
    CHECK_FALSE(s.ended);   // PC 4 is still conscious
}

TEST_CASE("monsters die at 0 HP; opt-in policy ends when no opponent is conscious") {
    auto s = at_turn_of(1);
    s.hooks.end_condition = end_when_no_pc_or_no_opponent;
    mut(s, 2).hp = 0; mut(s, 2).dead = true;
    mut(s, 3).hp = 1; mut(s, 3).ac = -20;
    for (int tries = 0; tries < 50 && !s.ended; ++tries) {
        auto copy = s;
        auto r = step(copy, {1, ActionKind::Attack, 3});
        REQUIRE(r.ok);
        if (find(copy, 3)->dead) { s = copy; break; }
        s.rng = rng::Rng(uint64_t(tries) + 7, 0);
    }
    CHECK(find(s, 3)->dead);
    CHECK_FALSE(find(s, 3)->has(Condition::Unconscious));
    CHECK(s.ended);
    REQUIRE(s.outcome.has_value());
    CHECK(s.outcome->reason == EndReason::NoOpponentConscious);
    REQUIRE(s.outcome->pcs.size() == 1);
    CHECK(s.outcome->pcs[0].state == PcState::Conscious);
    CHECK_FALSE(step(s, {1, ActionKind::EndTurn}).ok);
}

TEST_CASE("reactions: one opportunity attack per round, not vs Disengage, not on own turn") {
    auto s = at_turn_of(2);
    CHECK_FALSE(step(s, {2, ActionKind::OpportunityAttack, 1}).ok);   // own turn
    CHECK(step(s, {1, ActionKind::OpportunityAttack, 2, 0, 0, false, true}).ok);
    CHECK(find(s, 1)->reaction_used);
    CHECK_FALSE(step(s, {1, ActionKind::OpportunityAttack, 2}).ok);   // reaction spent
    auto g = at_turn_of(3);
    if (!find(g, 3)->dead) {
        REQUIRE(step(g, {3, ActionKind::Disengage, 0, 0, 0, true}).ok);
        CHECK_FALSE(step(g, {1, ActionKind::OpportunityAttack, 3}).ok);
    }
    // Reaction refreshes at the start of the reactor's own turn.
    while (current_actor(s) != 1) REQUIRE(step(s, {current_actor(s), ActionKind::EndTurn}).ok);
    CHECK_FALSE(find(s, 1)->reaction_used);
}

TEST_CASE("replay: same seed + log -> same state; different seed or log -> different") {
    const uint64_t seed = 0xE11C0u;
    auto log = record_log(seed, 200);
    REQUIRE(log.size() > 10);
    auto a = replay(seed, log);
    auto b = replay(seed, log);
    CHECK(state_hash(a) == state_hash(b));

    CHECK(state_hash(replay(seed + 1, log)) != state_hash(a));

    auto altered = log;   // first attack becomes a Dodge: fewer dice drawn, different outcome
    for (auto& r : altered) if (r.kind == ActionKind::Attack) { r = {r.actor, ActionKind::Dodge}; break; }
    CHECK(state_hash(replay(seed, altered)) != state_hash(a));

    auto shorter = log; shorter.pop_back();
    CHECK(state_hash(replay(seed, shorter)) != state_hash(a));
}

TEST_CASE("golden: encounter replay final state hash") {
    const uint64_t seed = 0xE11C0u;
    auto log = record_log(seed, 200);
    auto s = replay(seed, log);
    CHECK(s.ended);
    CHECK(log.size() == 43u);
    CHECK(state_hash(s) == 0x897B63640DB62C83ull);
}

TEST_CASE("dying PC rolls a death save at the start of each of its turns until resolved") {
    auto s = begin_encounter({fighter(1), fighter(4), goblin(2), goblin(3)}, 321);
    auto& pc = mut(s, 1);
    pc.hp = 0;
    add_condition(pc, Condition::Unconscious);
    int saves = 0;
    bool resolved = false;
    for (int i = 0; i < 40 && !resolved && !s.ended; ++i) {
        auto r = step(s, {current_actor(s), ActionKind::EndTurn});
        REQUIRE(r.ok);
        for (const auto& e : r.events) {
            if (e.kind == EventKind::DeathSaveRolled && e.actor == 1) {
                ++saves;
                CHECK(e.a >= 1); CHECK(e.a <= 20);
            }
            if (e.actor == 1 && (e.kind == EventKind::Stabilized || e.kind == EventKind::CombatantDied ||
                                 e.kind == EventKind::RegainedHp)) resolved = true;
        }
        if (current_actor(s) == 1 && find(s, 1)->hp == 0 && !find(s, 1)->dead) {
            CHECK_FALSE(s.budget.action);
            CHECK(s.budget.movement_ft == 0);
        }
    }
    CHECK(resolved);
    CHECK(saves >= 2);   // at least two saves to reach 3 of anything (nat 20 aside: then >= 1)
    const Combatant* p = find(s, 1);
    if (p->dead) CHECK(p->death.failures == 3);
    else if (p->hp == 1) CHECK_FALSE(p->has(Condition::Unconscious));
    else CHECK(p->death.stable);
}
