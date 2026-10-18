#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
Combatant ghost_like() {   // resists B/P/S from nonmagical attacks (specter, ghost, werewolf-like)
    auto c = make_combatant(9, 10, 5, 200);
    c.resist_qualified.nonmagical = bit(DamageType::Bludgeoning) | bit(DamageType::Piercing) | bit(DamageType::Slashing);
    return c;
}
// HP lost by `target` from one fixed-damage attack that hit, or -1 on a miss.
int attack_loss(const Attack& atk, Combatant target, uint64_t seed) {
    Combatant a = pc(1); a.attacks = {atk};
    target.id = 2; target.team = 1;
    auto s = begin_encounter({a, target}, seed);
    end_turns_until(s, 1);
    REQUIRE(current_actor(s) == 1);
    const int before = find(s, 2)->hp;
    auto r = step(s, {1, ActionKind::Attack, 2});
    REQUIRE(r.ok);
    bool hit = false;
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) hit = e.c & 1;
    return hit ? before - find(s, 2)->hp : -1;
}
} // namespace

TEST_CASE("qualified_applies: nonmagical, not silvered, not adamantine") {
    QualifiedDamageMask m;
    m.nonmagical = bit(DamageType::Slashing);
    m.nonmagical_not_silvered = bit(DamageType::Piercing);
    m.nonmagical_not_adamantine = bit(DamageType::Bludgeoning);
    CHECK(qualified_applies(m, DamageType::Slashing, 0));
    CHECK_FALSE(qualified_applies(m, DamageType::Slashing, kAttackMagical));
    CHECK(qualified_applies(m, DamageType::Slashing, kAttackSilvered | kAttackAdamantine));   // plain "nonmagical"
    CHECK(qualified_applies(m, DamageType::Piercing, 0));
    CHECK_FALSE(qualified_applies(m, DamageType::Piercing, kAttackSilvered));
    CHECK(qualified_applies(m, DamageType::Piercing, kAttackAdamantine));
    CHECK(qualified_applies(m, DamageType::Bludgeoning, kAttackSilvered));
    CHECK_FALSE(qualified_applies(m, DamageType::Bludgeoning, kAttackAdamantine));
    CHECK_FALSE(qualified_applies(m, DamageType::Fire, 0));                              // type not listed
    CHECK_FALSE(qualified_applies(m, DamageType::Piercing, kAttackMagical | kAttackAdamantine));
}

TEST_CASE("apply_damage: qualified resistance and immunity") {
    auto c = ghost_like();
    CHECK(apply_damage(c, 9, DamageType::Slashing).dealt == 4);                        // default source: nonmagical
    CHECK(apply_damage(c, 9, DamageType::Slashing, kAttackMagical).dealt == 9);          // magic weapon / spell
    CHECK(apply_damage(c, 9, DamageType::Slashing, kAttackSilvered).dealt == 4);         // silvered alone does not help
    CHECK(apply_damage(c, 9, DamageType::Fire).dealt == 9);                            // unlisted type
    c.immune_qualified.nonmagical_not_silvered = bit(DamageType::Slashing);            // werewolf-style
    CHECK(apply_damage(c, 9, DamageType::Slashing).dealt == 0);                        // immunity wins over resistance
    CHECK(apply_damage(c, 9, DamageType::Slashing, kAttackSilvered).dealt == 4);         // silvered: back to resistance
    CHECK(apply_damage(c, 9, DamageType::Slashing, kAttackMagical).dealt == 9);
    // Qualified and unqualified resistance do not stack; vulnerability still doubles after halving.
    c.resistances = bit(DamageType::Piercing);
    CHECK(apply_damage(c, 9, DamageType::Piercing).dealt == 4);
    c.vulnerabilities = bit(DamageType::Piercing);
    CHECK(apply_damage(c, 9, DamageType::Piercing).dealt == 8);
    // Petrified: resist everything, qualified or not, and a magical source still gets halved.
    auto p = make_combatant(1, 10, 10, 100);
    add_condition(p, Condition::Petrified);
    CHECK(apply_damage(p, 9, DamageType::Slashing, kAttackMagical).dealt == 4);
}

TEST_CASE("unqualified behaviour is unchanged by default") {
    auto c = make_combatant(1, 10, 12, 100);
    c.resistances = bit(DamageType::Fire); c.immunities = bit(DamageType::Poison);
    for (uint8_t src : {uint8_t(0), uint8_t(kAttackMagical), uint8_t(kAttackSilvered), uint8_t(kAttackMagical | kAttackAdamantine)}) {
        CHECK(apply_damage(c, 9, DamageType::Fire, src).dealt == 4);
        CHECK(apply_damage(c, 9, DamageType::Poison, src).dealt == 0);
        CHECK(apply_damage(c, 9, DamageType::Slashing, src).dealt == 9);
    }
}

TEST_CASE("resolve_attack reports the attack's qualifiers") {
    auto a = make_combatant(1);
    auto t = make_combatant(2, 10, -10);
    Attack sword{"sword+1", 30, {0, 0, 5}, DamageType::Slashing, 5, false, false};
    rng::Rng r0(5, 0);
    CHECK(resolve_attack(r0, a, sword, t, {}).qualifiers == 0);
    sword.qualifiers = kAttackMagical | kAttackSilvered;
    rng::Rng r1(5, 0);
    CHECK(resolve_attack(r1, a, sword, t, {}).qualifiers == (kAttackMagical | kAttackSilvered));
}

TEST_CASE("encounter: nonmagical weapon attack is halved; magic or silvered-vs-silver qualifier bypasses") {
    Attack plain{"club", 30, {0, 0, 10}, DamageType::Bludgeoning, 5, false, false};   // flat 10, always hits (not a nat 1)
    Attack magic = plain; magic.id = "club+1"; magic.qualifiers = kAttackMagical;
    int plain_hits = 0, magic_hits = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        int p = attack_loss(plain, ghost_like(), seed);
        int m = attack_loss(magic, ghost_like(), seed);
        if (p >= 0) { CHECK(p == 5); ++plain_hits; }     // 10 halved
        if (m >= 0) { CHECK(m == 10); ++magic_hits; }
    }
    CHECK(plain_hits > 20); CHECK(magic_hits > 20);
    // silvered weapons beat "nonmagical, not silvered" but not plain "nonmagical".
    auto weres = ghost_like();
    weres.resist_qualified = {};
    weres.immune_qualified.nonmagical_not_silvered = bit(DamageType::Bludgeoning);
    Attack silver = plain; silver.qualifiers = kAttackSilvered;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        int p = attack_loss(plain, weres, seed), v = attack_loss(silver, weres, seed);
        if (p >= 0) CHECK(p == 0);
        if (v >= 0) CHECK(v == 10);
    }
}

TEST_CASE("encounter: spell damage is magical and ignores nonmagical resistance") {
    auto w = wizard(1);
    auto plain = make_combatant(2, 10, 5, 200); plain.team = 1;
    auto guarded = plain;   // would halve every listed type if the spell were tagged nonmagical
    guarded.resist_qualified.nonmagical = bit(DamageType::Fire) | bit(DamageType::Force);
    guarded.immune_qualified.nonmagical_not_silvered = bit(DamageType::Fire);
    auto run = [&](const Combatant& tgt, uint32_t spell, uint8_t level, uint64_t seed) {
        auto s = begin_encounter({w, tgt}, seed, nullptr, spell_hooks());
        end_turns_until(s, 1);
        REQUIRE(current_actor(s) == 1);
        const int before = find(s, 2)->hp;
        REQUIRE(step(s, cast(1, spell, 2, level)).ok);
        return before - find(s, 2)->hp;
    };
    int fire_damage = 0;
    for (uint64_t seed = 0; seed < 40; ++seed) {
        CHECK(run(guarded, kMagicMissile, 1, seed) == run(plain, kMagicMissile, 1, seed));   // Force darts
        const int a = run(guarded, kFireBolt, 0, seed), b = run(plain, kFireBolt, 0, seed);
        CHECK(a == b);                                                                       // Fire spell attack
        fire_damage += a;
    }
    CHECK(fire_damage > 0);
}

TEST_CASE("qualifiers apply to every damage component of a multi-component attack") {
    auto t = ghost_like();   // nonmagical B/P/S resisted; fire unaffected
    t.resist_qualified.nonmagical |= bit(DamageType::Fire);   // and fire only from nonmagical sources
    const DamagePiece parts[] = {{10, DamageType::Slashing}, {6, DamageType::Fire}, {4, DamageType::Radiant}};
    CHECK(apply_damage_parts(t, parts).dealt == 5 + 3 + 4);                          // nonmagical: B/P/S and fire halved
    CHECK(apply_damage_parts(t, parts, kAttackMagical).dealt == 10 + 6 + 4);          // magical: nothing halved
    CHECK(apply_damage_parts(t, parts, kAttackSilvered).dealt == 5 + 3 + 4);          // plain "nonmagical" ignores silver

    Attack flame{"flame tongue", 30, {0, 0, 10}, DamageType::Slashing, 5, false, false};
    flame.extra_damage = {{{0, 0, 6}, DamageType::Fire}};
    int hits = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        const int plain = attack_loss(flame, t, seed);
        flame.qualifiers = kAttackMagical;
        const int magic = attack_loss(flame, t, seed);
        flame.qualifiers = 0;
        if (plain >= 0) { CHECK(plain == 8); ++hits; }
        if (magic >= 0) CHECK(magic == 16);
    }
    CHECK(hits > 20);
}
