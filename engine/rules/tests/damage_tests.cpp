#include <doctest/doctest.h>

#include "test_util.hpp"

using namespace rules5e;

TEST_CASE("plain damage reduces hp and clamps at zero") {
    auto c = make_combatant(1, 10, 12, 10);
    auto d = apply_damage(c, 4, DamageType::Fire);
    CHECK(d.dealt == 4); CHECK(d.hp_after == 6); CHECK_FALSE(d.dropped_to_zero);
    d = apply_damage(c, 9, DamageType::Fire);
    CHECK(c.hp == 0); CHECK(d.dropped_to_zero); CHECK_FALSE(d.killed_outright);
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
}

TEST_CASE("temporary hit points absorb damage first") {
    auto c = make_combatant(1, 10, 12, 10);
    c.temp_hp = 5;
    apply_damage(c, 3, DamageType::Force);
    CHECK(c.temp_hp == 2); CHECK(c.hp == 10);
    apply_damage(c, 6, DamageType::Force);
    CHECK(c.temp_hp == 0); CHECK(c.hp == 6);
}

TEST_CASE("massive damage kills outright") {
    auto c = make_combatant(1, 10, 12, 10);
    auto d = apply_damage(c, 19, DamageType::Force);   // 9 over, < 10
    CHECK(d.dropped_to_zero); CHECK_FALSE(d.killed_outright);
    auto c2 = make_combatant(2, 10, 12, 10);
    d = apply_damage(c2, 20, DamageType::Force);       // 10 over == hp_max
    CHECK(d.killed_outright);
    d = apply_damage(c, 10, DamageType::Force);        // already at 0, 10 >= hp_max
    CHECK(d.killed_outright); CHECK_FALSE(d.dropped_to_zero);
}
