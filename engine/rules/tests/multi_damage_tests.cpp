// Multi-component attack damage (Attack::extra_damage, apply_damage_parts) and condition immunity.
#include <doctest/doctest.h>

#include "encounter_util.hpp"
#include "test_util.hpp"

using namespace rules5e;
using namespace enc;

TEST_CASE("apply_damage_parts: each component gets its own type's immunity/resistance/vulnerability") {
    auto c = make_combatant(1, 10, 12, 100);
    c.resistances = bit(DamageType::Fire);
    c.vulnerabilities = bit(DamageType::Cold);
    c.immunities = bit(DamageType::Poison);
    const DamagePiece parts[] = {{9, DamageType::Slashing}, {9, DamageType::Fire}, {5, DamageType::Cold},
                                 {20, DamageType::Poison}};
    auto d = apply_damage_parts(c, parts);
    CHECK(d.dealt == 9 + 4 + 10 + 0);
    CHECK(d.hp_after == 100 - 23);
    // One piece == apply_damage.
    for (const DamagePiece& p : parts) {
        const DamagePiece one[] = {p};
        auto a = apply_damage_parts(c, one);
        auto b = apply_damage(c, p.amount, p.type);
        CHECK(a.dealt == b.dealt);
        CHECK(a.hp_after == b.hp_after);
        CHECK(a.temp_hp_after == b.temp_hp_after);
    }
    CHECK(apply_damage_parts(c, {}).dealt == 0);
}

TEST_CASE("apply_damage_parts: temp HP and massive damage see the combined hit once") {
    auto c = make_combatant(1, 10, 12, 10);
    c.temp_hp = 3;
    const DamagePiece two[] = {{4, DamageType::Slashing}, {4, DamageType::Fire}};
    auto d = apply_damage_parts(c, two);
    CHECK(d.dealt == 8);
    CHECK(d.temp_hp_after == 0);
    CHECK(d.hp_after == 5);
    // 10 HP, total 12 -> 2 left over < hp_max 10: down but not instantly dead.
    c.temp_hp = 0;
    const DamagePiece big[] = {{6, DamageType::Slashing}, {6, DamageType::Fire}};
    d = apply_damage_parts(c, big);
    CHECK(d.dropped_to_zero);
    CHECK_FALSE(d.instant_death);
    const DamagePiece huge[] = {{10, DamageType::Slashing}, {10, DamageType::Fire}};
    CHECK(apply_damage_parts(c, huge).instant_death);   // 10 left over >= hp_max
}

TEST_CASE("condition immunity: add_condition refuses an immune condition, others unaffected") {
    auto c = make_combatant(1);
    c.condition_immunities = bit(Condition::Frightened) | bit(Condition::Poisoned);
    add_condition(c, Condition::Frightened);
    add_condition(c, Condition::Poisoned);
    CHECK(c.conditions == 0);
    add_condition(c, Condition::Prone);
    CHECK(c.has(Condition::Prone));
    CHECK_FALSE(c.has(Condition::Frightened));
}

namespace {
struct Hit { bool hit, crit; int dealt; int damage_events; };

// Player 1 (attack 0 = `atk`) hits monster 2 (500 HP, given immunities) on the first seed >= from
// that satisfies `want_crit`.
Hit swing(const Attack& atk, bool want_crit, uint32_t immune, uint32_t resist, uint64_t* seed_out = nullptr) {
    for (uint64_t seed = 1; seed < 4000; ++seed) {
        auto s = begin_encounter({pc(1), monster(2)}, seed);
        mut(s, 1).attacks = {atk};
        auto& m = mut(s, 2);
        m.hp = m.hp_max = 500;
        m.immunities = immune;
        m.resistances = resist;
        end_turns_until(s, 1);
        auto r = step(s, {1, ActionKind::Attack, 2, 0});
        REQUIRE(r.ok);
        Hit h{false, false, 0, 0};
        for (const auto& e : r.events) {
            if (e.kind == EventKind::AttackResolved) { h.hit = e.c & 1; h.crit = e.c & 2; }
            if (e.kind == EventKind::DamageDealt) { h.dealt += e.a; ++h.damage_events; }
        }
        if (h.hit && h.crit == want_crit) { if (seed_out) *seed_out = seed; return h; }
    }
    FAIL("no seed");
    return {};
}
} // namespace

TEST_CASE("encounter: multi-component hit is one DamageDealt; extras roll after the first and double on a crit") {
    Attack plain = kLongsword;                                   // 1d8+3 slashing
    Attack multi = kLongsword;
    multi.extra_damage = {{DiceExpr{1, 1, 0}, DamageType::Fire}};   // deterministic +1 fire
    for (bool crit : {false, true}) {
        uint64_t seed = 0;
        const Hit a = swing(plain, crit, 0, 0, &seed);
        // Same seed for the multi attack: the first component's dice are identical (extras draw later).
        auto s = begin_encounter({pc(1), monster(2)}, seed);
        mut(s, 1).attacks = {multi};
        mut(s, 2).hp = mut(s, 2).hp_max = 500;
        end_turns_until(s, 1);
        auto r = step(s, {1, ActionKind::Attack, 2, 0});
        REQUIRE(r.ok);
        int dealt = 0, n = 0;
        for (const auto& e : r.events) if (e.kind == EventKind::DamageDealt) { dealt += e.a; ++n; }
        CHECK(n == 1);
        CHECK(dealt == a.dealt + (crit ? 2 : 1));   // 1d1 doubles on a crit
    }
}

TEST_CASE("encounter: per-type immunity/resistance applies to the matching component only") {
    Attack multi = kLongsword;
    multi.extra_damage = {{DiceExpr{0, 0, 10}, DamageType::Fire}};   // flat 10 fire
    uint64_t seed = 0;
    const Hit base = swing(kLongsword, false, 0, 0, &seed);
    CHECK(swing(multi, false, 0, 0).dealt >= 0);
    auto run = [&](uint32_t immune, uint32_t resist) {
        auto s = begin_encounter({pc(1), monster(2)}, seed);
        mut(s, 1).attacks = {multi};
        auto& m = mut(s, 2);
        m.hp = m.hp_max = 500; m.immunities = immune; m.resistances = resist;
        end_turns_until(s, 1);
        auto r = step(s, {1, ActionKind::Attack, 2, 0});
        int dealt = 0;
        for (const auto& e : r.events) if (e.kind == EventKind::DamageDealt) dealt += e.a;
        return dealt;
    };
    CHECK(run(0, 0) == base.dealt + 10);
    CHECK(run(bit(DamageType::Fire), 0) == base.dealt);                 // fire component zeroed
    CHECK(run(0, bit(DamageType::Fire)) == base.dealt + 5);             // fire halved
    CHECK(run(bit(DamageType::Slashing), 0) == 10);                     // weapon component zeroed
    CHECK(run(bit(DamageType::Slashing) | bit(DamageType::Fire), 0) == 0);
}

TEST_CASE("encounter: single-component attacks are unchanged by the extra_damage plumbing") {
    // Empty extra_damage must take the original path: identical events to a plain attack for many seeds.
    for (uint64_t seed = 1; seed < 40; ++seed) {
        auto s1 = begin_encounter({pc(1), monster(2)}, seed);
        auto s2 = begin_encounter({pc(1), monster(2)}, seed);
        mut(s2, 1).attacks[0].extra_damage = {};
        end_turns_until(s1, 1); end_turns_until(s2, 1);
        auto r1 = step(s1, {1, ActionKind::Attack, 2, 0});
        auto r2 = step(s2, {1, ActionKind::Attack, 2, 0});
        REQUIRE(r1.events.size() == r2.events.size());
        for (std::size_t i = 0; i < r1.events.size(); ++i) {
            CHECK(r1.events[i].kind == r2.events[i].kind);
            CHECK(r1.events[i].a == r2.events[i].a);
            CHECK(r1.events[i].b == r2.events[i].b);
        }
    }
}

TEST_CASE("downing a PC with condition immunities to Unconscious/Prone emits no ConditionAdded for them") {
    for (uint64_t seed = 1; seed < 400; ++seed) {
        auto s = begin_encounter({pc(1), monster(2)}, seed);
        mut(s, 1).hp = 1;   // max 28: dropped to 0, not instantly killed
        mut(s, 1).condition_immunities = bit(Condition::Prone);
        end_turns_until(s, 2);
        auto r = step(s, {2, ActionKind::Attack, 1, 0});
        REQUIRE(r.ok);
        if (!mut(s, 1).has(Condition::Unconscious)) continue;   // missed
        CHECK_FALSE(mut(s, 1).has(Condition::Prone));
        int prone_events = 0, unconscious_events = 0;
        for (const auto& e : r.events)
            if (e.kind == EventKind::ConditionAdded) {
                prone_events += e.a == int32_t(Condition::Prone);
                unconscious_events += e.a == int32_t(Condition::Unconscious);
            }
        CHECK(prone_events == 0);
        CHECK(unconscious_events == 1);
        return;
    }
    FAIL("no hit");
}
