#include <doctest/doctest.h>

#include "test_util.hpp"

using namespace rules5e;
using C = Condition;

TEST_CASE("incapacitating conditions") {
    for (C k : {C::Incapacitated, C::Paralyzed, C::Petrified, C::Stunned, C::Unconscious}) {
        auto c = make_combatant(1);
        add_condition(c, k);
        CHECK(is_incapacitated(c));
    }
    for (C k : {C::Blinded, C::Charmed, C::Deafened, C::Frightened, C::Grappled, C::Invisible,
                C::Poisoned, C::Prone, C::Restrained}) {
        auto c = make_combatant(1);
        add_condition(c, k);
        CHECK_FALSE(is_incapacitated(c));
    }
}

TEST_CASE("speed 0 conditions and exhaustion") {
    auto c = make_combatant(1); c.speed_ft = 30;
    CHECK(effective_speed(c) == 30);
    for (C k : {C::Grappled, C::Restrained, C::Paralyzed, C::Petrified, C::Stunned, C::Unconscious}) {
        auto x = c; add_condition(x, k);
        CHECK(effective_speed(x) == 0);
    }
    auto p = c; add_condition(p, C::Prone); CHECK(effective_speed(p) == 30);
    c.exhaustion_level = 1; CHECK(effective_speed(c) == 30);
    c.exhaustion_level = 2; CHECK(effective_speed(c) == 15);
    c.exhaustion_level = 5; CHECK(effective_speed(c) == 0);
}

TEST_CASE("exhaustion levels") {
    auto c = make_combatant(1, 10, 12, 11);
    CHECK(check_roll_mode(c) == RollMode::Normal);
    c.exhaustion_level = 1; CHECK(check_roll_mode(c) == RollMode::Disadvantage);
    CHECK(save_roll_mode(c, Ability::Con) == RollMode::Normal);
    c.exhaustion_level = 3;
    CHECK(save_roll_mode(c, Ability::Con) == RollMode::Disadvantage);
    auto t = make_combatant(2);
    CHECK(attack_roll_mode(c, t, {}) == RollMode::Disadvantage);
    CHECK(effective_hp_max(c) == 11);
    c.exhaustion_level = 4; CHECK(effective_hp_max(c) == 5);
    CHECK_FALSE(dies_of_exhaustion(c));
    c.exhaustion_level = 6; CHECK(dies_of_exhaustion(c));
}

TEST_CASE("auto-fail Str/Dex saves") {
    for (C k : {C::Paralyzed, C::Petrified, C::Stunned, C::Unconscious}) {
        auto c = make_combatant(1); add_condition(c, k);
        CHECK(auto_fails_save(c, Ability::Str));
        CHECK(auto_fails_save(c, Ability::Dex));
        CHECK_FALSE(auto_fails_save(c, Ability::Con));
        CHECK_FALSE(auto_fails_save(c, Ability::Wis));
    }
    auto r = make_combatant(1); add_condition(r, C::Restrained);
    CHECK_FALSE(auto_fails_save(r, Ability::Dex));
    CHECK(save_roll_mode(r, Ability::Dex) == RollMode::Disadvantage);
    CHECK(save_roll_mode(r, Ability::Str) == RollMode::Normal);
}

TEST_CASE("ability check disadvantage: poisoned, frightened") {
    for (C k : {C::Poisoned, C::Frightened}) {
        auto c = make_combatant(1); add_condition(c, k);
        CHECK(check_roll_mode(c) == RollMode::Disadvantage);
        CHECK(check_roll_mode(c, RollMode::Advantage) == RollMode::Normal);
    }
}

TEST_CASE("attack modes: every condition") {
    auto base_a = make_combatant(1), base_t = make_combatant(2);
    AttackContext near; near.target_within_5ft = true;
    struct Row { C k; bool on_attacker; RollMode far, close; };
    const Row rows[] = {
        {C::Blinded, true, RollMode::Disadvantage, RollMode::Disadvantage},
        {C::Frightened, true, RollMode::Disadvantage, RollMode::Disadvantage},
        {C::Poisoned, true, RollMode::Disadvantage, RollMode::Disadvantage},
        {C::Prone, true, RollMode::Disadvantage, RollMode::Disadvantage},
        {C::Restrained, true, RollMode::Disadvantage, RollMode::Disadvantage},
        {C::Invisible, true, RollMode::Advantage, RollMode::Advantage},
        {C::Charmed, true, RollMode::Normal, RollMode::Normal},
        {C::Deafened, true, RollMode::Normal, RollMode::Normal},
        {C::Grappled, true, RollMode::Normal, RollMode::Normal},
        {C::Blinded, false, RollMode::Advantage, RollMode::Advantage},
        {C::Paralyzed, false, RollMode::Advantage, RollMode::Advantage},
        {C::Petrified, false, RollMode::Advantage, RollMode::Advantage},
        {C::Restrained, false, RollMode::Advantage, RollMode::Advantage},
        {C::Stunned, false, RollMode::Advantage, RollMode::Advantage},
        {C::Unconscious, false, RollMode::Advantage, RollMode::Advantage},
        {C::Invisible, false, RollMode::Disadvantage, RollMode::Disadvantage},
        {C::Prone, false, RollMode::Disadvantage, RollMode::Advantage},
        {C::Grappled, false, RollMode::Normal, RollMode::Normal},
    };
    for (const auto& r : rows) {
        auto a = base_a, t = base_t;
        add_condition(r.on_attacker ? a : t, r.k);
        CAPTURE(int(r.k)); CAPTURE(r.on_attacker);
        CHECK(attack_roll_mode(a, t, {}) == r.far);
        CHECK(attack_roll_mode(a, t, near) == r.close);
    }
    auto t = base_t; t.dodging = true; t.speed_ft = 30;   // Dodge needs speed > 0
    CHECK(attack_roll_mode(base_a, t, {}) == RollMode::Disadvantage);
    add_condition(t, C::Stunned);   // dodge lost when incapacitated; stunned grants advantage
    CHECK(attack_roll_mode(base_a, t, {}) == RollMode::Advantage);
}

TEST_CASE("auto-crit within 5 ft on paralyzed and unconscious, not beyond") {
    const Attack hit_always{"x", 40, {1, 6, 0}, DamageType::Slashing, 5, false, false};
    for (C k : {C::Paralyzed, C::Unconscious}) {
        auto a = make_combatant(1), t = make_combatant(2);
        add_condition(t, k);
        rng::Rng r(17, 0);
        AttackContext near; near.target_within_5ft = true;
        for (int i = 0; i < 50; ++i) {
            auto res = resolve_attack(r, a, hit_always, t, near);
            if (res.hit) CHECK(res.critical);
            auto far = resolve_attack(r, a, hit_always, t, {});
            if (far.hit && far.roll.natural != 20) CHECK_FALSE(far.critical);
        }
    }
}
