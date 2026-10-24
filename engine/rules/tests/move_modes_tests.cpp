// SRD 5.1 movement modes (feat/move-modes): ActionRequest::move_mode, move_allowance, Dash, prone flight.
#include <doctest/doctest.h>

#include "encounter_util.hpp"

using namespace enc;

namespace {
EncounterState start(uint8_t fly) {
    auto a = pc(1), b = monster(2);
    a.fly_ft = fly;
    auto s = begin_encounter({a, b}, 3);
    end_turns_until(s, 1);
    return s;
}
ActionRequest move(int ft, uint8_t mode) {
    ActionRequest r{1, ActionKind::Move};
    r.move_ft = int16_t(ft); r.move_tiles = uint8_t(ft / 5); r.move_mode = mode;
    return r;
}
}  // namespace

TEST_CASE("move modes: defaults and no such speed") {
    ActionRequest q{}; CHECK(q.move_mode == 0);
    Combatant c{}; CHECK(c.fly_ft == 0); CHECK(c.swim_ft == 0); CHECK(c.climb_ft == 0); CHECK(c.burrow_ft == 0); CHECK_FALSE(c.hover);
    CHECK(FormStats{}.burrow_ft == 0);
    auto s = start(0);
    CHECK(move_allowance(s, *find(s, 1), 1) == -1);
    CHECK(step(s, move(5, 1)).error == "no such speed");
    CHECK(move_allowance(s, *find(s, 1), 4) == -1);   // altitude PR2: burrow needs a burrow speed
    CHECK(step(s, move(5, 4)).error == "no such speed");
    CHECK(step(s, move(5, 5)).error == "bad move_mode");
}

TEST_CASE("move modes: switching speeds shares the budget; Dash grants each speed again") {
    auto s = start(60);
    CHECK(move_allowance(s, *find(s, 1), 0) == 30);
    CHECK(move_allowance(s, *find(s, 1), 1) == 60);
    REQUIRE(step(s, move(20, 0)).ok);
    CHECK(move_allowance(s, *find(s, 1), 1) == 40);
    REQUIRE(step(s, move(30, 1)).ok);
    CHECK(s.budget.movement_ft == -20);
    CHECK(move_allowance(s, *find(s, 1), 1) == 10);
    CHECK(step(s, move(5, 0)).error == "not enough movement");
    REQUIRE(step(s, {1, ActionKind::Dash}).ok);
    CHECK(move_allowance(s, *find(s, 1), 0) == 10);
    CHECK(move_allowance(s, *find(s, 1), 1) == 70);
}

TEST_CASE("move modes: a prone creature cannot fly") {
    auto s = start(60);
    add_condition(mut(s, 1), Condition::Prone);
    CHECK(step(s, move(5, 1)).error == "cannot fly while prone");
}

TEST_CASE("burrow speed: a form replaces and restores it like fly, and a copy matches") {
    Combatant c{};
    c.hp = c.hp_max = 20;
    c.burrow_ft = 20;
    c.fly_ft = 40;
    FormStats beast;
    beast.hp_max = 11;
    beast.burrow_ft = 10;
    beast.fly_ft = 0;
    REQUIRE(apply_form(c, FormKind::Polymorph, beast));
    CHECK(c.burrow_ft == 10);
    CHECK(c.fly_ft == 0);
    REQUIRE(c.base);
    CHECK(c.base->burrow_ft == 20);
    CHECK(c.base->fly_ft == 40);
    const Combatant saved = c;
    CHECK(saved.burrow_ft == c.burrow_ft);
    CHECK(saved.base->burrow_ft == c.base->burrow_ft);
    CHECK(saved.fly_ft == c.fly_ft);
    REQUIRE(revert_form(c));
    CHECK(c.burrow_ft == 20);
    CHECK(c.fly_ft == 40);
    CHECK_FALSE(c.base);
}

TEST_CASE("burrow speed: replay keeps it; the digest ignores it the same way as fly, swim and climb") {
    auto make = [] {
        auto a = pc(1), b = monster(2);
        a.burrow_ft = 15;
        return begin_encounter({a, b}, 3);
    };
    auto live = make();
    auto again = make();
    end_turns_until(live, 1);
    end_turns_until(again, 1);
    ActionLog log;
    REQUIRE(step_logged(live, move(10, 0), log).ok);
    REQUIRE(step_logged(live, {1, ActionKind::EndTurn}, log).ok);
    for (const auto& ls : log) {
        auto r = replay_step(again, ls);
        REQUIRE(r.ok);
        CHECK_FALSE(r.desync);
    }
    CHECK(find(live, 1)->burrow_ft == 15);
    CHECK(find(again, 1)->burrow_ft == find(live, 1)->burrow_ft);
    CHECK(find(again, 1)->fly_ft == find(live, 1)->fly_ft);
    CHECK(find(again, 1)->swim_ft == find(live, 1)->swim_ft);
    CHECK(find(again, 1)->climb_ft == find(live, 1)->climb_ft);
    // Static content, not folded: burrow moves no digest, and neither do the other speeds.
    const uint64_t h = summons_digest(live);
    mut(live, 1).burrow_ft = 40;
    CHECK(summons_digest(live) == h);
    mut(live, 1).fly_ft = 60;
    CHECK(summons_digest(live) == h);
    mut(live, 1).swim_ft = 30;
    CHECK(summons_digest(live) == h);
    mut(live, 1).climb_ft = 30;
    CHECK(summons_digest(live) == h);
    auto third = make();
    end_turns_until(third, 1);
    for (const auto& ls : log) REQUIRE(replay_step(third, ls).ok);
    CHECK(summons_digest(third) == summons_digest(again));
    CHECK(find(third, 1)->burrow_ft == 15);
}
