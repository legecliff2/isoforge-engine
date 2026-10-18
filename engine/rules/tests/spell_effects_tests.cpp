// Multi-effect spells (R-G2 / S6): extra effects, riders (applies_to: affected), the no-save apply_condition effect.
#include <doctest/doctest.h>

#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kLaugh = spell_id_of("test.spell.laughter");
const uint32_t kSear = spell_id_of("test.spell.sear");
const uint32_t kInvis = spell_id_of("test.spell.invis");
const uint32_t kBoth = spell_id_of("test.spell.both");

SpellEffect cond_effect(Condition c, bool rider) {
    SpellEffect e; e.kind = SpellEffectKind::ApplyCondition; e.condition = c; e.applies_condition = true; e.affected_only = rider;
    return e;
}

EncounterHooks hooks() {
    auto book = srd_spellbook();
    SpellDef laugh; laugh.content_id = "test.spell.laughter"; laugh.id = kLaugh; laugh.level = 1;
    laugh.components = kComponentV | kComponentS; laugh.range_ft = 30; laugh.concentration = true;
    laugh.kind = SpellEffectKind::Save; laugh.save_ability = Ability::Wis; laugh.applies_condition = true;
    laugh.condition = Condition::Prone; laugh.repeat_save = RepeatSave::EndOfTurn;
    laugh.duration = SpellDuration::Minutes; laugh.duration_amount = 1;
    laugh.extra_effects = {cond_effect(Condition::Incapacitated, true)};
    book->add(laugh);
    SpellDef sear; sear.content_id = "test.spell.sear"; sear.id = kSear; sear.level = 1;   // spell attack + blinded rider on a hit
    sear.components = kComponentV | kComponentS; sear.range_ft = 60; sear.kind = SpellEffectKind::SpellAttack;
    sear.dice = {1, 8, 0}; sear.damage_type = DamageType::Radiant; sear.upcast_dice = 1;
    sear.extra_effects = {cond_effect(Condition::Blinded, true)};
    book->add(sear);
    SpellDef invis; invis.content_id = "test.spell.invis"; invis.id = kInvis; invis.level = 2;
    invis.components = kComponentV | kComponentS; invis.range_ft = 5; invis.concentration = true;
    invis.kind = SpellEffectKind::ApplyCondition; invis.condition = Condition::Invisible; invis.applies_condition = true;
    invis.duration = SpellDuration::Hours; invis.duration_amount = 1;
    book->add(invis);
    SpellDef both; both.content_id = "test.spell.both"; both.id = kBoth; both.level = 1;   // damage on a failed save + unconditional rider on all
    both.components = kComponentV | kComponentS; both.range_ft = 60; both.kind = SpellEffectKind::Save;
    both.save_ability = Ability::Dex; both.dice = {2, 6, 0}; both.damage_type = DamageType::Thunder; both.on_save = OnSave::Half;
    both.upcast_dice = 1;
    SpellEffect heal; heal.kind = SpellEffectKind::Heal; heal.dice = {1, 4, 0};
    both.extra_effects = {heal};
    book->add(both);
    EncounterHooks h;
    h.spells = book;
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    return h;
}

Combatant caster(CombatantId id) {
    auto c = wizard(id);
    c.spells_known.insert(c.spells_known.end(), {kLaugh, kSear, kInvis, kBoth});
    return c;
}
Combatant tank(CombatantId id) { auto c = monster(id); c.hp = c.hp_max = 5000; c.ac = 10; return c; }
const Event* first(const std::vector<Event>& ev, EventKind k) { for (const auto& e : ev) if (e.kind == k) return &e; return nullptr; }
} // namespace

TEST_CASE("effects array: a failed save applies every condition; one save, one concentration") {
    int failed = 0, saved = 0;
    for (uint64_t seed = 0; seed < 80; ++seed) {
        auto s = begin_encounter({caster(1), tank(2)}, seed, nullptr, hooks());
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kLaugh, 2, 1));
        REQUIRE(r.ok);
        CHECK(count(r.events, EventKind::SavingThrowRolled) == 1);   // the rider adds no save
        const Combatant& t = *find(s, 2);
        if (first(r.events, EventKind::SavingThrowRolled)->c) {
            ++saved;
            CHECK_FALSE(t.has(Condition::Prone)); CHECK_FALSE(t.has(Condition::Incapacitated));
            CHECK(s.effects.empty()); CHECK(find(s, 1)->concentration_spell == 0);
        } else {
            ++failed;
            CHECK(t.has(Condition::Prone)); CHECK(t.has(Condition::Incapacitated));
            CHECK(s.effects.size() == 2);
            CHECK(count(r.events, EventKind::ConditionAdded) == 2);
            CHECK(find(s, 1)->concentration_spell == kLaugh);
        }
    }
    CHECK(failed > 0); CHECK(saved > 0);
}

TEST_CASE("effects array: a successful repeat save ends both effects and the concentration") {
    int freed = 0;
    for (uint64_t seed = 0; seed < 200 && freed < 3; ++seed) {
        auto s = begin_encounter({caster(1), tank(2)}, seed, nullptr, hooks());
        end_turns_until(s, 1);
        if (!step(s, cast(1, kLaugh, 2, 1)).ok || s.effects.size() != 2) continue;
        end_turns_until(s, 2);
        auto e = step(s, {2, ActionKind::EndTurn});
        CHECK(count(e.events, EventKind::SavingThrowRolled) == 1);   // one repeat save for the spell, not one per effect
        if (s.effects.empty()) {
            ++freed;
            CHECK_FALSE(find(s, 2)->has(Condition::Prone)); CHECK_FALSE(find(s, 2)->has(Condition::Incapacitated));
            CHECK(find(s, 1)->concentration_spell == 0);
        } else {
            CHECK(s.effects.size() == 2);   // still held: both remain
            CHECK(find(s, 2)->has(Condition::Incapacitated));
        }
    }
    CHECK(freed > 0);
}

TEST_CASE("effects array: a rider resolves only for targets the previous effect affected") {
    int hits = 0, misses = 0;
    for (uint64_t seed = 0; seed < 120; ++seed) {
        auto s = begin_encounter({caster(1), tank(2)}, seed, nullptr, hooks());
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kSear, 2, 1));
        REQUIRE(r.ok);
        const Event* a = first(r.events, EventKind::AttackResolved);
        REQUIRE(a);
        const bool hit = (a->c & 1) != 0;
        CHECK(find(s, 2)->has(Condition::Blinded) == hit);
        CHECK(count(r.events, EventKind::ConditionAdded) == (hit ? 1 : 0));
        (hit ? hits : misses)++;
    }
    CHECK(hits > 0); CHECK(misses > 0);
}

TEST_CASE("effects array: apply_condition alone has no save and lasts with concentration") {
    auto s = begin_encounter({caster(1), tank(2)}, 3, nullptr, hooks());
    mut(s, 2).team = 0;   // an ally
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kInvis, 2, 2));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::SavingThrowRolled) == 0);
    CHECK(find(s, 2)->has(Condition::Invisible));
    CHECK(find(s, 1)->concentration_spell == kInvis);
    CHECK(s.effects.size() == 1);
    // legal_actions offers it on allies (Invisible is the beneficial condition), not on hostiles
    auto u = begin_encounter({caster(1), tank(2), tank(3)}, 3, nullptr, hooks());
    mut(u, 2).team = 0; mut(u, 3).team = 1;
    end_turns_until(u, 1);
    bool ally = false, hostile = false;
    for (const auto& q : legal_actions(u)) if (q.kind == ActionKind::CastSpell && q.spell_id == kInvis) { (q.target == 3 ? hostile : ally) = true; }
    CHECK(ally); CHECK_FALSE(hostile);
}

TEST_CASE("effects array: condition immunity blocks apply_condition; extras never scale") {
    auto s = begin_encounter({caster(1), tank(2)}, 3, nullptr, hooks());
    mut(s, 2).team = 0; mut(s, 2).condition_immunities = bit(Condition::Invisible);
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kInvis, 2, 2)).ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Invisible));
    CHECK(s.effects.empty());
    CHECK(find(s, 1)->concentration_spell == 0);   // nothing applied: no concentration to hold

    const auto h = hooks();
    const SpellDef* both = h.spells->find(kBoth);
    REQUIRE(both);
    const SpellDef v = effect_view(*both, both->extra_effects[0]);
    CHECK(v.kind == SpellEffectKind::Heal);
    CHECK(v.extra_effects.empty());
    CHECK(v.upcast_dice == 0);
    CHECK(scaled_dice(v, 5, 5) == DiceExpr{1, 4, 0});     // the primary's upcast does not leak into the extra effect
    CHECK(scaled_dice(*both, 3, 5).count == 4);           // while the first effect still scales (2d6 + 2)
}

TEST_CASE("effects array: an unconditional extra effect resolves for every target") {
    auto s = begin_encounter({caster(1), tank(2)}, 5, nullptr, hooks());
    mut(s, 2).hp = 100;
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kBoth, 2, 1));
    REQUIRE(r.ok);
    CHECK(count(r.events, EventKind::SavingThrowRolled) == 1);
    CHECK(count(r.events, EventKind::Healed) == 1);   // the heal effect ran on the same target, save or not
}
