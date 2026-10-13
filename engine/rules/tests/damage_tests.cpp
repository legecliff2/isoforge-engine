#include <doctest/doctest.h>

#include "test_util.hpp"

using namespace rules5e;

TEST_CASE("apply_damage is pure and clamps at zero") {
    auto c = make_combatant(1, 10, 12, 10);
    const auto before = c;
    auto d = apply_damage(c, 4, DamageType::Fire);
    CHECK(c.hp == before.hp);                 // untouched
    CHECK(d.dealt == 4); CHECK(d.hp_after == 6); CHECK_FALSE(d.dropped_to_zero);
    commit_damage(c, d);
    CHECK(c.hp == 6);
    d = apply_damage(c, 9, DamageType::Fire);
    CHECK(d.hp_after == 0); CHECK(d.dropped_to_zero); CHECK_FALSE(d.instant_death);
    commit_damage(c, d);
    CHECK(c.conditions == 0);                 // never adds conditions
    CHECK_FALSE(c.dead);
    CHECK(apply_damage(c, -5, DamageType::Fire).dealt == 0);
}

TEST_CASE("resistance halves rounding down, vulnerability doubles, immunity zeroes") {
    auto c = make_combatant(1, 10, 12, 100);
    c.resistances = bit(DamageType::Fire);
    c.vulnerabilities = bit(DamageType::Cold) | bit(DamageType::Fire);
    c.immunities = bit(DamageType::Poison);
    CHECK(apply_damage(c, 7, DamageType::Slashing).dealt == 7);
    CHECK(apply_damage(c, 7, DamageType::Cold).dealt == 14);
    CHECK(apply_damage(c, 7, DamageType::Fire).dealt == 6);   // 7/2=3, then *2
    CHECK(apply_damage(c, 7, DamageType::Poison).dealt == 0);
    c.vulnerabilities = 0;
    CHECK(apply_damage(c, 7, DamageType::Fire).dealt == 3);
    add_condition(c, Condition::Petrified);                   // resistance to all damage
    CHECK(apply_damage(c, 9, DamageType::Slashing).dealt == 4);
}

TEST_CASE("temporary hit points absorb damage first") {
    auto c = make_combatant(1, 10, 12, 10);
    c.temp_hp = 5;
    commit_damage(c, apply_damage(c, 3, DamageType::Force));
    CHECK(c.temp_hp == 2); CHECK(c.hp == 10);
    commit_damage(c, apply_damage(c, 6, DamageType::Force));
    CHECK(c.temp_hp == 0); CHECK(c.hp == 6);
}

TEST_CASE("massive damage: instant death when leftover >= HP max") {
    auto c = make_combatant(1, 10, 12, 10);
    auto d = apply_damage(c, 19, DamageType::Force);   // 9 over
    CHECK(d.dropped_to_zero); CHECK_FALSE(d.instant_death);
    d = apply_damage(c, 20, DamageType::Force);        // 10 over == HP max
    CHECK(d.instant_death);
    c.temp_hp = 5;
    d = apply_damage(c, 24, DamageType::Force);        // temp absorbs 5, 9 over
    CHECK_FALSE(d.instant_death);
    c.temp_hp = 0; c.hp = 0;
    CHECK(apply_damage(c, 10, DamageType::Force).instant_death);   // at 0: damage >= HP max
    CHECK_FALSE(apply_damage(c, 9, DamageType::Force).instant_death);
    CHECK_FALSE(apply_damage(c, 9, DamageType::Force).dropped_to_zero);
}
