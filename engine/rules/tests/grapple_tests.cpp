// R-G8: conditions with a source, grapple / shove / escape, drag, size limits.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {

Combatant strong(CombatantId id) { auto c = pc(id, 200); c.team = 0; return c; }
Combatant foe(CombatantId id, Size sz = Size::Medium) {
    auto m = monster(id); m.hp = m.hp_max = 500; m.size = sz; m.team = 1; return m;
}
EncounterState start(Combatant a, Combatant b, uint64_t seed = 1) {
    auto s = begin_encounter({std::move(a), std::move(b)}, seed, nullptr, spell_hooks());
    end_turns_until(s, 1);
    return s;
}
ActionRequest special(uint8_t kind, CombatantId actor, CombatantId target, bool near = true) {
    ActionRequest r{actor, ActionKind::Attack, target};
    r.special = kind; r.target_within_5ft = near;
    return r;
}
std::vector<const Event*> checks(const std::vector<Event>& ev) {
    std::vector<const Event*> out;
    for (const auto& e : ev) if (e.kind == EventKind::CheckRolled) out.push_back(&e);
    return out;
}
bool has_event(const std::vector<Event>& ev, EventKind k) {
    for (const auto& e : ev) if (e.kind == k) return true;
    return false;
}
}  // namespace

TEST_CASE("defaults and size limits") {
    ActionRequest r{1, ActionKind::Attack};
    CHECK(r.special == 0);
    EncounterState s;
    CHECK(s.sourced.empty());
    CHECK_FALSE(has_sourced_conditions(s));
    CHECK(size_within_one(Size::Medium, Size::Medium));
    CHECK(size_within_one(Size::Medium, Size::Large));
    CHECK_FALSE(size_within_one(Size::Medium, Size::Huge));
    CHECK(size_within_one(Size::Huge, Size::Tiny));
    CHECK_FALSE(size_within_one(Size::Small, Size::Huge));
}

TEST_CASE("grapple: Athletics against the better of Athletics / Acrobatics; the attacker must win outright") {
    int won = 0, lost = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        const auto r = step(s, special(1, 1, 2));
        REQUIRE(r.ok);
        const auto c = checks(r.events);
        REQUIRE(c.size() == 2);
        CHECK(c[0]->actor == 1); CHECK(c[1]->actor == 2);
        const bool grappled = c[0]->b > c[1]->b;
        CHECK(s.combatants[1].has(Condition::Grappled) == grappled);
        CHECK(has_event(r.events, EventKind::ConditionAdded) == grappled);
        CHECK((c[1]->c & 1) == int(!grappled));
        if (grappled) {
            ++won;
            CHECK(condition_source(s, 2, Condition::Grappled) == 1);
            REQUIRE(s.sourced.size() == 1);
            CHECK(s.sourced[0].escape_dc == 0);
            CHECK(effective_speed(s.combatants[1]) == 0);
            CHECK(grappled_by(s, 1) == std::vector<CombatantId>{2});
            CHECK(has_sourced_conditions(s));
        } else { ++lost; CHECK(s.sourced.empty()); }
        CHECK_FALSE(s.budget.action);
    }
    CHECK(won > 0); CHECK(lost > 0);
}

TEST_CASE("grapple and shove refuse: too large, out of reach, no such target, charmed, no action") {
    auto s = start(strong(1), foe(2, Size::Huge));
    CHECK_FALSE(step(s, special(1, 1, 2)).ok);   // Huge is two sizes above Medium
    CHECK_FALSE(step(s, special(2, 1, 2)).ok);
    CHECK(s.budget.action);
    auto t = start(strong(1), foe(2, Size::Large));
    CHECK_FALSE(step(t, special(1, 1, 2, false)).ok);   // not within 5 ft
    CHECK_FALSE(step(t, special(1, 1, 1)).ok);          // self
    CHECK_FALSE(step(t, special(1, 1, 9)).ok);
    CHECK_FALSE(step(t, special(7, 1, 2)).ok);          // bad kind
    CHECK(step(t, special(1, 1, 2)).ok);                // Large is one size up: allowed
    CHECK_FALSE(step(t, special(1, 1, 2)).ok);          // the action is spent
    auto u = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(apply_sourced_condition(u, 2, 1, Condition::Charmed, 0, sink));
    CHECK_FALSE(step(u, special(1, 1, 2)).ok);
    CHECK(u.budget.action);
    ActionRequest attack{1, ActionKind::Attack, 2}; attack.target_within_5ft = true;
    CHECK_FALSE(step(u, attack).ok);                    // a charmed creature cannot attack its charmer
}

TEST_CASE("grapple and shove replace one attack of the Attack action (Extra Attack)") {
    auto c = strong(1); c.attacks_per_action = 2;
    auto s = start(c, foe(2));
    REQUIRE(step(s, special(1, 1, 2)).ok);
    CHECK_FALSE(s.budget.action);
    CHECK(s.budget.attacks_remaining == 1);
    REQUIRE(step(s, special(2, 1, 2)).ok);
    CHECK(s.budget.attacks_remaining == 0);
    CHECK_FALSE(step(s, special(2, 1, 2)).ok);
}

TEST_CASE("shove: prone, or pushed 5 ft (a Moved event for the target)") {
    int prone = 0, pushed = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        auto r = step(s, special(2, 1, 2));
        const auto c = checks(r.events);
        REQUIRE(c.size() == 2);
        const bool win = c[0]->b > c[1]->b;
        CHECK(s.combatants[1].has(Condition::Prone) == win);
        prone += win;
        auto t = start(strong(1), foe(2), seed);
        auto q = step(t, special(3, 1, 2));
        const auto d = checks(q.events);
        REQUIRE(d.size() == 2);
        const bool win2 = d[0]->b > d[1]->b;
        const Event* mv = nullptr;
        for (const auto& e : q.events) if (e.kind == EventKind::Moved) mv = &e;
        CHECK((mv != nullptr) == win2);
        if (mv) { CHECK(mv->actor == 2); CHECK(mv->target == 1); CHECK(mv->a == 5); ++pushed; }
        CHECK(t.sourced.empty());
    }
    CHECK(prone > 0); CHECK(pushed > 0);
}

TEST_CASE("escape: against the recorded DC, or a contest against the grappler") {
    int freed = 0, held = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        std::vector<Event> sink;
        REQUIRE(grapple(s, 1, 2, 12, sink));
        CHECK(s.sourced[0].escape_dc == 12);
        end_turns_until(s, 2);
        const auto r = step(s, special(4, 2, 1));
        REQUIRE(r.ok);
        const auto c = checks(r.events);
        REQUIRE(c.size() == 1);
        const bool free = c[0]->b >= 12;
        CHECK(s.combatants[1].has(Condition::Grappled) == !free);
        CHECK(has_event(r.events, EventKind::ConditionRemoved) == free);
        CHECK(s.sourced.empty() == free);
        (free ? freed : held)++;
    }
    CHECK(freed > 0); CHECK(held > 0);
    freed = held = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start(strong(1), foe(2), seed);
        std::vector<Event> sink;
        REQUIRE(grapple(s, 1, 2, 0, sink));
        end_turns_until(s, 2);
        const auto r = step(s, special(4, 2, 0));   // any grappler
        REQUIRE(r.ok);
        const auto c = checks(r.events);
        REQUIRE(c.size() == 2);
        CHECK(c[0]->actor == 2); CHECK(c[1]->actor == 1);
        const bool free = c[0]->b > c[1]->b;
        CHECK(s.combatants[1].has(Condition::Grappled) == !free);
        (free ? freed : held)++;
    }
    CHECK(freed > 0); CHECK(held > 0);
}

TEST_CASE("escape refusals") {
    auto s = start(strong(1), foe(2));
    end_turns_until(s, 2);
    CHECK_FALSE(step(s, special(4, 2, 1)).ok);   // not grappled
    s.combatants[1].conditions |= bit(Condition::Grappled);   // grappled by something with no recorded source
    CHECK_FALSE(step(s, special(4, 2, 1)).ok);
    auto r = special(4, 2, 1); r.dc = 10;
    CHECK(step(s, r).ok);
    CHECK_FALSE(s.budget.action);
    auto t = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(grapple(t, 1, 2, 10, sink));
    end_turns_until(t, 2);
    CHECK_FALSE(step(t, special(4, 2, 7)).ok);   // not held by creature 7
    CHECK(t.budget.action);
}

TEST_CASE("a grapple ends when the grappler is incapacitated or dies") {
    auto s = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(grapple(s, 1, 2, 0, sink));
    s.combatants[0].conditions |= bit(Condition::Stunned);
    std::vector<Event> ev;
    prune_sourced(s, ev);
    CHECK_FALSE(s.combatants[1].has(Condition::Grappled));
    CHECK(s.sourced.empty());
    CHECK(has_event(ev, EventKind::ConditionRemoved));
    // a second holder keeps the condition
    auto t = start(strong(1), foe(2));
    REQUIRE(apply_sourced_condition(t, 1, 2, Condition::Grappled, 0, sink));
    t.combatants.push_back(foe(3));
    REQUIRE(apply_sourced_condition(t, 3, 2, Condition::Grappled, 0, sink));
    t.combatants[0].conditions |= bit(Condition::Stunned);
    prune_sourced(t, ev);
    CHECK(t.combatants[1].has(Condition::Grappled));
    CHECK(t.sourced.size() == 1);
    CHECK(t.sourced[0].source == 3);
    // an entry whose condition has gone is dropped quietly
    t.combatants[1].conditions = 0;
    prune_sourced(t, ev);
    CHECK(t.sourced.empty());
}

TEST_CASE("moving a grappled creature costs double unless it is two sizes smaller") {
    auto s = start(strong(1), foe(2));
    std::vector<Event> sink;
    REQUIRE(grapple(s, 1, 2, 0, sink));
    const int before = s.budget.movement_ft;
    ActionRequest mv{1, ActionKind::Move}; mv.move_ft = 10;
    REQUIRE(step(s, mv).ok);
    CHECK(before - s.budget.movement_ft == 20);
    auto t = start(strong(1), foe(2, Size::Tiny));
    REQUIRE(grapple(t, 1, 2, 0, sink));
    const int b2 = t.budget.movement_ft;
    REQUIRE(step(t, mv).ok);
    CHECK(b2 - t.budget.movement_ft == 10);
    auto u = start(strong(1), foe(2));
    const int b3 = u.budget.movement_ft;
    REQUIRE(step(u, mv).ok);
    CHECK(b3 - u.budget.movement_ft == 10);
}

TEST_CASE("grapple() refuses a too-large, immune, dead or self target and leaves no entry") {
    auto s = start(strong(1), foe(2, Size::Gargantuan));
    std::vector<Event> ev;
    CHECK_FALSE(grapple(s, 1, 2, 0, ev));
    CHECK_FALSE(grapple(s, 1, 1, 0, ev));
    CHECK_FALSE(grapple(s, 1, 9, 0, ev));
    CHECK(s.sourced.empty());
    auto t = start(strong(1), foe(2));
    t.combatants[1].condition_immunities |= bit(Condition::Grappled);
    CHECK_FALSE(grapple(t, 1, 2, 0, ev));
    CHECK(ev.empty());
    auto u = start(strong(1), foe(2));
    u.combatants[1].dead = true;
    CHECK_FALSE(grapple(u, 1, 2, 0, ev));
}

TEST_CASE("sourced_digest folds every field and is order-stable") {
    auto s = start(strong(1), foe(2));
    std::vector<Event> ev;
    REQUIRE(grapple(s, 1, 2, 12, ev));
    const uint64_t d1 = sourced_digest(s);
    CHECK(d1 != sourced_digest(EncounterState{}));
    CHECK(sourced_digest(s) == d1);
    REQUIRE(grapple(s, 1, 2, 13, ev));   // same key: the DC is updated, not duplicated
    CHECK(s.sourced.size() == 1);
    CHECK(sourced_digest(s) != d1);
}
