// R-G5 static feature kinds: attack_bonus (Archery), ac_bonus (Defense), crit_range (Improved Critical).
#include <doctest/doctest.h>

#include "encounter_util.hpp"
#include "rules5e/armor.hpp"
#include "rules5e/attack.hpp"

using namespace enc;

namespace {
Combatant archer(CombatantId id, int to_hit) {
    auto c = pc(id, 200);
    c.attacks = {Attack{"sword", int8_t(to_hit), {1, 6, 3}, DamageType::Slashing, 5, false, false},
                 Attack{"bow", int8_t(to_hit), {1, 6, 3}, DamageType::Piercing, 60, true, false}};
    return c;
}
Combatant target(CombatantId id, int ac) {
    auto m = monster(id); m.hp = m.hp_max = 5000; m.ac = int16_t(ac); return m;
}
// the AttackResolved event of one attack with `index` on a fresh encounter
struct Roll { int natural = 0, total = 0; bool hit = false, crit = false; };
Roll attack_once(Combatant a, Combatant t, uint8_t index, uint64_t seed) {
    auto s = begin_encounter({std::move(a), std::move(t)}, seed);
    end_turns_until(s, 1);
    ActionRequest r{1, ActionKind::Attack, 2, index, 0, false, true};
    const auto res = step(s, r);
    Roll out;
    REQUIRE(res.ok);
    for (const auto& e : res.events)
        if (e.kind == EventKind::AttackResolved) { out.natural = e.a; out.total = e.b; out.hit = e.c & 1; out.crit = (e.c & 2) != 0; }
    return out;
}
}  // namespace

TEST_CASE("defaults") {
    Combatant c{};
    CHECK(c.attack_bonus_melee == 0); CHECK(c.attack_bonus_ranged == 0); CHECK(c.crit_min == 20);
    AcInput in;
    CHECK(in.armored_bonus == 0);
}

TEST_CASE("attack_bonus: Archery adds to ranged weapon attack rolls only") {
    for (uint64_t seed = 0; seed < 25; ++seed) {
        auto plain = archer(1, 5), styled = archer(1, 5);
        styled.attack_bonus_ranged = 2;
        const Roll a = attack_once(plain, target(2, 15), 1, seed), b = attack_once(styled, target(2, 15), 1, seed);
        CHECK(a.natural == b.natural);
        CHECK(b.total - a.total == 2);
        CHECK(a.total == a.natural + 5);
        const Roll m = attack_once(plain, target(2, 15), 0, seed), n = attack_once(styled, target(2, 15), 0, seed);
        CHECK(m.total == n.total);   // the melee attack is unchanged
        styled.attack_bonus_melee = -1;
        CHECK(attack_once(styled, target(2, 15), 0, seed).total == m.total - 1);
    }
}

TEST_CASE("attack_bonus changes who hits") {
    int flipped = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto plain = archer(1, 0), styled = archer(1, 0);
        styled.attack_bonus_ranged = 2;
        const Roll a = attack_once(plain, target(2, 12), 1, seed), b = attack_once(styled, target(2, 12), 1, seed);
        if (a.hit != b.hit) { ++flipped; CHECK(b.hit); CHECK(a.natural + 2 >= 12); CHECK(a.natural < 12); }
    }
    CHECK(flipped > 0);
}

TEST_CASE("crit_range: Improved Critical crits on 19 only when the attack hits") {
    int crit19 = 0, miss19 = 0;
    for (uint64_t seed = 0; seed < 400; ++seed) {
        auto def = archer(1, 5), imp = archer(1, 5), sup = archer(1, 5);
        imp.crit_min = 19; sup.crit_min = 18;
        for (int ac : {10, 40}) {
            const Roll d = attack_once(def, target(2, ac), 0, seed);
            const Roll i = attack_once(imp, target(2, ac), 0, seed);
            const Roll u = attack_once(sup, target(2, ac), 0, seed);
            CHECK(d.natural == i.natural);
            CHECK(d.hit == i.hit);
            CHECK(d.crit == (d.natural == 20));
            CHECK(i.crit == (i.natural == 20 || (i.hit && i.natural >= 19)));
            CHECK(u.crit == (u.natural == 20 || (u.hit && u.natural >= 18)));
            if (i.natural == 19 && i.hit) { ++crit19; CHECK(i.crit); }
            if (i.natural == 19 && !i.hit) { ++miss19; CHECK_FALSE(i.crit); }
        }
    }
    CHECK(crit19 > 0); CHECK(miss19 > 0);
}

TEST_CASE("crit_min is clamped: a natural 1 never hits and 20 is always a crit") {
    auto c = archer(1, 50); c.crit_min = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        const Roll r = attack_once(c, target(2, 10), 0, seed);
        if (r.natural == 1) { CHECK_FALSE(r.hit); CHECK_FALSE(r.crit); }
        else { CHECK(r.hit); CHECK(r.crit); }   // crit_min <= 1 clamps to 2: every hit crits
    }
}

TEST_CASE("a crit from the wider range doubles the damage dice") {
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto imp = archer(1, 50); imp.crit_min = 19;
        auto def = archer(1, 50);
        const Roll a = attack_once(imp, target(2, 10), 0, seed);
        if (a.natural != 19) continue;
        CHECK(a.crit);
        CHECK_FALSE(attack_once(def, target(2, 10), 0, seed).crit);
        return;
    }
    FAIL("no natural 19 in 200 seeds");
}

TEST_CASE("resolve_attack honours the static kinds too") {
    int seen = 0;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        auto a = archer(1, 5); a.attack_bonus_ranged = 2; a.crit_min = 19;
        auto t = target(2, 40);
        rng::Rng r(seed, 0);
        const auto res = resolve_attack(r, a, a.attacks[1], t, AttackContext{});
        CHECK(res.roll.total == res.roll.natural + 7);
        CHECK(res.critical == (res.roll.natural == 20));   // a 19 misses AC 40
        ++seen;
    }
    CHECK(seen == 100);
}

TEST_CASE("ac_bonus: Defense adds only while armor is worn") {
    AcInput in;
    in.abilities = AbilityScores{{10, 14, 10, 10, 10, 10}};
    in.armored_bonus = 1;
    CHECK(compute_ac(in).ac == 12);   // unarmored: 10 + Dex 2, no Defense
    in.has_armor = true; in.armor.category = ArmorCategory::Light; in.armor.base_ac = 11;
    CHECK(compute_ac(in).ac == 14);   // 11 + 2 + 1
    in.armor.category = ArmorCategory::Heavy; in.armor.dex_bonus = false; in.armor.base_ac = 16;
    CHECK(compute_ac(in).ac == 17);
    in.flat_bonus = 1;                // other static bonuses stack
    CHECK(compute_ac(in).ac == 18);
}
