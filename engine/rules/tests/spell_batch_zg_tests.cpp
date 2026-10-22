// VERSION_BUMP_BATCH 7.2zg spell batch (rules side): Slow's spell-failure roll and save gate; the others follow per group.
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include "spell_util.hpp"

using namespace enc;

namespace {
const uint32_t kSlowZ = spell_id_of("test.zg.slow");
ModifierSpec row(uint32_t id, ModStat st) { ModifierSpec m; m.id = id; m.source = kSlowZ; m.stat = st; m.applies_to = ModAppliesTo::Target; return m; }
EncounterHooks zg_hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef sl; sl.id = kSlowZ; sl.content_id = "test.zg.slow"; sl.level = 3; sl.range_ft = 120; sl.concentration = true;
    sl.kind = SpellEffectKind::ApplyModifier; sl.modifier = row(spell_id_of("test.zg.slow#slow_marker"), ModStat::Slow);
    sl.modifier.repeat_save = 1; sl.modifier.repeat_ability = Ability::Wis;
    sl.has_modifier_save = true; sl.modifier_save = Ability::Wis; sl.fixed_dc = 30;
    SpellEffect ac; ac.kind = SpellEffectKind::ApplyModifier; ac.affected_only = true;
    ac.modifier = row(spell_id_of("test.zg.slow#slow_ac"), ModStat::Ac); ac.modifier.bonus = -2;
    sl.extra_effects = {ac};
    book->add(sl);
    h.spells = book;
    return h;
}
int count_kind(const std::vector<Event>& ev, EventKind k) { return int(std::count_if(ev.begin(), ev.end(), [&](const Event& e) { return e.kind == k; })); }
}  // namespace

TEST_CASE("7.2zg slow: one save gates every row; a slowed caster of a 1-action spell rolls the d20 (11+: lost)") {
    auto w = wizard(1); w.spells_known.push_back(kSlowZ); w.spell_slots_max = {0, 4, 3, 3, 0, 0, 0, 0, 0, 0}; w.spell_slots = w.spell_slots_max;
    never_react(w);
    auto m = monster(2); m.hp = m.hp_max = 300; never_react(m);
    auto s = begin_encounter({w, m}, 7, nullptr, zg_hooks());
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kSlowZ, 2, 3));
    REQUIRE(r.ok);
    CHECK(count_kind(r.events, EventKind::SavingThrowRolled) == 1);   // the extra row does not save again
    int rows = 0; for (const auto& am : s.modifiers) rows += am.target == 2;
    CHECK(rows == 2);
    // slow the wizard itself and cast Fire Bolt (1 action) until both outcomes show
    int lost = 0, cast_ok = 0;
    for (int i = 0; i < 12; ++i) {
        (void)step(s, ActionRequest{current_actor(s), ActionKind::EndTurn});
        end_turns_until(s, 1);
        std::vector<Event> sink;
        ModifierSpec mk = row(spell_id_of("test.zg.slow#slow_marker"), ModStat::Slow);
        if (!std::any_of(s.modifiers.begin(), s.modifiers.end(), [](const auto& x) { return x.target == 1 && x.spec.stat == ModStat::Slow; }))
            REQUIRE(apply_modifier(s, 2, 1, mk, sink));
        auto c = step(s, cast(1, kFireBolt, 2));
        REQUIRE(c.ok);
        const Event* ft = nullptr;
        for (const auto& e : c.events) if (e.kind == EventKind::FeatureTriggered && e.actor == 1) ft = &e;
        REQUIRE(ft);
        CHECK(ft->a == int32_t(kSlowZ));
        CHECK(ft->b >= 1); CHECK(ft->b <= 20);
        CHECK(ft->c == int32_t(ft->b >= 11));
        if (ft->c) { ++lost; CHECK(count_kind(c.events, EventKind::SpellCountered) == 1); CHECK(count_kind(c.events, EventKind::AttackResolved) == 0); }
        else ++cast_ok;
    }
    CHECK(lost > 0); CHECK(cast_ok > 0);
}

namespace {
const uint32_t kFalseLife = spell_id_of("test.zg.false_life");
const uint32_t kHeroism = spell_id_of("test.zg.heroism");
const uint32_t kScareZ = spell_id_of("test.zg.scare");
EncounterHooks temp_hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef fl; fl.id = kFalseLife; fl.content_id = "test.zg.false_life"; fl.level = 1; fl.range_ft = 0;
    fl.kind = SpellEffectKind::TempHp; fl.temp_hp.dice = DiceExpr{1, 4, 4}; fl.temp_hp.per_slot_above = 5;
    fl.duration = SpellDuration::Hours; fl.duration_amount = 1;
    book->add(fl);
    SpellDef he; he.id = kHeroism; he.content_id = "test.zg.heroism"; he.level = 1; he.range_ft = 5; he.concentration = true;
    he.kind = SpellEffectKind::TempHp; he.temp_hp.ability = true; he.temp_hp.per_turn = true;
    he.temp_hp.immune_conditions = bit(Condition::Frightened);
    he.duration = SpellDuration::Minutes; he.duration_amount = 1;
    book->add(he);
    SpellDef sc; sc.id = kScareZ; sc.content_id = "test.zg.scare"; sc.level = 1; sc.range_ft = 60; sc.kind = SpellEffectKind::Save;
    sc.save_ability = Ability::Wis; sc.fixed_dc = 100; sc.applies_condition = true; sc.condition = Condition::Frightened;
    sc.duration = SpellDuration::Minutes; sc.duration_amount = 1;
    book->add(sc);
    h.spells = book;
    return h;
}
}  // namespace

TEST_CASE("7.2zg temp_hp: False Life 1d4+4 (+5 per slot above 1st); Heroism per turn + frightened immunity while it lasts") {
    auto w = wizard(1); w.spells_known.insert(w.spells_known.end(), {kFalseLife, kHeroism}); never_react(w);
    auto e = wizard(2, 1); e.spells_known.push_back(kScareZ); never_react(e);
    auto s = begin_encounter({w, e}, 7, nullptr, temp_hooks());
    end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kFalseLife, 1, 1)).ok);
    const int t1 = mut(s, 1).temp_hp;
    CHECK(t1 >= 5); CHECK(t1 <= 8);
    mut(s, 1).temp_hp = 0;
    (void)step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1);
    REQUIRE(step(s, cast(1, kFalseLife, 1, 3)).ok);   // 2 slots above: +10
    CHECK(mut(s, 1).temp_hp >= 15); CHECK(mut(s, 1).temp_hp <= 18);
    mut(s, 1).temp_hp = 0;
    (void)step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 1);
    const int mod = modifier(mut(s, 1).abilities, mut(s, 1).spellcasting_ability);
    REQUIRE(step(s, cast(1, kHeroism, 1, 1)).ok);
    CHECK(mut(s, 1).temp_hp == mod);
    mut(s, 1).temp_hp = 0;
    (void)step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 2);
    REQUIRE(step(s, cast(2, kScareZ, 1, 1)).ok);           // DC 100: would always frighten
    CHECK_FALSE(mut(s, 1).has(Condition::Frightened));     // immune while Heroism lasts
    (void)step(s, ActionRequest{2, ActionKind::EndTurn}); end_turns_until(s, 1);
    CHECK(mut(s, 1).temp_hp == mod);                       // refreshed at the start of the turn
    s.effects.erase(std::remove_if(s.effects.begin(), s.effects.end(), [](const auto& x) { return x.spell_id == kHeroism; }),
                    s.effects.end());                     // the spell ended: the immunity goes with its effect
    (void)step(s, ActionRequest{1, ActionKind::EndTurn}); end_turns_until(s, 2);
    REQUIRE(step(s, cast(2, kScareZ, 1, 1)).ok);
    CHECK(mut(s, 1).has(Condition::Frightened));
}

namespace {
const uint32_t kAcidArrow = spell_id_of("test.zg.acid_arrow");
EncounterHooks acid_hooks(int8_t to_hit) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kAcidArrow; d.content_id = "test.zg.acid_arrow"; d.level = 2; d.range_ft = 90;
    d.kind = SpellEffectKind::SpellAttack; d.melee = false; d.dice = DiceExpr{4, 4, 0}; d.damage_type = DamageType::Acid;
    d.fixed_attack = to_hit; d.miss_half = true;
    SpellEffect later; later.kind = SpellEffectKind::DelayedDamage; later.affected_only = true; later.dice = DiceExpr{2, 4, 0};
    later.damage_type = DamageType::Acid; later.expiry = 2;
    d.upcast_dice = 1; later.upcast_dice = 1;   // the loader copies scaling.upcast.dice to the delayed_damage rider
    d.extra_effects = {later};
    book->add(d);
    h.spells = book;
    return h;
}
}  // namespace

TEST_CASE("7.2zg on_miss half: a miss deals half the initial 4d4, no later damage; a hit deals it in full plus the rider") {
    for (int8_t to_hit : {int8_t(-100), int8_t(100)}) {
        auto w = wizard(1); w.spells_known.push_back(kAcidArrow); never_react(w);
        auto m = monster(2); m.hp = m.hp_max = 300; m.ac = 15; never_react(m);
        auto s = begin_encounter({w, m}, 7, nullptr, acid_hooks(to_hit));
        end_turns_until(s, 1);
        auto r = step(s, cast(1, kAcidArrow, 2, 2));
        REQUIRE(r.ok);
        const int lost = 300 - mut(s, 2).hp;
        int dmg_events = 0; for (const auto& e : r.events) dmg_events += e.kind == EventKind::DamageDealt && e.target == 2;
        if (to_hit < 0) { CHECK(lost >= 2); CHECK(lost <= 8); CHECK(dmg_events == 1); }
        else { CHECK(lost >= 4); CHECK(lost <= 16); }
        const bool rider = std::any_of(s.effects.begin(), s.effects.end(), [](const auto& x) { return x.spell_id == kAcidArrow; });
        CHECK(rider == (to_hit > 0));
    }
}

namespace {
const uint32_t kRebuke = spell_id_of("test.zg.hellish_rebuke");
EncounterHooks rebuke_hooks() {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kRebuke; d.content_id = "test.zg.hellish_rebuke"; d.level = 1; d.range_ft = 60;
    d.casting_time = CastingTime::Reaction; d.reaction_trigger = ReactionTrigger::Damaged; d.requires_sight = true;
    d.kind = SpellEffectKind::Save; d.save_ability = Ability::Dex; d.dice = DiceExpr{2, 10, 0}; d.damage_type = DamageType::Fire;
    d.on_save = OnSave::Half; d.fixed_dc = 30;
    book->add(d);
    h.spells = book;
    return h;
}
// Runs monster attacks on the wizard until one deals damage; returns that step's events.
std::vector<Event> hit_wizard(EncounterState& s) {
    for (int i = 0; i < 40; ++i) {
        end_turns_until(s, 2);
        const int hp = mut(s, 1).hp;
        ActionRequest atk{2, ActionKind::Attack, 1}; atk.attack_index = 0;
        auto r = step(s, atk);
        REQUIRE(r.ok);
        if (mut(s, 1).hp < hp) return r.events;
        (void)step(s, ActionRequest{2, ActionKind::EndTurn});
    }
    FAIL("no hit");
    return {};
}
}  // namespace

TEST_CASE("7.2zg trigger damaged: Hellish Rebuke answers damage from a seen creature (slot spent, Dex save vs the damager)") {
    auto w = wizard(1); w.hp = w.hp_max = 400; w.spells_known.push_back(kRebuke); never_react(w);
    set_policy(w, ReactionTrigger::Damaged, ReactionPolicy::Auto);
    auto m = monster(2); m.hp = m.hp_max = 300; never_react(m);
    auto s = begin_encounter({w, m}, 11, nullptr, rebuke_hooks());
    const int slots = mut(s, 1).spell_slots[1];
    auto ev = hit_wizard(s);
    const Event* cast_ev = nullptr;
    for (const auto& e : ev) if (e.kind == EventKind::SpellCast && e.actor == 1) cast_ev = &e;
    REQUIRE(cast_ev);
    CHECK(cast_ev->a == int32_t(kRebuke)); CHECK(cast_ev->target == 2); CHECK(cast_ev->b == 1);
    CHECK(mut(s, 1).spell_slots[1] == slots - 1);
    CHECK(mut(s, 1).reaction_used);
    CHECK(mut(s, 2).hp < 300);   // DC 30: the save fails, 2d10 fire
    CHECK(count_kind(ev, EventKind::SpellResolved) == 1);
}

TEST_CASE("7.2zg trigger damaged: no offer when the caster cannot see the damager, or it is beyond the range") {
    for (int variant = 0; variant < 3; ++variant) {
        CAPTURE(variant);
        auto w = wizard(1); w.hp = w.hp_max = 400; w.spells_known.push_back(kRebuke); never_react(w);
        set_policy(w, ReactionTrigger::Damaged, ReactionPolicy::Auto);
        if (variant == 0) add_condition(w, Condition::Blinded);
        auto m = monster(2); m.hp = m.hp_max = 300; never_react(m);
        auto h = rebuke_hooks();
        const uint16_t dist = variant == 1 ? 65 : 60;   // variant 2: exactly 60 ft is in range
        h.sight_facts = [dist](const EncounterState&, const Combatant&, const Combatant&) {
            SightFacts f{}; f.line_of_sight = true; f.distance_ft = dist; return f;
        };
        h.in_range = [](const EncounterState&, const ReactionWindow& rw, const Combatant& r) { return r.id == rw.target; };
        auto s = begin_encounter({w, m}, 11, nullptr, h);
        auto ev = hit_wizard(s);
        CHECK(count_kind(ev, EventKind::SpellCast) == (variant == 2 ? 1 : 0));
        CHECK((mut(s, 2).hp == 300) == (variant != 2));
        CHECK(mut(s, 1).reaction_used == (variant == 2));
    }
}

TEST_CASE("7.2zg damage_type_choices: Protection from Energy grants resistance to the chosen type only") {
    const uint32_t kPfe = spell_id_of("test.zg.protection_from_energy");
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    SpellDef d; d.id = kPfe; d.content_id = "test.zg.protection_from_energy"; d.level = 3; d.range_ft = 5; d.concentration = true;
    d.kind = SpellEffectKind::ApplyModifier; d.modifier.id = spell_id_of("test.zg.protection_from_energy#modifier");
    d.modifier.source = kPfe; d.modifier.stat = ModStat::Resistance; d.modifier.damage_type = DamageType::Acid;
    d.modifier.has_damage_type = true; d.modifier.concentration = true;
    d.damage_choices = {DamageType::Acid, DamageType::Cold, DamageType::Fire, DamageType::Lightning, DamageType::Thunder};
    d.duration = SpellDuration::Hours; d.duration_amount = 1;
    book->add(d);
    h.spells = book;
    auto w = wizard(1); w.spells_known.push_back(kPfe); w.spell_slots_max = {0, 4, 3, 3, 0, 0, 0, 0, 0, 0}; w.spell_slots = w.spell_slots_max;
    never_react(w);
    auto m = monster(2); never_react(m);
    auto s = begin_encounter({w, m}, 7, nullptr, h);
    end_turns_until(s, 1);
    auto none = step(s, cast(1, kPfe, 1, 3));
    CHECK_FALSE(none.ok); CHECK(none.error == "choose a damage type");
    auto req = cast(1, kPfe, 1, 3); req.damage_choice = 3;   // fire
    auto r = step(s, req);
    REQUIRE(r.ok);
    int fire = 0, other = 0;
    for (const auto& am : s.modifiers)
        if (am.target == 1 && am.spec.stat == ModStat::Resistance) (am.spec.damage_type == DamageType::Fire ? fire : other)++;
    CHECK(fire == 1); CHECK(other == 0);
    CHECK((mut(s, 1).mod_resist & (1u << unsigned(DamageType::Fire))) != 0);
    CHECK((mut(s, 1).mod_resist & (1u << unsigned(DamageType::Acid))) == 0);
}

namespace {
// The natural d20 of monster 2's first attack on wizard 1 (seed `seed`); blur: a Blur row on the wizard; senses on the attacker.
int first_natural(uint64_t seed, bool blur, uint16_t blindsight, uint16_t tremorsense, uint8_t exempt) {
    auto w = wizard(1); w.hp = w.hp_max = 400; never_react(w);
    auto m = monster(2); m.blindsight_ft = blindsight; m.tremorsense_ft = tremorsense; never_react(m);
    auto s = begin_encounter({w, m}, seed, nullptr, spell_hooks());
    if (blur) {
        ModifierSpec b; b.id = spell_id_of("test.zg.blur#modifier"); b.source = spell_id_of("test.zg.blur"); b.stat = ModStat::AttacksAgainst;
        b.mode = RollMode::Disadvantage; b.exempt_senses = exempt; b.applies_to = ModAppliesTo::Target;
        std::vector<Event> sink;
        REQUIRE(apply_modifier(s, 1, 1, b, sink));
    }
    end_turns_until(s, 2);
    ActionRequest atk{2, ActionKind::Attack, 1}; atk.attack_index = 0;
    auto r = step(s, atk);
    REQUIRE(r.ok);
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved && e.actor == 2) return e.a;
    FAIL("no attack");
    return 0;
}
}  // namespace

TEST_CASE("7.2zg exempt_senses: Blur imposes disadvantage unless the attacker has an exempt sense") {
    int lower = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        CAPTURE(seed);
        const int plain = first_natural(seed, false, 0, 0, 0);
        const int blurred = first_natural(seed, true, 0, 0, kSenseBlindsight | kSenseTruesight);
        CHECK(blurred <= plain);   // disadvantage: the lower of the same first d20 and one more
        lower += blurred < plain;
        CHECK(first_natural(seed, true, 30, 0, kSenseBlindsight | kSenseTruesight) == plain);   // blindsight: not fooled
        CHECK(first_natural(seed, true, 0, 30, kSenseBlindsight | kSenseTruesight) <= plain);    // tremorsense not listed: fooled
        CHECK(first_natural(seed, true, 0, 30, kSenseTremorsense) == plain);                    // listed: not fooled
    }
    CHECK(lower > 0);
}

TEST_CASE("7.2zg Acid Arrow at slot 3: both the initial damage and the delayed rider gain 1d4 (scaling.upcast.dice)") {
    auto w = wizard(1); w.spells_known.push_back(kAcidArrow); w.spell_slots_max = {0, 4, 3, 3, 0, 0, 0, 0, 0, 0}; w.spell_slots = w.spell_slots_max;
    never_react(w);
    auto m = monster(2); m.hp = m.hp_max = 300; m.ac = 15; never_react(m);
    auto s = begin_encounter({w, m}, 7, nullptr, acid_hooks(100));
    end_turns_until(s, 1);
    auto r = step(s, cast(1, kAcidArrow, 2, 3));
    REQUIRE(r.ok);
    const int lost = 300 - mut(s, 2).hp;
    CHECK(lost >= 5); CHECK(lost <= 40);   // 5d4 (10d4 on a crit)
    const auto it = std::find_if(s.effects.begin(), s.effects.end(), [](const auto& x) { return x.spell_id == kAcidArrow; });
    REQUIRE(it != s.effects.end());
    CHECK(it->damage.count == 3); CHECK(it->damage.sides == 4);   // 2d4 + 1d4
}

namespace {
// Natural d20 of monster 2's first attack on wizard 1; outlined: an `outlined` row on the wizard; invisible: the wizard is invisible.
int outlined_natural(uint64_t seed, bool outlined, bool invisible, bool blind_attacker) {
    auto w = wizard(1); w.hp = w.hp_max = 400; never_react(w);
    if (invisible) add_condition(w, Condition::Invisible);
    auto m = monster(2); never_react(m);
    if (blind_attacker) add_condition(m, Condition::Blinded);
    auto s = begin_encounter({w, m}, seed, nullptr, spell_hooks());
    if (outlined) {
        ModifierSpec o; o.id = spell_id_of("test.zg.faerie_fire#modifier"); o.source = spell_id_of("test.zg.faerie_fire");
        o.stat = ModStat::Outlined; o.applies_to = ModAppliesTo::Target;
        std::vector<Event> sink;
        REQUIRE(apply_modifier(s, 2, 1, o, sink));
    }
    end_turns_until(s, 2);
    ActionRequest atk{2, ActionKind::Attack, 1}; atk.attack_index = 0;
    auto r = step(s, atk);
    REQUIRE(r.ok);
    for (const auto& e : r.events) if (e.kind == EventKind::AttackResolved && e.actor == 2) return e.a;
    FAIL("no attack");
    return 0;
}
}  // namespace

TEST_CASE("7.2zg outlined: advantage on attacks against a seen outlined creature; invisibility gives it no benefit") {
    int higher = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        CAPTURE(seed);
        const int plain = outlined_natural(seed, false, false, false);
        const int lit = outlined_natural(seed, true, false, false);
        CHECK(lit >= plain); higher += lit > plain;
        CHECK(outlined_natural(seed, true, true, false) == lit);    // invisible + outlined: advantage, no disadvantage
        CHECK(outlined_natural(seed, false, true, false) <= plain); // invisible alone: disadvantage (as before)
        // a blinded attacker cannot see it: no advantage from outlined (its own Blinded disadvantage applies as before)
        CHECK(outlined_natural(seed, true, false, true) == outlined_natural(seed, false, false, true));
    }
    CHECK(higher > 0);
}
