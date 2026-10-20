// SRD 5.1 Squeezing (feat/size-space): Combatant::squeezing, set / cleared by a Move's ActionRequest::squeeze.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
EncounterState start(uint64_t seed) {
    auto a = pc(1), b = monster(2);
    a.hp = a.hp_max = int16_t(200); b.hp = b.hp_max = int16_t(200);
    auto s = begin_encounter({a, b}, seed);
    end_turns_until(s, 1);
    return s;
}
int natural(EncounterState& s) {
    ActionRequest atk{1, ActionKind::Attack, 2, 0};
    atk.target_within_5ft = true;
    auto r = step(s, atk);
    REQUIRE(r.ok);
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) return e.a;
    FAIL("no attack");
    return 0;
}
}  // namespace

TEST_CASE("squeeze: a Move sets and clears the flag") {
    auto s = start(3);
    ActionRequest mv{1, ActionKind::Move};
    mv.move_ft = 10; mv.squeeze = 1;
    REQUIRE(step(s, mv).ok);
    CHECK(find(s, 1)->squeezing);
    mv.move_ft = 5; mv.squeeze = 0;
    REQUIRE(step(s, mv).ok);
    CHECK(find(s, 1)->squeezing);
    mv.squeeze = 2;
    REQUIRE(step(s, mv).ok);
    CHECK_FALSE(find(s, 1)->squeezing);
}

TEST_CASE("squeeze: disadvantage on its attacks, advantage on attacks against it") {
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        auto plain = start(seed), sq = start(seed), tg = start(seed);
        mut(sq, 1).squeezing = true;
        mut(tg, 2).squeezing = true;
        const int n0 = natural(plain), n1 = natural(sq), n2 = natural(tg);
        CHECK(n1 <= n0);   // the lower of two; the first die is the twin's single die
        CHECK(n2 >= n0);
    }
}

namespace {
const uint32_t kDexBlast = spell_id_of("test.spell#dex_blast");
EncounterHooks dex_hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kDexBlast; d.content_id = "test.spell#dex_blast"; d.level = 1; d.range_ft = 60;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.on_save = OnSave::Half; d.dice = DiceExpr{1, 6, 0};
    d.damage_type = DamageType::Fire; d.fixed_dc = 15; d.components = kComponentV | kComponentS;
    book->add(d);
    h.spells = book;
    return h;
}
int dex_save_natural(uint64_t seed, bool squeezing) {
    auto w = wizard(1);
    w.spells_known.push_back(kDexBlast);
    auto m = monster(2);
    m.hp = m.hp_max = int16_t(200);
    m.squeezing = squeezing;
    auto s = begin_encounter({w, m}, seed, nullptr, dex_hooks());
    end_turns_until(s, 1);
    ActionRequest r{1, ActionKind::CastSpell, 2};
    r.spell_id = kDexBlast; r.spell_level = 1; r.targets = {2};
    auto res = step(s, r);
    REQUIRE_MESSAGE(res.ok, res.error);
    for (const auto& e : res.events) if (e.kind == EventKind::SavingThrowRolled && e.actor == 2) return e.a;
    FAIL("no save");
    return 0;
}
}  // namespace

TEST_CASE("squeeze: disadvantage on Dex saving throws") {
    bool lower_seen = false;
    for (uint64_t seed = 1; seed <= 16; ++seed) {
        const int plain = dex_save_natural(seed, false), sq = dex_save_natural(seed, true);
        CHECK(sq <= plain);   // the lower of two; the first die is the plain save's single die
        lower_seen = lower_seen || sq < plain;
    }
    CHECK(lower_seen);   // the second die was really rolled
}
