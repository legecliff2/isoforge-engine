// R-G6: bonus-action attack options (two-weapon fighting, Martial Arts, Flurry of Blows, Frenzy).
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kKi = 800, kRageMod = 77;

// attacks: 0 short sword (light), 1 dagger (light, off-hand), 2 bow (ranged)
Combatant fighter(CombatantId id) {
    auto c = pc(id, 200);
    Attack sword{"shortsword", 40, {1, 6, 3}, DamageType::Piercing, 5, false, true};
    Attack dagger{"dagger", 40, {1, 4, 3}, DamageType::Piercing, 5, false, true};
    Attack bow{"bow", 40, {1, 6, 3}, DamageType::Piercing, 60, true, false};
    c.attacks = {sword, dagger, bow};
    return c;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 500; m.ac = 10;
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    return m;
}
BonusAttack twf(bool style) {
    BonusAttack b; b.id = 1; b.attack_index = 1; b.requires_attacks = 3; b.different_weapon = true;
    if (!style) { b.has_damage = true; b.damage = DiceExpr{1, 4, 0}; }
    return b;
}
BonusAttack martial_arts() { BonusAttack b; b.id = 2; b.attack_index = 1; b.requires_attacks = 3; return b; }
BonusAttack flurry() {
    BonusAttack b; b.id = 3; b.attack_index = 1; b.count = 2; b.requires_attacks = 3; b.pool = kKi; b.pool_cost = 1; return b;
}
BonusAttack frenzy() { BonusAttack b; b.id = 4; b.any_melee = true; b.requires_modifier = kRageMod; return b; }

EncounterState start(Combatant c, uint64_t seed = 1) {
    auto s = begin_encounter({std::move(c), dummy(2)}, seed, nullptr, spell_hooks());
    end_turns_until(s, 1);
    return s;
}
ActionRequest atk(uint8_t idx) { return ActionRequest{1, ActionKind::Attack, 2, idx, 0, false, true}; }
ActionRequest bonus(uint8_t option, uint8_t idx) {
    ActionRequest r{1, ActionKind::Attack, 2, idx, 0, true, true};
    r.bonus_option = option;
    return r;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
}  // namespace

TEST_CASE("defaults") {
    BonusAttack b;
    CHECK(b.count == 1); CHECK_FALSE(b.any_melee); CHECK(b.requires_attacks == 0); CHECK_FALSE(b.has_damage);
    TurnBudget t;
    CHECK(t.attacks_used_mask == 0); CHECK(t.bonus_option == 0); CHECK(t.bonus_remaining == 0);
    ActionRequest r{1, ActionKind::Attack};
    CHECK(r.bonus_option == 0);
    Combatant c{};
    CHECK(c.bonus_attacks.empty());
}

TEST_CASE("the Attack action records the attacks it used; bonus attacks do not") {
    auto c = fighter(1); c.attacks_per_action = 2; c.bonus_attacks = {martial_arts()};
    auto s = start(c);
    REQUIRE(step(s, atk(0)).ok);
    CHECK(s.budget.attacks_used_mask == 1u);
    REQUIRE(step(s, atk(2)).ok);   // Extra Attack
    CHECK(s.budget.attacks_used_mask == 5u);
    REQUIRE(step(s, bonus(1, 1)).ok);
    CHECK(s.budget.attacks_used_mask == 5u);
    end_turns_until(s, 2); end_turns_until(s, 1);
    CHECK(s.budget.attacks_used_mask == 0u);
}

TEST_CASE("two-weapon fighting: needs the Attack action with a light weapon and a different off-hand weapon") {
    auto c = fighter(1); c.bonus_attacks = {twf(false)};
    auto s = start(c);
    CHECK_FALSE(bonus_attack_available(s, s.combatants[0], 1));
    auto early = step(s, bonus(1, 1));
    CHECK_FALSE(early.ok);
    CHECK(s.budget.bonus_action);
    REQUIRE(step(s, atk(0)).ok);
    CHECK(bonus_attack_available(s, s.combatants[0], 1));
    CHECK_FALSE(step(s, bonus(1, 0)).ok);   // wrong attack for the option
    CHECK_FALSE(step(s, bonus(2, 1)).ok);   // no such option
    ActionRequest plain = bonus(1, 1); plain.as_bonus_action = false;
    CHECK_FALSE(step(s, plain).ok);
    auto r = step(s, bonus(1, 1));
    REQUIRE(r.ok);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK_FALSE(step(s, bonus(1, 1)).ok);   // the bonus action is spent
    // the same weapon cannot be both: only the dagger was used -> off-hand dagger is refused
    auto s2 = start(c);
    REQUIRE(step(s2, atk(1)).ok);
    CHECK_FALSE(bonus_attack_available(s2, s2.combatants[0], 1));
}

TEST_CASE("two-weapon fighting damage: the off-hand dice replace the baked ability modifier unless the style keeps it") {
    int compared = 0;
    for (uint64_t seed = 0; seed < 40 && compared < 5; ++seed) {
        auto with_override = fighter(1); with_override.bonus_attacks = {twf(false)};
        auto with_style = fighter(1); with_style.bonus_attacks = {twf(true)};
        auto a = start(with_override, seed);
        auto b = start(with_style, seed);
        auto a0 = step(a, atk(0)); auto b0 = step(b, atk(0));
        REQUIRE(a0.ok); REQUIRE(b0.ok);
        const auto ra = step(a, bonus(1, 1));
        const auto rb = step(b, bonus(1, 1));
        REQUIRE(ra.ok); REQUIRE(rb.ok);
        const int da = dealt_to(ra.events, 2), db = dealt_to(rb.events, 2);
        if (da == 0 || db == 0) continue;
        CHECK(db - da == 3);
        ++compared;
    }
    CHECK(compared > 0);
}

TEST_CASE("Martial Arts: the unarmed strike may follow the same weapon") {
    auto c = fighter(1); c.bonus_attacks = {martial_arts()};
    auto s = start(c);
    REQUIRE(step(s, atk(1)).ok);
    CHECK(bonus_attack_available(s, s.combatants[0], 1));
    REQUIRE(step(s, bonus(1, 1)).ok);
    CHECK_FALSE(s.budget.bonus_action);
}

TEST_CASE("Flurry of Blows: one ki, two strikes, the second does not spend again") {
    auto c = fighter(1); c.bonus_attacks = {flurry()};
    auto s = start(c);
    register_pool(s, 1, kKi, 3, 3);
    CHECK_FALSE(step(s, bonus(1, 1)).ok);   // no Attack action yet
    CHECK(find_pool(s, 1, kKi)->cur == 3);
    REQUIRE(step(s, atk(0)).ok);
    auto first = step(s, bonus(1, 1));
    REQUIRE(first.ok);
    CHECK(find_pool(s, 1, kKi)->cur == 2);
    int spent = 0; for (const auto& e : first.events) spent += e.kind == EventKind::ResourceSpent;
    CHECK(spent == 1);
    const Event* sp = nullptr;
    for (const auto& e : first.events) if (e.kind == EventKind::ResourceSpent) sp = &e;
    REQUIRE(sp);
    CHECK(sp->a == int32_t(kKi)); CHECK(sp->b == 1); CHECK(sp->c == 2);
    CHECK(s.budget.bonus_remaining == 1);
    auto second = step(s, bonus(1, 1));
    REQUIRE(second.ok);
    CHECK(find_pool(s, 1, kKi)->cur == 2);
    CHECK(s.budget.bonus_remaining == 0);
    CHECK(s.budget.bonus_option == 0);
    CHECK_FALSE(step(s, bonus(1, 1)).ok);   // only two strikes
    // next turn: out of ki -> unavailable
    end_turns_until(s, 2); end_turns_until(s, 1);
    for (auto& p : s.pools) p.cur = 0;
    REQUIRE(step(s, atk(0)).ok);
    CHECK_FALSE(bonus_attack_available(s, s.combatants[0], 1));
    CHECK_FALSE(step(s, bonus(1, 1)).ok);
}

TEST_CASE("Frenzy: any melee attack while the Rage modifier is on, no prior attack needed") {
    auto c = fighter(1); c.bonus_attacks = {frenzy()};
    auto s = start(c);
    CHECK_FALSE(bonus_attack_available(s, s.combatants[0], 1));
    CHECK_FALSE(step(s, bonus(1, 0)).ok);
    ModifierSpec rage; rage.id = kRageMod; rage.source = kRageMod; rage.stat = ModStat::Damage; rage.bonus = 2;
    rage.applies_to = ModAppliesTo::Self;
    std::vector<Event> sink; apply_modifier(s, 1, 1, rage, sink);
    CHECK(bonus_attack_available(s, s.combatants[0], 1));
    CHECK_FALSE(step(s, bonus(1, 2)).ok);   // the bow is not a melee attack
    CHECK(s.budget.bonus_action);
    REQUIRE(step(s, bonus(1, 1)).ok);       // any melee attack: the dagger
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);                 // the action is untouched
}

TEST_CASE("bonus_attack_available is pure and rejects bad options") {
    auto c = fighter(1); c.bonus_attacks = {martial_arts()};
    auto s = start(c);
    const auto before = s.budget.attacks_used_mask;
    CHECK_FALSE(bonus_attack_available(s, s.combatants[0], 0));
    CHECK_FALSE(bonus_attack_available(s, s.combatants[0], 2));
    CHECK(s.budget.attacks_used_mask == before);
    CHECK(s.budget.bonus_action);
}
