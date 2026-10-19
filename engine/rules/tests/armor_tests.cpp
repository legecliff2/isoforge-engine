// Row 15: compute_ac (SRD 5.1 armor, shields, unarmored / natural AC, Dex cap, Str shortfall).
#include <doctest/doctest.h>

#include "rules5e/armor.hpp"

using namespace rules5e;

namespace {
AcInput base(int str, int dex, int con = 10, int wis = 10) {
    AcInput in;
    in.abilities.score = {int8_t(str), int8_t(dex), int8_t(con), int8_t(10), int8_t(wis), int8_t(10)};
    in.base_speed_ft = 30;
    return in;
}
ArmorPiece leather() { ArmorPiece a; a.category = ArmorCategory::Light; a.base_ac = 11; return a; }
ArmorPiece scale() {
    ArmorPiece a; a.category = ArmorCategory::Medium; a.base_ac = 14; a.has_dex_cap = true; a.dex_cap = 2;
    a.stealth_disadvantage = true; return a;
}
ArmorPiece chain_mail() {
    ArmorPiece a; a.category = ArmorCategory::Heavy; a.base_ac = 16; a.dex_bonus = false; a.str_min = 13;
    a.stealth_disadvantage = true; return a;
}
}  // namespace

TEST_CASE("ability order assumed by the helper") {
    CHECK(int(Ability::Str) == 0); CHECK(int(Ability::Dex) == 1); CHECK(int(Ability::Con) == 2);
    CHECK(int(Ability::Wis) == 4);
}

TEST_CASE("unarmored: 10 + Dex") {
    CHECK(compute_ac(base(10, 14)).ac == 12);
    CHECK(compute_ac(base(10, 8)).ac == 9);      // negative Dex lowers it
    CHECK(compute_ac(base(10, 10)).ac == 10);
}

TEST_CASE("light armor adds full Dex; magic bonus adds") {
    auto in = base(10, 18); in.has_armor = true; in.armor = leather();
    CHECK(compute_ac(in).ac == 15);
    in.armor.magic_bonus = 2;
    CHECK(compute_ac(in).ac == 17);
    auto low = base(10, 6); low.has_armor = true; low.armor = leather();
    CHECK(compute_ac(low).ac == 9);              // 11 - 2
}

TEST_CASE("medium armor caps Dex at +2 but not negative Dex") {
    auto in = base(10, 18); in.has_armor = true; in.armor = scale();
    CHECK(compute_ac(in).ac == 16);
    auto mid = base(10, 14); mid.has_armor = true; mid.armor = scale();
    CHECK(compute_ac(mid).ac == 16);
    auto low = base(10, 8); low.has_armor = true; low.armor = scale();
    CHECK(compute_ac(low).ac == 13);
    CHECK(armor_dex_term(scale(), 4) == 2);
    CHECK(armor_dex_term(scale(), -1) == -1);
}

TEST_CASE("heavy armor ignores Dex") {
    auto in = base(15, 18); in.has_armor = true; in.armor = chain_mail();
    CHECK(compute_ac(in).ac == 16);
    in.abilities.score[1] = 4;
    CHECK(compute_ac(in).ac == 16);
}

TEST_CASE("shield adds 2 (and its magic bonus), with or without armor") {
    auto in = base(15, 10); in.has_armor = true; in.armor = chain_mail(); in.has_shield = true;
    CHECK(compute_ac(in).ac == 18);
    in.shield.magic_bonus = 1;
    CHECK(compute_ac(in).ac == 19);
    auto un = base(10, 14); un.has_shield = true;
    CHECK(compute_ac(un).ac == 14);
}

TEST_CASE("Str shortfall: speed drops 10 ft, only when armor needs more Str") {
    auto in = base(12, 10); in.has_armor = true; in.armor = chain_mail();
    auto r = compute_ac(in);
    CHECK(r.str_shortfall); CHECK(r.speed_ft == 20);
    in.abilities.score[0] = 13;
    r = compute_ac(in);
    CHECK_FALSE(r.str_shortfall); CHECK(r.speed_ft == 30);
    auto none = base(8, 10); none.has_armor = true; none.armor = leather();   // str_min 0
    CHECK(compute_ac(none).speed_ft == 30);
    auto slow = base(8, 10); slow.has_armor = true; slow.armor = chain_mail(); slow.base_speed_ft = 5;
    CHECK(compute_ac(slow).speed_ft == 0);   // never negative
    slow.base_speed_ft = 0;
    CHECK(compute_ac(slow).speed_ft == 0);
}

TEST_CASE("stealth disadvantage follows worn armor only") {
    auto in = base(15, 10); in.has_armor = true; in.armor = scale();
    CHECK(compute_ac(in).stealth_disadvantage);
    in.armor = leather();
    CHECK_FALSE(compute_ac(in).stealth_disadvantage);
    in.has_shield = true;
    CHECK_FALSE(compute_ac(in).stealth_disadvantage);
    CHECK_FALSE(compute_ac(base(10, 10)).stealth_disadvantage);
}

TEST_CASE("Unarmored Defense: Barbarian 10 + Dex + Con, Monk 10 + Dex + Wis") {
    auto bar = base(16, 14, 16); bar.has_alt_ability = true; bar.alt_ability = Ability::Con;
    CHECK(compute_ac(bar).ac == 15);
    auto monk = base(10, 16, 10, 16); monk.has_alt_ability = true; monk.alt_ability = Ability::Wis;
    CHECK(compute_ac(monk).ac == 16);
    auto neg = base(10, 14, 6); neg.has_alt_ability = true; neg.alt_ability = Ability::Con;
    CHECK(compute_ac(neg).ac == 12);             // a negative alternative never lowers plain 10 + Dex
    bar.has_armor = true; bar.armor = scale();   // armor replaces it
    CHECK(compute_ac(bar).ac == 16);
}

TEST_CASE("natural armor: fixed base, optionally plus Dex, best formula wins") {
    auto m = base(10, 12); m.natural_base = 13;
    CHECK(compute_ac(m).ac == 13);
    m.natural_dex_bonus = true;
    CHECK(compute_ac(m).ac == 14);
    auto weak = base(10, 18); weak.natural_base = 11;
    CHECK(compute_ac(weak).ac == 14);            // 10 + Dex beats 11
}

TEST_CASE("flat bonus, clamping and defaults") {
    auto in = base(10, 10); in.flat_bonus = 1;
    CHECK(compute_ac(in).ac == 11);
    auto bad = base(10, 1); bad.flat_bonus = -20;
    CHECK(compute_ac(bad).ac == 0);
    AcInput d;
    CHECK(d.unarmored_base == 10);
    CHECK_FALSE(d.has_armor);
    CHECK(compute_ac(d).ac >= 0);
}
