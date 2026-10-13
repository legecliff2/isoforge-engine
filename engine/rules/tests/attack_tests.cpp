#include <doctest/doctest.h>

#include "test_util.hpp"

using namespace rules5e;

static const Attack kScimitar{"scimitar", 4, {1, 6, 2}, DamageType::Slashing, 5, false, true};

TEST_CASE("natural 20 always hits and crits; natural 1 always misses") {
    auto atk = make_combatant(1);
    auto tgt_hi = make_combatant(2, 10, 30);   // unhittable except on a 20
    auto tgt_lo = make_combatant(3, 10, -10);  // always hit except on a 1
    Attack weak = kScimitar; weak.to_hit = -5;
    Attack strong = kScimitar; strong.to_hit = 30;

    rng::Rng r20(seed_with_first_d20(20), 0);
    auto hit = resolve_attack(r20, atk, weak, tgt_hi, {});
    CHECK(hit.roll.natural == 20);
    CHECK(hit.hit); CHECK(hit.critical);
    CHECK(hit.damage >= 4); CHECK(hit.damage <= 14);   // 2d6+2
    CHECK(hit.type == DamageType::Slashing);

    rng::Rng r1(seed_with_first_d20(1), 0);
    auto miss = resolve_attack(r1, atk, strong, tgt_lo, {});
    CHECK(miss.roll.natural == 1);
    CHECK_FALSE(miss.hit); CHECK_FALSE(miss.critical);
    CHECK(miss.damage == 0);
}

TEST_CASE("hit iff total >= AC + cover (non-natural rolls)") {
    auto atk = make_combatant(1);
    rng::Rng r(31, 0);
    for (int i = 0; i < 2000; ++i) {
        auto tgt = make_combatant(2, 10, 10 + i % 10);
        AttackContext ctx; ctx.cover_ac_bonus = int8_t((i % 3) == 0 ? 2 : 0);
        auto res = resolve_attack(r, atk, kScimitar, tgt, ctx);
        if (res.roll.natural == 20 || res.roll.natural == 1) continue;
        CHECK(res.hit == (res.roll.total >= tgt.ac + ctx.cover_ac_bonus));
        CHECK_FALSE(res.critical);
        if (res.hit) { CHECK(res.damage >= 3); CHECK(res.damage <= 8); }
    }
}

TEST_CASE("damage is clamped at zero") {
    auto atk = make_combatant(1);
    auto tgt = make_combatant(2, 10, -10);
    Attack feeble{"feeble", 30, {1, 4, -10}, DamageType::Bludgeoning, 5, false, false};
    rng::Rng r(3, 3);
    for (int i = 0; i < 50; ++i) {
        auto res = resolve_attack(r, atk, feeble, tgt, {});
        CHECK(res.damage >= 0);
    }
}

TEST_CASE("attack_roll_mode applies SRD condition sources and cancels") {
    auto a = make_combatant(1), t = make_combatant(2);
    CHECK(attack_roll_mode(a, t, {}) == RollMode::Normal);
    AttackContext adv; adv.mode = RollMode::Advantage;
    CHECK(attack_roll_mode(a, t, adv) == RollMode::Advantage);

    add_condition(a, Condition::Poisoned);
    CHECK(attack_roll_mode(a, t, {}) == RollMode::Disadvantage);
    CHECK(attack_roll_mode(a, t, adv) == RollMode::Normal);  // cancel
    remove_condition(a, Condition::Poisoned);
    CHECK_FALSE(a.has(Condition::Poisoned));

    add_condition(t, Condition::Prone);
    AttackContext near; near.target_within_5ft = true;
    CHECK(attack_roll_mode(a, t, near) == RollMode::Advantage);
    CHECK(attack_roll_mode(a, t, {}) == RollMode::Disadvantage);
    remove_condition(t, Condition::Prone);

    add_condition(t, Condition::Restrained);
    CHECK(attack_roll_mode(a, t, {}) == RollMode::Advantage);
    add_condition(t, Condition::Invisible);
    CHECK(attack_roll_mode(a, t, {}) == RollMode::Normal);
}

TEST_CASE("hits on a paralyzed/unconscious target within 5 ft are crits") {
    auto a = make_combatant(1);
    auto t = make_combatant(2, 10, -10);
    add_condition(t, Condition::Unconscious);
    AttackContext near; near.target_within_5ft = true;
    rng::Rng r(8, 8);
    int crits = 0, hits = 0;
    for (int i = 0; i < 200; ++i) {
        auto res = resolve_attack(r, a, kScimitar, t, near);
        CHECK(res.roll.mode == RollMode::Advantage);
        if (res.hit) { ++hits; crits += res.critical; }
    }
    CHECK(hits > 0);
    CHECK(crits == hits);
    rng::Rng r2(8, 8);
    auto far = resolve_attack(r2, a, kScimitar, t, {});
    if (far.roll.natural != 20) CHECK_FALSE(far.critical);
}
