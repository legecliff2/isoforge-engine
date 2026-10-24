// VERSION_BUMP_BATCH 7.2zi: Spare the Dying, True Strike, Expeditious Retreat, Protection from Poison,
// Ray of Enfeeblement, Beacon of Hope, Warding Bond. No new SIM_EVENTS tag (modifiers_digest sub-byte 0x5F).
#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <string>
#include "spell_util.hpp"

using namespace enc;

namespace {

const uint32_t kSpare = spell_id_of("test.zi.spare_the_dying");
const uint32_t kSpareExtra = spell_id_of("test.zi.spare_extra");
const uint32_t kStrike = spell_id_of("test.zi.true_strike");
const uint32_t kStrikeMod = spell_id_of("test.zi.true_strike#modifier");
const uint32_t kRetreat = spell_id_of("test.zi.expeditious_retreat");
const uint32_t kRetreatMod = spell_id_of("test.zi.expeditious_retreat#modifier");
const uint32_t kPoison = spell_id_of("test.zi.protection_from_poison");
const uint32_t kPoisonRes = spell_id_of("test.zi.protection_from_poison#resist");
const uint32_t kPoisonSave = spell_id_of("test.zi.protection_from_poison#save");
const uint32_t kRay = spell_id_of("test.zi.ray_of_enfeeblement");
const uint32_t kRayMod = spell_id_of("test.zi.ray_of_enfeeblement#modifier");
const uint32_t kRayFail = spell_id_of("test.zi.ray_fail");
const uint32_t kBeacon = spell_id_of("test.zi.beacon_of_hope");
const uint32_t kBeaconMod = spell_id_of("test.zi.beacon_of_hope#modifier");
const uint32_t kBond = spell_id_of("test.zi.warding_bond");
const uint32_t kBondAc = spell_id_of("test.zi.warding_bond#ac");
const uint32_t kBondSave = spell_id_of("test.zi.warding_bond#save");
const uint32_t kBondRes = spell_id_of("test.zi.warding_bond#resist");
const uint32_t kBondShare = spell_id_of("test.zi.warding_bond#share");
const uint32_t kPoisonBolt = spell_id_of("test.zi.poison_bolt");
const uint32_t kFireBoltFlat = spell_id_of("test.zi.fire_bolt_flat");
const uint32_t kPoisonSaveSpell = spell_id_of("test.zi.poison_save");
const uint32_t kPoisonDmgSave = spell_id_of("test.zi.poison_dmg_save");
const uint32_t kWisSave = spell_id_of("test.zi.wis_save");
const uint32_t kConSave = spell_id_of("test.zi.con_save");
const uint32_t kFlatBolt = spell_id_of("test.zi.flat_bolt");

ModifierSpec rounds_on(ModStat st, uint16_t n, ModAppliesTo who = ModAppliesTo::Target) {
    ModifierSpec m;
    m.stat = st; m.applies_to = who; m.expiry = 1; m.rounds = n;
    return m;
}

SpellDef spare_def() {
    SpellDef d; d.id = kSpare; d.content_id = "test.zi.spare_the_dying"; d.level = 0; d.range_ft = 5;
    d.kind = SpellEffectKind::Stabilize;
    d.excluded_types = bit(CreatureType::Undead) | bit(CreatureType::Construct);
    return d;
}
SpellDef strike_def() {
    SpellDef d; d.id = kStrike; d.content_id = "test.zi.true_strike"; d.level = 0; d.range_ft = 30;
    d.concentration = true; d.duration = SpellDuration::Rounds; d.duration_amount = 1;
    d.kind = SpellEffectKind::ApplyModifier;
    d.modifier = rounds_on(ModStat::AttackRoll, 0, ModAppliesTo::Self);
    d.modifier.expiry = 3; d.modifier.rounds = 0; d.modifier.id = kStrikeMod;
    d.modifier.mode = RollMode::Advantage; d.modifier.consume = 1;
    d.modifier.mark_scope = 1; d.modifier.on_next_turn = 1;
    return d;
}
SpellDef retreat_def() {
    SpellDef d; d.id = kRetreat; d.content_id = "test.zi.expeditious_retreat"; d.level = 1; d.range_ft = 0;
    d.casting_time = CastingTime::BonusAction; d.concentration = true;
    d.duration = SpellDuration::Minutes; d.duration_amount = 10;
    d.kind = SpellEffectKind::ApplyModifier;
    d.modifier = rounds_on(ModStat::BonusDash, 600, ModAppliesTo::Self);
    d.modifier.id = kRetreatMod;
    d.dash_on_cast = true;
    return d;
}
SpellDef poison_def() {
    SpellDef d; d.id = kPoison; d.content_id = "test.zi.protection_from_poison"; d.level = 2; d.range_ft = 5;
    d.duration = SpellDuration::Hours; d.duration_amount = 1;
    d.kind = SpellEffectKind::EndCondition; d.ends_conditions = bit(Condition::Poisoned);
    SpellEffect res; res.kind = SpellEffectKind::ApplyModifier;
    res.modifier = rounds_on(ModStat::Resistance, 600); res.modifier.id = kPoisonRes;
    res.modifier.damage_type = DamageType::Poison; res.modifier.has_damage_type = true;
    SpellEffect sv; sv.kind = SpellEffectKind::ApplyModifier;
    sv.modifier = rounds_on(ModStat::Save, 600); sv.modifier.id = kPoisonSave;
    sv.modifier.mode = RollMode::Advantage; sv.modifier.save_vs_conditions = bit(Condition::Poisoned);
    d.extra_effects = {res, sv};
    return d;
}
SpellDef ray_def(uint32_t id, int8_t dc) {
    SpellDef d; d.id = id; d.content_id = "test.zi.ray"; d.level = 2; d.range_ft = 60; d.concentration = true;
    d.duration = SpellDuration::Minutes; d.duration_amount = 1; d.fixed_attack = 30; d.fixed_dc = dc;
    d.kind = SpellEffectKind::SpellAttack; d.melee = false; d.hit_only = true; d.damage_type = DamageType::Necrotic;
    SpellEffect fx; fx.kind = SpellEffectKind::ApplyModifier; fx.affected_only = true;
    fx.modifier = rounds_on(ModStat::HalveWeaponDamage, 10); fx.modifier.id = kRayMod;
    fx.modifier.scope_ability = 1; fx.modifier.repeat_save = 1; fx.modifier.repeat_ability = Ability::Con;
    d.extra_effects = {fx};
    return d;
}
SpellDef beacon_def() {
    SpellDef d; d.id = kBeacon; d.content_id = "test.zi.beacon_of_hope"; d.level = 3; d.range_ft = 30; d.targets = 255;
    d.concentration = true; d.duration = SpellDuration::Minutes; d.duration_amount = 1;
    d.kind = SpellEffectKind::ApplyModifier;
    d.modifier = rounds_on(ModStat::Save, 10); d.modifier.id = kBeaconMod;
    d.modifier.ability = Ability::Wis; d.modifier.has_ability = true; d.modifier.mode = RollMode::Advantage;
    d.modifier.death_save_adv = 1; d.modifier.heal_maximum = 1;
    return d;
}
SpellDef bond_def() {
    SpellDef d; d.id = kBond; d.content_id = "test.zi.warding_bond"; d.level = 2; d.range_ft = 5;
    d.duration = SpellDuration::Hours; d.duration_amount = 1; d.concentration = false;
    d.kind = SpellEffectKind::ApplyModifier;
    d.modifier = rounds_on(ModStat::Ac, 600); d.modifier.id = kBondAc; d.modifier.bonus = 1;
    SpellEffect sv; sv.kind = SpellEffectKind::ApplyModifier;
    sv.modifier = rounds_on(ModStat::Save, 600); sv.modifier.id = kBondSave; sv.modifier.bonus = 1;
    SpellEffect rs; rs.kind = SpellEffectKind::ApplyModifier;
    rs.modifier = rounds_on(ModStat::Resistance, 600); rs.modifier.id = kBondRes; rs.modifier.resist_all = 1;
    SpellEffect sh; sh.kind = SpellEffectKind::ApplyModifier;
    sh.modifier = rounds_on(ModStat::ShareDamage, 600); sh.modifier.id = kBondShare;
    sh.modifier.end_beyond_ft = 60; sh.modifier.end_caster_at_zero = 1;
    d.extra_effects = {sv, rs, sh};
    return d;
}
SpellDef flat_attack(uint32_t id, DamageType type, bool save, bool poisoned) {
    SpellDef d; d.id = id; d.content_id = "test.zi.bolt"; d.level = 0; d.range_ft = 60; d.fixed_attack = 30;
    if (save) {
        d.kind = SpellEffectKind::Save; d.save_ability = Ability::Con; d.fixed_dc = 10;
        if (poisoned) { d.applies_condition = true; d.condition = Condition::Poisoned; d.dice = {0, 0, 0}; }
        else { d.dice = {0, 0, 8}; d.damage_type = type; d.on_save = OnSave::None; }
    } else {
        d.kind = SpellEffectKind::SpellAttack; d.dice = {0, 0, 8}; d.damage_type = type;
    }
    return d;
}

EncounterHooks zi_hooks(const std::shared_ptr<int>& dist) {
    auto h = spell_hooks();
    auto book = std::make_shared<SpellBook>(*h.spells);
    book->add(spare_def());
    SpellDef extra; extra.id = kSpareExtra; extra.content_id = "test.zi.spare_extra"; extra.level = 1; extra.range_ft = 5;
    extra.kind = SpellEffectKind::Heal; extra.dice = {1, 4, 0};
    SpellEffect rider; rider.kind = SpellEffectKind::Stabilize; extra.extra_effects = {rider};
    book->add(extra);
    book->add(strike_def()); book->add(retreat_def()); book->add(poison_def());
    book->add(ray_def(kRay, 1)); book->add(ray_def(kRayFail, 30));
    book->add(beacon_def()); book->add(bond_def());
    book->add(flat_attack(kPoisonBolt, DamageType::Poison, false, false));
    book->add(flat_attack(kFireBoltFlat, DamageType::Fire, false, false));
    book->add(flat_attack(kPoisonSaveSpell, DamageType::Poison, true, true));
    book->add(flat_attack(kPoisonDmgSave, DamageType::Poison, true, false));
    SpellDef wis = flat_attack(kWisSave, DamageType::Force, true, false);
    wis.kind = SpellEffectKind::Save; wis.save_ability = Ability::Wis; wis.dice = {0, 0, 0}; wis.fixed_dc = 10;
    book->add(wis);
    SpellDef con = wis; con.id = kConSave; con.content_id = "test.zi.con_save"; con.save_ability = Ability::Con;
    book->add(con);
    SpellDef bolt; bolt.id = kFlatBolt; bolt.content_id = "test.zi.flat_bolt"; bolt.level = 0; bolt.range_ft = 60;
    bolt.kind = SpellEffectKind::SpellAttack; bolt.dice = {0, 0, 8}; bolt.damage_type = DamageType::Force; bolt.fixed_attack = 30;
    book->add(bolt);
    h.spells = book;
    if (dist) h.sight_facts = [dist](const EncounterState&, const Combatant&, const Combatant&) {
        SightFacts f; f.distance_ft = uint16_t(*dist); return f;
    };
    return h;
}

Combatant wiz(CombatantId id, uint8_t team = 0) {
    auto c = wizard(id, team);
    c.hp = c.hp_max = 40; c.speed_ft = 30;
    c.spells_known.insert(c.spells_known.end(),
                          {kSpare, kSpareExtra, kStrike, kRetreat, kPoison, kRay, kRayFail, kBeacon, kBond, kCureWounds,
                           kPoisonBolt, kFireBoltFlat, kPoisonSaveSpell, kPoisonDmgSave, kWisSave, kConSave, kFlatBolt});
    c.spell_slots_max[3] = 2; c.spell_slots = c.spell_slots_max;
    never_react(c);
    return c;
}
Combatant foe(CombatantId id) {
    auto m = monster(id); m.hp = m.hp_max = 40; m.ac = 10; never_react(m); return m;
}
EncounterState start(std::vector<Combatant> cs, uint64_t seed = 1, const std::shared_ptr<int>& dist = nullptr) {
    auto s = begin_encounter(std::move(cs), seed, nullptr, zi_hooks(dist));
    end_turns_until(s, 1);
    return s;
}
ActionRequest touch(CombatantId a, uint32_t spell, CombatantId t, uint8_t level = 0) {
    auto r = cast(a, spell, t, level); r.target_within_5ft = true; return r;
}
ActionRequest cast_at(CombatantId a, uint32_t spell, std::vector<CombatantId> ts, uint8_t level) {
    ActionRequest r{a, ActionKind::CastSpell, ts.empty() ? CombatantId(0) : ts[0]};
    r.spell_id = spell; r.spell_level = level;
    if (ts.size() > 1) r.targets = ts;
    return r;
}
const ActiveModifier* mod_id(const EncounterState& s, uint32_t id) {
    for (const auto& m : s.modifiers) if (m.spec.id == id) return &m;
    return nullptr;
}
int count_stat(const EncounterState& s, ModStat st) {
    int n = 0; for (const auto& m : s.modifiers) n += m.spec.stat == st; return n;
}
int dealt_to(const std::vector<Event>& ev, CombatantId id) {
    int n = 0; for (const auto& e : ev) if (e.kind == EventKind::DamageDealt && e.target == id) n += e.a; return n;
}
int natural_of(const std::vector<Event>& ev, EventKind k, CombatantId actor) {
    for (const auto& e : ev) if (e.kind == k && e.actor == actor) return e.a;
    return -1;
}
bool expired(const std::vector<Event>& ev, uint32_t id, int reason) {
    for (const auto& e : ev)
        if (e.kind == EventKind::ModifierExpired && e.a == int32_t(id) && e.b == reason) return true;
    return false;
}
rng::Pcg32::State advance(rng::Pcg32::State st, int n) {
    rng::Rng r(0, 0); r.load(st); for (int i = 0; i < n; ++i) (void)r.d(20); return r.save();
}
std::pair<int, int> peek2(rng::Pcg32::State st) {
    rng::Rng r(0, 0); r.load(st); return {r.d(20), r.d(20)};
}
void flat_weapon(EncounterState& s, CombatantId id, bool finesse) {
    mut(s, id).attacks[0].to_hit = 30;
    mut(s, id).attacks[0].damage = DiceExpr{0, 0, 8};
    mut(s, id).attacks[0].finesse = finesse;
    mut(s, id).crit_min = 21;   // a crit would still be flat; keep the roll to one d20
}
void end_before(EncounterState& s, CombatantId id) {
    for (int i = 0; i < 20 && current_actor(s) != id; ++i) {
        if (s.order[(s.turn_index + 1) % s.order.size()].id == id) return;
        (void)step(s, {current_actor(s), ActionKind::EndTurn});
    }
}
bool lists_bonus_dash(const EncounterState& s) {
    for (const auto& r : legal_actions(s)) if (r.kind == ActionKind::Dash && r.as_bonus_action) return true;
    return false;
}
// A +30 attack still misses on a natural 1. Retry on the same turn until it hits.
auto until_hit(EncounterState& s, CombatantId actor, uint32_t spell, CombatantId target) {
    decltype(step(s, cast(actor, spell, target))) r{};
    for (int i = 0; i < 40; ++i) {
        s.budget.action = true;
        r = step(s, cast(actor, spell, target));
        if (!r.ok || natural_of(r.events, EventKind::AttackResolved, actor) != 1) return r;
    }
    return r;
}

}  // namespace

TEST_CASE("7.2zi stabilize: a dying creature at 0 HP; undead, constructs, the conscious, and the caster are refused") {
    CHECK(uint8_t(SpellEffectKind::WeaponSmite) == 25);
    CHECK(uint8_t(SpellEffectKind::Stabilize) == 26);
    auto s = start({wiz(1), wiz(2), foe(3)});
    make_dying(mut(s, 2));
    auto far = cast(1, kSpare, 2);
    CHECK_FALSE(step(s, far).ok);
    CHECK(std::string(step(s, far).error) == "stabilize: touch range");
    CHECK_FALSE(find(s, 2)->death.stable);
    auto self = touch(1, kSpare, 1);
    CHECK(std::string(step(s, self).error) == "stabilize: one living creature");
    mut(s, 2).creature_type = CreatureType::Undead;
    CHECK(std::string(step(s, touch(1, kSpare, 2)).error) == "stabilize: no effect on undead or constructs");
    mut(s, 2).creature_type = CreatureType::Construct;
    CHECK(std::string(step(s, touch(1, kSpare, 2)).error) == "stabilize: no effect on undead or constructs");
    mut(s, 2).creature_type = CreatureType::Humanoid;
    mut(s, 2).hp = 5; remove_condition(mut(s, 2), Condition::Unconscious);
    CHECK(std::string(step(s, touch(1, kSpare, 2)).error) == "stabilize: target is not dying");
    make_dying(mut(s, 2)); mut(s, 2).death.stable = true;
    CHECK(std::string(step(s, touch(1, kSpare, 2)).error) == "stabilize: target is not dying");
    mut(s, 2).death.stable = false; mut(s, 2).dead = true;
    CHECK(std::string(step(s, touch(1, kSpare, 2)).error) == "bad target");
    mut(s, 2).dead = false;
    auto extra = step(s, touch(1, kSpareExtra, 2, 1));
    CHECK_FALSE(extra.ok); CHECK(std::string(extra.error) == "stabilize: primary effect only");
    auto ok = step(s, touch(1, kSpare, 2));
    REQUIRE(ok.ok);
    CHECK(find(s, 2)->death.stable); CHECK(find(s, 2)->hp == 0);
    int n = 0; for (const auto& e : ok.events) n += e.kind == EventKind::Stabilized && e.actor == 2;
    CHECK(n == 1);
}

TEST_CASE("7.2zi True Strike: advantage only on the caster's next turn, and only against the marked creature") {
    auto s = start({wiz(1), foe(2), foe(3)});
    flat_weapon(s, 1, false);
    REQUIRE(step(s, cast(1, kStrike, 2)).ok);
    CHECK(find(s, 1)->concentration_spell == kStrike);
    const auto* row = mod_id(s, kStrikeMod);
    REQUIRE(row); CHECK(row->target == 1); CHECK(row->mark == 2); CHECK_FALSE(row->zi_armed);
    CHECK(row->timed.kind == Expiry::EndOfCasterNextTurn); CHECK(row->timed.rounds_left == 2);
    s.budget.action = true;
    const auto same = s.rng.save();
    auto atk = step(s, ActionRequest{1, ActionKind::Attack, 2});
    REQUIRE(atk.ok);
    CHECK(natural_of(atk.events, EventKind::AttackResolved, 1) == peek2(same).first);
    CHECK(s.rng.save().state == advance(same, 1).state);
    CHECK(mod_id(s, kStrikeMod));
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    row = mod_id(s, kStrikeMod); REQUIRE(row); CHECK(row->zi_armed); CHECK(row->timed.rounds_left == 1);
    const auto next = s.rng.save();
    auto other = step(s, ActionRequest{1, ActionKind::Attack, 3});
    REQUIRE(other.ok);
    CHECK(natural_of(other.events, EventKind::AttackResolved, 1) == peek2(next).first);
    CHECK(s.rng.save().state == advance(next, 1).state);
    CHECK(mod_id(s, kStrikeMod));
    s.budget.action = true;
    const auto marked = s.rng.save();
    const auto dice = peek2(marked);
    auto hit = step(s, ActionRequest{1, ActionKind::Attack, 2});
    REQUIRE(hit.ok);
    CHECK(natural_of(hit.events, EventKind::AttackResolved, 1) == std::max(dice.first, dice.second));
    CHECK(s.rng.save().state == advance(marked, 2).state);
    CHECK(expired(hit.events, kStrikeMod, 3));
    CHECK_FALSE(mod_id(s, kStrikeMod));
}

TEST_CASE("7.2zi Expeditious Retreat: Dash on the cast, then as a bonus action until concentration ends") {
    auto s = start({wiz(1), foe(2)});
    const int before = s.budget.movement_ft;
    const int speed = modified_speed(s, *find(s, 1));
    CHECK_FALSE(lists_bonus_dash(s));
    auto casted = step(s, cast(1, kRetreat, 1, 1));
    REQUIRE(casted.ok);
    CHECK(s.budget.movement_ft == int16_t(before + speed));
    CHECK(s.budget.dashes == 1);
    CHECK(natural_of(casted.events, EventKind::Dashed, 1) == s.budget.movement_ft);
    CHECK_FALSE(s.budget.bonus_action); CHECK(s.budget.action);
    CHECK_FALSE(lists_bonus_dash(s));   // the cast already took the Dash; the bonus action is spent
    CHECK(find(s, 1)->concentration_spell == kRetreat);
    CHECK((find(s, 1)->bonus_action_grants & bit(ActionKind::Dash)) == 0);
    REQUIRE(mod_id(s, kRetreatMod)); CHECK(mod_id(s, kRetreatMod)->timed.rounds_left == 600);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);   // not moving the extra feet is legal
    end_turns_until(s, 1);
    CHECK(lists_bonus_dash(s));
    CHECK(s.budget.dashes == 0);
    const int speed2 = modified_speed(s, *find(s, 1));
    const int move0 = s.budget.movement_ft;
    ActionRequest dash{1, ActionKind::Dash}; dash.as_bonus_action = true;
    auto r = step(s, dash);
    REQUIRE(r.ok);
    CHECK(s.budget.movement_ft == int16_t(move0 + speed2));
    CHECK(s.budget.dashes == 1);
    CHECK(s.budget.action);
    CHECK(natural_of(r.events, EventKind::Dashed, 1) == s.budget.movement_ft);
    REQUIRE(step(s, {1, ActionKind::EndTurn}).ok);
    end_turns_until(s, 1);
    CHECK(lists_bonus_dash(s));
    CHECK(s.budget.bonus_action);
    REQUIRE(step(s, cast(1, kStrike, 2)).ok);   // True Strike is concentration: the retreat ends
    CHECK(find(s, 1)->concentration_spell == kStrike);
    CHECK_FALSE(mod_id(s, kRetreatMod));
    CHECK_FALSE(lists_bonus_dash(s));
    ActionRequest again{1, ActionKind::Dash}; again.as_bonus_action = true;
    CHECK_FALSE(step(s, again).ok);
    auto bare = start({wiz(1), foe(2)});
    ActionRequest no{1, ActionKind::Dash}; no.as_bonus_action = true;
    CHECK_FALSE(step(bare, no).ok);
    auto act = start({wiz(1), foe(2)});
    REQUIRE(step(act, cast(1, kRetreat, 1, 1)).ok);
    const int after_cast = act.budget.movement_ft;
    ActionRequest ad{1, ActionKind::Dash};
    auto acted = step(act, ad);
    REQUIRE(acted.ok);
    CHECK(act.budget.movement_ft == int16_t(after_cast + modified_speed(act, *find(act, 1))));
    CHECK(act.budget.dashes == 2);
}

TEST_CASE("7.2zi Protection from Poison: ends poisoned, resists poison damage, advantage only against being poisoned") {
    auto s = start({wiz(1), foe(2)});
    add_condition(mut(s, 2), Condition::Poisoned);
    REQUIRE(step(s, cast(1, kPoison, 2, 2)).ok);
    CHECK_FALSE(find(s, 2)->has(Condition::Poisoned));
    CHECK(find(s, 1)->concentration_spell == 0);
    REQUIRE(mod_id(s, kPoisonRes)); REQUIRE(mod_id(s, kPoisonSave));
    CHECK(mod_id(s, kPoisonRes)->timed.rounds_left == 600);
    bool bolted = false;
    for (uint64_t seed = 1; seed <= 40 && !bolted; ++seed) {
        s = start({wiz(1), foe(2)}, seed);
        REQUIRE(step(s, cast(1, kPoison, 2, 2)).ok);
        (void)step(s, {1, ActionKind::EndTurn});
        end_turns_until(s, 1);
        const auto st = s.rng.save();
        auto bolt = step(s, cast(1, kPoisonBolt, 2));
        REQUIRE(bolt.ok);
        if (natural_of(bolt.events, EventKind::AttackResolved, 1) == 1) continue;
        bolted = true;
        CHECK(dealt_to(bolt.events, 2) == 4);
        CHECK(s.rng.save().state == advance(st, 1).state);   // the attack die only; the 8 is flat
    }
    CHECK(bolted);
    auto fresh = start({wiz(1), foe(2)});
    REQUIRE(step(fresh, cast(1, kPoison, 2, 2)).ok);   // the buff lands with no poisoned condition to end
    CHECK(mod_id(fresh, kPoisonRes));
    (void)step(fresh, {1, ActionKind::EndTurn});
    end_turns_until(fresh, 1);
    const auto cond = fresh.rng.save();
    const auto cd = peek2(cond);
    auto vs = step(fresh, cast(1, kPoisonSaveSpell, 2));
    REQUIRE(vs.ok);
    CHECK(natural_of(vs.events, EventKind::SavingThrowRolled, 2) == std::max(cd.first, cd.second));
    CHECK(fresh.rng.save().state == advance(cond, 2).state);
    fresh.budget.action = true;
    const auto dmg = fresh.rng.save();
    auto only = step(fresh, cast(1, kPoisonDmgSave, 2));
    REQUIRE(only.ok);
    CHECK(natural_of(only.events, EventKind::SavingThrowRolled, 2) == peek2(dmg).first);
    CHECK(fresh.rng.save().state == advance(dmg, 1).state);
}

TEST_CASE("7.2zi Ray of Enfeeblement: a hit deals no hit points and halves Strength weapon damage until the Con save") {
    bool saw = false;
    for (uint64_t seed = 1; seed <= 40 && !saw; ++seed) {
        auto s = start({wiz(1), foe(2)}, seed);
        flat_weapon(s, 2, false);
        auto casted = step(s, cast(1, kRay, 2, 2));
        REQUIRE(casted.ok);
        if (natural_of(casted.events, EventKind::AttackResolved, 1) == 1) {
            CHECK(dealt_to(casted.events, 2) == 0);
            CHECK_FALSE(mod_id(s, kRayMod));
            continue;
        }
        CHECK(dealt_to(casted.events, 2) == 0);
        REQUIRE(mod_id(s, kRayMod)); CHECK(mod_id(s, kRayMod)->target == 2);
        CHECK(find(s, 1)->concentration_spell == kRay);
        (void)step(s, {1, ActionKind::EndTurn});
        end_turns_until(s, 2);
        auto hit = step(s, ActionRequest{2, ActionKind::Attack, 1});
        REQUIRE(hit.ok);
        if (natural_of(hit.events, EventKind::AttackResolved, 2) == 1) continue;
        mut(s, 2).spells_known = {kFlatBolt};
        auto spell = until_hit(s, 2, kFlatBolt, 1);
        REQUIRE(spell.ok);
        if (natural_of(spell.events, EventKind::AttackResolved, 2) == 1) continue;
        saw = true;
        CHECK(dealt_to(hit.events, 1) == 4);
        CHECK(dealt_to(spell.events, 1) == 8);
        auto end = step(s, {2, ActionKind::EndTurn});
        REQUIRE(end.ok);
        CHECK(expired(end.events, kRayMod, 6));
        CHECK(find(s, 1)->concentration_spell == 0);
    }
    CHECK(saw);
    auto dex = start({wiz(1), foe(2)}, 1);
    mut(dex, 2).abilities.score[std::size_t(Ability::Dex)] = 16;
    flat_weapon(dex, 2, true);
    bool dex_hit = false;
    for (uint64_t seed = 1; seed <= 40 && !dex_hit; ++seed) {
        dex = start({wiz(1), foe(2)}, seed);
        mut(dex, 2).abilities.score[std::size_t(Ability::Dex)] = 16;
        flat_weapon(dex, 2, true);
        auto casted = step(dex, cast(1, kRayFail, 2, 2));
        REQUIRE(casted.ok);
        if (!mod_id(dex, kRayMod)) continue;
        (void)step(dex, {1, ActionKind::EndTurn});
        end_turns_until(dex, 2);
        auto hit = step(dex, ActionRequest{2, ActionKind::Attack, 1});
        REQUIRE(hit.ok);
        if (natural_of(hit.events, EventKind::AttackResolved, 2) == 1) continue;
        dex_hit = true;
        CHECK(dealt_to(hit.events, 1) == 8);
        auto end = step(dex, {2, ActionKind::EndTurn});
        CHECK(mod_id(dex, kRayMod));
        CHECK_FALSE(expired(end.events, kRayMod, 6));
    }
    CHECK(dex_hit);
}

TEST_CASE("7.2zi Beacon of Hope: Wisdom and death saves have advantage, and healing dice are the maximum") {
    auto s = start({wiz(1), wiz(2), wiz(3, 1)});
    REQUIRE(step(s, cast_at(1, kBeacon, {2, 3}, 3)).ok);
    CHECK(find(s, 1)->concentration_spell == kBeacon);
    CHECK(count_stat(s, ModStat::Save) == 2);
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    const auto wis = s.rng.save();
    const auto wd = peek2(wis);
    auto wr = step(s, cast(1, kWisSave, 2));
    REQUIRE(wr.ok);
    CHECK(natural_of(wr.events, EventKind::SavingThrowRolled, 2) == std::max(wd.first, wd.second));
    CHECK(s.rng.save().state == advance(wis, 2).state);
    s.budget.action = true;
    const auto con = s.rng.save();
    auto cr = step(s, cast(1, kConSave, 2));
    REQUIRE(cr.ok);
    CHECK(natural_of(cr.events, EventKind::SavingThrowRolled, 2) == peek2(con).first);
    CHECK(s.rng.save().state == advance(con, 1).state);
    s.budget.action = true;
    mut(s, 2).hp = 10;
    const auto before = s.rng.save();
    auto heal = step(s, cast(1, kCureWounds, 2, 1));
    REQUIRE(heal.ok);
    CHECK(natural_of(heal.events, EventKind::Healed, 1) == 11);   // 8 + Int 16
    CHECK(s.rng.save().state == before.state);
    make_dying(mut(s, 2));
    end_before(s, 2);
    const auto death = s.rng.save();
    const auto dd = peek2(death);
    auto turn = step(s, {current_actor(s), ActionKind::EndTurn});
    REQUIRE(turn.ok);
    CHECK(current_actor(s) == 2);
    CHECK(natural_of(turn.events, EventKind::DeathSaveRolled, 2) == std::max(dd.first, dd.second));
    CHECK(s.rng.save().state == advance(death, 2).state);
}

TEST_CASE("7.2zi Warding Bond: +1 AC and saves, resist all, the caster takes the same damage, and the SRD end conditions") {
    auto dist = std::make_shared<int>(30);
    auto s = start({wiz(1), wiz(2), wiz(3), foe(4)}, 1, dist);
    mut(s, 1).hp = mut(s, 1).hp_max = 30;
    REQUIRE(step(s, cast(1, kBond, 2, 2)).ok);
    CHECK(find(s, 1)->concentration_spell == 0);
    CHECK(modified_ac_bonus(s, 2) == 1);
    CHECK(modifier_flat(s, 2, ModStat::Save) == 1);
    CHECK(mod_id(s, kBondShare)); CHECK(mod_id(s, kBondShare)->timed.rounds_left == 600);
    (void)step(s, {1, ActionKind::EndTurn});
    end_turns_until(s, 1);
    mut(s, 1).resistances |= bit(DamageType::Fire);
    mut(s, 1).temp_hp = 3;
    const int hp = find(s, 1)->hp;
    auto fire = until_hit(s, 1, kFireBoltFlat, 2);
    REQUIRE(fire.ok);
    CHECK(dealt_to(fire.events, 2) == 4);          // resist all
    CHECK(dealt_to(fire.events, 1) == 4);          // the caster's fire resistance does not reduce the share
    CHECK(find(s, 1)->temp_hp == 0);
    CHECK(find(s, 1)->hp == int16_t(hp - 1));      // temp HP still absorbs
    CHECK(mod_id(s, kBondShare));
    mut(s, 1).hp = 2; mut(s, 1).temp_hp = 0;
    auto drop = until_hit(s, 1, kFireBoltFlat, 2);
    REQUIRE(drop.ok);
    CHECK(find(s, 1)->hp == 0);
    CHECK(expired(drop.events, kBondShare, 0));
    CHECK(count_stat(s, ModStat::ShareDamage) == 0);
    auto stay = start({wiz(1), wiz(2), foe(3)}, 1, dist);
    mut(stay, 1).hp = mut(stay, 1).hp_max = 40;
    REQUIRE(step(stay, cast(1, kBond, 2, 2)).ok);
    (void)step(stay, {1, ActionKind::EndTurn});
    end_turns_until(stay, 1);
    mut(stay, 2).hp = 4;
    auto ally = until_hit(stay, 1, kFireBoltFlat, 2);
    REQUIRE(ally.ok);
    CHECK(find(stay, 2)->hp == 0);
    CHECK(find(stay, 1)->hp > 0);
    CHECK(mod_id(stay, kBondShare));               // the warded creature at 0 HP does not end it
    *dist = 65;
    auto moved = step(stay, ActionRequest{1, ActionKind::Move, 0, 0, 5});
    REQUIRE(moved.ok);
    CHECK(expired(moved.events, kBondShare, 0));
    auto near = start({wiz(1), wiz(2), foe(3)}, 1, dist);
    *dist = 30;
    REQUIRE(step(near, cast(1, kBond, 2, 2)).ok);
    auto step_near = step(near, ActionRequest{1, ActionKind::Move, 0, 0, 5});
    REQUIRE(step_near.ok);
    CHECK(mod_id(near, kBondShare));
    auto none = start({wiz(1), wiz(2), foe(3)});
    REQUIRE(step(none, cast(1, kBond, 2, 2)).ok);
    REQUIRE(step(none, ActionRequest{1, ActionKind::Move, 0, 0, 5}).ok);
    CHECK(mod_id(none, kBondShare));               // no sight hook: distance does not end the bond
    ActionRequest too{1, ActionKind::Move, 0, 0, 500};
    CHECK_FALSE(step(none, too).ok);
    CHECK(mod_id(none, kBondShare));
    auto recast = start({wiz(1), wiz(2), wiz(3), foe(4)});
    REQUIRE(step(recast, cast(1, kBond, 2, 2)).ok);
    (void)step(recast, {1, ActionKind::EndTurn});
    end_turns_until(recast, 1);
    auto again = step(recast, cast(1, kBond, 2, 2));
    REQUIRE(again.ok);
    CHECK(expired(again.events, kBondShare, 2));
    CHECK(count_stat(recast, ModStat::ShareDamage) == 1);
    auto third = start({wiz(1), wiz(2), wiz(3), foe(4)});
    REQUIRE(step(third, cast(1, kBond, 2, 2)).ok);
    (void)step(third, {1, ActionKind::EndTurn});
    end_turns_until(third, 1);
    REQUIRE(step(third, cast(1, kBond, 3, 2)).ok);
    CHECK(count_stat(third, ModStat::ShareDamage) == 2);   // a third creature does not end the first bond
    auto other = start({wiz(1), wiz(2), wiz(3), foe(4)});
    REQUIRE(step(other, cast(1, kBond, 2, 2)).ok);
    end_turns_until(other, 3);
    auto replaced = step(other, cast(3, kBond, 2, 2));
    REQUIRE(replaced.ok);
    CHECK(expired(replaced.events, kBondShare, 2));
    CHECK(count_stat(other, ModStat::ShareDamage) == 1);
    CHECK(mod_id(other, kBondShare)->caster == 3);
}

TEST_CASE("7.2zi Warding Bond: casting it on a connected creature ends the previous bond, then the new share is once") {
    auto s = start({wiz(1), wiz(2), wiz(3), foe(4)});
    mut(s, 1).hp = mut(s, 1).hp_max = 40;
    mut(s, 2).hp = mut(s, 2).hp_max = 40;
    mut(s, 3).hp = mut(s, 3).hp_max = 40;
    REQUIRE(step(s, cast(1, kBond, 2, 2)).ok);
    end_turns_until(s, 3);
    auto again = step(s, cast(3, kBond, 1, 2));   // 1 is already the caster of a bond
    REQUIRE(again.ok);
    CHECK(expired(again.events, kBondShare, 2));
    CHECK(count_stat(s, ModStat::ShareDamage) == 1);
    CHECK(mod_id(s, kBondShare)->caster == 3);
    CHECK(mod_id(s, kBondShare)->target == 1);
    end_turns_until(s, 2);
    const int h2 = find(s, 2)->hp;
    auto unwarded = until_hit(s, 2, kFireBoltFlat, 1);
    REQUIRE(unwarded.ok);
    CHECK(dealt_to(unwarded.events, 1) == 4);                 // 3's bond: resist all
    CHECK(find(s, 3)->hp == int16_t(40 - 4));                 // the share, once
    CHECK(find(s, 2)->hp == h2);                              // 2 is not warded, so nothing returns
    s.budget.action = true;
    auto plain = until_hit(s, 2, kFireBoltFlat, 4);
    REQUIRE(plain.ok);
    CHECK(dealt_to(plain.events, 4) == 8);                    // the old target is no longer warded
}
