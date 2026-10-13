#include <doctest/doctest.h>

#include "rules5e/rules.hpp"

using namespace rules5e;

TEST_CASE("parse_dice accepts standard SRD notation") {
    CHECK(parse_dice("2d6+3").value() == DiceExpr{2, 6, 3});
    CHECK(parse_dice("1d8-1").value() == DiceExpr{1, 8, -1});
    CHECK(parse_dice("d20").value() == DiceExpr{1, 20, 0});
    CHECK(parse_dice("D12").value() == DiceExpr{1, 12, 0});
    CHECK(parse_dice(" 3 d 4 + 2 ").value() == DiceExpr{3, 4, 2});
    CHECK(parse_dice("1d100").value() == DiceExpr{1, 100, 0});
    CHECK(parse_dice("5").value() == DiceExpr{0, 0, 5});
    CHECK(parse_dice("-2").value() == DiceExpr{0, 0, -2});
}

TEST_CASE("parse_dice rejects malformed input") {
    for (const char* bad : {"", "d", "2d", "0d6", "2d0", "256d6", "2d256", "2d6+", "2d6+x", "2d6 3",
                            "2x6", "+d6", "-1d6", "2d6+3+1", "40000", "1d6+40000", "abc"})
        CHECK_MESSAGE(!parse_dice(bad).has_value(), bad);
}

TEST_CASE("roll_dice stays within bounds and doubles dice on crit") {
    rng::Rng r(1, 1);
    DiceExpr e{2, 6, 3};
    CHECK(dice_min(e) == 5);
    CHECK(dice_max(e) == 15);
    bool saw_min = false, saw_max = false;
    for (int i = 0; i < 5000; ++i) {
        int v = roll_dice(r, e);
        CHECK(v >= 5); CHECK(v <= 15);
        saw_min |= v == 5; saw_max |= v == 15;
        int c = roll_dice(r, e, true);       // 4d6+3
        CHECK(c >= 7); CHECK(c <= 27);
    }
    CHECK(saw_min); CHECK(saw_max);
    CHECK(roll_dice(r, DiceExpr{0, 0, 4}) == 4);
    CHECK(roll_dice(r, DiceExpr{0, 0, 4}, true) == 4);  // bonus never doubled
}

TEST_CASE("roll_dice is reproducible from the same seed") {
    rng::Rng a(77, 2), b(77, 2);
    for (int i = 0; i < 100; ++i) CHECK(roll_dice(a, {3, 8, -2}) == roll_dice(b, {3, 8, -2}));
}

TEST_CASE("ability modifiers follow SRD table") {
    static_assert(modifier(1) == -5);
    static_assert(modifier(2) == -4);
    static_assert(modifier(3) == -4);
    static_assert(modifier(8) == -1);
    static_assert(modifier(9) == -1);
    static_assert(modifier(10) == 0);
    static_assert(modifier(11) == 0);
    static_assert(modifier(12) == 1);
    static_assert(modifier(15) == 2);
    static_assert(modifier(20) == 5);
    static_assert(modifier(30) == 10);
    AbilityScores s{{8, 14, 10, 10, 8, 8}};  // goblin
    CHECK(modifier(s, Ability::Dex) == 2);
    CHECK(modifier(s, Ability::Str) == -1);
}

TEST_CASE("roll_d20 advantage/disadvantage picks the right die") {
    rng::Rng r(9, 9);
    for (int i = 0; i < 1000; ++i) {
        auto n = roll_d20(r, 3, RollMode::Normal);
        CHECK(n.natural_other == 0);
        CHECK(n.total == n.natural + 3);
        auto a = roll_d20(r, 0, RollMode::Advantage);
        CHECK(a.natural >= a.natural_other);
        auto d = roll_d20(r, -1, RollMode::Disadvantage);
        CHECK(d.natural <= d.natural_other);
        CHECK(d.total == d.natural - 1);
    }
    // Advantage consumes two draws in order: same rng sequence, max of both.
    rng::Rng x(4, 4), y(4, 4);
    int first = y.d(20), second = y.d(20);
    auto adv = roll_d20(x, 0, RollMode::Advantage);
    CHECK(adv.natural == (first > second ? first : second));
}

TEST_CASE("combine_modes cancels") {
    CHECK(combine_modes(true, true) == RollMode::Normal);
    CHECK(combine_modes(false, false) == RollMode::Normal);
    CHECK(combine_modes(true, false) == RollMode::Advantage);
    CHECK(combine_modes(false, true) == RollMode::Disadvantage);
}
