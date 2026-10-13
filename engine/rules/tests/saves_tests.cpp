#include <doctest/doctest.h>

#include "test_util.hpp"

using namespace rules5e;

TEST_CASE("save modifier: ability mod + proficiency when proficient") {
    auto c = make_combatant(1, 16);
    c.proficiency_bonus = 2;
    c.save_proficiencies = bit(Ability::Dex);
    CHECK(save_modifier(c, Ability::Dex) == 5);
    CHECK(save_modifier(c, Ability::Str) == 0);
}

TEST_CASE("saving throw totals and success vs DC") {
    auto c = make_combatant(1, 16);
    c.proficiency_bonus = 3;
    c.save_proficiencies = bit(Ability::Dex);
    rng::Rng r(42, 0);
    for (int i = 0; i < 1000; ++i) {
        int dc = 5 + i % 20;
        auto s = saving_throw(r, c, Ability::Dex, dc);
        CHECK_FALSE(s.auto_fail);
        CHECK(s.roll.total == s.roll.natural + 6);
        CHECK(s.success == (s.roll.total >= dc));
    }
}

TEST_CASE("saving throw advantage/disadvantage and dodge") {
    auto c = make_combatant(1);
    rng::Rng r(5, 0);
    auto adv = saving_throw(r, c, Ability::Wis, 10, RollMode::Advantage);
    CHECK(adv.roll.mode == RollMode::Advantage);
    CHECK(adv.roll.natural >= adv.roll.natural_other);
    add_condition(c, Condition::Restrained);
    CHECK(saving_throw(r, c, Ability::Dex, 10).roll.mode == RollMode::Disadvantage);
    CHECK(saving_throw(r, c, Ability::Dex, 10, RollMode::Advantage).roll.mode == RollMode::Normal);
    remove_condition(c, Condition::Restrained);
    c.dodging = true;
    CHECK(saving_throw(r, c, Ability::Dex, 10).roll.mode == RollMode::Advantage);
}

TEST_CASE("auto-fail draws no dice") {
    auto c = make_combatant(1, 20);
    add_condition(c, Condition::Stunned);
    rng::Rng r(1, 1);
    auto before = r.save();
    auto s = saving_throw(r, c, Ability::Dex, 1);
    CHECK(s.auto_fail); CHECK_FALSE(s.success);
    CHECK(r.save().state == before.state);
    auto con = saving_throw(r, c, Ability::Con, 1);
    CHECK_FALSE(con.auto_fail);
}

TEST_CASE("death saves: thresholds, nat 1, nat 20") {
    auto seeded = [](int want) { return rng::Rng(seed_with_first_d20(want), 0); };
    {
        auto r = seeded(20);
        auto res = roll_death_save(r, DeathSaves{1, 2, false});
        CHECK(res.outcome == DeathSaveOutcome::RegainedHp);
        CHECK(res.after.successes == 0); CHECK(res.after.failures == 0);
    }
    {
        auto r = seeded(1);
        auto res = roll_death_save(r, DeathSaves{0, 0, false});
        CHECK(res.outcome == DeathSaveOutcome::Pending);
        CHECK(res.after.failures == 2);
        auto r2 = seeded(1);
        CHECK(roll_death_save(r2, DeathSaves{2, 1, false}).outcome == DeathSaveOutcome::Died);
    }
    {
        auto r = seeded(10);
        auto res = roll_death_save(r, DeathSaves{2, 2, false});
        CHECK(res.outcome == DeathSaveOutcome::Stabilized);
        CHECK(res.after.stable);
    }
    {
        auto r = seeded(9);
        auto res = roll_death_save(r, DeathSaves{2, 2, false});
        CHECK(res.outcome == DeathSaveOutcome::Died);
        CHECK(res.after.failures == 3);
    }
}

TEST_CASE("damage at 0 HP adds failures; crit adds two; ends stability") {
    auto s = death_save_failures_from_damage(DeathSaves{0, 0, true}, false);
    CHECK(s.failures == 1); CHECK_FALSE(s.stable);
    s = death_save_failures_from_damage(s, true);
    CHECK(s.failures == 3);
    s = death_save_failures_from_damage(s, true);
    CHECK(s.failures == 3);   // capped
}
