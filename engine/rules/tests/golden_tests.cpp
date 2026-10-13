#include <doctest/doctest.h>

#include "golden_hash.hpp"
#include "test_util.hpp"

// Golden hashes for rules outcomes. If one changes, the rules output changed: that needs a
// rules_profile bump and Rules owner approval (ARCHITECTURE.md §4.7).
using namespace rules5e;

TEST_CASE("golden: dice expression rolls") {
    rng::Rng r(0x5EED'0001ull, 0);
    GoldenHash g;
    const char* exprs[] = {"1d4", "2d6+3", "1d8-1", "3d10+5", "1d20", "4d6", "1d12+2", "8d6", "1d100"};
    for (int i = 0; i < 3000; ++i) {
        auto e = parse_dice(exprs[i % 9]).value();
        g.i32(roll_dice(r, e, i % 7 == 0));
    }
    CHECK(g.h == 0x5C3D6818D3944336ull);
}

TEST_CASE("golden: d20 modes, initiative, attacks, damage") {
    rng::Rng r(0x5EED'0002ull, 3);
    GoldenHash g;
    for (int i = 0; i < 1000; ++i) {
        auto d = roll_d20(r, i % 11 - 5, RollMode(i % 3));
        g.i32(d.natural); g.i32(d.natural_other); g.i32(d.total);
    }
    std::vector<Combatant> cs;
    for (uint32_t i = 1; i <= 8; ++i) cs.push_back(make_combatant(i, 6 + int(i) * 2, 10 + int(i), 20));
    for (const auto& e : roll_initiative(r, cs)) { g.u32(e.id); g.i32(e.total); g.i32(e.dex_mod); }

    const Attack scimitar{"scimitar", 4, {1, 6, 2}, DamageType::Slashing, 5, false, true};
    for (int i = 0; i < 2000; ++i) {
        auto& a = cs[std::size_t(i % 8)];
        auto& t = cs[std::size_t((i + 3) % 8)];
        AttackContext ctx; ctx.mode = RollMode(i % 3); ctx.target_within_5ft = i % 2 == 0;
        auto res = resolve_attack(r, a, scimitar, t, ctx);
        g.i32(res.roll.natural); g.i32(res.roll.total);
        g.byte(uint8_t(res.hit)); g.byte(uint8_t(res.critical)); g.i32(res.damage);
        if (res.hit) {
            auto ap = apply_damage(t, res.damage, res.type);
            g.i32(ap.dealt); g.i32(ap.hp_after);
            if (t.hp == 0) t.hp = t.hp_max;  // keep the workload going
        }
    }
    CHECK(g.h == 0xFFEB83C98089E98Dull);
}
