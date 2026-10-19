// R-G15 (rest): end-a-condition (Lesser Restoration), delayed damage (Acid Arrow), sustained damage (Heat Metal), Wake.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kRestore = spell_id_of("test.spell#lesser_restoration");
const uint32_t kAcidLater = spell_id_of("test.spell#acid_later");
const uint32_t kAcidArrow = spell_id_of("test.spell#acid_arrow");
const uint32_t kHeatMetal = spell_id_of("test.spell#heat_metal");
const uint32_t kSleep2 = spell_id_of("test.spell#sleep2");

EncounterHooks hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef lr; lr.id = kRestore; lr.content_id = "test.spell#lesser_restoration"; lr.level = 2; lr.range_ft = 5;
    lr.kind = SpellEffectKind::EndCondition;
    lr.ends_conditions = bit(Condition::Blinded) | bit(Condition::Deafened) | bit(Condition::Paralyzed) | bit(Condition::Poisoned);
    book->add(lr);
    SpellDef dl; dl.id = kAcidLater; dl.content_id = "test.spell#acid_later"; dl.level = 2; dl.range_ft = 90;
    dl.kind = SpellEffectKind::DelayedDamage; dl.dice = DiceExpr{2, 4, 0}; dl.damage_type = DamageType::Acid; dl.expiry = 2;
    book->add(dl);
    SpellDef aa; aa.id = kAcidArrow; aa.content_id = "test.spell#acid_arrow"; aa.level = 2; aa.range_ft = 90;
    aa.kind = SpellEffectKind::SpellAttack; aa.dice = DiceExpr{4, 4, 0}; aa.damage_type = DamageType::Acid; aa.fixed_attack = 30;
    SpellEffect later; later.kind = SpellEffectKind::DelayedDamage; later.dice = DiceExpr{2, 4, 0}; later.damage_type = DamageType::Acid;
    later.expiry = 2; later.affected_only = true;
    aa.extra_effects = {later};
    book->add(aa);
    SpellDef hm; hm.id = kHeatMetal; hm.content_id = "test.spell#heat_metal"; hm.level = 2; hm.range_ft = 60;
    hm.kind = SpellEffectKind::SustainedDamage; hm.dice = DiceExpr{2, 8, 0}; hm.upcast_dice = 1; hm.damage_type = DamageType::Fire;
    hm.concentration = true;
    book->add(hm);
    SpellDef sl; sl.id = kSleep2; sl.content_id = "test.spell#sleep2"; sl.level = 1; sl.range_ft = 90;
    sl.kind = SpellEffectKind::HpPool; sl.dice = DiceExpr{0, 0, 40}; sl.condition = Condition::Unconscious;
    sl.expiry = 1; sl.duration_rounds = 10; sl.ends_on_damage = true;
    book->add(sl);
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id) {
    auto c = wizard(id);
    c.spells_known = {kRestore, kAcidLater, kAcidArrow, kHeatMetal, kSleep2};
    return c;
}
Combatant mob(CombatantId id, int hp) { auto m = monster(id); m.hp = m.hp_max = int16_t(hp); return m; }
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, hooks());
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_at(uint32_t spell, CombatantId target, uint8_t level) {
    ActionRequest r{1, ActionKind::CastSpell, target};
    r.spell_id = spell; r.spell_level = level;
    return r;
}
ActionRequest sustain(CombatantId target) {
    ActionRequest r{1, ActionKind::CastSpell, target};
    r.spell_id = kHeatMetal; r.sustain = true;
    return r;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
bool has_ev(const std::vector<Event>& ev, EventKind k) { for (const auto& e : ev) if (e.kind == k) return true; return false; }
void end_turn_of(EncounterState& s, CombatantId id, std::vector<Event>* out = nullptr) {
    end_turns_until(s, id);
    const auto r = step(s, ActionRequest{id, ActionKind::EndTurn});
    if (out) *out = r.events;
}
}  // namespace

TEST_CASE("enums and defaults") {
    CHECK(uint8_t(SpellEffectKind::EndCondition) == 11);
    CHECK(uint8_t(SpellEffectKind::DelayedDamage) == 12);
    CHECK(uint8_t(SpellEffectKind::SustainedDamage) == 13);
    SpellDef d; CHECK(d.ends_conditions == 0);
    ActiveSpellEffect e{1, 2, 3, Condition::Blinded, Ability::Wis, 0, RepeatSave::None, false};
    CHECK_FALSE(e.no_condition); CHECK(e.damage_when == 0); CHECK(e.cast_stamp == 0); CHECK(e.magical);
    ActionRequest r{1, ActionKind::CastSpell};
    CHECK_FALSE(r.sustain);
}

TEST_CASE("EndCondition: one listed condition per cast, lowest first, ending the effect that caused it") {
    auto s = start({caster(1), mob(2, 30), pc(3, 30)});
    s.effects.push_back(ActiveSpellEffect{kHoldPerson, 2, 3, Condition::Paralyzed, Ability::Wis, 13, RepeatSave::EndOfTurn, true});
    mut(s, 3).conditions |= bit(Condition::Paralyzed) | bit(Condition::Poisoned) | bit(Condition::Prone);
    mut(s, 2).concentration_spell = kHoldPerson;
    const auto r = step(s, cast_at(kRestore, 3, 2));
    REQUIRE(r.ok);
    // Paralyzed (8) < Poisoned (10): the paralysis ends, through its spell effect
    CHECK_FALSE(find(s, 3)->has(Condition::Paralyzed));
    CHECK(find(s, 3)->has(Condition::Poisoned));
    CHECK(has_ev(r.events, EventKind::SpellEffectEnded));
    CHECK(s.effects.empty());
    end_turns_until(s, 3); end_turns_until(s, 1);
    const auto q = step(s, cast_at(kRestore, 3, 2));
    REQUIRE(q.ok);
    CHECK_FALSE(find(s, 3)->has(Condition::Poisoned));
    CHECK(find(s, 3)->has(Condition::Prone));   // not in the list
    CHECK(has_ev(q.events, EventKind::ConditionRemoved));
    end_turns_until(s, 3); end_turns_until(s, 1);
    const auto n = step(s, cast_at(kRestore, 3, 2));   // nothing to end
    REQUIRE(n.ok);
    CHECK_FALSE(has_ev(n.events, EventKind::ConditionRemoved));
    CHECK(find(s, 3)->has(Condition::Prone));
}

TEST_CASE("DelayedDamage: lands at the end of the target's next turn, then the effect ends") {
    auto s = start({caster(1), mob(2, 60), mob(4, 60)});
    const auto r = step(s, cast_at(kAcidLater, 2, 2));
    REQUIRE(r.ok);
    CHECK(dealt_to(r.events, 2) == 0);
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].no_condition); CHECK(s.effects[0].damage_when == 1);
    CHECK(s.effects[0].timed.kind == Expiry::EndOfTargetNextTurn);
    std::vector<Event> ev;
    end_turn_of(s, 1, &ev);   // the caster's turn ends: nothing yet
    CHECK(dealt_to(ev, 2) == 0);
    end_turn_of(s, 2, &ev);   // the target's turn ends: 2d4 acid
    const int d = dealt_to(ev, 2);
    CHECK(d >= 2); CHECK(d <= 8);
    CHECK(s.effects.empty());
    CHECK(has_ev(ev, EventKind::SpellEffectEnded));
    CHECK(find(s, 2)->conditions == 0);   // no condition was ever applied
}

TEST_CASE("Acid Arrow: the delayed rider only follows a hit") {
    int hits = 0, misses = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start({caster(1), mob(2, 200), mob(4, 60)}, seed);
        const auto r = step(s, cast_at(kAcidArrow, 2, 2));
        REQUIRE(r.ok);
        bool hit = false;
        for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) hit = e.c & 1;
        CHECK((s.effects.size() == 1) == hit);
        (hit ? hits : misses)++;
    }
    CHECK(hits > 0); CHECK(misses > 0);   // a natural 1 misses even at +30
}

TEST_CASE("SustainedDamage: damage now, again with a bonus action on later turns, ends with concentration") {
    auto s = start({caster(1), mob(2, 300), mob(4, 60)});
    const auto r = step(s, cast_at(kHeatMetal, 2, 3));   // upcast: 3d8
    REQUIRE(r.ok);
    const int first = dealt_to(r.events, 2);
    CHECK(first >= 3); CHECK(first <= 24);
    CHECK(find(s, 1)->concentration_spell == kHeatMetal);
    REQUIRE(s.effects.size() == 1);
    CHECK(s.effects[0].damage_when == 2); CHECK(s.effects[0].damage.count == 3);
    CHECK_FALSE(step(s, sustain(2)).ok);   // not on the turn it was cast
    end_turns_until(s, 2); end_turns_until(s, 1);
    CHECK_FALSE(step(s, sustain(4)).ok);   // no effect on that creature
    const auto q = step(s, sustain(2));
    REQUIRE(q.ok);
    const int again = dealt_to(q.events, 2);
    CHECK(again >= 3); CHECK(again <= 24);
    CHECK_FALSE(s.budget.bonus_action);
    CHECK(s.budget.action);
    CHECK(has_ev(q.events, EventKind::SpellResolved));
    CHECK_FALSE(has_ev(q.events, EventKind::SpellCast));
    CHECK(find(s, 1)->spell_slots[3] == 1);   // one slot for the cast, none for sustaining
    CHECK_FALSE(step(s, sustain(2)).ok);   // the bonus action is spent
    end_turns_until(s, 2); end_turns_until(s, 1);
    mut(s, 1).conditions |= bit(Condition::Stunned);   // concentration breaks after the next step
    REQUIRE(encounter_check(s, 2, Skill::Athletics, 10).ok);
    CHECK(s.effects.empty());
    mut(s, 1).conditions = 0;
    CHECK_FALSE(step(s, sustain(2)).ok);
}

TEST_CASE("Wake (special 5): an action ends a sleeper's ends_on_damage effects") {
    auto ally = pc(3, 30);
    auto s = start({caster(1), mob(2, 5), ally, mob(4, 200)});
    REQUIRE(step(s, cast_at(kSleep2, 2, 1)).ok);
    REQUIRE(find(s, 2)->has(Condition::Unconscious));
    end_turns_until(s, 3);
    ActionRequest wake{3, ActionKind::Attack, 2}; wake.special = 5;
    CHECK_FALSE(step(s, wake).ok);              // not within 5 ft
    wake.target_within_5ft = true;
    ActionRequest other = wake; other.target = 4;
    CHECK_FALSE(step(s, other).ok);             // not asleep
    CHECK(s.budget.action);
    const auto r = step(s, wake);
    REQUIRE(r.ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Unconscious));
    CHECK(has_ev(r.events, EventKind::SpellEffectEnded));
    CHECK_FALSE(s.budget.action);
    CHECK(s.effects.empty());
}
