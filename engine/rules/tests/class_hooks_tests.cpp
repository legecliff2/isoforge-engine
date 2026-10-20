// VERSION_BUMP_BATCH 7.2y: class feature hooks. Fast Hands (UseObject 1..3, Cunning Action bonus action), weapon-bound
// (scope wielded) modifiers, bonus_vs damage dice, target_creature_types on feature save actions, replay determinism.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kTurn = spell_id_of("test.cleric#turn_undead");
const uint32_t kSacred = 9001;

Combatant rogue(CombatantId id) {
    auto c = pc(id, 80);
    c.attacks = {Attack{"rapier", 40, {1, 8, 0}, DamageType::Piercing, 5, false, true},
                 Attack{"shortbow", 40, {1, 6, 0}, DamageType::Piercing, 80, true, false}};
    c.proficiency_bonus = 3;
    return c;
}
Combatant dummy(CombatantId id, CreatureType ct = CreatureType::Humanoid) {
    auto m = monster(id);
    m.hp = m.hp_max = 500;
    m.ac = 1;
    m.creature_type = ct;
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    return m;
}
EncounterHooks hk() {
    auto h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kTurn; d.content_id = "test.cleric#turn_undead"; d.level = 0; d.range_ft = 30;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Wis; d.fixed_dc = 13; d.on_save = OnSave::None;
    d.dice = DiceExpr{1, 4, 0}; d.damage_type = DamageType::Radiant; d.targets = 1; d.is_spell = false; d.magical = false;
    d.target_types = bit(CreatureType::Undead) | bit(CreatureType::Fiend);
    b->add(d);
    h.spells = b;
    h.feature_modifiers = [](const EncounterState&, const Combatant&, uint32_t f) {
        std::vector<ModifierSpec> v;
        if (f == kSacred) {
            ModifierSpec m; m.id = 77; m.source = 77; m.stat = ModStat::AttackRoll; m.bonus = 100;
            m.applies_to = ModAppliesTo::Self; m.weapon_scope = 1; v.push_back(m);
        }
        return v;
    };
    return h;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed, EncounterHooks h = hk()) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    end_turns_until(s, 1);
    return s;
}
ActionRequest use_obj(CombatantId a, uint8_t k, int dc, bool bonus) {
    ActionRequest r{a, ActionKind::UseObject}; r.object_use = k; r.dc = int16_t(dc); r.as_bonus_action = bonus; return r;
}
ActionRequest hit(CombatantId a, CombatantId t, uint8_t idx = 0) {
    ActionRequest r{a, ActionKind::Attack, t, 0, 0, false, true}; r.attack_index = idx; return r;
}
ActionRequest sacred(CombatantId a, uint8_t idx) {
    ActionRequest r{a, ActionKind::UseFeature}; r.feature_id = kSacred; r.attack_index = idx; return r;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}

}  // namespace

TEST_CASE("7.2y Fast Hands: UseObject as an action; as a bonus action only with the grant") {
    auto s = start({rogue(1), dummy(2)}, 3);
    CHECK_FALSE(step(s, use_obj(1, 0, 0, false)).ok);
    CHECK_FALSE(step(s, use_obj(1, 2, 0, false)).ok);       // a check needs a dc
    CHECK_FALSE(step(s, use_obj(1, 1, 0, true)).ok);        // no Fast Hands
    auto r = step(s, use_obj(1, 1, 0, false));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::CheckRolled) == 0);
    CHECK_FALSE(step(s, use_obj(1, 1, 0, false)).ok);       // action spent
    auto fh = rogue(1);
    fh.bonus_action_grants |= bit(ActionKind::UseObject);
    auto s2 = start({fh, dummy(2)}, 3);
    auto b = step(s2, use_obj(1, 2, 15, true));             // Sleight of Hand as the bonus action
    REQUIRE(b.ok);
    CHECK(count(b.events, EventKind::CheckRolled) == 1);
    CHECK(step(s2, use_obj(1, 3, 15, false)).ok);           // thieves' tools with the action
    CHECK_FALSE(step(s2, use_obj(1, 3, 15, true)).ok);      // bonus action already spent
}

TEST_CASE("7.2y Fast Hands: thieves' tools add proficiency to the Dex check") {
    int diff = 0;
    for (uint64_t seed = 0; seed < 20; ++seed) {
        auto a = rogue(1), b = rogue(1);
        b.thieves_tools = true;
        auto sa = start({a, dummy(2)}, seed), sb = start({b, dummy(2)}, seed);
        auto ra = step(sa, use_obj(1, 3, 15, false)), rb = step(sb, use_obj(1, 3, 15, false));
        REQUIRE(ra.ok); REQUIRE(rb.ok);
        const auto ia = std::find_if(ra.events.begin(), ra.events.end(), [](const Event& e) { return e.kind == EventKind::CheckRolled; });
        const auto ib = std::find_if(rb.events.begin(), rb.events.end(), [](const Event& e) { return e.kind == EventKind::CheckRolled; });
        REQUIRE(ia != ra.events.end()); REQUIRE(ib != rb.events.end());
        CHECK(ib->b - ia->b == 3);
        diff += ib->b - ia->b;
    }
    CHECK(diff == 60);
}

TEST_CASE("7.2y scope wielded: the bound modifier applies only to that weapon and ends when the owner drops") {
    auto s = start({rogue(1), dummy(2)}, 5);
    CHECK_FALSE(step(s, sacred(1, 1)).ok);   // a ranged attack can't be bound (wielded melee weapon)
    CHECK_FALSE(step(s, sacred(1, 9)).ok);   // no such weapon
    auto u = step(s, sacred(1, 0));
    INFO(u.error);
    REQUIRE(u.ok);
    REQUIRE(s.modifiers.size() == 1);
    CHECK(s.modifiers[0].weapon == 1);
    const uint64_t dg = modifiers_digest(s);
    s.modifiers[0].weapon = 0;
    CHECK(modifiers_digest(s) != dg);        // the binding is folded
    s.modifiers[0].weapon = 1;
    end_turns_until(s, 2); end_turns_until(s, 1);   // Sacred Weapon took the action
    // +100 with the rapier always hits; the shortbow does not get it (AC 1 hits anyway, so compare to-hit totals)
    auto r = step(s, hit(1, 2, 0));
    REQUIRE(r.ok);
    const auto ar = std::find_if(r.events.begin(), r.events.end(), [](const Event& e) { return e.kind == EventKind::AttackResolved; });
    REQUIRE(ar != r.events.end());
    CHECK(ar->b >= 100);
    end_turns_until(s, 2); end_turns_until(s, 1);
    auto r2 = step(s, hit(1, 2, 1));
    REQUIRE(r2.ok);
    const auto ar2 = std::find_if(r2.events.begin(), r2.events.end(), [](const Event& e) { return e.kind == EventKind::AttackResolved; });
    REQUIRE(ar2 != r2.events.end());
    CHECK(ar2->b < 100);
    // falls unconscious: the modifier ends (reason 7) after the next step
    mut(s, 1).conditions |= bit(Condition::Unconscious);
    auto e = step(s, ActionRequest{1, ActionKind::EndTurn});
    (void)e;
    CHECK(s.modifiers.empty());
}

TEST_CASE("7.2y bonus_vs: the extra dice only against the listed creature types") {
    int more = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto r = rogue(1);
        r.attacks[0].vs_types = bit(CreatureType::Undead);
        r.attacks[0].vs_dice = DiceExpr{0, 0, 50};
        auto sh = start({r, dummy(2, CreatureType::Humanoid)}, seed), su = start({r, dummy(2, CreatureType::Undead)}, seed);
        auto plain = rogue(1);
        auto sp = start({plain, dummy(2, CreatureType::Humanoid)}, seed);
        const int h = dealt_to(step(sh, hit(1, 2)).events, 2), u = dealt_to(step(su, hit(1, 2)).events, 2);
        const int p = dealt_to(step(sp, hit(1, 2)).events, 2);
        CHECK(h == p);
        if (p > 0) { CHECK(u >= p + 50); ++more; }
    }
    CHECK(more > 0);
}

TEST_CASE("7.2y target_creature_types: a feature save action skips other types (no save, no effect)") {
    auto s = start({rogue(1), dummy(2, CreatureType::Humanoid)}, 7);
    mut(s, 1).spells_known = {kTurn};
    auto r = step(s, cast(1, kTurn, 2, 0));
    CHECK(count(r.events, EventKind::SavingThrowRolled) == 0);
    CHECK(dealt_to(r.events, 2) == 0);
    auto s2 = start({rogue(1), dummy(2, CreatureType::Undead)}, 7);
    mut(s2, 1).spells_known = {kTurn};
    auto r2 = step(s2, cast(1, kTurn, 2, 0));
    REQUIRE(r2.ok);
    CHECK(count(r2.events, EventKind::SavingThrowRolled) == 1);
}

TEST_CASE("7.2y replay determinism: the same seed and requests give the same events and state") {
    auto once = [](uint64_t seed) {
        auto fh = rogue(1);
        fh.bonus_action_grants |= bit(ActionKind::UseObject);
        fh.thieves_tools = true;
        fh.attacks[0].vs_types = bit(CreatureType::Undead);
        fh.attacks[0].vs_dice = DiceExpr{1, 8, 0};
        auto s = start({fh, dummy(2, CreatureType::Undead)}, seed);
        std::vector<Event> all;
        for (const auto& q : {sacred(1, 0), use_obj(1, 3, 15, true)}) { auto r = step(s, q); all.insert(all.end(), r.events.begin(), r.events.end()); }
        end_turns_until(s, 1);
        auto r = step(s, hit(1, 2));
        all.insert(all.end(), r.events.begin(), r.events.end());
        return std::make_pair(all, modifiers_digest(s) ^ uint64_t(find(s, 2)->hp));
    };
    for (uint64_t seed = 0; seed < 5; ++seed) {
        auto a = once(seed), b = once(seed);
        REQUIRE(a.first.size() == b.first.size());
        for (std::size_t i = 0; i < a.first.size(); ++i) {
            CHECK(a.first[i].kind == b.first[i].kind); CHECK(a.first[i].a == b.first[i].a); CHECK(a.first[i].b == b.first[i].b);
        }
        CHECK(a.second == b.second);
    }
}
