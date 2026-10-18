// SRD weapon properties (T&C-G18): vocabulary, Heavy, Lance, Loading, Finesse ability choice.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
Attack weapon(uint16_t props, bool ranged = false, bool finesse = false) {
    Attack a{"w", 30, {1, 8, 2}, DamageType::Piercing, 5, ranged, finesse};
    a.properties = props;
    return a;
}
ActionRequest atk(CombatantId a, CombatantId t, uint8_t index) { return ActionRequest{a, ActionKind::Attack, t, index}; }
Combatant tank() { auto c = monster(2); c.hp = c.hp_max = 5000; return c; }
// Fraction of 400 attacks whose d20 is >= 11 (50% flat, ~25% with disadvantage); AC chosen so 11 is the cut.
int high_rolls(const Combatant& a, const Attack& w, bool within5, uint64_t seed) {
    Combatant t = monster(2); t.ac = int8_t(41);   // total = d20 + 30 >= 41  <=> d20 >= 11 (nat 20 always hits)
    rng::Rng r(seed, 0); AttackContext ctx; ctx.target_within_5ft = within5;
    int n = 0;
    for (int i = 0; i < 400; ++i) n += resolve_attack(r, a, w, t, ctx).hit ? 1 : 0;
    return n;
}
} // namespace

TEST_CASE("weapon properties: vocabulary") {
    CHECK(weapon_property_from_string("finesse") == kWeaponFinesse);
    CHECK(weapon_property_from_string("two_handed") == kWeaponTwoHanded);
    CHECK(weapon_property_from_string("two-handed") == kWeaponTwoHanded);
    for (auto n : {"ammunition", "heavy", "light", "loading", "reach", "special", "thrown", "versatile", "lance"})
        CHECK(weapon_property_from_string(n).has_value());
    CHECK_FALSE(weapon_property_from_string("range").has_value());
    CHECK_FALSE(weapon_property_from_string("Finesse").has_value());
    CHECK(has_property(weapon(kWeaponLight | kWeaponFinesse), kWeaponLight));
    CHECK_FALSE(has_property(weapon(kWeaponLight), kWeaponHeavy));
}

TEST_CASE("weapon properties: finesse picks the better of Str and Dex") {
    Combatant c = monster(1);
    c.abilities = AbilityScores{{10, 16, 10, 10, 10, 10}};
    CHECK(attack_ability(c, weapon(0)) == Ability::Str);
    CHECK(attack_ability(c, weapon(kWeaponFinesse)) == Ability::Dex);
    CHECK(attack_ability(c, weapon(0, false, true)) == Ability::Dex);   // legacy flag still works
    CHECK(attack_ability(c, weapon(0, true)) == Ability::Dex);
    CHECK(attack_ability(c, weapon(kWeaponThrown, false)) == Ability::Str);
    c.abilities = AbilityScores{{16, 14, 10, 10, 10, 10}};
    CHECK(attack_ability(c, weapon(kWeaponFinesse)) == Ability::Str);
    c.abilities = AbilityScores{{14, 14, 10, 10, 10, 10}};
    CHECK(attack_ability(c, weapon(kWeaponFinesse)) == Ability::Str);   // tie: Str
}

TEST_CASE("weapon properties: Heavy gives Small and Tiny wielders disadvantage") {
    Combatant c = monster(1);
    auto heavy = weapon(kWeaponHeavy);
    CHECK_FALSE(weapon_disadvantage(c, heavy, false));
    for (Size z : {Size::Tiny, Size::Small}) { c.size = z; CHECK(weapon_disadvantage(c, heavy, false)); CHECK_FALSE(weapon_disadvantage(c, weapon(0), false)); }
    for (Size z : {Size::Medium, Size::Large, Size::Gargantuan}) { c.size = z; CHECK_FALSE(weapon_disadvantage(c, heavy, false)); }
    c.size = Size::Medium; int normal = high_rolls(c, heavy, false, 5);
    c.size = Size::Small;  int dis = high_rolls(c, heavy, false, 5);
    CHECK(normal > 150); CHECK(dis < 140); CHECK(dis < normal);
}

TEST_CASE("weapon properties: Lance has disadvantage only within 5 ft") {
    Combatant c = monster(1);
    auto lance = weapon(kWeaponLance | kWeaponReach);
    CHECK(weapon_disadvantage(c, lance, true));
    CHECK_FALSE(weapon_disadvantage(c, lance, false));
    CHECK(high_rolls(c, lance, true, 9) < high_rolls(c, lance, false, 9));
}

TEST_CASE("weapon properties: Loading fires once per Attack action despite Extra Attack") {
    Combatant a = pc(1, 500);
    a.attacks = {weapon(kWeaponLoading | kWeaponAmmunition, true), weapon(0)};
    a.attacks[0].range_ft = 80; a.attacks[0].long_range_ft = 320; a.attacks[0].reach_or_range_ft = 80;
    a.attacks_per_action = 3;
    auto s = begin_encounter({a, tank()}, 3);
    mut(s, 1).team = 0; mut(s, 2).team = 1;
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    REQUIRE(step(s, atk(1, 2, 0)).ok);
    CHECK(s.budget.attacks_remaining == 2);
    CHECK_FALSE(step(s, atk(1, 2, 0)).ok);               // crossbow already fired this action
    CHECK(s.budget.attacks_remaining == 2);              // a refusal changes nothing
    for (const auto& r : legal_actions(s)) if (r.kind == ActionKind::Attack) CHECK(r.attack_index == 1);
    REQUIRE(step(s, atk(1, 2, 1)).ok);                   // a different weapon is fine
    REQUIRE(step(s, atk(1, 2, 1)).ok);
    CHECK_FALSE(step(s, atk(1, 2, 0)).ok);
    (void)step(s, {1, ActionKind::EndTurn}); end_turns_until(s, 1);   // next round: loaded again
    REQUIRE(step(s, atk(1, 2, 0)).ok);
}

TEST_CASE("weapon properties: non-loading weapons are unrestricted and defaults are inert") {
    Combatant a = pc(1, 500);
    a.attacks = {weapon(kWeaponLight | kWeaponThrown | kWeaponVersatile)};
    a.attacks_per_action = 2;
    auto s = begin_encounter({a, tank()}, 3);
    mut(s, 1).team = 0; mut(s, 2).team = 1;
    end_turns_until(s, 1);
    REQUIRE(step(s, atk(1, 2, 0)).ok);
    REQUIRE(step(s, atk(1, 2, 0)).ok);
    CHECK(Combatant{}.size == Size::Medium);
    CHECK(Attack{}.properties == 0);
}
