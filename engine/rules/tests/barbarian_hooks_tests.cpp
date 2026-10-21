// VERSION_BUMP_BATCH 7.2z: barbarian hooks. Modifier scope by ability, ends_early, blocks, on_end exhaustion with Frenzy's
// requires_effect bonus attack, Danger Sense save advantages, and replay determinism.
#include <doctest/doctest.h>

#include <algorithm>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kRage = 7001;
const uint32_t kBlastZ = spell_id_of("test.z#blast");

Combatant barbarian(CombatantId id, uint8_t team = 0) {
    auto c = pc(id, 200);
    c.team = team;
    c.abilities.score[std::size_t(Ability::Str)] = 16;
    c.abilities.score[std::size_t(Ability::Dex)] = 18;
    c.attacks = {Attack{"greataxe", 40, {1, 12, 3}, DamageType::Slashing, 5, false, false},
                 Attack{"rapier", 40, {1, 8, 4}, DamageType::Piercing, 5, false, true},
                 Attack{"handaxe", 40, {1, 6, 3}, DamageType::Slashing, 20, true, false}};
    return c;
}
Combatant dummy(CombatantId id) {
    auto m = monster(id);
    m.hp = m.hp_max = 500; m.ac = 1;
    m.attacks = {Attack{"claw", 40, {1, 4, 0}, DamageType::Slashing, 5, false, false}};
    return m;
}
ModifierSpec rage_row(uint32_t id, ModStat st) {
    ModifierSpec m; m.id = id; m.source = kRage; m.stat = st; m.applies_to = ModAppliesTo::Self;
    m.expiry = 1; m.rounds = 10; m.scope = 1; m.scope_ability = 1;
    m.ends_early = 3; m.blocks = 3; m.on_end_exhaustion = 1;
    return m;
}
EncounterHooks hk() {
    auto h = spell_hooks();
    auto b = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kBlastZ; d.content_id = "test.z#blast"; d.level = 0; d.range_ft = 60; d.kind = SpellEffectKind::Save;
    d.save_ability = Ability::Dex; d.fixed_dc = 30; d.on_save = OnSave::None; d.dice = DiceExpr{1, 4, 0};
    d.damage_type = DamageType::Fire; d.targets = 1;
    b->add(d);
    h.spells = b;
    h.feature_modifiers = [](const EncounterState&, const Combatant&, uint32_t f) {
        std::vector<ModifierSpec> v;
        if (f == kRage) { ModifierSpec m = rage_row(kRage + 1, ModStat::Damage); m.bonus = 50; v.push_back(m); }
        return v;
    };
    return h;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed, EncounterHooks h = hk()) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    end_turns_until(s, 1);
    return s;
}
ActionRequest rage() { ActionRequest r{1, ActionKind::UseFeature}; r.feature_id = kRage; r.as_bonus_action = true; return r; }
ActionRequest hit(CombatantId a, CombatantId t, uint8_t idx = 0) {
    ActionRequest r{a, ActionKind::Attack, t, 0, 0, false, true}; r.attack_index = idx; return r;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
bool raging(const EncounterState& s) {
    return std::any_of(s.modifiers.begin(), s.modifiers.end(), [](const ActiveModifier& m) { return m.spec.source == kRage; });
}
void next_own_turn(EncounterState& s) { step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1); }

}  // namespace

TEST_CASE("7.2z scope ability: Rage damage only on a melee weapon attack using Strength") {
    int checked = 0;
    for (uint64_t seed = 0; seed < 10; ++seed) {
        for (uint8_t idx : {uint8_t(0), uint8_t(1), uint8_t(2)}) {
            auto a = start({barbarian(1), dummy(2)}, seed), b = start({barbarian(1), dummy(2)}, seed);
            REQUIRE(step(a, rage()).ok);
            const int with = dealt_to(step(a, hit(1, 2, idx)).events, 2), without = dealt_to(step(b, hit(1, 2, idx)).events, 2);
            if (without == 0) continue;
            if (idx == 0) CHECK(with == without + 50);   // greataxe: Str melee
            else CHECK(with == without);                 // rapier (Dex 18 > Str 16, finesse) and the thrown / ranged handaxe
            ++checked;
        }
    }
    CHECK(checked > 0);
}

TEST_CASE("7.2z ends_early: ends at turn end without an attack on a hostile or damage taken; unconscious ends it at once") {
    auto s = start({barbarian(1), dummy(2)}, 4);
    REQUIRE(step(s, rage()).ok);
    REQUIRE(step(s, hit(1, 2)).ok);
    next_own_turn(s);                       // attacked: still raging (the dummy's own turn may also have damaged it)
    CHECK(raging(s));
    mut(s, 2).attacks.clear();              // no more damage taken
    auto e = step(s, ActionRequest{1, ActionKind::EndTurn});   // no attack this turn and none taken since the last one
    CHECK_FALSE(raging(s));
    CHECK(std::any_of(e.events.begin(), e.events.end(), [](const Event& x) {
        return x.kind == EventKind::ModifierExpired && x.b == 8; }));
    CHECK(find(s, 1)->exhaustion_level == 1);   // on_end; no bonus attack requires it
    // unconscious
    auto u = start({barbarian(1), dummy(2)}, 4);
    REQUIRE(step(u, rage()).ok);
    mut(u, 1).conditions |= bit(Condition::Unconscious);
    step(u, ActionRequest{1, ActionKind::EndTurn});
    CHECK_FALSE(raging(u));
}

TEST_CASE("7.2z blocks: no spells while raging; concentration breaks when it starts") {
    auto w = barbarian(1);
    w.spellcasting_ability = Ability::Int; w.caster_level = 5;
    w.spell_slots_max = {0, 4, 3, 0, 0, 0, 0, 0, 0, 0}; w.spell_slots = w.spell_slots_max;
    w.spells_known = {kBlastZ};
    w.concentration_spell = kHoldPerson;
    auto s = start({w, dummy(2)}, 2);
    auto r = step(s, rage());
    REQUIRE(r.ok);
    CHECK(find(s, 1)->concentration_spell == 0);
    CHECK(count(r.events, EventKind::ConcentrationEnded) == 1);
    CHECK_FALSE(step(s, cast(1, kBlastZ, 2, 0)).ok);
}

TEST_CASE("7.2z Frenzy: requires_effect bonus attack; on_end exhaustion only if it was used") {
    for (bool frenzy : {false, true}) {
        auto b = barbarian(1);
        BonusAttack fr; fr.id = 1; fr.any_melee = true; fr.requires_modifier = kRage + 1;
        b.bonus_attacks = {fr};
        auto s = start({b, dummy(2)}, 6);
        CHECK_FALSE(bonus_attack_available(s, *find(s, 1), 1));   // not raging
        REQUIRE(step(s, rage()).ok);
        REQUIRE(step(s, hit(1, 2)).ok);
        next_own_turn(s);
        if (frenzy) {
            REQUIRE(bonus_attack_available(s, *find(s, 1), 1));
            ActionRequest q = hit(1, 2); q.as_bonus_action = true; q.bonus_option = 1;
            REQUIRE(step(s, q).ok);
        }
        mut(s, 2).attacks.clear();
        if (frenzy) next_own_turn(s);   // attacked this turn; the next turn ends it
        step(s, ActionRequest{1, ActionKind::EndTurn});
        CHECK_FALSE(raging(s));
        CHECK(find(s, 1)->exhaustion_level == (frenzy ? 1 : 0));
    }
}

TEST_CASE("7.2z Danger Sense: advantage on Dex saves vs a seen source; not unseen, not blinded") {
    int better = 0;
    for (uint64_t seed = 0; seed < 30; ++seed) {
        auto run = [&](bool sense, bool blinded, bool unseen) {
            auto w = wizard(1); w.spells_known = {kBlastZ};
            auto b = barbarian(2, 1);
            if (sense) b.save_advantages = {SaveAdvantage{Ability::Dex, true,
                                            bit(Condition::Blinded) | bit(Condition::Deafened) | bit(Condition::Incapacitated)}};
            if (blinded) b.conditions |= bit(Condition::Blinded);
            auto h = hk();
            if (unseen) h.sight_facts = [](const EncounterState&, const Combatant& v, const Combatant& sub) {
                SightFacts f; if (v.id == 2 && sub.id == 1) f.subject_invisible = true; return f; };
            auto s = start({w, b}, seed, h);
            auto r = step(s, cast(1, kBlastZ, 2, 0));
            REQUIRE(r.ok);
            for (const auto& e : r.events) if (e.kind == EventKind::SavingThrowRolled) return int(e.a);
            return -1;
        };
        const int base = run(false, false, false), adv = run(true, false, false);
        REQUIRE(base > 0);
        CHECK(adv >= base);   // advantage: the first d20 is the same draw, the higher of two is kept
        if (adv > base) ++better;
        CHECK(run(true, true, false) == run(false, true, false));    // blinded: no benefit
        CHECK(run(true, false, true) == run(false, false, true));    // can't see the caster: no benefit
    }
    CHECK(better > 0);
}

TEST_CASE("7.2z digest folds the new state only when set; replay is deterministic") {
    auto s = start({barbarian(1), dummy(2)}, 9);
    const uint64_t d0 = modifiers_digest(s);
    REQUIRE(step(s, rage()).ok);
    const uint64_t d1 = modifiers_digest(s);
    CHECK(d1 != d0);
    REQUIRE(step(s, hit(1, 2)).ok);   // fought
    CHECK(modifiers_digest(s) != d1);
    auto once = [](uint64_t seed) {
        auto s2 = start({barbarian(1), dummy(2)}, seed);
        std::vector<Event> all;
        for (const auto& q : {rage(), hit(1, 2)}) { auto r = step(s2, q); all.insert(all.end(), r.events.begin(), r.events.end()); }
        next_own_turn(s2);
        auto r = step(s2, ActionRequest{1, ActionKind::EndTurn});
        all.insert(all.end(), r.events.begin(), r.events.end());
        return std::make_pair(all.size(), modifiers_digest(s2) ^ uint64_t(find(s2, 2)->hp) ^ uint64_t(find(s2, 1)->exhaustion_level));
    };
    for (uint64_t seed = 0; seed < 5; ++seed) CHECK(once(seed) == once(seed));
}
