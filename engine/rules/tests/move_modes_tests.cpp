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
    Combatant c{}; CHECK(c.fly_ft == 0); CHECK(c.swim_ft == 0); CHECK(c.climb_ft == 0); CHECK_FALSE(c.hover);
    auto s = start(0);
    CHECK(move_allowance(s, *find(s, 1), 1) == -1);
    CHECK(step(s, move(5, 1)).error == "no such speed");
    CHECK(step(s, move(5, 4)).error == "bad move_mode");
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
