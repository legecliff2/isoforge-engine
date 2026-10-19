// R-G15 spell kinds: HP-pool (Sleep, Color Spray), Dispel Magic, Revivify.
#include <doctest/doctest.h>

#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kSleep = spell_id_of("test.spell#sleep");
const uint32_t kDispel = spell_id_of("test.spell#dispel");
const uint32_t kRevivify = spell_id_of("test.spell#revivify");
const uint32_t kBlessT = spell_id_of("test.spell#bless");

SpellDef sleep_def(DiceExpr pool) {
    SpellDef d; d.id = kSleep; d.content_id = "test.spell#sleep"; d.level = 1; d.range_ft = 90;
    d.kind = SpellEffectKind::HpPool; d.dice = pool; d.upcast_dice = 2; d.condition = Condition::Unconscious;
    d.expiry = 1; d.duration_rounds = 10; d.ends_on_damage = true; d.targets = 8;
    d.excluded_types = bit(CreatureType::Undead);
    return d;
}
EncounterHooks hooks(DiceExpr pool = DiceExpr{0, 0, 12}) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(sleep_def(pool));
    SpellDef dm; dm.id = kDispel; dm.content_id = "test.spell#dispel"; dm.level = 3; dm.range_ft = 120; dm.kind = SpellEffectKind::Dispel;
    book->add(dm);
    SpellDef rv; rv.id = kRevivify; rv.content_id = "test.spell#revivify"; rv.level = 3; rv.range_ft = 5; rv.kind = SpellEffectKind::Revive;
    book->add(rv);
    SpellDef bl; bl.id = kBlessT; bl.content_id = "test.spell#bless"; bl.level = 1; bl.kind = SpellEffectKind::ApplyModifier;
    bl.concentration = true; bl.targets = 3;
    bl.modifier.id = 40; bl.modifier.stat = ModStat::AttackRoll; bl.modifier.die = DiceExpr{1, 4, 0}; bl.modifier.applies_to = ModAppliesTo::Target;
    book->add(bl);
    h.spells = book;
    return h;
}
Combatant caster(CombatantId id) {
    auto c = wizard(id);
    c.spells_known = {kSleep, kDispel, kRevivify, kBlessT};
    c.spell_slots_max = {0, 4, 3, 3, 0, 1, 0, 0, 0, 0};
    c.spell_slots = c.spell_slots_max;
    return c;
}
Combatant mob(CombatantId id, int hp) { auto m = monster(id); m.hp = m.hp_max = int16_t(hp); return m; }
EncounterState start(std::vector<Combatant> cs, EncounterHooks h = hooks(), uint64_t seed = 1) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, std::move(h));
    end_turns_until(s, 1);
    return s;
}
ActionRequest cast_at(uint32_t spell, std::vector<CombatantId> targets, uint8_t level) {
    ActionRequest r{1, ActionKind::CastSpell, targets[0]};
    r.spell_id = spell; r.spell_level = level; r.targets = std::move(targets);
    return r;
}
bool has_ev(const std::vector<Event>& ev, EventKind k) { for (const auto& e : ev) if (e.kind == k) return true; return false; }
}  // namespace

TEST_CASE("enums and defaults") {
    CHECK(uint8_t(SpellEffectKind::HpPool) == 8);
    CHECK(uint8_t(SpellEffectKind::Dispel) == 9);
    CHECK(uint8_t(SpellEffectKind::Revive) == 10);
    SpellDef d;
    CHECK_FALSE(d.ends_on_damage); CHECK(d.dispel_dc_base == 10); CHECK(d.revive_within_rounds == 10);
    ActiveSpellEffect e{1, 2, 3, Condition::Blinded, Ability::Wis, 0, RepeatSave::None, false};
    CHECK(e.slot_level == 0); CHECK_FALSE(e.ends_on_damage);
    ActiveModifier m;
    CHECK(m.slot_level == 0);
    Combatant c{};
    CHECK(c.died_round == 0);
    CHECK(scaled_dice(sleep_def(DiceExpr{5, 8, 0}), 3, 5).count == 9);   // Sleep: 5d8 + 2d8 per slot level above 1st
}

TEST_CASE("HP pool: lowest current HP first, stops at the first creature the remainder cannot cover") {
    auto s = start({caster(1), mob(2, 7), mob(3, 3), mob(4, 30), mob(5, 5)});
    const auto r = step(s, cast_at(kSleep, {2, 3, 4, 5}, 1));
    REQUIRE(r.ok);
    // pool 12: 3 (creature 3) -> 9, 5 (creature 5) -> 4, 7 > 4: stop
    CHECK(find(s, 3)->has(Condition::Unconscious));
    CHECK(find(s, 5)->has(Condition::Unconscious));
    CHECK_FALSE(find(s, 2)->has(Condition::Unconscious));
    CHECK_FALSE(find(s, 4)->has(Condition::Unconscious));
    int added = 0; for (const auto& e : r.events) added += e.kind == EventKind::ConditionAdded;
    CHECK(added == 2);
    REQUIRE(s.effects.size() == 2);
    CHECK(s.effects[0].ends_on_damage); CHECK(s.effects[0].slot_level == 1);
    CHECK(s.effects[0].timed.kind == Expiry::Rounds);
}

TEST_CASE("HP pool: skips the dead, the dropped, the immune, excluded types and those already affected; ties by id") {
    auto undead = mob(2, 1); undead.creature_type = CreatureType::Undead;
    auto immune = mob(3, 1); immune.condition_immunities = bit(Condition::Unconscious);
    auto down = mob(4, 1); down.hp = 0;
    auto tie_a = mob(6, 6), tie_b = mob(5, 6);
    auto s = start({caster(1), undead, immune, down, tie_b, tie_a});
    REQUIRE(step(s, cast_at(kSleep, {2, 3, 4, 5, 6}, 1)).ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Unconscious));
    CHECK_FALSE(find(s, 3)->has(Condition::Unconscious));
    CHECK(find(s, 5)->has(Condition::Unconscious));      // 6 HP each: id 5 first (12 -> 6), then id 6 (6 -> 0)
    CHECK(find(s, 6)->has(Condition::Unconscious));
    auto t = start({caster(1), mob(5, 7), mob(6, 7)});
    REQUIRE(step(t, cast_at(kSleep, {6, 5}, 1)).ok);       // 12 covers one 7: the lower id
    CHECK(find(t, 5)->has(Condition::Unconscious));
    CHECK_FALSE(find(t, 6)->has(Condition::Unconscious));
}

TEST_CASE("HP pool: Sleep ends when the sleeper takes damage") {
    bool checked = false;
    for (uint64_t seed = 0; seed < 20 && !checked; ++seed) {
        auto fighter = pc(5, 50);
        fighter.attacks = {Attack{"club", 40, {0, 0, 1}, DamageType::Bludgeoning, 5, false, false}};
        auto s = start({caster(1), mob(2, 7), fighter, mob(4, 40)}, hooks(), seed);
        REQUIRE(step(s, cast_at(kSleep, {2}, 1)).ok);
        REQUIRE(find(s, 2)->has(Condition::Unconscious));
        end_turns_until(s, 5);
        ActionRequest hit{5, ActionKind::Attack, 2, 0, 0, false, true};
        const auto r = step(s, hit);
        INFO(r.error, " actor ", current_actor(s));
        REQUIRE(r.ok);
        bool landed = false;
        for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved) landed = e.c & 1;
        if (!landed) continue;
        checked = true;
        CHECK_FALSE(find(s, 2)->has(Condition::Unconscious));
        CHECK(has_ev(r.events, EventKind::SpellEffectEnded));
        CHECK(s.effects.empty());
    }
    CHECK(checked);
}

namespace {
void hold(EncounterState& s, CombatantId caster_id, CombatantId target, uint8_t slot) {
    s.effects.push_back(ActiveSpellEffect{kHoldPerson, caster_id, target, Condition::Paralyzed, Ability::Wis, 13, RepeatSave::EndOfTurn,
                                          true, Timed{}, slot, false});
    mut(s, target).conditions |= bit(Condition::Paralyzed);
    mut(s, caster_id).concentration_spell = kHoldPerson;
}
}  // namespace

TEST_CASE("Dispel: a spell of the slot level or lower ends automatically, and with it the caster's concentration") {
    auto s = start({caster(1), mob(2, 30), pc(3, 30)});
    hold(s, 2, 3, 2);
    const auto r = step(s, cast_at(kDispel, {3}, 3));
    REQUIRE(r.ok);
    CHECK_FALSE(find(s, 3)->has(Condition::Paralyzed));
    CHECK(s.effects.empty());
    CHECK(find(s, 2)->concentration_spell == 0);
    CHECK(has_ev(r.events, EventKind::ConcentrationEnded));
    CHECK_FALSE(has_ev(r.events, EventKind::SpellCheckRolled));
}

TEST_CASE("Dispel: a higher-level spell needs a spellcasting check against 10 + its level; upcasting removes the check") {
    int ended = 0, kept = 0;
    for (uint64_t seed = 0; seed < 60; ++seed) {
        auto s = start({caster(1), mob(2, 30), pc(3, 30)}, hooks(), seed);
        hold(s, 2, 3, 5);
        const auto r = step(s, cast_at(kDispel, {3}, 3));
        REQUIRE(r.ok);
        const Event* chk = nullptr;
        for (const auto& e : r.events) if (e.kind == EventKind::SpellCheckRolled) chk = &e;
        REQUIRE(chk);
        const bool ok = chk->b >= 15;   // 10 + level 5
        CHECK((chk->c & 1) == int(ok));
        CHECK(find(s, 3)->has(Condition::Paralyzed) == !ok);
        (ok ? ended : kept)++;
    }
    CHECK(ended > 0); CHECK(kept > 0);
    auto u = start({caster(1), mob(2, 30), pc(3, 30)});
    hold(u, 2, 3, 5);
    const auto r = step(u, cast_at(kDispel, {3}, 5));
    REQUIRE(r.ok);
    CHECK_FALSE(has_ev(r.events, EventKind::SpellCheckRolled));
    CHECK_FALSE(find(u, 3)->has(Condition::Paralyzed));
}

TEST_CASE("Dispel: spell modifiers end (reason 5), feature modifiers stay, other targets keep the spell") {
    auto s = start({caster(1), mob(2, 30), pc(3, 30), pc(4, 30)});
    ModifierSpec bless; bless.id = 40; bless.source = kBlessT; bless.stat = ModStat::AttackRoll; bless.bonus = 1; bless.concentration = true;
    ModifierSpec rage; rage.id = 41; rage.source = 9999; rage.stat = ModStat::Damage; rage.bonus = 2;
    std::vector<Event> sink;
    REQUIRE(apply_modifier(s, 2, 3, bless, sink));
    REQUIRE(apply_modifier(s, 2, 4, bless, sink));
    REQUIRE(apply_modifier(s, 3, 3, rage, sink));
    mut(s, 2).concentration_spell = kBlessT;
    const auto r = step(s, cast_at(kDispel, {3}, 3));
    REQUIRE(r.ok);
    const Event* gone = nullptr;
    for (const auto& e : r.events) if (e.kind == EventKind::ModifierExpired) gone = &e;
    REQUIRE(gone);
    CHECK(gone->a == 40); CHECK(gone->b == 5); CHECK(gone->target == 3);
    int on3 = 0, on4 = 0;
    for (const auto& m : s.modifiers) { on3 += m.target == 3; on4 += m.target == 4; }
    CHECK(on3 == 1);   // Rage (not a spell) stays
    CHECK(on4 == 1);   // Bless on the other creature stays
    CHECK(find(s, 2)->concentration_spell == kBlessT);
    // nothing to dispel: the spell resolves and does nothing
    auto t = start({caster(1), mob(2, 30)});
    const auto q = step(t, cast_at(kDispel, {2}, 3));
    REQUIRE(q.ok);
    CHECK_FALSE(has_ev(q.events, EventKind::SpellEffectEnded));
}

TEST_CASE("Spell effects record the slot level they were cast at") {
    auto s = start({caster(1), mob(2, 30), pc(3, 30)});
    REQUIRE(step(s, cast_at(kBlessT, {3}, 2)).ok);
    REQUIRE(s.modifiers.size() == 1);
    CHECK(s.modifiers[0].slot_level == 2);
}

TEST_CASE("Revive: a creature dead for at most the window returns with 1 HP") {
    auto s = start({caster(1), mob(2, 30), pc(3, 30), pc(4, 30)});
    auto& d = mut(s, 3);
    d.dead = true; d.hp = 0; d.conditions |= bit(Condition::Unconscious); d.death = DeathSaves{0, 3, false};
    CHECK_FALSE(step(s, cast_at(kRevivify, {3}, 3)).ok);   // no recorded death round yet
    REQUIRE(encounter_check(s, 1, Skill::Athletics, 10).ok);   // any completed step stamps the death round
    CHECK(find(s, 3)->died_round == s.round);
    const auto r = step(s, cast_at(kRevivify, {3}, 3));
    REQUIRE(r.ok);
    const Combatant* c = find(s, 3);
    CHECK_FALSE(c->dead); CHECK(c->hp == 1); CHECK(c->died_round == 0);
    CHECK(c->death.failures == 0);
    CHECK_FALSE(c->has(Condition::Unconscious));
    CHECK(has_ev(r.events, EventKind::Healed));
    CHECK(has_ev(r.events, EventKind::ConditionRemoved));
    CHECK(is_conscious(*c));
}

TEST_CASE("Revive refusals: alive target, dead too long") {
    auto s = start({caster(1), mob(2, 30), pc(3, 30), pc(4, 30)});
    CHECK_FALSE(step(s, cast_at(kRevivify, {3}, 3)).ok);   // alive
    auto& d = mut(s, 3);
    d.dead = true; d.hp = 0;
    REQUIRE(encounter_check(s, 1, Skill::Athletics, 10).ok);
    s.round = uint16_t(s.round + 11);
    CHECK_FALSE(step(s, cast_at(kRevivify, {3}, 3)).ok);
    CHECK(find(s, 3)->dead);
    CHECK(find(s, 1)->spell_slots[3] == 3);   // no slot spent on a refused cast
}
