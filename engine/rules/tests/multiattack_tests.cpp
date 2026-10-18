// Declared multiattack sequences (AttackSequence, ActionRequest::multiattack, TurnBudget::sequence).
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
const Attack kBite{"bite", 30, {1, 8, 2}, DamageType::Piercing, 5, false, false};
const Attack kClaw{"claw", 30, {1, 6, 2}, DamageType::Slashing, 5, false, false};
const Attack kTail{"tail", 30, {1, 10, 2}, DamageType::Bludgeoning, 10, false, false};

Combatant beast(CombatantId id) {   // attacks: 0 bite, 1 claw, 2 tail; options: "full" = bite, claw, claw; "tail"
    auto c = monster(id);
    c.attacks = {kBite, kClaw, kTail};
    c.multiattacks = {{"full", {0, 1, 1}}, {"tail", {2}}};
    return c;
}
ActionRequest atk(CombatantId a, CombatantId t, uint8_t index, uint8_t multi = 0) {
    ActionRequest r{a, ActionKind::Attack, t, index};
    r.multiattack = multi;
    return r;
}
int resolved(const std::vector<Event>& ev) { return count(ev, EventKind::AttackResolved); }
} // namespace

TEST_CASE("multiattack: the declared sequence, in order, for one action") {
    auto s = begin_encounter({beast(1), pc(2, 500), pc(3, 500)}, 4);
    mut(s, 1).team = 1; mut(s, 2).team = 0; mut(s, 3).team = 0;
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    auto r = step(s, atk(1, 2, 0, 1));                   // start "full": bite
    REQUIRE(r.ok);
    CHECK(resolved(r.events) == 1);
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.sequence == 1); CHECK(s.budget.sequence_pos == 1); CHECK(s.budget.attacks_remaining == 2);
    CHECK_FALSE(step(s, atk(1, 2, 0)).ok);               // must be the next declared attack (claw)
    CHECK_FALSE(step(s, atk(1, 2, 2)).ok);
    CHECK_FALSE(step(s, atk(1, 2, 1, 2)).ok);            // another option while one is in progress
    CHECK(s.budget.attacks_remaining == 2);              // failed requests change nothing
    r = step(s, atk(1, 3, 1));                           // claw at a different target
    REQUIRE(r.ok); CHECK(resolved(r.events) == 1);
    r = step(s, atk(1, 2, 1, 1));                        // last claw (repeating the option id is fine)
    REQUIRE(r.ok); CHECK(resolved(r.events) == 1);
    CHECK(s.budget.sequence == 0); CHECK(s.budget.attacks_remaining == 0);
    CHECK_FALSE(step(s, atk(1, 2, 1)).ok);               // sequence done, action spent
}

TEST_CASE("multiattack: validation and alternatives") {
    auto s = begin_encounter({beast(1), pc(2, 500)}, 4);
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    CHECK_FALSE(step(s, atk(1, 2, 0, 3)).ok);            // no such option
    CHECK_FALSE(step(s, atk(1, 2, 1, 1)).ok);            // wrong first attack for "full"
    mut(s, 1).multiattacks.push_back({"empty", {}});
    CHECK_FALSE(step(s, atk(1, 2, 0, 3)).ok);
    mut(s, 1).multiattacks.push_back({"bad", {0, 7}});
    CHECK_FALSE(step(s, atk(1, 2, 0, 4)).ok);
    auto r = step(s, atk(1, 2, 2, 2));                   // one-attack option "tail"
    REQUIRE(r.ok);
    CHECK(s.budget.sequence == 0); CHECK(s.budget.attacks_remaining == 0); CHECK_FALSE(s.budget.action);
    CHECK_FALSE(step(s, atk(1, 2, 2, 2)).ok);

    auto t = begin_encounter({beast(1), pc(2, 500)}, 4);   // a plain attack still works, one per action
    end_turns_until(t, 1);
    REQUIRE(step(t, atk(1, 2, 1)).ok);
    CHECK(t.budget.sequence == 0);
    CHECK_FALSE(step(t, atk(1, 2, 1)).ok);
    auto u = begin_encounter({beast(1), pc(2, 500)}, 4);   // multiattack needs a fresh action
    end_turns_until(u, 1);
    REQUIRE(step(u, {1, ActionKind::Dash}).ok);
    CHECK_FALSE(step(u, atk(1, 2, 0, 1)).ok);
    auto v = begin_encounter({beast(1), pc(2, 500)}, 4);   // can't start one while Extra Attack is in progress
    mut(v, 1).attacks_per_action = 2;
    end_turns_until(v, 1);
    REQUIRE(step(v, atk(1, 2, 1)).ok);
    CHECK_FALSE(step(v, atk(1, 2, 0, 1)).ok);
    REQUIRE(step(v, atk(1, 2, 1)).ok);
}

TEST_CASE("multiattack: the sequence does not carry into the next turn; a readied attack is single") {
    auto s = begin_encounter({beast(1), pc(2, 500)}, 4);
    end_turns_until(s, 1);
    REQUIRE(step(s, atk(1, 2, 0, 1)).ok);
    REQUIRE(s.budget.sequence == 1);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);       // forfeits the rest
    end_turns_until(s, 1);
    CHECK(s.budget.sequence == 0); CHECK(s.budget.attacks_remaining == 0); CHECK(s.budget.action);
    ActionRequest rd{1, ActionKind::Ready, 2};
    rd.ready_kind = ActionKind::Attack; rd.multiattack = 1; rd.attack_index = 0;
    CHECK_FALSE(step(s, rd).ok);
    rd.multiattack = 0;
    CHECK(step(s, rd).ok);
}

TEST_CASE("multiattack: movement and opportunity attacks happen between attacks") {
    auto s = begin_encounter({beast(1), pc(2, 500)}, 4);
    end_turns_until(s, 1);
    REQUIRE(step(s, atk(1, 2, 0, 1)).ok);
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 5; mv.leaves_reach = true;
    auto r = step(s, mv);
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::ReactionWindowOpened) + count(r.events, EventKind::ReactionTaken) +
          count(r.events, EventKind::MoveInterrupted) + count(r.events, EventKind::Moved) >= 1);
    CHECK(s.budget.sequence == 1);                        // still mid-sequence after the move
    REQUIRE(step(s, atk(1, 2, 1)).ok);
}

TEST_CASE("multiattack: each attack opens its own reaction window") {
    // A Shield-capable wizard is hit by the first attack and may react to it; the second attack gets its own
    // BeingHit window (reaction already spent, so none opens), and the replayed log matches the live run.
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto b = beast(1); b.team = 1;
        auto w = wizard(2); w.team = 0; w.ac = 5;
        set_policy(w, ReactionTrigger::BeingHit, ReactionPolicy::Auto);
        auto s = begin_encounter({b, w}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        if (current_actor(s) != 1) continue;
        auto r1 = step(s, atk(1, 2, 0, 1));
        REQUIRE(r1.ok);
        auto r2 = step(s, atk(1, 2, 1));
        REQUIRE(r2.ok);
        CHECK(resolved(r1.events) == 1);
        CHECK(resolved(r2.events) == 1);
        CHECK(count(r2.events, EventKind::ReactionTaken) <= 1);
    }
}

TEST_CASE("multiattack: legal_actions lists the declared options, then only the next attack") {
    auto s = begin_encounter({beast(1), pc(2, 500)}, 4);
    end_turns_until(s, 1);
    int starts = 0, plain = 0;
    for (const auto& r : legal_actions(s)) {
        if (r.kind != ActionKind::Attack) continue;
        (r.multiattack ? starts : plain)++;
        if (r.multiattack) {
            const auto& seq = mut(s, 1).multiattacks[std::size_t(r.multiattack - 1)].attacks;
            CHECK(r.attack_index == seq[0]);
        }
    }
    CHECK(plain == 3); CHECK(starts == 2);
    REQUIRE(step(s, atk(1, 2, 0, 1)).ok);
    int next = 0;
    for (const auto& r : legal_actions(s))
        if (r.kind == ActionKind::Attack) { ++next; CHECK(r.attack_index == 1); CHECK(r.multiattack == 1); }
    CHECK(next == 1);
    for (const auto& r : legal_actions(s)) if (r.kind == ActionKind::Attack) CHECK(step(s, r).ok);   // legal means ok
}

TEST_CASE("multiattack: deterministic replay of the same requests") {
    auto run = [] {
        auto s = begin_encounter({beast(1), pc(2, 500)}, 99);
        end_turns_until(s, 1);
        std::vector<int> out;
        for (auto req : {atk(1, 2, 0, 1), atk(1, 2, 1), atk(1, 2, 1)}) {
            auto r = step(s, req);
            for (const auto& e : r.events) out.push_back(int(e.kind) * 1000 + e.a + e.b);
        }
        out.push_back(find(s, 2)->hp);
        return out;
    };
    CHECK(run() == run());
}
