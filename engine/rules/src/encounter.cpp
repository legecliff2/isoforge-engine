#include "rules5e/encounter.hpp"

#include <algorithm>
#include <cassert>
#include <array>

#include "rules5e/abilities.hpp"
#include "rules5e/checks.hpp"
#include "rules5e/weapon.hpp"
#include "rules5e/d20.hpp"
#include "rules5e/saves.hpp"
#include "rules5e/conditions.hpp"
#include "rules5e/damage.hpp"
#include "rules5e/dice.hpp"
#include "rules5e/healing.hpp"
#include "rules5e/font_of_magic.hpp"

namespace rules5e {

namespace {

Combatant* find_mut(EncounterState& s, CombatantId id) {
    for (auto& c : s.combatants) if (c.id == id) return &c;
    return nullptr;
}

bool can_act(const Combatant& c) { return !c.dead && c.hp > 0 && !is_incapacitated(c); }
bool is_dying(const Combatant& c) { return !c.dead && c.hp <= 0 && !c.death.stable; }

void emit(std::vector<Event>& ev, EventKind k, CombatantId actor, CombatantId target = 0,
          int32_t a = 0, int32_t b = 0, int32_t c = 0) {
    ev.push_back(Event{k, actor, target, a, b, c});
}

// R-G12: a transform ends (Reverted{actor, a = source, b = reason, c = overflow}); no-op when not transformed.
void form_reverted(Combatant& c, int reason, int overflow, std::vector<Event>& ev) {
    const uint32_t src = c.form_source;
    if (!revert_form(c)) return;
    emit(ev, EventKind::Reverted, c.id, 0, int32_t(src), reason, overflow);
}

void kill(Combatant& c, std::vector<Event>& ev) {
    form_reverted(c, 4, 0, ev);   // R-G12: a creature that dies in a form reverts (shapechanger: "reverts on death")
    c.dead = true;
    c.hp = 0;
    emit(ev, EventKind::CombatantDied, c.id);
}


// ---- Timed modifiers (row 12) ----
void refresh_mod_cache(EncounterState& s) {
    for (auto& c : s.combatants) c.mod_resist = 0;
    for (const auto& m : s.modifiers)
        if (m.spec.stat == ModStat::Resistance && m.spec.has_damage_type)
            for (auto& c : s.combatants) if (c.id == m.target) c.mod_resist |= bit(m.spec.damage_type);
}

bool mod_in_scope(const ModifierSpec& m, ModStat st, int ability) {
    if (m.stat != st) return false;
    return !(m.has_ability && ability >= 0 && int(m.ability) != ability);
}

// Advantage / disadvantage sources from the modifiers of `id` for `st` (AttackRoll, Save, AbilityCheck, AttacksAgainst).
void mod_modes(const EncounterState& s, CombatantId id, ModStat st, int ability, bool& adv, bool& dis) {
    for (const auto& m : s.modifiers) {
        if (m.gated || m.target != id || !mod_in_scope(m.spec, st, ability)) continue;
        if (m.spec.mode == RollMode::Advantage) adv = true;
        else if (m.spec.mode == RollMode::Disadvantage) dis = true;
    }
}

// Dice of the modifiers of `id` for `st`, rolled now in application order (one rng draw per die, none without one).
int mod_roll(EncounterState& s, CombatantId id, ModStat st, int ability) {
    int total = 0;
    for (const auto& m : s.modifiers) {
        if (m.gated || m.target != id || !mod_in_scope(m.spec, st, ability) || m.spec.die.count == 0) continue;
        total += m.spec.die_mult * roll_dice(s.rng, m.spec.die, false);
    }
    return total;
}

void remove_modifier(EncounterState& s, std::size_t i, int reason, std::vector<Event>& ev) {
    const ActiveModifier m = s.modifiers[i];
    s.modifiers.erase(s.modifiers.begin() + std::ptrdiff_t(i));
    emit(ev, EventKind::ModifierExpired, m.caster, m.target, int32_t(m.spec.id), reason);
    if (m.spec.stat == ModStat::Resistance) refresh_mod_cache(s);
    if (m.spec.stat == ModStat::Haste && reason != 2) {   // Haste ended: lethargy (none while another Haste still holds)
        const bool still = std::any_of(s.modifiers.begin(), s.modifiers.end(), [&](const ActiveModifier& o) {
            return o.target == m.target && o.spec.stat == ModStat::Haste; });
        Combatant* t = find_mut(s, m.target);
        if (!still && t && !t->dead) t->lethargy = 1;
    }
}

bool has_marker(const EncounterState& s, CombatantId id, ModStat st) {
    return std::any_of(s.modifiers.begin(), s.modifiers.end(), [&](const ActiveModifier& m) {
        return m.target == id && m.spec.stat == st; });
}

// 7.2u: Chill Touch: a NoHealing marker stops hit point regain on every path (temp HP is not regaining hit points).
HealingApplied heal_hp(const EncounterState& s, const Combatant& c, int16_t amount) {
    if (!s.modifiers.empty() && has_marker(s, c.id, ModStat::NoHealing)) return {0, c.hp, false};
    return apply_healing(c, amount);
}

// reason: 0 duration ran out, 1 concentration ended, 2 replaced by a same-id modifier, 3 spent by an attack roll, 4 damaged, 5 dispelled (R-G15),
// 6 repeat save succeeded (Slow)
void tick_modifiers_round_start(EncounterState& s, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.modifiers.size();) {
        if (tick_round_start(s.modifiers[i].timed)) remove_modifier(s, i, 0, ev); else ++i;
    }
}
void tick_modifiers_turn_end(EncounterState& s, CombatantId cid, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.modifiers.size();) {
        if (s.modifiers[i].target == cid && tick_target_turn_end(s.modifiers[i].timed)) remove_modifier(s, i, 0, ev); else ++i;
    }
}
void drop_caster_turn_modifiers(EncounterState& s, CombatantId caster, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.modifiers.size();) {
        const auto& m = s.modifiers[i];
        if ((m.caster == caster && m.spec.until_caster_turn) ||
            (m.target == caster && m.timed.kind == Expiry::StartOfTargetNextTurn)) remove_modifier(s, i, 0, ev);   // 7.2u
        else ++i;
    }
}
// 7.2u: end of `cid`'s turn: EndOfCasterNextTurn timers of modifiers `cid` applied tick.
void tick_caster_turn_end(EncounterState& s, CombatantId cid, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.modifiers.size();) {
        auto& m = s.modifiers[i];
        if (m.caster == cid && m.timed.kind == Expiry::EndOfCasterNextTurn && m.timed.rounds_left > 0 && --m.timed.rounds_left == 0)
            remove_modifier(s, i, 0, ev);
        else ++i;
    }
}
void drop_concentration_modifiers(EncounterState& s, CombatantId caster, uint32_t source, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.modifiers.size();) {
        const auto& m = s.modifiers[i];
        if (m.caster == caster && m.spec.concentration && m.spec.source == source) remove_modifier(s, i, 1, ev); else ++i;
    }
}

// Early consumption (ModifierExpired reason 3: spent by an attack roll, 4: the target took damage).
void consume_modifiers(EncounterState& s, int kind, CombatantId id, int reason, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.modifiers.size();) {
        if (!s.modifiers[i].gated && s.modifiers[i].spec.consume == kind && s.modifiers[i].target == id) remove_modifier(s, i, reason, ev); else ++i;
    }
}

int aura_save_bonus(const EncounterState& s, const Combatant& t, Ability ab);   // R-G11 PR 2 (D7), below spell_def
DamageApplied zone_apply_damage(const EncounterState& s, const Combatant& t, int16_t amount, DamageType type, uint8_t qualifiers);

SaveResult save_roll_raw(EncounterState& s, const Combatant& t, Ability ab, int dc, RollMode mode, const SaveSource* src) {
    bool adv = mode == RollMode::Advantage, dis = mode == RollMode::Disadvantage;
    mod_modes(s, t.id, ModStat::Save, int(ab), adv, dis);
    const RollMode m = combine_modes(adv, dis);
    SaveResult r = src ? saving_throw(s.rng, t, ab, dc, m, *src) : saving_throw(s.rng, t, ab, dc, m);
    if (r.auto_fail) return r;
    if ((t.reroll_ones & 4) && reroll_natural_one(s.rng, r.roll)) r.success = r.roll.total >= dc;   // Lucky
    const int add = modifier_flat(s, t.id, ModStat::Save, int(ab)) + mod_roll(s, t.id, ModStat::Save, int(ab)) +
                    (s.zones.empty() ? 0 : aura_save_bonus(s, t, ab));
    if (add != 0) { r.roll.total = int16_t(r.roll.total + add); r.success = r.roll.total >= dc; }
    return r;
}

// d20 saving throw with modifiers: modes merge, then flat bonus and dice (drawn after the d20, only if a die exists).
// Row 13, Legendary Resistance: a failed save turns into a success by spending 1 from Combatant::legendary_pool
// (ResourceSpent is emitted before the caller's SavingThrowRolled, which then reports success).
SaveResult save_roll(EncounterState& s, const Combatant& t, Ability ab, int dc, RollMode mode, std::vector<Event>& ev,
                     const SaveSource* src = nullptr) {
    SaveResult r = save_roll_raw(s, t, ab, dc, mode, src);
    if (!r.success && t.legendary_pool != 0 && can_spend(s, t.id, t.legendary_pool, 1) && spend_pool(s, t.id, t.legendary_pool, 1)) {
        r.success = true;
        emit(ev, EventKind::ResourceSpent, t.id, 0, int32_t(t.legendary_pool), 1, find_pool(s, t.id, t.legendary_pool)->cur);
    }
    return r;
}

// R-G8: record the source of a spell-applied Charmed / Frightened (other conditions carry no source).
void note_source(EncounterState& s, Condition c, CombatantId source, CombatantId target) {
    if (c != Condition::Charmed && c != Condition::Frightened) return;
    for (auto& e : s.sourced) if (e.condition == c && e.target == target && e.source == source) return;
    s.sourced.push_back(SourcedCondition{c, target, source, 0});
}
bool charmed_by(const EncounterState& s, CombatantId actor, CombatantId other) {
    for (const auto& e : s.sourced) if (e.condition == Condition::Charmed && e.target == actor && e.source == other) return true;
    return false;
}

// R-G12 PR 2: summon `c` ends (reason 1 concentration, 3 0 HP, 5 encounter ended): SummonEnded, then it is dead and gone (no
// CombatantDied of its own; a form it wears ends silently with it).
void end_concentration(EncounterState& s, CombatantId caster, std::vector<Event>& ev);
void end_summon(EncounterState& s, Combatant& c, int32_t reason, std::vector<Event>& ev) {
    if (c.summoner == 0 || c.gone) return;
    // its own concentration ends the usual way first (ConcentrationEnded; modifiers, zones, Polymorph links cleaned up)
    if (c.concentration_spell != 0) end_concentration(s, c.id, ev);
    c.form = FormKind::None; c.form_source = 0; c.form_caster = 0; c.form_archetype = 0; c.form_end_round = 0;
    c.base.reset(); c.form_blocked_features.clear();
    c.gone = true; c.dead = true; c.hp = 0; c.commanded = 0;
    emit(ev, EventKind::SummonEnded, c.summoner, c.id, int32_t(c.summon_source), reason);
}

void check_end(EncounterState& s, std::vector<Event>& ev) {
    if (s.ended) return;
    auto o = s.hooks.end_condition ? s.hooks.end_condition(s) : default_end_condition(s);
    if (!o) return;
    s.ended = true;
    if (s.summons != 0) {   // R-G12 PR 2 (D15): every summon ends first, ascending id (a transformed summon just ends: no Reverted)
        std::vector<CombatantId> ids;
        for (const auto& c : s.combatants) if (c.summoner != 0 && !c.gone) ids.push_back(c.id);
        for (const auto& w : s.summon_weapons) ids.push_back(w.token_id);   // PR 3: weapon tokens in the same pass, by token id
        std::sort(ids.begin(), ids.end());
        // their own concentration ends first, before the contiguous teardown block (teardown_begin stays one block)
        for (CombatantId id : ids) if (const Combatant* c = find(s, id); c && c->concentration_spell != 0) end_concentration(s, id, ev);
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](CombatantId id) { const Combatant* c = find(s, id); return c && c->gone; }), ids.end());
        for (CombatantId id : ids) {
            if (Combatant* c = find_mut(s, id)) { end_summon(s, *c, 5, ev); continue; }
            for (std::size_t k = 0; k < s.summon_weapons.size(); ++k) {
                if (s.summon_weapons[k].token_id != id) continue;
                const ActiveSummonWeapon w = s.summon_weapons[k];
                s.summon_weapons.erase(s.summon_weapons.begin() + std::ptrdiff_t(k));
                emit(ev, EventKind::SummonEnded, w.owner, w.token_id, int32_t(w.spell_id), 5);
                break;
            }
        }
    }
    // R-G12 (D15, SUMMONS_TRANSFORMS.md §3.5): every transform ends with the encounter, ascending id, before EncounterEnded
    if (s.transforms != 0) {
        std::vector<CombatantId> ids;
        for (const auto& c : s.combatants) if (c.form != FormKind::None) ids.push_back(c.id);
        std::sort(ids.begin(), ids.end());
        for (CombatantId id : ids) form_reverted(*find_mut(s, id), 5, 0, ev);
    }
    emit(ev, EventKind::EncounterEnded, 0, 0, int32_t(o->reason));
    s.outcome = std::move(*o);
}

void stabilize(Combatant& t, std::vector<Event>& ev) {
    t.death = DeathSaves{0, 0, true};
    emit(ev, EventKind::Stabilized, t.id);
}

// Everything that follows damage: dropping to 0, massive damage, death-save failures.
void after_damage(Combatant& t, const DamageApplied& d, bool critical, std::vector<Event>& ev) {
    if (d.dropped_to_zero && (t.form == FormKind::Polymorph || t.form == FormKind::WildShape)) {
        // R-G12: the form drops to 0 HP and the creature reverts; excess damage carries over to its normal form, where the drop,
        // massive damage (against the normal form's hp_max, D12) and death saves apply as usual. No massive damage in the form.
        const int to_hp = int(d.dealt) - (int(t.temp_hp) - int(d.temp_hp_after));
        const int overflow = std::max(0, to_hp - int(t.hp));
        commit_damage(t, d);
        emit(ev, EventKind::DamageDealt, 0, t.id, int32_t(d.dealt) - overflow, 0);   // what the form (and temp HP) absorbed
        form_reverted(t, 2, overflow, ev);   // the carried DamageDealt below holds the overflow: the two sum to the hit
        if (overflow > 0) {
            const DamageApplied carried{int16_t(std::min(overflow, 32767)), t.temp_hp, t.hp, false, false, d.types_dealt};
            after_damage(t, reduce_applied(t, carried, 0), critical, ev);
        }
        return;
    }
    bool was_at_zero = t.hp <= 0;
    commit_damage(t, d);
    emit(ev, EventKind::DamageDealt, 0, t.id, d.dealt, d.hp_after);
    if (t.regeneration.amount > 0 && (d.types_dealt & t.regeneration.suppressed_by)) t.regen_suppressed = true;
    if (d.instant_death) { kill(t, ev); return; }
    if (d.dropped_to_zero) {
        if (!t.uses_death_saves && t.regeneration.amount > 0) {   // regenerators die only at their next turn start
            t.regen_down = true;
            emit(ev, EventKind::CombatantDowned, t.id);
            return;
        }
        if (!t.uses_death_saves) { kill(t, ev); return; }
        emit(ev, EventKind::CombatantDowned, t.id);
        t.death = DeathSaves{};
        for (Condition k : {Condition::Unconscious, Condition::Prone}) {
            if (!t.has(k)) {
                add_condition(t, k);
                if (t.has(k)) emit(ev, EventKind::ConditionAdded, t.id, 0, int32_t(k));   // not if immune
            }
        }
        return;
    }
    if (was_at_zero && d.dealt > 0 && t.uses_death_saves) {
        t.death = death_save_failures_from_damage(t.death, critical);
        emit(ev, EventKind::DeathSaveRolled, t.id, 0, 0, t.death.successes, t.death.failures);
        if (t.death.failures >= 3) kill(t, ev);
    }
}

// ---- Lasting spell effects and concentration ----

// Removes effect `i`; its condition ends unless another effect still imposes it.
void end_effect(EncounterState& s, std::size_t i, std::vector<Event>& ev) {
    const ActiveSpellEffect e = s.effects[i];
    s.effects.erase(s.effects.begin() + std::ptrdiff_t(i));
    Combatant* t = find_mut(s, e.target);
    if (!t) return;
    // EC spider follow-up (flagged): an effect's `also` condition holds the condition too, and ends with its effect
    auto held = [&](Condition k) {
        return std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
            return o.target == e.target && ((!o.no_condition && o.condition == k) || (o.has_also && o.also == k)); });
    };
    if (e.has_also && !held(e.also) && t->has(e.also)) {
        remove_condition(*t, e.also);
        emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(e.also));
    }
    bool still = held(e.condition);
    if (!e.no_condition && !still && t->has(e.condition)) {
        remove_condition(*t, e.condition);
        emit(ev, EventKind::SpellEffectEnded, t->id, 0, int32_t(e.spell_id), int32_t(e.condition));
        emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(e.condition));
    } else {
        emit(ev, EventKind::SpellEffectEnded, t->id, 0, int32_t(e.spell_id), -1);
    }
}

// ---- R-G11 zones: lifetime (triggers are further down, after damage_creature) ----
uint32_t turn_stamp(const EncounterState& s) { return (uint32_t(s.round) << 16) | s.turn_index; }

// R-G12 PR 2: the ids the next `n` summons take: from the counter (seeded by sim), never at or below an existing id.
std::vector<CombatantId> next_summon_ids(const EncounterState& s, std::size_t n) {
    CombatantId top = 0;
    for (const auto& c : s.combatants) top = std::max(top, c.id);
    const CombatantId base = std::max<CombatantId>(s.next_summon_id, top + 1);
    std::vector<CombatantId> ids;
    for (std::size_t k = 0; k < n; ++k) ids.push_back(CombatantId(base + k));
    return ids;
}

// R-G12 PR 2: a turn entry inserted at `pos` moves every later turn of this round one index on: every stored cast_stamp
// (round << 16 | turn_index) of this round at or after `pos` follows its turn, so it still names the same turn.
void shift_turn_stamps(EncounterState& s, std::size_t pos) {
    auto fix = [&](uint32_t& st) {
        if ((st >> 16) == s.round && (st & 0xFFFFu) >= pos) ++st;
    };
    for (auto& e : s.effects) fix(e.cast_stamp);
    for (auto& z : s.zones) { fix(z.cast_stamp); for (auto& f : z.fired) fix(f.stamp); }
    for (auto& c : s.combatants) fix(c.trigger_turn_stamp);
}

// Does summon `c` need a command to act this turn (else it only Dodges, D4)?
bool awaits_command(const Combatant& c) {
    return c.summoner != 0 && c.commanded != 1 &&
           (c.summon_control == uint8_t(SummonControl::Verbal) || c.summon_control == uint8_t(SummonControl::BonusAction));
}

ActiveZone* find_zone_mut(EncounterState& s, uint32_t id) {
    for (auto& z : s.zones) if (z.zone_id == id) return &z;
    return nullptr;
}

// A live zone of `owner`'s spell (keeps its concentration alive like a remaining effect).
bool zone_alive(const EncounterState& s, CombatantId owner, uint32_t spell) {
    return std::any_of(s.zones.begin(), s.zones.end(), [&](const ActiveZone& z) { return z.owner == owner && z.spell_id == spell; });
}

// Ends the "while in the zone" effects of zone `zone_id` (on `who` only, or on everyone when 0), in application order.
void end_zone_effects(EncounterState& s, uint32_t zone_id, CombatantId who, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.effects.size();) {
        const ActiveSpellEffect& e = s.effects[i];
        if (e.zone_id == zone_id && (who == 0 || e.target == who)) end_effect(s, i, ev); else ++i;
    }
}

// Removes zone `i`: ZoneEnded{owner, a = zone id, b = reason}, then its "while in the zone" effects end.
void end_zone(EncounterState& s, std::size_t i, int32_t reason, std::vector<Event>& ev) {
    const ActiveZone z = s.zones[i];
    s.zones.erase(s.zones.begin() + std::ptrdiff_t(i));
    emit(ev, EventKind::ZoneEnded, z.owner, 0, int32_t(z.zone_id), reason);
    end_zone_effects(s, z.zone_id, 0, ev);
}

// Ends `c`'s concentration and every effect that depends on it.
void end_concentration(EncounterState& s, CombatantId caster, std::vector<Event>& ev) {
    Combatant* c = find_mut(s, caster);
    if (!c || c->concentration_spell == 0) return;
    const uint32_t sp = c->concentration_spell;
    c->concentration_spell = 0;
    emit(ev, EventKind::ConcentrationEnded, caster, 0, int32_t(sp));
    for (auto& t : s.combatants)   // R-G12: Polymorph ends with its caster's concentration (Reverted b = 1)
        if (t.form == FormKind::Polymorph && t.form_caster == caster && t.form_source == sp) form_reverted(t, 1, 0, ev);
    // TODO(R-G12 D5): Conjure Elemental's elemental turns hostile (its own turn, not controlled) instead of ending; later PR.
    for (auto& t : s.combatants)   // R-G12 PR 2 (D10): concentration summons end with it (SummonEnded b = 1), ascending order
        if (t.summoner == caster && t.summon_source == sp && t.summon_concentration && !t.gone) end_summon(s, t, 1, ev);
    drop_concentration_modifiers(s, caster, sp, ev);
    for (std::size_t i = 0; i < s.zones.size();) {   // R-G11: its zones end first (ZoneEnded reason 1)
        if (s.zones[i].owner == caster && s.zones[i].concentration && s.zones[i].spell_id == sp) end_zone(s, i, 1, ev);
        else ++i;
    }
    for (std::size_t i = 0; i < s.effects.size();) {
        const auto& e = s.effects[i];
        if (e.caster == caster && e.concentration && e.spell_id == sp) end_effect(s, i, ev);
        else ++i;
    }
}

// Petrifying Gaze: the effect's condition becomes its escalate_to condition for good (no more saves, no timer). Same
// events as any condition change: ConditionRemoved for the old one (unless something else still imposes it), ConditionAdded.
void escalate_effect(EncounterState& s, std::size_t i, std::vector<Event>& ev) {
    ActiveSpellEffect& e = s.effects[i];
    Combatant* t = find_mut(s, e.target);
    const Condition old = e.condition, next = e.escalate_to;
    e.condition = next; e.escalates = false; e.repeat_save = RepeatSave::None; e.timed = Timed{};
    if (!t) return;
    const bool still = std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
        return o.target == t->id && !o.no_condition && o.condition == old; });
    if (!still && t->has(old)) {
        remove_condition(*t, old);
        emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(old));
    }
    if (!(t->condition_immunities & bit(next)) && !t->has(next)) {
        add_condition(*t, next);
        emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(next));
    }
}

// Repeat saves with this timing for effects on `c` (Hold Person: end of turn). A success ends that
// effect; the caster's concentration ends once none of its effects from that spell remain.
void repeat_saves(EncounterState& s, CombatantId cid, RepeatSave timing, std::vector<Event>& ev) {
    std::vector<ActiveSpellEffect> due;
    for (const auto& e : s.effects) if (e.target == cid && e.repeat_save == timing) due.push_back(e);
    std::vector<std::pair<uint32_t, CombatantId>> done;   // one save per (spell, caster) even if it left several effects
    for (const auto& e : due) {
        Combatant* c = find_mut(s, cid);
        if (!c || c->dead) return;
        if (std::find(done.begin(), done.end(), std::make_pair(e.spell_id, e.caster)) != done.end()) continue;
        done.emplace_back(e.spell_id, e.caster);
        auto it = std::find_if(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
            return o.spell_id == e.spell_id && o.caster == e.caster && o.target == e.target; });
        if (it == s.effects.end()) continue;
        const SaveSource ssrc{true, 0, 0};
        auto r = save_roll(s, *c, e.save_ability, e.dc, RollMode::Normal, ev, &ssrc);   // an ongoing spell effect
        emit(ev, EventKind::SavingThrowRolled, cid, CombatantId(e.save_ability), r.roll.natural, r.roll.total, int32_t(r.success));
        if (!r.success) {
            if (it->escalates) escalate_effect(s, std::size_t(it - s.effects.begin()), ev);   // Petrifying Gaze: second failure
            continue;
        }
        end_effect(s, std::size_t(it - s.effects.begin()), ev);
        for (;;) {   // a multi-effect spell ends all its effects on this target together
            auto more = std::find_if(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
                return o.spell_id == e.spell_id && o.caster == e.caster && o.target == e.target; });
            if (more == s.effects.end()) break;
            end_effect(s, std::size_t(more - s.effects.begin()), ev);
        }
        bool remaining = std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
            return o.caster == e.caster && o.spell_id == e.spell_id; }) || zone_alive(s, e.caster, e.spell_id);
        Combatant* caster = find_mut(s, e.caster);
        if (!remaining && caster && caster->concentration_spell == e.spell_id) end_concentration(s, e.caster, ev);
    }
}

// Slow-style modifier repeat saves at the end of `cid`'s turn: one save per (source, caster); a success removes every
// modifier of that source and caster on it (ModifierExpired reason 6), then ends the caster's concentration on the source
// once nothing of it remains. SavingThrowRolled as for repeat_saves.
void repeat_modifier_saves(EncounterState& s, CombatantId cid, std::vector<Event>& ev) {
    std::vector<ActiveModifier> due;
    for (const auto& m : s.modifiers) if (m.target == cid && m.spec.repeat_save == 1) due.push_back(m);
    std::vector<std::pair<uint32_t, CombatantId>> done;
    for (const auto& m : due) {
        Combatant* c = find_mut(s, cid);
        if (!c || c->dead) return;
        const uint32_t src = m.spec.source ? m.spec.source : m.spec.id;
        if (std::find(done.begin(), done.end(), std::make_pair(src, m.caster)) != done.end()) continue;
        done.emplace_back(src, m.caster);
        const SaveSource ssrc{true, 0, 0};
        auto r = save_roll(s, *c, m.spec.repeat_ability, m.spec.repeat_dc, RollMode::Normal, ev, &ssrc);
        emit(ev, EventKind::SavingThrowRolled, cid, CombatantId(m.spec.repeat_ability), r.roll.natural, r.roll.total, int32_t(r.success));
        if (!r.success) continue;
        for (std::size_t k = 0; k < s.modifiers.size();) {
            const auto& o = s.modifiers[k];
            if (o.target == cid && o.caster == m.caster && (o.spec.source ? o.spec.source : o.spec.id) == src) remove_modifier(s, k, 6, ev);
            else ++k;
        }
        const bool remaining = std::any_of(s.modifiers.begin(), s.modifiers.end(), [&](const ActiveModifier& o) {
            return o.caster == m.caster && o.spec.source == src; });
        Combatant* caster = find_mut(s, m.caster);
        if (!remaining && caster && caster->concentration_spell == src) end_concentration(s, m.caster, ev);
    }
}

// Timed effects that ran out: end them (and any sibling effects of the same spell on the same target, like a
// repeat-save success), then end the caster's concentration once nothing of that spell remains. Events are the
// existing SpellEffectEnded (+ ConditionRemoved when the condition goes away).
using GoneEffect = std::pair<uint32_t, std::pair<CombatantId, CombatantId>>;   // (spell, (caster, target))
void expire_effects(EncounterState& s, const std::vector<GoneEffect>& gone, std::vector<Event>& ev) {
    for (const auto& g : gone) {
        for (;;) {
            auto it = std::find_if(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
                return o.spell_id == g.first && o.caster == g.second.first && o.target == g.second.second; });
            if (it == s.effects.end()) break;
            end_effect(s, std::size_t(it - s.effects.begin()), ev);
        }
        bool remaining = std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
            return o.caster == g.second.first && o.spell_id == g.first; }) || zone_alive(s, g.second.first, g.first);
        Combatant* caster = find_mut(s, g.second.first);
        if (!remaining && caster && caster->concentration_spell == g.first) end_concentration(s, g.second.first, ev);
    }
}

// New round: Rounds timers tick (all effects, in application order).
struct TrigSrc;
struct Ctx;
void damage_creature(EncounterState& s, Combatant& t, const DamageApplied& d, bool critical, std::vector<Event>& ev,
                     const TrigSrc* src, Ctx* ctx);
// R-G15: delayed damage (Acid Arrow) lands as its timer runs out, before the effect ends.
void deal_effect_damage(EncounterState& s, const ActiveSpellEffect& e, std::vector<Event>& ev) {
    Combatant* t = find_mut(s, e.target);
    if (!t || t->dead) return;
    const int dmg = std::max(0, roll_dice(s.rng, e.damage));
    damage_creature(s, *t, zone_apply_damage(s, *t, int16_t(dmg), e.damage_type, e.magical ? uint8_t(kAttackMagical) : uint8_t(0)), false, ev, nullptr, nullptr);
}

void tick_effects_round_start(EncounterState& s, std::vector<Event>& ev) {
    std::vector<GoneEffect> gone;
    std::vector<ActiveSpellEffect> due;
    for (auto& e : s.effects)
        if (tick_round_start(e.timed)) { gone.push_back({e.spell_id, {e.caster, e.target}}); if (e.damage_when == 1) due.push_back(e); }
    for (const auto& e : due) deal_effect_damage(s, e, ev);
    expire_effects(s, gone, ev);
    tick_modifiers_round_start(s, ev);
    for (std::size_t i = 0; i < s.zones.size();) {   // R-G11 PR 2: a feature aura ends with its dead owner (ZoneEnded reason 2)
        const Combatant* o = s.zones[i].feature ? find(s, s.zones[i].owner) : nullptr;
        if (s.zones[i].feature && (!o || o->dead)) end_zone(s, i, 2, ev); else ++i;
    }
    if (!s.zones.empty()) {   // R-G11: zone timers (ZoneEnded reason 0); the spell ends with its last zone
        std::vector<uint32_t> out;
        for (auto& z : s.zones) if (tick_round_start(z.timed)) out.push_back(z.zone_id);
        for (uint32_t id : out) {
            for (std::size_t i = 0; i < s.zones.size(); ++i) {
                if (s.zones[i].zone_id != id) continue;
                const CombatantId owner = s.zones[i].owner; const uint32_t sp = s.zones[i].spell_id;
                end_zone(s, i, 0, ev);
                const Combatant* c = find(s, owner);
                const bool left = zone_alive(s, owner, sp) || std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
                    return o.caster == owner && o.spell_id == sp; });
                if (!left && c && c->concentration_spell == sp) end_concentration(s, owner, ev);
                break;
            }
        }
    }
}

// End of `cid`'s turn: EndOfTargetNextTurn timers on `cid` tick.
void tick_effects_turn_end(EncounterState& s, CombatantId cid, std::vector<Event>& ev) {
    std::vector<GoneEffect> gone;
    std::vector<ActiveSpellEffect> due;
    for (auto& e : s.effects)
        if (e.target == cid && tick_target_turn_end(e.timed)) { gone.push_back({e.spell_id, {e.caster, e.target}}); if (e.damage_when == 1) due.push_back(e); }
    for (const auto& e : due) deal_effect_damage(s, e, ev);
    expire_effects(s, gone, ev);
    tick_modifiers_turn_end(s, cid, ev);
    if (!s.modifiers.empty()) tick_caster_turn_end(s, cid, ev);
}

// SRD: concentration ends when the caster is incapacitated or dies.
void sweep_concentration(EncounterState& s, std::vector<Event>& ev) {
    if (s.transforms != 0 && !s.ended) {
        for (auto& c : s.combatants)   // R-G12: Wild Shape also ends when the druid falls unconscious (Reverted b = 2)
            if (c.form == FormKind::WildShape && c.has(Condition::Unconscious)) form_reverted(c, 2, 0, ev);
        for (const auto& c : s.combatants) {   // a Polymorph whose target reverted (0 HP, death) ends: the caster's concentration
            if (c.concentration_spell == 0 || !s.hooks.spells) continue;
            const SpellDef* d = s.hooks.spells->find(c.concentration_spell);
            if (!d || d->kind != SpellEffectKind::Polymorph) continue;
            const bool live = std::any_of(s.combatants.begin(), s.combatants.end(), [&](const Combatant& t) {
                return t.form == FormKind::Polymorph && t.form_caster == c.id && t.form_source == c.concentration_spell; });
            if (!live) end_concentration(s, c.id, ev);
        }
    }
    for (std::size_t i = 0; i < s.combatants.size(); ++i) {
        const Combatant& c = s.combatants[i];
        if (c.concentration_spell != 0 && (c.dead || c.hp <= 0 || is_incapacitated(c)))
            end_concentration(s, c.id, ev);
    }
}

// ---- Triggers (rows 9a/9b), defined before do_attack ----
struct TrigSrc {                 // who dealt the damage, and how (attack facts for weapon predicates)
    Combatant* who = nullptr;
    const Attack* atk = nullptr; // null for spell/save damage
    bool is_spell = false;
    RollMode mode = RollMode::Normal;
    bool ally = false;
};
struct TrigCtx {                 // facts a predicate can read
    const Attack* atk = nullptr;
    bool is_spell = false;
    RollMode mode = RollMode::Normal;
    bool ally = false;
    bool crit = false;
    uint32_t types = 0;          // damage types of the event (bitmask)
    int dmg = 0;                 // damage taken / dealt (Save predicate dc_from_damage)
    CombatantId mod_caster = 0;  // 7.2u: the caster of the modifier whose `when` is evaluated (SubjectIsCaster)
};
struct Ctx;
struct TrigOut { bool set_hp = false; int16_t hp = 0; };
bool run_event_triggers(EncounterState& s, Combatant& owner, Combatant& subject, TriggerOn on, const TrigCtx& x,
                        std::vector<Event>& ev, TrigOut* out = nullptr, Ctx* ctx = nullptr);

struct Ctx;
void damage_creature(EncounterState& s, Combatant& t, const DamageApplied& d, bool critical, std::vector<Event>& ev,
                     const TrigSrc* src = nullptr, Ctx* ctx = nullptr);

const SpellDef* spell_def(const EncounterState& s, uint32_t id) {
    return s.hooks.spells ? s.hooks.spells->find(id) : nullptr;
}

// ---- R-G11 rules PR 2: passive zone queries (Silence, D7 auras). All answer false / 0 at once without zones. ----
// A needs_conscious_owner zone does nothing while its owner is dead, at 0 HP or unconscious.
bool zone_suppressed(const EncounterState& s, const ActiveZone& z, const SpellDef& def) {
    if (!def.zone.needs_conscious_owner) return false;
    const Combatant* o = find(s, z.owner);
    return !o || o->dead || o->hp <= 0 || o->has(Condition::Unconscious);
}
// Is `c` covered by the passive part of aura `z`: the owner of a creature aura (always inside it) or a creature in the zone;
// the owner's team only when aura_allies_only.
bool in_aura(const EncounterState& s, const ActiveZone& z, const SpellDef& def, const Combatant& c) {
    if (c.dead || zone_suppressed(s, z, def)) return false;
    if (def.zone.aura_allies_only) {
        const Combatant* o = find(s, z.owner);
        if (!o || o->team != c.team) return false;
    }
    if (def.zone.anchor == ZoneAnchor::Creature && c.id == z.owner) return true;
    return s.hooks.zone_contains && s.hooks.zone_contains(s, z.zone_id, c.id, false);
}
// D7: flat save bonus from auras (aura_modifier, stat Save). The same spell's auras don't stack: the highest counts.
int aura_save_bonus(const EncounterState& s, const Combatant& t, Ability ab) {
    std::vector<std::pair<uint32_t, int>> best;   // (spell, bonus)
    for (const auto& z : s.zones) {
        const SpellDef* d = spell_def(s, z.spell_id);
        if (!d || d->zone.aura_modifier.id == 0 || !mod_in_scope(d->zone.aura_modifier, ModStat::Save, int(ab)) || !in_aura(s, z, *d, t))
            continue;
        int bonus = d->zone.aura_modifier.bonus;
        if (d->zone.has_aura_bonus_ability) {
            const Combatant* o = find(s, z.owner);
            bonus = std::max(1, o ? modifier(o->abilities, d->zone.aura_bonus_ability) : 1);
        }
        auto it = std::find_if(best.begin(), best.end(), [&](const std::pair<uint32_t, int>& p) { return p.first == z.spell_id; });
        if (it == best.end()) best.emplace_back(z.spell_id, bonus);
        else it->second = std::max(it->second, bonus);
    }
    int sum = 0;
    for (const auto& p : best) sum += p.second;
    return sum;
}
// D7: can `t` not be given `c` because of an aura (aura_immunity)?
bool aura_immune(const EncounterState& s, const Combatant& t, Condition c) {
    for (const auto& z : s.zones) {
        const SpellDef* d = spell_def(s, z.spell_id);
        if (d && (d->zone.aura_immunity & bit(c)) && in_aura(s, z, *d, t)) return true;
    }
    return false;
}
// Condition immunity: the creature's own, or an aura's.
bool immune_to(const EncounterState& s, const Combatant& t, Condition c) {
    return (t.condition_immunities & bit(c)) != 0 || (!s.zones.empty() && aura_immune(s, t, c));
}
// Damage as apply_damage, plus Silence's thunder immunity for a creature completely inside a Silence zone.
DamageApplied zone_apply_damage(const EncounterState& s, const Combatant& t, int16_t amount, DamageType type, uint8_t qualifiers) {
    if (type == DamageType::Thunder && !s.zones.empty() && in_silence(s, t.id)) {
        Combatant immune = t;
        immune.immunities |= bit(DamageType::Thunder);
        return apply_damage(immune, amount, type, qualifiers);
    }
    return apply_damage(t, amount, type, qualifiers);
}
DamageApplied zone_apply_damage_parts(const EncounterState& s, const Combatant& t, std::span<const DamagePiece> parts, uint8_t qualifiers) {
    if (!s.zones.empty() && std::any_of(parts.begin(), parts.end(), [](const DamagePiece& p) { return p.type == DamageType::Thunder; }) &&
        in_silence(s, t.id)) {
        Combatant immune = t;
        immune.immunities |= bit(DamageType::Thunder);
        return apply_damage_parts(immune, parts, qualifiers);
    }
    return apply_damage_parts(t, parts, qualifiers);
}

// Reaction spells the reactor can cast now for this trigger (SpellBook options), before hook options.
// One option per available slot level, ascending (Auto takes option 0 = the lowest slot).
std::vector<ReactionOption> spell_reaction_options(const EncounterState& s, const Combatant& r,
                                                   const ReactionWindow& w, SpellEffectKind kind) {
    const ReactionTrigger trig = w.trigger;
    // BeingHit without an attack roll = targeted by a spell (Magic Missile): only spells that list it.
    const bool targeted_only = trig == ReactionTrigger::BeingHit && w.spell_id != 0 && w.attack_natural == 0;
    const Combatant* trigger_actor = find(s, w.trigger_actor);
    std::vector<ReactionOption> out;
    if (!s.hooks.spells) return out;
    for (uint32_t id : r.spells_known) {
        const SpellDef* d = s.hooks.spells->find(id);
        if (!d || d->casting_time != CastingTime::Reaction || d->reaction_trigger != trig || d->kind != kind) continue;
        if ((d->components & kComponentV) && !s.zones.empty() && in_silence(s, r.id)) continue;   // R-G11 PR 2: Silence
        if (s.hooks.can_provide_components && !s.hooks.can_provide_components(s, r, *d)) continue;
        if (targeted_only && std::find(d->or_targeted_by.begin(), d->or_targeted_by.end(), w.spell_id) == d->or_targeted_by.end()) continue;
        if (d->requires_sight && s.hooks.can_see && trigger_actor && !s.hooks.can_see(s, r, *trigger_actor)) continue;
        for (uint8_t L = std::max<uint8_t>(d->level, 1); L <= 9; ++L) {
            if (!has_slot(r, L)) continue;
            ReactionOption o{0, kind == SpellEffectKind::AcBonus ? ReactionOptionKind::AcBonus : ReactionOptionKind::Counter,
                             kind == SpellEffectKind::AcBonus ? d->ac_bonus : int8_t(0), d->id, L};
            out.push_back(o);   // ascending: option 0 = lowest slot (what Auto takes)
        }
    }
    return out;
}

// Per-call reaction context: decision source, recorded decisions, current chain depth.
struct Ctx {
    std::vector<ReactionRecord>& records;
    const std::vector<ReactionRecord>* replay = nullptr;   // replay_step: answer Ask from here
    std::size_t replay_pos = 0;
    bool replay_only = false;                              // replay_step: running out of decisions = desync
    bool desync = false;
    std::optional<NeedsDecision> need = std::nullopt;      // first unanswered Ask poll (run is then discarded)
    uint8_t depth = 0;                                     // 0 = action, 1+ = inside a reaction
    // R-G14: per-cast options of the spell being resolved (set by cast_spell around resolve_spell only).
    const std::vector<CombatantId>* spared = nullptr;      // Careful / Sculpt Spells: automatic save success
    bool spared_no_damage = false;                         //   Sculpt: no damage on a half-on-save spell
    CombatantId heighten = 0;                              // Heightened: disadvantage on this creature's first save
    int empower = 0;                                       // Empowered: damage dice rerolled (0 = off)
    // R-G9: the cast's forced-move / teleport choices (set by cast_spell around resolve_spell only) and the forced moves
    // queued by effects, turned into placeholders by defer_forced (before SpellResolved, else at the end of the step) and resolved by finalize_forced.
    uint16_t forced_feet = 0, teleport_feet = 0;
    bool has_teleport_dest = false; int32_t teleport_x = 0, teleport_y = 0;
    std::vector<ForcedMoveQuery> forced_queue{};
    // R-G9 deferral: every forced move / teleport of the step, in emission order, waiting for finalize_forced (after all of
    // the step's decision points). flags = Moved.c; group = one effect's batch (forced_move_order sorts within a group).
    struct Deferred { ForcedMoveQuery q; int32_t flags = 0; uint32_t group = 0; };
    std::vector<Deferred> forced_deferred{};
    uint32_t forced_groups = 0;
    bool forced_resolved = false;
    // R-G11: the cast's zone choices (set by cast_spell around resolve_spell only).
    bool has_zone_dest = false; int32_t zone_x = 0, zone_y = 0;
    const std::vector<CombatantId>* zone_excluded = nullptr;
    bool has_zone_end = false; int32_t zone_x2 = 0, zone_y2 = 0; int8_t zone_side = 0; bool zone_ring = false;   // PR 2: walls
    // R-G11 (Q7): forced-move results recorded by an earlier run of this request (PendingDecision::forced_results), reused
    // instead of calling the hook; and every result of this run, stored with the pause when the run is discarded.
    const std::vector<std::pair<uint32_t, ForcedMoveResult>>* forced_recorded = nullptr;
    std::vector<std::pair<uint32_t, ForcedMoveResult>> forced_results{};
    bool zone_fired = false;   // a zone trigger resolved inside finalize_forced (deaths / concentration are swept again)
    uint8_t form_archetype = 0;   // R-G12: the cast's form choice (set by cast_spell around resolve_spell only)
    // R-G12 PR 2: the cast's creature choices (set around resolve_spell only), the creatures built by the Summon effect (placed
    // by attempt after the step: appending to combatants would move the step's Combatant references), and the ids they took.
    const std::vector<uint8_t>* summon_archetypes = nullptr;
    const std::vector<ActionRequest::SummonTile>* summon_tiles = nullptr;   // R-G12 PR 3: the weapon's spawn tile
    std::vector<std::pair<Combatant, uint8_t>> summons_pending{};
    SummonSpec summon_spec{};
    std::vector<CombatantId> assigned_summon_ids{};
    bool ranged_disadvantage = false;   // EC spider follow-up (flagged): the cast's ranged facts (cast_spell around resolve_spell)
};

// R-G11 zone triggers (defined after damage_creature).
void zone_membership(EncounterState& s, Ctx* ctx, CombatantId who, const std::vector<uint32_t>& entered,
                     const std::vector<uint32_t>& left, const std::vector<uint32_t>& within, std::vector<Event>& ev);
void zone_changes(EncounterState& s, Ctx* ctx, const std::vector<ZoneChange>& changes, std::vector<Event>& ev);

// R-G9: ends the grapple between `x` and `y` (either way round) after a forced move left them out of reach: the sourced entries
// go, and Grappled goes from a creature no other grappler still holds (ConditionRemoved).
void end_grapple_pair(EncounterState& s, CombatantId x, CombatantId y, std::vector<Event>& ev) {
    std::vector<CombatantId> freed;
    for (std::size_t i = 0; i < s.sourced.size();) {
        const SourcedCondition& e = s.sourced[i];
        if (e.condition == Condition::Grappled && ((e.source == x && e.target == y) || (e.source == y && e.target == x))) {
            freed.push_back(e.target);
            s.sourced.erase(s.sourced.begin() + std::ptrdiff_t(i));
        } else ++i;
    }
    for (CombatantId id : freed) {
        Combatant* c = find_mut(s, id);
        if (!c || !c->has(Condition::Grappled)) continue;
        const bool still = std::any_of(s.sourced.begin(), s.sourced.end(), [&](const SourcedCondition& e) {
            return e.condition == Condition::Grappled && e.target == id; });
        if (still) continue;
        remove_condition(*c, Condition::Grappled);
        emit(ev, EventKind::ConditionRemoved, id, 0, int32_t(Condition::Grappled));
    }
}

// R-G9: emits a placeholder Moved{actor = mover, target = source, a = requested feet, b = 0, c = flags} for each queued forced
// move / teleport (one group = one effect) and defers it. Ready triggers see these placeholders (the intent). The hook runs
// later, in finalize_forced, once the step can no longer pause. A creature already dead is not moved.
void defer_forced(EncounterState& s, Ctx& ctx, std::vector<Event>& ev, int32_t shove_flags = -1) {
    if (ctx.forced_queue.empty()) return;
    std::vector<ForcedMoveQuery> q = std::move(ctx.forced_queue);
    ctx.forced_queue.clear();
    const uint32_t group = ctx.forced_groups++;
    for (ForcedMoveQuery& m : q) {
        const Combatant* c = find_mut(s, m.mover);
        if (!c || c->dead) continue;
        const int32_t flags = shove_flags >= 0 ? shove_flags : (m.teleport ? kMovedTeleport : kMovedForced);
        m.step_seq = uint32_t(ctx.forced_deferred.size());
        ctx.forced_deferred.push_back(Ctx::Deferred{m, flags, group});
        emit(ev, EventKind::Moved, m.mover, m.source, int32_t(m.feet), 0, flags);
    }
}

// R-G9: resolves the step's deferred forced moves after every decision point of the step (the run can no longer be
// discarded), so `forced_move` sees each (step, step_seq) exactly once. Each group is ordered by `forced_move_order` (a
// permutation, else the given order); each placeholder Moved becomes Moved{a = feet actually moved} followed by
// ConditionRemoved{Grappled} for every grapple the hook reports out of reach (same reaction depth). No hook: a = requested
// feet and no grapple ends (the placeholder stands).
void finalize_forced(EncounterState& s, Ctx& ctx, std::vector<Event>& events) {
    assert(!ctx.forced_resolved && !ctx.need);   // once per step, and only in the run that is kept
    ctx.forced_resolved = true;
    if (ctx.forced_deferred.empty()) return;
    const auto& D = ctx.forced_deferred;
    // placeholders: the rules Moved events with a source (target != 0); voluntary and readied moves have target 0
    std::vector<std::size_t> slot;
    for (std::size_t i = 0; i < events.size(); ++i)
        if (events[i].kind == EventKind::Moved && events[i].target != 0) slot.push_back(i);
    assert(slot.size() == D.size());
    if (slot.size() != D.size()) return;   // release builds: leave the intents as they are
#ifndef NDEBUG
    std::vector<bool> called(D.size(), false);
#endif
    std::vector<std::vector<Event>> replace(D.size());   // per placeholder: what it becomes (empty = dropped; group head holds all)
    std::vector<bool> drop(D.size(), false);
    for (std::size_t k = 0; k < D.size();) {
        std::size_t e = k;
        while (e < D.size() && D[e].group == D[k].group) ++e;
        std::vector<ForcedMoveQuery> qs;
        for (std::size_t i = k; i < e; ++i) qs.push_back(D[i].q);
        std::vector<std::size_t> order(qs.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        if (s.hooks.forced_move_order && qs.size() > 1) {
            std::vector<std::size_t> o = s.hooks.forced_move_order(s, std::span<const ForcedMoveQuery>(qs));
            std::vector<bool> seen(qs.size(), false);
            bool perm = o.size() == qs.size();
            for (std::size_t x = 0; perm && x < o.size(); ++x) {
                if (o[x] >= qs.size() || seen[o[x]]) perm = false;
                else seen[o[x]] = true;
            }
            if (perm) order = std::move(o);
        }
        std::vector<Event>& out = replace[k];
        for (std::size_t oi : order) {
            const std::size_t at = k + oi;
            const Ctx::Deferred& d = D[at];
            const Event& ph = events[slot[at]];
            ForcedMoveResult res;
            Combatant* m = find_mut(s, d.q.mover);
            const ForcedMoveResult* rec = nullptr;   // R-G11 (Q7): an earlier run of this request already asked the hook
            if (ctx.forced_recorded)
                for (const auto& x : *ctx.forced_recorded) if (x.first == d.q.step_seq) rec = &x.second;
            if (rec) {
                res = *rec;
            } else if (m && !m->dead && s.hooks.forced_move) {
#ifndef NDEBUG
                assert(!called[at]);   // each (step, step_seq) reaches the hook once
                called[at] = true;
#endif
                res = s.hooks.forced_move(s, d.q);
            } else {
                res.feet_moved = d.q.feet;
            }
            if (rec || (m && !m->dead && s.hooks.forced_move)) ctx.forced_results.emplace_back(d.q.step_seq, res);
            std::vector<Event> tail;
            emit(tail, EventKind::Moved, d.q.mover, d.q.source, int32_t(res.feet_moved), 0, d.flags);
            for (CombatantId other : res.out_of_reach_of) end_grapple_pair(s, d.q.mover, other, tail);
            const uint8_t saved_depth = ctx.depth;
            if (!s.zones.empty() && m && !m->dead &&   // R-G11: the move's zone triggers, before the next forced move
                (!res.zones_entered.empty() || !res.zones_left.empty() || !res.zones_within.empty())) {
                ctx.zone_fired = true;
                ctx.depth = ph.reaction_depth;
                zone_membership(s, &ctx, d.q.mover, res.zones_entered, res.zones_left, res.zones_within, tail);
                ctx.depth = saved_depth;
            }
            for (Event& t : tail) { if (t.reaction_depth < ph.reaction_depth) t.reaction_depth = ph.reaction_depth; out.push_back(t); }
        }
        for (std::size_t i = k + 1; i < e; ++i) drop[i] = true;
        k = e;
    }
    std::vector<Event> rebuilt;
    rebuilt.reserve(events.size() + 4);
    std::size_t next = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (next < slot.size() && slot[next] == i) {
            if (!drop[next]) rebuilt.insert(rebuilt.end(), replace[next].begin(), replace[next].end());
            ++next;
            continue;
        }
        rebuilt.push_back(events[i]);
    }
    events = std::move(rebuilt);
}

// R-G14 Empowered Spell: rolls each die, then rerolls up to `n` dice that rolled below the die's average, lowest first (earliest
// on ties); the new rolls are kept. Same draws as roll_dice when no die is rerolled.
int roll_empowered(rng::Rng& r, DiceExpr e, bool critical, int n) {
    const int cnt = int(e.count) * (critical ? 2 : 1);
    std::vector<int> v;
    for (int k = 0; k < cnt; ++k) v.push_back(r.d(e.sides));
    std::vector<bool> done(v.size(), false);
    for (int k = 0; k < n; ++k) {
        int pick = -1;
        for (std::size_t i = 0; i < v.size(); ++i)
            if (!done[i] && v[i] * 2 < e.sides + 1 && (pick < 0 || v[i] < v[std::size_t(pick)])) pick = int(i);
        if (pick < 0) break;
        done[std::size_t(pick)] = true;
        v[std::size_t(pick)] = r.d(e.sides);
    }
    int total = e.bonus;
    for (int x : v) total += x;
    return total;
}

// Marks events appended since `from` as produced at reaction depth `d`.
void mark_depth(std::vector<Event>& ev, std::size_t from, uint8_t d) {
    for (std::size_t i = from; i < ev.size(); ++i) if (ev[i].reaction_depth < d) ev[i].reaction_depth = d;
}

// Eligible ids (initiative order) -> poll order via the hook; unknown/duplicate ids are dropped.
std::vector<CombatantId> poll_order(const EncounterState& s, const std::vector<CombatantId>& eligible) {
    if (eligible.empty()) return {};
    auto ordered = s.hooks.poll_order ? s.hooks.poll_order(s, eligible) : default_poll_order(s, eligible);
    std::vector<CombatantId> out;
    for (CombatantId id : ordered)
        if (std::find(eligible.begin(), eligible.end(), id) != eligible.end() &&
            std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    return out;
}

std::vector<CombatantId> initiative_ids(const EncounterState& s) {
    std::vector<CombatantId> ids;
    for (const auto& e : s.order) ids.push_back(e.id);
    return ids;
}

bool may_react(const EncounterState& s, const Combatant& c, ReactionTrigger t) {
    if (c.lethargy != 0 || (!s.modifiers.empty() && (has_marker(s, c.id, ModStat::Slow) || has_marker(s, c.id, ModStat::NoReactions))))
        return false;   // Haste ended / Slow / 7.2u Shocking Grasp
    // R-G12 PR 3 (SRD Find Familiar): a familiar never attacks, so it takes no opportunity attack (Pact's trade is its only one)
    if (t == ReactionTrigger::LeavingReach && c.summoner != 0 && c.summon_control == uint8_t(SummonControl::Telepathic)) return false;
    return !c.dead && !c.surprised && can_act(c) && !c.reaction_used &&
           c.reaction_policy[std::size_t(t)] != ReactionPolicy::Never;
}

// Returns true if a window may open at this depth (no chaining unless the hook allows it).
bool may_open(const EncounterState& s, const ReactionWindow& w) {
    return w.depth == 0 || (s.hooks.allow_chain && s.hooks.allow_chain(s, w));
}

ReactionWindow new_window(EncounterState& s, const Ctx& ctx, ReactionTrigger t, CombatantId actor) {
    ReactionWindow w{};
    w.trigger = t; w.seq = s.next_window_seq; w.trigger_actor = actor; w.depth = ctx.depth;
    return w;
}

// Asks one reactor. Auto: take option 0 (BeingHit: only if `auto_ok`). Ask: the decisions given so
// far (log / async answers / sync adapter); if none is left, the run pauses (ctx.need). Invalid choices decline. Emits Taken/Declined.
ReactionDecision poll(EncounterState&, Ctx& ctx, const ReactionWindow& w, const Combatant& r,
                      std::span<const ReactionOption> opts, std::vector<Event>& ev, bool auto_ok = true) {
    ReactionDecision d{};
    if (r.reaction_policy[std::size_t(w.trigger)] == ReactionPolicy::Auto) {
        d = ReactionDecision{w.seq, auto_ok, 0};
    } else {
        if (ctx.replay && ctx.replay_pos < ctx.replay->size()) {
            const auto& rec = (*ctx.replay)[ctx.replay_pos];
            if (rec.window_seq == w.seq && rec.reactor == r.id && rec.trigger == w.trigger) {
                d = rec.decision; ++ctx.replay_pos;
            } else ctx.desync = true;
        } else if (ctx.replay_only) {
            ctx.desync = true;
        } else if (!ctx.need) {
            // Pause: record what to ask. The rest of this run is discarded by the caller.
            ctx.need = make_needs_decision(w, r.id, std::vector<ReactionOption>(opts.begin(), opts.end()));
        }
        if (d.react && d.option >= opts.size()) d = ReactionDecision{};
        d.window_seq = w.seq;
        ctx.records.push_back(ReactionRecord{w.seq, w.trigger, r.id, d});
    }
    if (d.react && d.option >= opts.size()) d = ReactionDecision{w.seq, false, 0};
    emit(ev, d.react ? EventKind::ReactionTaken : EventKind::ReactionDeclined, r.id, w.trigger_actor,
         int32_t(w.trigger), int32_t(w.seq), d.option);
    return d;
}

bool in_range(const EncounterState& s, const ReactionWindow& w, const Combatant& r) {
    if (s.hooks.in_range) return s.hooks.in_range(s, w, r);
    return (w.trigger == ReactionTrigger::BeingHit || w.trigger == ReactionTrigger::Damaged) && r.id == w.target;
}

bool evaluate_hit(const D20Roll& roll, const Combatant& t, const AttackContext& ctx, bool& crit, int crit_min = 20) {
    crit = false;
    if (roll.natural == 20) { crit = true; return true; }
    if (roll.natural == 1) return false;
    const bool hit = roll.total >= int(t.ac) + int(ctx.cover_ac_bonus) + int(t.reaction_ac_bonus);
    crit = hit && roll.natural >= std::max(2, crit_min);
    return hit;
}

// One attack roll, including Help and hidden-attacker advantage. Attacking reveals the attacker.
// On a hit, a BeingHit window opens for the target before damage (Shield-style AC bump).
// Dice order matches resolve_attack: d20(s), then damage dice.
bool spell_cast_window(EncounterState& s, Ctx& ctx, Combatant& caster, uint32_t spell_id, uint8_t level,
                       CombatantId target, std::vector<Event>& ev);

// BeingHit window for `t` (attack hit, or a spell such as Magic Missile with w.spell_id set).
// Returns true if `t` reacted. SpellBook AcBonus options (Shield) spend a slot and are spells
// themselves (a nested SpellCast window when chaining is allowed); hook options just apply.
bool being_hit_window(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, const ReactionWindow& w,
                      std::vector<Event>& ev, const std::function<bool(const ReactionOption&)>& auto_ok) {
    if (!may_open(s, w) || !may_react(s, t, w.trigger) || !in_range(s, w, t)) return false;
    auto opts = spell_reaction_options(s, t, w, SpellEffectKind::AcBonus);
    if (s.hooks.being_hit_options) {
        auto extra = s.hooks.being_hit_options(s, w, t);
        opts.insert(opts.end(), extra.begin(), extra.end());
    }
    if (opts.empty()) return false;
    ++s.next_window_seq;
    emit(ev, EventKind::ReactionWindowOpened, a.id, t.id, int32_t(w.trigger), int32_t(w.seq));
    auto d = poll(s, ctx, w, t, opts, ev, auto_ok(opts[0]));
    if (!d.react) return false;
    const ReactionOption o = opts[d.option];
    if (o.uses_reaction) t.reaction_used = true;
    if (o.kind != ReactionOptionKind::AcBonus) return true;
    const SpellDef* def = o.slot_level > 0 ? spell_def(s, o.option_id) : nullptr;
    if (def) {
        spend_slot(t, o.slot_level);
        std::size_t from = ev.size();
        ++ctx.depth;
        emit(ev, EventKind::SpellCast, t.id, a.id, int32_t(def->id), o.slot_level);
        bool countered = spell_cast_window(s, ctx, t, def->id, o.slot_level, a.id, ev);
        emit(ev, countered ? EventKind::SpellCountered : EventKind::SpellResolved, t.id, a.id, int32_t(def->id), o.slot_level);
        --ctx.depth;
        mark_depth(ev, from, uint8_t(ctx.depth + 1));
        if (countered) return true;
        t.reaction_blocks_spell = def->blocks_spell;
    }
    t.reaction_ac_bonus = int8_t(t.reaction_ac_bonus + o.ac_bonus);
    emit(ev, EventKind::AcBonus, t.id, 0, o.ac_bonus, t.ac + t.reaction_ac_bonus);
    return true;
}

// ---- Triggers (rows 9a/9b): predicates, limits, effects ----

std::vector<Combatant*> area_save_targets(EncounterState& s, const Combatant& owner, const SpellDef& def);
std::vector<Combatant*> resolve_effect(EncounterState& s, Ctx& ctx, Combatant& a, const SpellDef& def, uint8_t lvl,
                                       const std::vector<Combatant*>& T, bool within_5ft, std::vector<Event>& ev);

struct DepthGuard {   // an area_save / damage_dealt chain cannot recurse without bound
    static int& depth() { thread_local int d = 0; return d; }
    DepthGuard() { ++depth(); }
    ~DepthGuard() { --depth(); }
};

// EC deferred monsters (flagged): a trigger with usage recharge is gated by its ActionUse row (owner, trigger id); no row =
// unlimited. Firing expends it; the owner's turn start rolls it back (start_turn, with the recharge actions).
bool trigger_recharging(const EncounterState& s, const Combatant& owner, const Trigger& tr) {
    if (tr.recharge_min == 0) return false;
    const ActionUse* u = find_action_use(s, owner.id, tr.id);
    return u && u->expended;
}
void expend_trigger(EncounterState& s, const Combatant& owner, const Trigger& tr) {
    if (tr.recharge_min == 0) return;
    for (auto& u : s.action_uses) if (u.actor == owner.id && u.spell_id == tr.id) u.expended = true;
}

// Evaluates one predicate. `failed_save` is set when a Save predicate was rolled and failed.
bool predicate_holds(EncounterState& s, Combatant& owner, const Combatant& subject, const TrigCtx& x, const Predicate& p,
                     std::vector<Event>& ev, bool& failed_save) {
    switch (p.kind) {
    case PredicateKind::Weapon: {
        if (x.is_spell || !x.atk) return false;
        const Attack& atk = *x.atk;
        switch (WeaponIs(p.a)) {
        case WeaponIs::Melee: return !atk.ranged;
        case WeaponIs::Ranged: return atk.ranged;
        case WeaponIs::Finesse: return is_finesse(atk);
        case WeaponIs::FinesseOrRanged: return is_finesse(atk) || atk.ranged;
        }
        return false;
    }
    case PredicateKind::AdvantageOrAllyAdjacent: return x.mode == RollMode::Advantage || (x.ally && x.mode != RollMode::Disadvantage);
    case PredicateKind::TargetCreatureType: return (p.mask & bit(subject.creature_type)) != 0;
    case PredicateKind::NotCreatureType: return (p.mask & bit(subject.creature_type)) == 0;
    case PredicateKind::DamageTypeIs: return (p.mask & x.types) != 0;
    case PredicateKind::DamageTypeNot: return (p.mask & x.types) == 0;
    case PredicateKind::NotCrit: return !x.crit;
    case PredicateKind::MovedAtLeastFt: return owner.id == current_actor(s) && s.budget.moved_ft >= p.a;   // own turn only
    case PredicateKind::SelfHpBelow: return int(owner.hp) * int(p.b) < int(owner.hp_max) * int(p.a);
    case PredicateKind::Save: {   // the owner rolls: a = Ability, b = DC (or the base with mask bit 0 = dc_from_damage)
        const int dc = int(p.b) + ((p.mask & 1u) ? x.dmg : 0);
        const Ability ab = Ability(p.a);
        auto r = save_roll(s, owner, ab, dc, RollMode::Normal, ev);
        emit(ev, EventKind::SavingThrowRolled, owner.id, CombatantId(ab), r.roll.natural, r.roll.total, int32_t(r.success));
        if (!r.success) failed_save = true;
        return r.success;
    }
    case PredicateKind::TargetSurprised: return s.round == 1 && subject.surprised_at_start;   // EC monster predicates (flagged)
    case PredicateKind::WeaponAttack: return !x.is_spell && x.atk != nullptr;
    case PredicateKind::TargetTag: return std::find(subject.tags.begin(), subject.tags.end(), p.mask) != subject.tags.end();
    case PredicateKind::NotTargetTag: return std::find(subject.tags.begin(), subject.tags.end(), p.mask) == subject.tags.end();
    case PredicateKind::AllyNearTarget: return x.ally;
    case PredicateKind::SubjectIsCaster: return x.mod_caster != 0 && subject.id == x.mod_caster;   // 7.2u
    case PredicateKind::OwnerCreatureType: return (p.mask & bit(owner.creature_type)) != 0;
    case PredicateKind::DamageAtMost: return x.dmg <= p.a;
    case PredicateKind::AttackIs: return !x.is_spell && x.atk && x.atk >= owner.attacks.data() && x.atk < owner.attacks.data() + owner.attacks.size() &&
                                         std::size_t(x.atk - owner.attacks.data()) == std::size_t(p.a);   // pointer identity into owner.attacks
    }
    return false;
}

// Modifier `when` scope: every predicate must hold. Rolling predicates (Save) never hold in a modifier.
bool when_ok(EncounterState& s, Combatant& owner, const Combatant& subject, const TrigCtx& x, const std::vector<Predicate>& when) {
    std::vector<Event> sink;
    bool failed = false;
    for (const Predicate& p : when) {
        if (p.kind == PredicateKind::Save) return false;
        if (!predicate_holds(s, owner, subject, x, p, sink, failed)) return false;
    }
    return true;
}

// Can effect `e` of a trigger on `on` do something right now? Reserved combinations never fire.
bool effect_applies(const EncounterState& s, const Combatant& owner, TriggerOn on, const RiderEffect& e) {
    switch (e.kind) {
    case RiderKind::HealSelf: case RiderKind::TempHp:
        return on != TriggerOn::Hit && on != TriggerOn::Crit && !owner.dead;
    case RiderKind::SetHp: return on == TriggerOn::DropToZero;
    case RiderKind::BonusAttack:
        return (on == TriggerOn::ReduceToZero || on == TriggerOn::DamageDealt) && !owner.dead && owner.id == current_actor(s) &&
               s.budget.bonus_action && e.value >= 0 && std::size_t(e.value) < owner.attacks.size();
    case RiderKind::AreaSave: {
        if (on == TriggerOn::Hit || on == TriggerOn::Crit || !s.hooks.area_targets) return false;
        const SpellDef* d = s.hooks.spells ? s.hooks.spells->find(e.spell_id) : nullptr;
        return d && d->kind == SpellEffectKind::Save;
    }
    default: return false;   // ExtraDamage (hit/crit only, in fire_hit_triggers), save_condition/apply_condition/spend (9c)
    }
}

// Fires the non-attack-roll triggers of `owner` for event `on` (damage_dealt, reduce_to_zero, drop_to_zero, death,
// turn_start, turn_end). Emits FeatureTriggered (actor = owner, target = subject, a = trigger id, b = TriggerOn,
// c = 1 applied / 3 failed save) and applies the effects in order. No triggers, no events, no rng.
bool run_event_triggers(EncounterState& s, Combatant& owner, Combatant& subject, TriggerOn on, const TrigCtx& x,
                        std::vector<Event>& ev, TrigOut* out, Ctx* ctx) {
    if (owner.triggers.empty() || DepthGuard::depth() >= 4) return false;
    DepthGuard guard;
    const uint32_t turn_stamp = uint32_t(s.round) << 16 | s.turn_index;
    if (owner.trigger_turn_stamp != turn_stamp) { owner.trigger_turn_stamp = turn_stamp; owner.trigger_turn_mask = 0; }
    if (owner.trigger_round_stamp != s.round) { owner.trigger_round_stamp = s.round; owner.trigger_round_mask = 0; }
    bool fired = false;
    for (std::size_t i = 0; i < owner.triggers.size() && i < 32; ++i) {
        const Trigger& tr = owner.triggers[i];
        if (tr.on != on || tr.choice != 0) continue;
        const uint32_t m = uint32_t(1) << i;
        if (tr.limit == TriggerLimit::OncePerTurn && (owner.trigger_turn_mask & m)) continue;
        if (tr.limit == TriggerLimit::OncePerRound && (owner.trigger_round_mask & m)) continue;
        if (trigger_recharging(s, owner, tr)) continue;   // EC deferred monsters (flagged): usage recharge
        bool any = false;
        for (const RiderEffect& e : tr.effects) if (effect_applies(s, owner, on, e)) any = true;
        if (!any) continue;
        bool ok = true, failed_save = false;
        for (const Predicate& p : tr.when)
            if (!predicate_holds(s, owner, subject, x, p, ev, failed_save)) { ok = false; break; }
        if (!ok) {
            if (failed_save) emit(ev, EventKind::FeatureTriggered, owner.id, subject.id, int32_t(tr.id), int32_t(on), 3);
            continue;
        }
        // EC monster predicates (flagged rules5e edit): a pool Spend gates and pays an event trigger too (per-rest usage, Relentless).
        // No registered pool = unlimited, as for hit triggers; slot spends stay hit-only.
        const RiderEffect* pool_spend = nullptr;
        for (const RiderEffect& e : tr.effects) if (e.kind == RiderKind::Spend && e.slot == 0) { pool_spend = &e; break; }
        const uint16_t pool_cost = pool_spend && pool_spend->value > 0 ? uint16_t(pool_spend->value) : uint16_t(1);
        if (pool_spend && find_pool(s, owner.id, pool_spend->pool) && !can_spend(s, owner.id, pool_spend->pool, pool_cost)) continue;
        if (pool_spend && find_pool(s, owner.id, pool_spend->pool) && spend_pool(s, owner.id, pool_spend->pool, pool_cost))
            emit(ev, EventKind::ResourceSpent, owner.id, 0, int32_t(pool_spend->pool), pool_cost, find_pool(s, owner.id, pool_spend->pool)->cur);
        expend_trigger(s, owner, tr);   // EC deferred monsters (flagged)
        if (tr.limit == TriggerLimit::OncePerTurn) owner.trigger_turn_mask |= m;
        if (tr.limit == TriggerLimit::OncePerRound) owner.trigger_round_mask |= m;
        emit(ev, EventKind::FeatureTriggered, owner.id, subject.id, int32_t(tr.id), int32_t(on), 1);
        fired = true;
        for (const RiderEffect& e : tr.effects) {
            if (!effect_applies(s, owner, on, e)) continue;
            switch (e.kind) {
            case RiderKind::HealSelf: case RiderKind::TempHp: {
                const int amt = (e.dice.count > 0 || e.dice.bonus > 0) ? std::max(0, roll_dice(s.rng, e.dice)) : int(e.value);
                if (amt <= 0) break;
                if (e.kind == RiderKind::TempHp) {   // temporary HP never stack: keep the larger pool
                    if (amt > owner.temp_hp && !owner.no_regain_hp) owner.temp_hp = int16_t(amt);
                } else {
                    auto h = heal_hp(s, owner, int16_t(amt));
                    commit_healing(owner, h);
                    emit(ev, EventKind::Healed, owner.id, owner.id, h.healed, h.hp_after, h.from_zero);
                }
                break;
            }
            case RiderKind::SetHp:
                if (out) { out->set_hp = true; out->hp = e.value; }
                break;
            case RiderKind::BonusAttack:
                s.budget.bonus_attack_grant = uint8_t(e.value + 1);   // spent by Attack with as_bonus_action
                break;
            case RiderKind::AreaSave: {
                const SpellDef* d = s.hooks.spells->find(e.spell_id);
                std::vector<Combatant*> T = area_save_targets(s, owner, *d);
                if (T.empty()) break;
                std::vector<ReactionRecord> recs;   // no caller context (turn_start): reactions do not open (depth 1)
                Ctx local{recs};
                local.depth = 1;
                resolve_effect(s, ctx ? *ctx : local, owner, *d, 0, T, false, ev);
                break;
            }
            default: break;
            }
        }
    }
    return fired;
}

std::vector<Combatant*> area_save_targets(EncounterState& s, const Combatant& owner, const SpellDef& def) {
    std::vector<Combatant*> T;
    for (CombatantId id : s.hooks.area_targets(s, owner, def)) {
        Combatant* c = find_mut(s, id);
        if (c && !c->dead && c->id != owner.id && std::find(T.begin(), T.end(), c) == T.end()) T.push_back(c);
    }
    return T;
}

// Death triggers (Death Burst) of creatures that died during this step, after all the step's events: queued so a
// reaction window cannot nest inside a death. A burst that kills another trigger owner is handled on the next pass.
void process_deaths(EncounterState& s, std::vector<Event>& ev, Ctx* ctx = nullptr) {
    for (std::size_t pass = 0; pass < s.combatants.size(); ++pass) {
        bool any = false;
        for (std::size_t i = 0; i < s.combatants.size(); ++i) {
            Combatant& c = s.combatants[i];
            if (!c.dead || c.death_triggers_done || c.triggers.empty()) continue;
            c.death_triggers_done = true;
            any = true;
            TrigCtx x;
            run_event_triggers(s, c, c, TriggerOn::Death, x, ev, nullptr, ctx);
        }
        if (!any) break;
    }
}

// ---- Reactions of row 11: Damaged and AllyAttacked windows ----

// Life Drain: the target's hit point maximum drops by `taken` (after a Con save vs `dc` when non-zero; success: no
// reduction) until it finishes a long rest; HP over the new maximum is lost; a maximum of 0 kills it. Event: ModifierApplied
// {actor = source, target, a = 0, b = ModStat::MaxHp, c = -taken} (+ CombatantDied).
void life_drain(EncounterState& s, Combatant& t, int taken, int dc, CombatantId source, std::vector<Event>& ev) {
    if (taken <= 0) return;
    if (dc > 0) {
        const SaveSource ssrc{false, 0, 0};
        auto r = save_roll(s, t, Ability::Con, dc, RollMode::Normal, ev, &ssrc);
        emit(ev, EventKind::SavingThrowRolled, t.id, CombatantId(Ability::Con), r.roll.natural, r.roll.total, int32_t(r.success));
        if (r.success) return;
    }
    t.hp_max_reduction = int16_t(std::min(int(t.hp_max), int(t.hp_max_reduction) + taken));
    emit(ev, EventKind::ModifierApplied, source, t.id, 0, int32_t(ModStat::MaxHp), -taken);
    const int cap = effective_hp_max(t);
    if (t.hp > cap) t.hp = int16_t(cap);
    if (cap <= 0) { kill(t, ev); check_end(s, ev); }
}

bool do_attack(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, const Attack& atk,
               bool within_5ft, std::vector<Event>& ev, bool ranged_disadvantage = false,
               uint32_t spell_id = 0, bool ally_near_target = false, const DiceExpr* damage_override = nullptr,
               bool two_handed_grip = false);

// The options `r` has in window `w` from its compiled Combatant::reactions, then the hook options.
std::vector<ReactionOption> reaction_options(const EncounterState& s, const Combatant& r, const ReactionWindow& w) {
    std::vector<ReactionOption> out;
    const Combatant* damager = w.trigger == ReactionTrigger::Damaged ? find(s, w.trigger_actor) : nullptr;
    const bool has_damager = damager && !damager->dead && damager->id != r.id;
    for (const Reaction& rx : r.reactions) {
        if (rx.window != w.trigger) continue;
        if ((rx.melee_only && w.ranged) || (rx.ranged_only && !w.ranged)) continue;
        ReactionOption o{};
        o.option_id = rx.id; o.uses_reaction = true;
        switch (rx.kind) {
        case ReactionEffectKind::ReduceDamage:
            if (w.trigger != ReactionTrigger::Damaged) continue;
            o.kind = ReactionOptionKind::ReduceDamage; o.half = rx.half; o.dice = rx.dice; break;
        case ReactionEffectKind::ImposeDisadvantage:
            if (w.trigger != ReactionTrigger::AllyAttacked) continue;
            o.kind = ReactionOptionKind::ImposeDisadvantage; break;
        case ReactionEffectKind::Retaliate:
            if (w.trigger != ReactionTrigger::Damaged || !has_damager || !spell_def(s, rx.spell_id)) continue;
            o.kind = ReactionOptionKind::Retaliate; o.option_id = rx.spell_id; break;
        case ReactionEffectKind::Attack:
            if (w.trigger != ReactionTrigger::Damaged || !has_damager || rx.attack_index >= r.attacks.size()) continue;
            o.kind = ReactionOptionKind::Attack; o.attack_index = rx.attack_index; break;
        case ReactionEffectKind::SubtractRoll:
            if (w.trigger != ReactionTrigger::AttackRolled) continue;
            if (rx.pool != 0 && !can_spend(s, r.id, rx.pool, rx.pool_cost ? rx.pool_cost : uint16_t(1))) continue;
            o.kind = ReactionOptionKind::SubtractRoll; o.dice = rx.dice; break;
        }
        out.push_back(o);
    }
    if (w.trigger != ReactionTrigger::Damaged && w.trigger != ReactionTrigger::AllyAttacked) return out;
    const auto& hook = w.trigger == ReactionTrigger::Damaged ? s.hooks.damaged_options : s.hooks.ally_attacked_options;
    if (hook) { auto extra = hook(s, w, r); out.insert(out.end(), extra.begin(), extra.end()); }
    return out;
}

// AllyAttacked: before the attack roll, each creature on the target's team (not the target) that can react and is
// "in range" (hook: Engine Core supplies adjacency; no hook = nobody) may impose disadvantage. Returns true if the
// attack is made with disadvantage.
bool ally_attacked_window(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, bool ranged, uint32_t spell_id,
                          std::vector<Event>& ev) {
    ReactionWindow w = new_window(s, ctx, ReactionTrigger::AllyAttacked, a.id);
    w.target = t.id; w.ranged = ranged; w.spell_id = spell_id;
    if (!may_open(s, w)) return false;
    auto eligible = [&](const Combatant& r) {
        return r.id != a.id && r.id != t.id && r.team == t.team && may_react(s, r, w.trigger) && s.hooks.in_range &&
               in_range(s, w, r) && !reaction_options(s, r, w).empty();
    };
    bool any = false;
    for (const auto& c : s.combatants) { if (eligible(c)) { any = true; break; } }
    if (!any) return false;
    std::vector<CombatantId> cands;
    for (CombatantId id : initiative_ids(s)) if (eligible(*find_mut(s, id))) cands.push_back(id);
    bool opened = false;
    for (CombatantId rid : poll_order(s, cands)) {
        Combatant& r = *find_mut(s, rid);
        if (!eligible(r)) continue;
        auto opts = reaction_options(s, r, w);
        if (!opened) {
            opened = true; ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, a.id, t.id, int32_t(w.trigger), int32_t(w.seq));
        }
        auto d = poll(s, ctx, w, r, opts, ev);
        if (!d.react) continue;
        const ReactionOption o = opts[d.option];
        if (o.uses_reaction) r.reaction_used = true;
        if (o.kind == ReactionOptionKind::ImposeDisadvantage) return true;   // disadvantage does not stack
    }
    return false;
}

// AttackRolled (R-G7): after an attack roll (not a natural 1 or 20) and before hit or miss, each enemy of the attacker that
// can react and is in range (hook: Engine Core; no hook = nobody) may subtract a die (Cutting Words). The first taker
// ends the window. Auto takes it only when the largest roll of the die could turn a hit into a miss. Returns the amount.
int attack_rolled_window(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, int total, int target_ac, bool ranged,
                         uint32_t spell_id, std::vector<Event>& ev) {
    ReactionWindow w = new_window(s, ctx, ReactionTrigger::AttackRolled, a.id);
    w.target = t.id; w.ranged = ranged; w.spell_id = spell_id;
    w.attack_total = int16_t(total); w.target_ac = int16_t(target_ac);
    if (!may_open(s, w) || !s.hooks.in_range) return 0;
    auto eligible = [&](const Combatant& r) {
        return r.id != a.id && r.team != a.team && may_react(s, r, w.trigger) && in_range(s, w, r) && !reaction_options(s, r, w).empty();
    };
    bool any = false;
    for (const auto& c : s.combatants) { if (eligible(c)) { any = true; break; } }
    if (!any) return 0;
    std::vector<CombatantId> cands;
    for (CombatantId id : initiative_ids(s)) if (eligible(*find_mut(s, id))) cands.push_back(id);
    bool opened = false;
    for (CombatantId rid : poll_order(s, cands)) {
        Combatant& r = *find_mut(s, rid);
        if (!eligible(r)) continue;
        auto opts = reaction_options(s, r, w);
        if (!opened) {
            opened = true; ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, a.id, t.id, int32_t(w.trigger), int32_t(w.seq));
        }
        const DiceExpr& d0 = opts[0].dice;
        const bool could_flip = total >= target_ac && total - (int(d0.count) * int(d0.sides) + int(d0.bonus)) < target_ac;
        auto d = poll(s, ctx, w, r, opts, ev, could_flip);
        if (!d.react) continue;
        const ReactionOption o = opts[d.option];
        if (o.uses_reaction) r.reaction_used = true;
        if (o.kind != ReactionOptionKind::SubtractRoll) return 0;
        for (const Reaction& rx : r.reactions) {
            if (rx.id != o.option_id || rx.kind != ReactionEffectKind::SubtractRoll || rx.pool == 0) continue;
            const uint16_t cost = rx.pool_cost ? rx.pool_cost : uint16_t(1);
            if (spend_pool(s, r.id, rx.pool, cost))
                emit(ev, EventKind::ResourceSpent, r.id, 0, int32_t(rx.pool), cost, find_pool(s, r.id, rx.pool)->cur);
            break;
        }
        return std::max(0, roll_dice(s.rng, o.dice));
    }
    return 0;
}

// Damage plus the Damaged window, triggers and the concentration check (SRD: CON save, DC max(10, half the damage)).
// Order: Damaged window (reduce_damage) -> drop_to_zero triggers -> commit and DamageDealt -> damage_dealt /
// reduce_to_zero triggers -> retaliate / riposte reactions -> concentration -> repeat saves.
void damage_creature(EncounterState& s, Combatant& t, const DamageApplied& d, bool critical, std::vector<Event>& ev,
                     const TrigSrc* src, Ctx* ctx) {
    DamageApplied applied = d;
    Combatant* attacker = src && src->who && src->who != &t && !src->who->dead ? src->who : nullptr;
    const bool was_up = t.hp > 0;
    std::optional<ReactionOption> after_reaction;   // retaliate / riposte: after the damage lands
    if (ctx && d.dealt > 0 && !t.dead && (!t.reactions.empty() || s.hooks.damaged_options)) {
        ReactionWindow w = new_window(s, *ctx, ReactionTrigger::Damaged, attacker ? attacker->id : 0);
        w.target = t.id; w.damage = d.dealt; w.ranged = src && src->atk && src->atk->ranged;
        w.spell_id = src && src->is_spell ? 1u : 0u;
        if (may_open(s, w) && may_react(s, t, w.trigger) && in_range(s, w, t)) {
            auto opts = reaction_options(s, t, w);
            if (!opts.empty()) {
                ++s.next_window_seq;
                emit(ev, EventKind::ReactionWindowOpened, w.trigger_actor, t.id, int32_t(w.trigger), int32_t(w.seq));
                const auto dec = poll(s, *ctx, w, t, opts, ev);
                if (dec.react) {
                    const ReactionOption o = opts[dec.option];
                    if (o.uses_reaction) t.reaction_used = true;
                    if (o.kind == ReactionOptionKind::ReduceDamage) {
                        const int red = o.half ? d.dealt / 2 : std::max(0, roll_dice(s.rng, o.dice));
                        applied = reduce_applied(t, d, red);
                    } else if (o.kind == ReactionOptionKind::Retaliate || o.kind == ReactionOptionKind::Attack) {
                        after_reaction = o;
                    }
                }
            }
        }
    }
    if (!t.triggers.empty() && applied.dropped_to_zero && !applied.instant_death) {   // Undead Fortitude: before the drop applies
        TrigCtx x; x.atk = src ? src->atk : nullptr; x.is_spell = src && src->is_spell; x.crit = critical;
        x.types = applied.types_dealt; x.dmg = applied.dealt;
        TrigOut o;
        run_event_triggers(s, t, attacker ? *attacker : t, TriggerOn::DropToZero, x, ev, &o, ctx);
        if (o.set_hp) {
            applied.hp_after = std::max<int16_t>(1, std::min<int16_t>(o.hp, t.hp_max));
            applied.dropped_to_zero = false;
        }
    }
    if (applied.dealt > 0 && !s.effects.empty()) {   // R-G15: Sleep-style effects end when the creature takes damage
        for (std::size_t i = 0; i < s.effects.size();) {
            if (s.effects[i].ends_on_damage && s.effects[i].target == t.id) end_effect(s, i, ev); else ++i;
        }
    }
    after_damage(t, applied, critical, ev);
    if (applied.dealt > 0 && !s.modifiers.empty()) consume_modifiers(s, 3, t.id, 4, ev);   // "until damaged"
    if (attacker && !attacker->triggers.empty() && applied.dealt > 0) {
        TrigCtx x; x.atk = src->atk; x.is_spell = src->is_spell; x.mode = src->mode; x.ally = src->ally; x.crit = critical;
        x.types = applied.types_dealt; x.dmg = applied.dealt;
        run_event_triggers(s, *attacker, t, TriggerOn::DamageDealt, x, ev, nullptr, ctx);
        if (was_up && (t.dead || t.hp <= 0) && !attacker->dead)
            run_event_triggers(s, *attacker, t, TriggerOn::ReduceToZero, x, ev, nullptr, ctx);
    }
    if (after_reaction && attacker && !attacker->dead && !t.dead && t.hp > 0) {   // the reactor must still stand
        const ReactionOption& o = *after_reaction;
        const std::size_t from = ev.size();
        ++ctx->depth;
        if (o.kind == ReactionOptionKind::Retaliate) {
            if (const SpellDef* def = spell_def(s, o.option_id))
                resolve_effect(s, *ctx, t, *def, 0, std::vector<Combatant*>{attacker}, true, ev);
        } else if (o.attack_index < t.attacks.size()) {
            do_attack(s, *ctx, t, *attacker, t.attacks[o.attack_index], true, ev);
        }
        --ctx->depth;
        mark_depth(ev, from, uint8_t(ctx->depth + 1));
    }
    if (applied.dealt <= 0) return;
    if (t.concentration_spell != 0) {
        if (t.dead || t.hp <= 0) end_concentration(s, t.id, ev);
        else {
            const int dc = std::max(10, applied.dealt / 2);
            auto r = save_roll(s, t, Ability::Con, dc, RollMode::Normal, ev);
            emit(ev, EventKind::SavingThrowRolled, t.id, CombatantId(Ability::Con), r.roll.natural, r.roll.total,
                 int32_t(r.success) | 2);
            if (!r.success) end_concentration(s, t.id, ev);
        }
    }
    repeat_saves(s, t.id, RepeatSave::OnDamage, ev);
}

// What the Hit / Crit triggers of one attack contribute: damage dice rolled with the attack, and synthetic
// save_condition / apply_condition effects applied after the damage (spell ids resolved with resolve_effect).
struct HitRiderPiece { DiceExpr dice; DamageType type; bool same_as_weapon; };
struct HitRiders { std::vector<HitRiderPiece> pieces; std::vector<uint32_t> after; };

bool hit_effect_ok(const EncounterState& s, const RiderEffect& e) {
    switch (e.kind) {
    case RiderKind::ExtraDamage: return true;
    case RiderKind::SaveCondition: case RiderKind::ApplyCondition: {
        const SpellDef* d = spell_def(s, e.spell_id);
        return d && d->kind == (e.kind == RiderKind::SaveCondition ? SpellEffectKind::Save : SpellEffectKind::ApplyCondition);
    }
    default: return false;
    }
}

// Slot levels at which the Spend effect of `tr` can be paid now (empty: it cannot). A trigger without a slot spend
// yields {0} (one option, no slot). Pool spends are checked here; the pool is charged when the trigger applies.
std::vector<uint8_t> spend_levels(const EncounterState& s, const Combatant& a, const Trigger& tr) {
    const RiderEffect* sp = nullptr;
    for (const RiderEffect& e : tr.effects) if (e.kind == RiderKind::Spend) { sp = &e; break; }
    if (!sp) return {0};
    if (sp->slot == 0) {   // pool: no registered row = unlimited (like pool-limited actions)
        const uint16_t cost = sp->value > 0 ? uint16_t(sp->value) : uint16_t(1);
        if (find_pool(s, a.id, sp->pool) && !can_spend(s, a.id, sp->pool, cost)) return {};
        return {0};
    }
    std::vector<uint8_t> out;
    const uint8_t lo = sp->slot == 10 ? uint8_t(1) : sp->slot, hi = sp->slot == 10 ? uint8_t(9) : sp->slot;
    for (uint8_t L = lo; L <= hi; ++L) if (has_slot(a, L)) out.push_back(L);
    return out;
}

// Fires every Hit / Crit trigger of `a` whose predicates hold, whose limit allows it and whose cost can be paid.
// choice auto: applies at once (a slot spend takes the lowest affordable level). choice ask: opens a HitRider window
// with the attacker as reactor (free options: one per affordable slot level, ascending; option 0 is what Auto takes);
// declining emits FeatureTriggered c = 2 and costs nothing. Applying emits FeatureTriggered (actor = owner,
// target = the creature hit, a = trigger id, b = TriggerOn, c = 1) in trigger order. No triggers: no events, no rng.
HitRiders fire_hit_triggers(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, const Attack& atk,
                            bool is_spell, RollMode mode, bool ally_near, bool crit, const D20Roll& roll,
                            std::vector<Event>& ev) {
    HitRiders out;
    if (a.triggers.empty()) return out;
    TrigCtx x; x.atk = &atk; x.is_spell = is_spell; x.mode = mode; x.ally = ally_near; x.crit = crit; x.types = bit(atk.type);
    const uint32_t turn_stamp = uint32_t(s.round) << 16 | s.turn_index;
    if (a.trigger_turn_stamp != turn_stamp) { a.trigger_turn_stamp = turn_stamp; a.trigger_turn_mask = 0; }
    if (a.trigger_round_stamp != s.round) { a.trigger_round_stamp = s.round; a.trigger_round_mask = 0; }
    for (std::size_t i = 0; i < a.triggers.size() && i < 32; ++i) {
        const Trigger& tr = a.triggers[i];
        if (tr.choice == 2) continue;                                  // never
        if (!(tr.on == TriggerOn::Hit || (tr.on == TriggerOn::Crit && crit))) continue;
        const uint32_t m = uint32_t(1) << i;
        if (tr.limit == TriggerLimit::OncePerTurn && (a.trigger_turn_mask & m)) continue;
        if (tr.limit == TriggerLimit::OncePerRound && (a.trigger_round_mask & m)) continue;
        if (trigger_recharging(s, a, tr)) continue;   // EC deferred monsters (flagged): usage recharge
        bool any = false;
        for (const RiderEffect& e : tr.effects) if (hit_effect_ok(s, e)) any = true;
        if (!any) continue;                                            // no hit effect: other kinds do not fire here
        bool ok = true, failed_save = false;
        for (const Predicate& p : tr.when)
            if (!predicate_holds(s, a, t, x, p, ev, failed_save)) { ok = false; break; }
        if (!ok) continue;
        const std::vector<uint8_t> levels = spend_levels(s, a, tr);
        if (levels.empty()) continue;                                  // cannot pay
        uint8_t level = levels[0];
        if (tr.choice == 1) {                                          // ask: HitRider window, the attacker decides
            if (a.dead || a.reaction_policy[std::size_t(ReactionTrigger::HitRider)] == ReactionPolicy::Never) continue;
            std::vector<ReactionOption> opts;
            for (uint8_t L : levels) opts.push_back(ReactionOption{uint8_t(i), ReactionOptionKind::Rider, 0, tr.id, L, false});
            ReactionWindow w = new_window(s, ctx, ReactionTrigger::HitRider, a.id);
            w.target = t.id; w.attack_total = roll.total; w.attack_natural = roll.natural;
            ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, a.id, t.id, int32_t(w.trigger), int32_t(w.seq));
            const ReactionDecision d = poll(s, ctx, w, a, opts, ev, true);
            if (!d.react) {
                emit(ev, EventKind::FeatureTriggered, a.id, t.id, int32_t(tr.id), int32_t(tr.on), 2);
                continue;
            }
            level = opts[d.option].slot_level;
        }
        for (const RiderEffect& e : tr.effects) {                      // pay
            if (e.kind != RiderKind::Spend) continue;
            if (e.slot != 0) spend_slot(a, level);
            else {
                const uint16_t cost = e.value > 0 ? uint16_t(e.value) : uint16_t(1);
                if (find_pool(s, a.id, e.pool) && spend_pool(s, a.id, e.pool, cost))
                    emit(ev, EventKind::ResourceSpent, a.id, 0, int32_t(e.pool), cost, find_pool(s, a.id, e.pool)->cur);
            }
            break;
        }
        expend_trigger(s, a, tr);   // EC deferred monsters (flagged)
        if (tr.limit == TriggerLimit::OncePerTurn) a.trigger_turn_mask |= m;
        if (tr.limit == TriggerLimit::OncePerRound) a.trigger_round_mask |= m;
        emit(ev, EventKind::FeatureTriggered, a.id, t.id, int32_t(tr.id), int32_t(tr.on), 1);
        for (const RiderEffect& e : tr.effects) {
            if (!hit_effect_ok(s, e)) continue;
            if (e.kind == RiderKind::ExtraDamage) {
                out.pieces.push_back({e.dice, e.type, e.same_as_weapon});
                if (e.slot_dice.count > 0 && level > e.slot_base)      // scale.per_slot_level: +add_dice per level above base
                    out.pieces.push_back({DiceExpr{uint8_t(e.slot_dice.count * (level - e.slot_base)), e.slot_dice.sides, 0},
                                          e.type, e.same_as_weapon});
            } else out.after.push_back(e.spell_id);
        }
    }
    return out;
}

bool do_attack(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, const Attack& atk,
               bool within_5ft, std::vector<Event>& ev, bool ranged_disadvantage,
               uint32_t spell_id, bool ally_near_target, const DiceExpr* damage_override, bool two_handed_grip) {
    bool adv = false;
    if (a.helped_vs == t.id) { adv = true; a.helped_vs = 0; a.helped_by = 0; }
    if (t.reckless_active) adv = true;   // Reckless Attack: attacks against it have advantage
    const bool gaze_dis = a.gaze_averted && !t.gazes.empty();   // Petrifying Gaze: eyes averted, the gazer is unseen
    if (t.gaze_averted && !a.gazes.empty()) adv = true;         // ... and the gazer is an unseen attacker to it
    if (a.reckless_active && !atk.ranged && spell_id == 0 &&
        !(atk.finesse && modifier(a.abilities, Ability::Dex) > modifier(a.abilities, Ability::Str))) adv = true;   // Str melee
    const int drain_before = int(t.hp) + int(t.temp_hp);   // Life Drain: damage taken by this hit
    if (a.stealth_total > 0) { adv = true; a.stealth_total = 0; }   // unseen attacker; now revealed
    AttackContext actx;
    actx.target_within_5ft = within_5ft;
    actx.ally_near_target = ally_near_target;
    actx.ranged = atk.ranged;
    actx.mode = adv ? RollMode::Advantage : RollMode::Normal;
    const bool protected_by_ally = ally_attacked_window(s, ctx, a, t, atk.ranged, spell_id, ev);   // Protection-style reactions
    actx.extra_disadvantage = (ranged_disadvantage && atk.ranged) || weapon_disadvantage(a, atk, within_5ft) || protected_by_ally || gaze_dis;   // combined with conditions in one place
    if (!s.modifiers.empty()) {   // timed modifiers (row 12): sources merged into the one advantage/disadvantage resolution
        for (auto& m : s.modifiers) {   // `when` scope of roll-time modifiers (follow-up): gate the ones that do not hold
            m.gated = false;
            if (m.spec.when.empty()) continue;
            Combatant* owner = nullptr; const Combatant* subject = nullptr;
            if (m.spec.stat == ModStat::AttackRoll && m.target == a.id) { owner = &a; subject = &t; }
            else if ((m.spec.stat == ModStat::AttacksAgainst || m.spec.stat == ModStat::Ac) && m.target == t.id) { owner = &t; subject = &a; }
            if (!owner) continue;
            TrigCtx x; x.atk = &atk; x.is_spell = spell_id != 0; x.mode = actx.mode; x.ally = ally_near_target;
            x.mod_caster = m.caster;
            m.gated = !when_ok(s, *owner, *subject, x, m.spec.when);
        }
        bool madv = false, mdis = false;
        mod_modes(s, a.id, ModStat::AttackRoll, -1, madv, mdis);
        mod_modes(s, t.id, ModStat::AttacksAgainst, -1, madv, mdis);
        if (madv) actx.mode = RollMode::Advantage;
        if (mdis) actx.extra_disadvantage = true;
        actx.cover_ac_bonus = int8_t(std::clamp(int(actx.cover_ac_bonus) + modified_ac_bonus(s, t.id), -128, 127));
    }
    const RollMode atk_mode = attack_roll_mode(a, t, actx);
    D20Roll roll = roll_d20(s.rng, int(atk.to_hit) + modifier_flat(s, a.id, ModStat::AttackRoll) +
                                (spell_id != 0 ? 0 : atk.ranged ? int(a.attack_bonus_ranged) : int(a.attack_bonus_melee)), atk_mode);
    if (a.reroll_ones & 1) reroll_natural_one(s.rng, roll);   // Lucky
    if (!s.modifiers.empty()) {
        roll.total = int16_t(roll.total + mod_roll(s, a.id, ModStat::AttackRoll, -1));
        consume_modifiers(s, 1, a.id, 3, ev);   // True Strike: spent by this attack roll
        consume_modifiers(s, 2, t.id, 3, ev);   // Guiding Bolt: spent by this attack roll against the target
        for (auto& m : s.modifiers) m.gated = false;
    }
    bool crit = false;
    if (roll.natural != 20 && roll.natural != 1) {   // Cutting Words-style subtraction, before hit or miss
        const int tac = int(t.ac) + int(actx.cover_ac_bonus) + int(t.reaction_ac_bonus);
        if (const int sub = attack_rolled_window(s, ctx, a, t, roll.total, tac, atk.ranged, spell_id, ev)) roll.total = int16_t(roll.total - sub);
    }
    const int crit_min = spell_id != 0 ? 20 : int(a.crit_min);   // Improved Critical: weapon attacks only
    bool hit = evaluate_hit(roll, t, actx, crit, crit_min);
    if (hit) {
        ReactionWindow w = new_window(s, ctx, ReactionTrigger::BeingHit, a.id);
        w.target = t.id; w.attack_total = roll.total; w.attack_natural = roll.natural;
        w.target_ac = int16_t(int(t.ac) + int(actx.cover_ac_bonus) + int(t.reaction_ac_bonus));
        w.spell_id = spell_id;
        // Auto only spends the reaction when option 0 would turn the hit into a miss.
        auto would_miss = [&](const ReactionOption& o) {
            return o.kind == ReactionOptionKind::AcBonus && roll.natural != 20 &&
                   roll.total < int(w.target_ac) + o.ac_bonus;
        };
        if (being_hit_window(s, ctx, a, t, w, ev, would_miss)) hit = evaluate_hit(roll, t, actx, crit, crit_min);
    }
    if (hit && within_5ft && (t.has(Condition::Paralyzed) || t.has(Condition::Unconscious))) crit = true;
    emit(ev, EventKind::AttackResolved, a.id, t.id, roll.natural, roll.total,
         int32_t(hit) | (int32_t(crit) << 1));
    const TrigSrc tsrc{&a, &atk, spell_id != 0, atk_mode, ally_near_target};
    if (hit) {
        // Triggers that fire on this hit (Sneak Attack-style): their FeatureTriggered events come before the damage
        // dice; extra_damage riders are rolled with the attack's damage so a crit doubles them.
        const HitRiders riders = fire_hit_triggers(s, ctx, a, t, atk, spell_id != 0, atk_mode, ally_near_target, crit, roll, ev);
        // EC swarm: the half-HP damage variant (only when authored)
        // Uses base max HP, not exhaustion-reduced max (Director ruling).
        const DiceExpr& wdice = damage_override ? *damage_override
                                : (atk.half_hp_damage.count > 0 && 2 * int(a.hp) <= int(a.hp_max)) ? atk.half_hp_damage : atk.damage;
        const bool gwf = a.great_weapon_fighting && spell_id == 0 && !atk.ranged &&
                         (has_property(atk, kWeaponTwoHanded) || (two_handed_grip && has_property(atk, kWeaponVersatile)));
        int dmg = gwf ? roll_dice_reroll_low(s.rng, wdice, crit, 2)
                      : (spell_id != 0 && ctx.empower > 0 ? roll_empowered(s.rng, wdice, crit, ctx.empower)
                                                          : roll_dice(s.rng, wdice, crit));
        if (dmg < 0) dmg = 0;
        std::vector<std::pair<int, DamageType>> modp;   // Damage modifiers (Rage, Hunter's Mark): weapon attacks only
        if (spell_id == 0 && !s.modifiers.empty()) {
            for (const auto& m : s.modifiers) {
                if (m.target != a.id || m.spec.stat != ModStat::Damage) continue;
                if ((m.spec.scope == 1 && atk.ranged) || (m.spec.scope == 2 && !atk.ranged)) continue;
                if (!m.spec.when.empty()) {
                    TrigCtx x; x.atk = &atk; x.is_spell = false; x.mode = atk_mode; x.ally = ally_near_target; x.crit = crit;
                    x.types = bit(m.spec.has_damage_type ? m.spec.damage_type : atk.type);
                    if (!when_ok(s, a, t, x, m.spec.when)) continue;
                }
                int amt = m.spec.bonus;
                if (m.spec.die.count > 0) amt += m.spec.die_mult * roll_dice(s.rng, m.spec.die, crit);
                modp.push_back({std::max(0, amt), m.spec.has_damage_type ? m.spec.damage_type : atk.type});
            }
        }
        if (!riders.pieces.empty() || !modp.empty()) {
            std::vector<DamagePiece> parts{{int16_t(dmg), atk.type}};
            for (const DamagePart& p : atk.extra_damage)
                parts.push_back({int16_t(std::max(0, roll_dice(s.rng, p.dice, crit))), p.type});
            for (const HitRiderPiece& r : riders.pieces)
                parts.push_back({int16_t(std::max(0, roll_dice(s.rng, r.dice, crit))), r.same_as_weapon ? atk.type : r.type});
            for (const auto& mp : modp) parts.push_back({int16_t(mp.first), mp.second});
            damage_creature(s, t, zone_apply_damage_parts(s, t, parts, atk.qualifiers), crit, ev, &tsrc, &ctx);
        } else if (atk.extra_damage.empty()) {
            damage_creature(s, t, zone_apply_damage(s, t, int16_t(dmg), atk.type, atk.qualifiers), crit, ev, &tsrc, &ctx);
        } else {   // multi-component hit: one DamageDealt carrying the per-type-modified sum
            std::vector<DamagePiece> parts{{int16_t(dmg), atk.type}};
            for (const DamagePart& p : atk.extra_damage)
                parts.push_back({int16_t(std::max(0, roll_dice(s.rng, p.dice, crit))), p.type});
            damage_creature(s, t, zone_apply_damage_parts(s, t, parts, atk.qualifiers), crit, ev, &tsrc, &ctx);
        }
        if (atk.drain_max_hp && !t.dead) life_drain(s, t, drain_before - (int(t.hp) + int(t.temp_hp)), atk.drain_save_dc, a.id, ev);
        for (uint32_t sid : riders.after) {   // save_condition / apply_condition riders: after the damage, if it still stands
            if (t.dead) break;
            if (const SpellDef* d = spell_def(s, sid)) resolve_effect(s, ctx, a, *d, 0, std::vector<Combatant*>{&t}, within_5ft, ev);
        }
    }
    return hit;
}

// LeavingReach: polls hostile reactors before the mover leaves reach. Returns false if the move
// is stopped (mover dead, at 0 HP, or speed 0).
bool leaving_reach_window(EncounterState& s, Ctx& ctx, Combatant& mover, std::vector<Event>& ev) {
    ReactionWindow w = new_window(s, ctx, ReactionTrigger::LeavingReach, mover.id);
    if (!may_open(s, w) || mover.disengaged) return true;
    auto options_for = [](const Combatant& r) {
        std::vector<ReactionOption> opts;
        for (uint8_t i = 0; i < r.attacks.size(); ++i)
            if (!r.attacks[i].ranged) opts.push_back({i, ReactionOptionKind::Attack});
        return opts;
    };
    auto eligible = [&](const Combatant& r) {
        return r.id != mover.id && r.team != mover.team && may_react(s, r, w.trigger) && in_range(s, w, r) &&
               !options_for(r).empty();
    };
    std::vector<CombatantId> cands;
    for (CombatantId id : initiative_ids(s)) if (eligible(*find_mut(s, id))) cands.push_back(id);
    bool opened = false;
    for (CombatantId rid : poll_order(s, cands)) {
        Combatant& r = *find_mut(s, rid);
        if (!eligible(r)) continue;   // re-check: earlier reactions can change eligibility
        auto opts = options_for(r);
        if (!opened) {
            opened = true; ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, mover.id, 0, int32_t(w.trigger), int32_t(w.seq));
        }
        auto d = poll(s, ctx, w, r, opts, ev);
        if (!d.react) continue;
        if (opts[d.option].uses_reaction) r.reaction_used = true;
        std::size_t from = ev.size();
        ++ctx.depth;
        do_attack(s, ctx, r, mover, r.attacks[opts[d.option].attack_index], true, ev);   // reach: within 5 ft
        --ctx.depth;
        mark_depth(ev, from, uint8_t(ctx.depth + 1));
        if (mover.dead || mover.hp <= 0 || effective_speed(mover) == 0) return false;
    }
    return true;
}

// SpellCast: polls hostile counters. Returns true if the spell was countered. A counter is itself a spell,
// so it opens a nested SpellCast window when allow_chain permits (counter the counter).
// SpellBook counters (Counterspell) spend the chosen slot; they succeed automatically if the slot
// level >= the spell's level, otherwise with a spellcasting ability check, DC counter_dc_base + level.
// Hook counters (option_id not in the SpellBook) are opaque, cast at level 3, and always succeed.
bool spell_cast_window(EncounterState& s, Ctx& ctx, Combatant& caster, uint32_t spell_id, uint8_t level,
                       CombatantId target, std::vector<Event>& ev) {
    ReactionWindow w = new_window(s, ctx, ReactionTrigger::SpellCast, caster.id);
    w.spell_id = spell_id; w.spell_level = level; w.target = target;
    if (!may_open(s, w)) return false;
    auto options_for = [&](const Combatant& r) {
        auto opts = spell_reaction_options(s, r, w, SpellEffectKind::Counter);
        if (s.hooks.spell_cast_options) {
            auto extra = s.hooks.spell_cast_options(s, w, r);
            opts.insert(opts.end(), extra.begin(), extra.end());
        }
        return opts;
    };
    auto eligible = [&](const Combatant& r) {
        return r.id != caster.id && r.team != caster.team && may_react(s, r, w.trigger) && in_range(s, w, r) &&
               !options_for(r).empty();
    };
    std::vector<CombatantId> cands;
    for (CombatantId id : initiative_ids(s)) if (eligible(*find_mut(s, id))) cands.push_back(id);
    bool opened = false;
    for (CombatantId rid : poll_order(s, cands)) {
        Combatant& r = *find_mut(s, rid);
        if (!eligible(r)) continue;
        auto opts = options_for(r);
        if (!opened) {
            opened = true; ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, caster.id, 0, int32_t(w.trigger), int32_t(w.seq));
        }
        auto d = poll(s, ctx, w, r, opts, ev);
        if (!d.react) continue;
        const ReactionOption o = opts[d.option];
        if (o.uses_reaction) r.reaction_used = true;
        const SpellDef* def = o.slot_level > 0 ? spell_def(s, o.option_id) : nullptr;
        const uint8_t clvl = def ? o.slot_level : 3;
        if (def) spend_slot(r, clvl);
        std::size_t from = ev.size();
        ++ctx.depth;
        emit(ev, EventKind::SpellCast, r.id, caster.id, int32_t(o.option_id), clvl);
        bool counter_countered = spell_cast_window(s, ctx, r, o.option_id, clvl, caster.id, ev);
        bool success = !counter_countered;
        if (success && def && level > clvl) {
            auto roll = roll_d20(s.rng, modifier(r.abilities, r.spellcasting_ability), check_roll_mode(r));
            success = roll.total >= int(def->counter_dc_base) + int(level);
            emit(ev, EventKind::SpellCheckRolled, r.id, caster.id, roll.natural, roll.total, int32_t(success));
        }
        --ctx.depth;
        mark_depth(ev, from, uint8_t(ctx.depth + 1));
        if (counter_countered) {
            emit(ev, EventKind::SpellCountered, r.id, 0, int32_t(o.option_id), clvl);
            continue;   // the counter failed; later reactors may still try
        }
        if (!success) continue;   // check failed: the spell goes on; later reactors may still try
        return true;    // spell countered
    }
    return false;
}

// ---- R-G11 zone triggers (docs/proposals/ZONES_AURAS.md §2.2) ----
TriggerOn zone_trigger_on(ZoneTrigger t) {
    switch (t) {
    case ZoneTrigger::Appear: return TriggerOn::ZoneAppear;
    case ZoneTrigger::Enter: return TriggerOn::ZoneEnter;
    case ZoneTrigger::StartTurn: return TriggerOn::ZoneStartTurn;
    case ZoneTrigger::EndTurn: return TriggerOn::ZoneEndTurn;
    case ZoneTrigger::MoveWithin: return TriggerOn::ZoneMoveWithin;
    }
    return TriggerOn::ZoneEnter;
}

// Is `c` affected by zone `z` at all: alive, not excluded, and not the owner of its own aura (§2.2).
bool zone_affects(const ActiveZone& z, const SpellDef& def, const Combatant& c) {
    if (c.dead) return false;
    if (def.zone.anchor == ZoneAnchor::Creature && c.id == z.owner) return false;
    return !std::binary_search(z.excluded.begin(), z.excluded.end(), c.id);
}

// One trigger row of zone `zone_id` for `who`. `hook_membership`: Appear / StartTurn / EndTurn ask hooks.zone_contains; the
// others come from sim's membership facts (a completely_within row still asks the hook for the whole footprint). Emits
// FeatureTriggered{owner, who, a = spell id, b = TriggerOn::Zone*, c = 0 saved / 1 no save / 3 failed}, then the save,
// damage and condition exactly as a Save / ApplyCondition effect would. Nothing is copied by pointer: damage can end the
// owner's concentration and with it the zone.
void zone_row(EncounterState& s, Ctx* ctx, uint32_t zone_id, std::size_t row_index, CombatantId who, bool hook_membership,
              std::vector<Event>& ev) {
    const ActiveZone* zp = find_zone(s, zone_id);
    const SpellDef* def = zp ? spell_def(s, zp->spell_id) : nullptr;
    Combatant* t = find_mut(s, who);
    if (!zp || !def || !t || row_index >= def->zone.triggers.size() || !zone_affects(*zp, *def, *t)) return;
    if (zone_suppressed(s, *zp, *def)) return;   // D7: a feature aura whose owner is not conscious
    const ActiveZone z = *zp;
    const ZoneTriggerRow row = def->zone.triggers[row_index];
    if (row.own_turn_only && current_actor(s) != who) return;
    if (hook_membership || row.completely_within) {
        const bool in = s.hooks.zone_contains && s.hooks.zone_contains(s, zone_id, who, row.completely_within);
        // region 1 (Wall of Fire's end of turn): the damaging side counts too, for hook-answered rows
        const bool side = !in && hook_membership && row.region == 1 && s.hooks.zone_side_contains && s.hooks.zone_side_contains(s, zone_id, who);
        if (!in && !side) return;
    }
    const SpellEffect& e = row.effect;
    // A creature already held by this zone's "while in the zone" condition is not saved against it again (Web).
    if (row.while_in_zone && e.applies_condition)
        for (const auto& x : s.effects) if (x.zone_id == zone_id && x.target == who) return;
    if (row.group != 0) {   // once per creature per zone per turn, per group (D2)
        const uint32_t now = turn_stamp(s);
        ActiveZone* zm = find_zone_mut(s, zone_id);
        for (const auto& f : zm->fired) if (f.creature == who && f.stamp == now && f.group == row.group) return;
        zm->fired.push_back(ZoneFired{who, now, row.group});
    }
    Combatant* owner = find_mut(s, z.owner);
    DiceExpr dice = e.dice;
    if (dice.count > 0 && z.slot_level > def->level)
        dice.count = uint8_t(std::min(255, int(dice.count) + int(def->upcast_dice) * (int(z.slot_level) - int(def->level))));
    // Spirit Guardians: an evil owner's aura deals its evil damage type (necrotic) instead
    const DamageType dtype = (row.has_evil_damage_type && owner && owner->evil) ? row.evil_damage_type : e.damage_type;
    const bool damaging = dice.count > 0 || dice.bonus > 0;
    // Stinking Cloud: immune to poison / poisoned, or not breathing: an automatic success, no roll (and no damage draw)
    const bool auto_success = e.kind == SpellEffectKind::Save &&
        ((row.auto_success_damage & t->immunities) != 0 || (row.auto_success_conditions & t->condition_immunities) != 0 ||
         (row.auto_success_no_breath && t->no_breath));
    const int dmg = damaging && !auto_success ? std::max(0, roll_dice(s.rng, dice)) : 0;
    const std::size_t at = ev.size();
    emit(ev, EventKind::FeatureTriggered, z.owner, who, int32_t(z.spell_id), int32_t(zone_trigger_on(row.on)), auto_success ? 2 : 1);
    bool failed = true;
    if (auto_success) failed = false;
    else if (e.kind == SpellEffectKind::Save) {
        const SaveSource src{def->magical, e.applies_condition ? bit(e.condition) : 0u, damaging ? bit(dtype) : 0u};
        // Moonbeam: a shapechanger makes the save with disadvantage
        const RollMode mode = row.shapechanger_disadvantage && t->shapechanger ? RollMode::Disadvantage : RollMode::Normal;
        const SaveResult r = save_roll(s, *t, e.save_ability, z.dc, mode, ev, &src);
        emit(ev, EventKind::SavingThrowRolled, who, CombatantId(e.save_ability), r.roll.natural, r.roll.total, int32_t(r.success));
        failed = !r.success;
        ev[at].c = failed ? 3 : 0;
    }
    int dealt = failed ? dmg : (e.on_save == OnSave::Half ? dmg / 2 : 0);
    if (e.kind == SpellEffectKind::Save && t->evasion && e.save_ability == Ability::Dex && e.on_save == OnSave::Half)
        dealt = failed ? dmg / 2 : 0;   // Evasion
    if (dealt > 0) {
        const TrigSrc ts{owner, nullptr, true};
        damage_creature(s, *t, zone_apply_damage(s, *t, int16_t(std::min(dealt, 32767)), dtype, def->magical ? uint8_t(kAttackMagical) : uint8_t(0)),
                        false, ev, owner ? &ts : nullptr, ctx);
    }
    // Stinking Cloud (D9): the creature spends its action this turn (only on its own turn; movement and bonus action remain)
    if (row.loses_action && failed && !t->dead && current_actor(s) == who) { s.budget.action = false; s.budget.action_lost = true; }
    if (e.applies_condition && failed && !t->dead && !immune_to(s, *t, e.condition)) {
        if (!t->has(e.condition)) {
            add_condition(*t, e.condition);
            emit(ev, EventKind::ConditionAdded, who, 0, int32_t(e.condition));
        }
        if (row.while_in_zone && find_zone(s, zone_id)) {   // lasts while in the zone (Web): ends on leaving, escape, zone end
            ActiveSpellEffect x{z.spell_id, z.owner, who, e.condition, e.save_ability, z.dc, RepeatSave::None, z.concentration,
                                Timed{}, z.slot_level, false};
            x.zone_id = zone_id;
            s.effects.push_back(x);
        }
    }
}

// Silence (kZoneSilence, PR 2): a creature completely inside is deafened while it stays in the zone (a zone-bound effect,
// like Web's restraint: it ends on leaving or when the zone ends). No FeatureTriggered, no roll.
void silence_deafen(EncounterState& s, uint32_t zone_id, CombatantId who, std::vector<Event>& ev) {
    const ActiveZone* z = find_zone(s, zone_id);
    const SpellDef* def = z ? spell_def(s, z->spell_id) : nullptr;
    Combatant* t = find_mut(s, who);
    if (!z || !def || !t || t->dead || !(def->zone.flags & kZoneSilence) || zone_suppressed(s, *z, *def)) return;
    if (!s.hooks.zone_contains || !s.hooks.zone_contains(s, zone_id, who, true)) return;
    for (const auto& x : s.effects) if (x.zone_id == zone_id && x.target == who) return;
    if (immune_to(s, *t, Condition::Deafened)) return;
    if (!t->has(Condition::Deafened)) {
        add_condition(*t, Condition::Deafened);
        emit(ev, EventKind::ConditionAdded, who, 0, int32_t(Condition::Deafened));
    }
    ActiveSpellEffect x{z->spell_id, z->owner, who, Condition::Deafened, Ability::Wis, z->dc, RepeatSave::None, z->concentration,
                        Timed{}, z->slot_level, false};
    x.zone_id = zone_id;
    s.effects.push_back(x);
}

// Every row of zone `zone_id` with trigger `on` for `who`, in row order. `aura_move`: the zone moved onto `who` (Enter rows
// only with counts_aura_move_as_enter, D1).
void zone_event(EncounterState& s, Ctx* ctx, uint32_t zone_id, CombatantId who, ZoneTrigger on, bool hook_membership,
                bool aura_move, std::vector<Event>& ev) {
    const ActiveZone* z = find_zone(s, zone_id);
    const SpellDef* def = z ? spell_def(s, z->spell_id) : nullptr;
    if (!def) return;
    const std::size_t n = def->zone.triggers.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (!find_zone(s, zone_id) || s.ended) return;
        const Combatant* c = find(s, who);
        if (!c || c->dead) return;
        const ZoneTriggerRow& row = def->zone.triggers[i];
        if (row.on != on || (aura_move && !row.counts_aura_move_as_enter)) continue;
        zone_row(s, ctx, zone_id, i, who, hook_membership, ev);
    }
}

// StartTurn / EndTurn rows of every zone for `who` (ascending zone id), membership from hooks.zone_contains.
void zone_turn_triggers(EncounterState& s, Ctx* ctx, CombatantId who, ZoneTrigger on, std::vector<Event>& ev) {
    std::vector<uint32_t> ids;
    for (const auto& z : s.zones) ids.push_back(z.zone_id);
    std::sort(ids.begin(), ids.end());
    for (uint32_t id : ids) {
        zone_event(s, ctx, id, who, on, true, false, ev);
        if (on == ZoneTrigger::StartTurn) silence_deafen(s, id, who, ev);   // PR 2: Silence catches who is inside now
    }
}

void zone_membership(EncounterState& s, Ctx* ctx, CombatantId who, const std::vector<uint32_t>& entered,
                     const std::vector<uint32_t>& left, const std::vector<uint32_t>& within, std::vector<Event>& ev) {
    std::vector<uint32_t> ids = entered;
    ids.insert(ids.end(), left.begin(), left.end());
    ids.insert(ids.end(), within.begin(), within.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (uint32_t id : ids) {   // per zone, ascending (Q9): Enter, MoveWithin once per 5 ft, then leaving
        if (!find_zone(s, id)) continue;
        const auto n_in = std::count(entered.begin(), entered.end(), id);
        const auto n_within = std::count(within.begin(), within.end(), id);
        if (n_in > 0) zone_event(s, ctx, id, who, ZoneTrigger::Enter, false, false, ev);
        for (std::ptrdiff_t k = 0; k < n_in + n_within; ++k) zone_event(s, ctx, id, who, ZoneTrigger::MoveWithin, false, false, ev);
        if (std::find(left.begin(), left.end(), id) != left.end()) end_zone_effects(s, id, who, ev);   // exits never deal damage
        else if (n_in + n_within > 0) silence_deafen(s, id, who, ev);   // PR 2: now completely inside a Silence zone
    }
}

// Other creatures' membership changes from an aura owner's move or a zone move (Q8): ascending zone id, then initiative
// order, then lower id (Q9). Entering counts only for rows with counts_aura_move_as_enter (D1); leaving ends "while in" effects.
void zone_changes(EncounterState& s, Ctx* ctx, const std::vector<ZoneChange>& changes, std::vector<Event>& ev) {
    if (changes.empty()) return;
    auto init_pos = [&](CombatantId id) {
        for (std::size_t i = 0; i < s.order.size(); ++i) if (s.order[i].id == id) return i;
        return s.order.size();
    };
    std::vector<ZoneChange> sorted = changes;
    std::stable_sort(sorted.begin(), sorted.end(), [&](const ZoneChange& x, const ZoneChange& y) {
        if (x.zone_id != y.zone_id) return x.zone_id < y.zone_id;
        const std::size_t px = init_pos(x.creature), py = init_pos(y.creature);
        return px != py ? px < py : x.creature < y.creature;
    });
    for (const ZoneChange& c : sorted) {
        if (!find_zone(s, c.zone_id)) continue;
        if (c.entered) {
            zone_event(s, ctx, c.zone_id, c.creature, ZoneTrigger::Enter, false, true, ev);
            silence_deafen(s, c.zone_id, c.creature, ev);   // PR 2: "while in the area" starts with the aura / zone move
        } else end_zone_effects(s, c.zone_id, c.creature, ev);
    }
}

// Petrifying Gaze: a living gazer other than c is on the board (c must answer GazeResponse at its turn start).
bool gaze_applies(const EncounterState& s, const Combatant& c) {
    if (c.dead || c.hp <= 0) return false;
    for (const auto& g : s.combatants)
        if (g.id != c.id && !g.dead && !g.gazes.empty()) return true;
    return false;
}

void start_turn(EncounterState& s, std::vector<Event>& ev) {
    Combatant& c = *find_mut(s, s.order[s.turn_index].id);
    c.reaction_used = false;
    c.dodging = false;
    c.disengaged = false;
    c.reaction_ac_bonus = 0;       // Shield-style bonus lasts until the start of its next turn
    c.reaction_blocks_spell = 0;
    c.gaze_averted = false;        // Petrifying Gaze: averted eyes last until the start of its next turn
    for (auto& o : s.combatants)   // Help lasts until the start of the helper's next turn
    {
        if (o.helped_by == c.id) { o.helped_by = 0; o.helped_vs = 0; }
        if (o.check_helped_by == c.id) o.check_helped_by = 0;
    }
    if (c.has_readied) {           // a readied action not used by now is lost
        std::erase_if(s.readied, [&](const ReadiedAction& r) { return r.actor == c.id; });
        c.has_readied = false;
        emit(ev, EventKind::ReadyExpired, c.id);
    }
    emit(ev, EventKind::TurnStarted, c.id, 0, s.round);
    if (c.form != FormKind::None && c.form_end_round != 0 && s.round >= c.form_end_round) form_reverted(c, 0, 0, ev);   // R-G12
    for (std::size_t k = 0; k < s.summon_weapons.size();) {   // R-G12 PR 3: a Spiritual Weapon's minute ends (SummonEnded b = 0)
        const ActiveSummonWeapon w = s.summon_weapons[k];
        if (w.owner != c.id || w.end_round == 0 || s.round < w.end_round) { ++k; continue; }
        s.summon_weapons.erase(s.summon_weapons.begin() + std::ptrdiff_t(k));
        emit(ev, EventKind::SummonEnded, w.owner, w.token_id, int32_t(w.spell_id), 0);
    }
    drop_caster_turn_modifiers(s, c.id, ev);
    for (const auto& m : s.modifiers)   // Heroism: temp hp again at the start of each turn
        if (m.target == c.id && m.spec.stat == ModStat::TempHp && m.spec.per_turn && !c.dead && !c.no_regain_hp && m.spec.bonus > c.temp_hp) {
            c.temp_hp = m.spec.bonus;
            emit(ev, EventKind::ModifierApplied, m.caster, c.id, int32_t(m.spec.id), int32_t(m.spec.stat), m.spec.bonus);
        }
    bool zone_downed = false;   // R-G11: dropped to 0 HP by a zone this turn start: no death save until its next turn
    bool action_lost = false;   // R-G11 PR 2: a StartTurn row took this turn's action (Stinking Cloud); kept over the budget reset
    if (!s.zones.empty()) {
        for (auto& z : s.zones)   // once-per-turn bookkeeping of earlier turns is stale now
            std::erase_if(z.fired, [&](const ZoneFired& f) { return f.stamp != turn_stamp(s); });
        const bool up = c.hp > 0;
        s.budget.action_lost = false;
        if (!c.dead) zone_turn_triggers(s, nullptr, c.id, ZoneTrigger::StartTurn, ev);   // no reaction windows (no Ctx)
        zone_downed = up && c.hp <= 0;
        action_lost = s.budget.action_lost;
    }
    repeat_saves(s, c.id, RepeatSave::StartOfTurn, ev);
    if (dies_of_exhaustion(c) && !c.dead) kill(c, ev);
    if (!c.dead && c.regeneration.amount > 0) {   // Regeneration (troll, vampire spawn)
        const bool suppressed = c.regen_suppressed;
        c.regen_suppressed = false;
        if (suppressed) {
            if (c.regen_down) { c.regen_down = false; kill(c, ev); }
        } else if (c.regen_down || c.hp < effective_hp_max(c)) {
            c.regen_down = false;
            auto h = heal_hp(s, c, c.regeneration.amount);
            commit_healing(c, h);
            emit(ev, EventKind::Healed, c.id, c.id, h.healed, h.hp_after, h.from_zero);
        }
    }
    if (!c.dead) {   // Recharge: one d6 per expended recharge action of this creature (draws only when one exists)
        for (auto& u : s.action_uses) {
            if (u.actor != c.id || !u.expended) continue;
            const SpellDef* d = spell_def(s, u.spell_id);
            int need = d && d->recharge_min ? d->recharge_min : 6;
            if (!d)   // EC deferred monsters (flagged): a recharge trigger's row (spell_id = trigger id)
                for (const Trigger& tr : c.triggers) if (tr.id == u.spell_id && tr.recharge_min) need = tr.recharge_min;
            const int roll = roll_dice(s.rng, DiceExpr{1, 6, 0}, false);
            const bool back = roll >= need;
            if (back) u.expended = false;
            emit(ev, EventKind::RechargeRolled, c.id, 0, int32_t(u.spell_id), roll, back ? 1 : 0);
        }
    }
    if (!c.dead) {   // Innate "Recharge N-6" spells: same roll, same position, after the action rows
        for (auto& is : c.innate) {
            if (is.recharge_min == 0 || is.uses_left > 0) continue;
            const int roll = roll_dice(s.rng, DiceExpr{1, 6, 0}, false);
            const bool back = roll >= is.recharge_min;
            if (back) is.uses_left = 1;
            emit(ev, EventKind::RechargeRolled, c.id, 0, int32_t(is.spell_id), roll, back ? 1 : 0);
        }
    }
    if (!c.dead && !c.triggers.empty()) {   // turn_start triggers (after regeneration and recharge)
        TrigCtx x;
        run_event_triggers(s, c, c, TriggerOn::TurnStart, x, ev);
    }
    if (is_dying(c) && c.uses_death_saves && !zone_downed) {
        auto r = roll_death_save(s.rng, c.death);
        c.death = r.after;
        emit(ev, EventKind::DeathSaveRolled, c.id, 0, r.natural, c.death.successes, c.death.failures);
        switch (r.outcome) {
            case DeathSaveOutcome::Died: kill(c, ev); break;
            case DeathSaveOutcome::Stabilized: emit(ev, EventKind::Stabilized, c.id); break;
            case DeathSaveOutcome::RegainedHp:
                if (!s.modifiers.empty() && has_marker(s, c.id, ModStat::NoHealing)) break;   // 7.2u Chill Touch: no regain (counts reset)
                c.hp = 1;
                remove_condition(c, Condition::Unconscious);   // stays Prone
                emit(ev, EventKind::RegainedHp, c.id, 0, 1);
                emit(ev, EventKind::ConditionRemoved, c.id, 0, int32_t(Condition::Unconscious));
                break;
            case DeathSaveOutcome::Pending: break;
        }
    }
    bool act = can_act(c);
    s.budget = TurnBudget{act, act, act, int16_t(c.hp > 0 ? modified_speed(s, c) : 0), 0, 0, 0};
    if (action_lost) { s.budget.action = false; s.budget.action_lost = true; }   // R-G11 PR 2 (D9): movement and bonus action remain
    c.reckless_active = false;   // Reckless Attack: attacks against it had advantage until now
    if (c.lethargy != 0) {       // Haste ended: no move, action or bonus action this turn
        c.lethargy = 2;
        s.budget.action = s.budget.bonus_action = false;
        s.budget.movement_ft = 0;
    } else if (act && !s.modifiers.empty() && has_marker(s, c.id, ModStat::Haste)) {
        s.budget.haste_action = true;
    }
    s.budget.gaze_pending = gaze_applies(s, c);   // Petrifying Gaze: answered by GazeResponse before anything else
    check_end(s, ev);
}

void advance_turn(EncounterState& s, std::vector<Event>& ev) {
    const auto n = s.order.size();
    const uint16_t round_before = s.round;
    for (std::size_t k = 0; k < n; ++k) {
        if (++s.turn_index >= n) { s.turn_index = 0; ++s.round; }
        if (!find_mut(s, s.order[s.turn_index].id)->dead) break;
    }
    if (s.round != round_before) tick_effects_round_start(s, ev);   // timed effects: a new round has started
    start_turn(s, ev);
}

// Ready: offer each event of this step to each readied action (readied order, then event order)
// through a ReadiedTrigger window for the readier. Events produced inside reactions are only
// offered when allow_chain permits.
void fire_readies(EncounterState& s, Ctx& ctx, std::vector<Event>& ev) {
    if (!s.hooks.ready_trigger || s.readied.empty()) return;
    const std::size_t n = ev.size();
    for (std::size_t i = 0; i < n && !s.ended; ++i) {
        const Event e = ev[i];
        for (std::size_t k = 0; k < s.readied.size();) {
            ReadiedAction r = s.readied[k];
            Combatant* a = find_mut(s, r.actor);
            ReactionWindow w = new_window(s, ctx, ReactionTrigger::ReadiedTrigger, e.actor);
            w.depth = e.reaction_depth;
            if (!a || !may_react(s, *a, w.trigger) || !may_open(s, w) || !s.hooks.ready_trigger(s, r, e)) { ++k; continue; }
            ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, e.actor, a->id, int32_t(w.trigger), int32_t(w.seq));
            const ReactionOption fire{r.response.attack_index, ReactionOptionKind::FireReady, 0, r.trigger_id};
            auto d = poll(s, ctx, w, *a, std::span<const ReactionOption>(&fire, 1), ev);
            if (!d.react) { ++k; continue; }   // ignoring the trigger keeps the readied action
            s.readied.erase(s.readied.begin() + std::ptrdiff_t(k));
            a->has_readied = false;
            a->reaction_used = true;
            std::size_t from = ev.size();
            // Re-evaluate the response's facts now that it fires (range, hostiles adjacent, ...).
            if (s.hooks.refresh_ready)
                if (auto q = s.hooks.refresh_ready(s, r, e)) {
                    q->actor = r.response.actor;   // the readier and the kind/target stay as readied
                    q->kind = r.response.kind;
                    q->target = r.response.target;
                    r.response = *q;
                }
            emit(ev, EventKind::ReadyTriggered, a->id, r.response.target, int32_t(r.trigger_id));
            ++ctx.depth;
            if (r.response.kind == ActionKind::Attack) {
                Combatant* t = find_mut(s, r.response.target);
                if (t && !t->dead && r.response.attack_index < a->attacks.size())
                    do_attack(s, ctx, *a, *t, a->attacks[r.response.attack_index], r.response.target_within_5ft, ev,
                              r.response.ranged_hostile_within_5ft || r.response.beyond_normal_range, 0,
                              r.response.ally_near_target);
            } else if (r.response.kind == ActionKind::Move) {
                int16_t ft = r.response.move_ft;
                int sp = effective_speed(*a);
                if (ft > sp) ft = int16_t(sp);
                // SRD: a readied move provokes like any other move. Its LeavingReach window opens at
                // depth 0 (allowed without allow_chain); its events stay marked as reaction events, so
                // they don't fire further Ready triggers (no chaining from the reaction itself).
                bool go = true;
                if (r.response.leaves_reach) {
                    uint8_t saved = ctx.depth;
                    ctx.depth = 0;
                    go = leaving_reach_window(s, ctx, *a, ev);
                    ctx.depth = saved;
                }
                if (go) {
                    emit(ev, EventKind::Moved, a->id, 0, ft, 0);
                    if (!s.zones.empty()) {   // R-G11: the readied move's membership facts (sim refreshes them in refresh_ready)
                        zone_membership(s, &ctx, a->id, r.response.zones_entered, r.response.zones_left, r.response.zones_within, ev);
                        zone_changes(s, &ctx, r.response.zone_changes, ev);
                    }
                } else emit(ev, EventKind::MoveInterrupted, a->id, 0, ft);
            }
            --ctx.depth;
            mark_depth(ev, from, uint8_t(ctx.depth + 1));
            check_end(s, ev);
        }
    }
}

StepResult fail(std::string_view why) { return {false, why, {}}; }

// Skill check inside the encounter: Help's "next check" advantage is consumed here.
CheckResult roll_check(EncounterState& s, Combatant& c, Skill k, int dc, RollMode mode, std::vector<Event>& ev) {
    bool helped = c.check_helped_by != 0;
    if (helped) {
        c.check_helped_by = 0;
        mode = combine_modes(true, mode == RollMode::Disadvantage);
    }
    {   // modifiers: modes merge, then flat bonus and dice after the d20
        bool adv = mode == RollMode::Advantage, dis = mode == RollMode::Disadvantage;
        mod_modes(s, c.id, ModStat::AbilityCheck, int(skill_ability(k)), adv, dis);
        mode = combine_modes(adv, dis);
    }
    auto r = skill_check(s.rng, c, k, dc, mode);
    if ((c.reroll_ones & 2) && reroll_natural_one(s.rng, r.roll)) r.success = r.roll.total >= dc;   // Lucky
    if (const int add = modifier_flat(s, c.id, ModStat::AbilityCheck, int(skill_ability(k))) + mod_roll(s, c.id, ModStat::AbilityCheck, int(skill_ability(k)))) {
        r.roll.total = int16_t(r.roll.total + add);
        r.success = r.roll.total >= dc;
    }
    emit(ev, EventKind::CheckRolled, c.id, CombatantId(k), r.roll.natural, r.roll.total,
         int32_t(r.success) | (int32_t(helped) << 1));
    return r;
}

// R-G11: a plain ability check (no skill; Web's Strength check to break free), same rules as roll_check. CheckRolled target =
// 0x100 | Ability (a Skill value otherwise).
CheckResult roll_ability_check(EncounterState& s, Combatant& c, Ability ab, int dc, std::vector<Event>& ev) {
    const bool helped = c.check_helped_by != 0;
    if (helped) c.check_helped_by = 0;
    bool adv = helped, dis = false;
    mod_modes(s, c.id, ModStat::AbilityCheck, int(ab), adv, dis);
    const D20Roll roll = roll_d20(s.rng, modifier(c.abilities, ab), check_roll_mode(c, combine_modes(adv, dis)));
    CheckResult r{roll, roll.total >= dc};
    if ((c.reroll_ones & 2) && reroll_natural_one(s.rng, r.roll)) r.success = r.roll.total >= dc;   // Lucky
    if (const int add = modifier_flat(s, c.id, ModStat::AbilityCheck, int(ab)) + mod_roll(s, c.id, ModStat::AbilityCheck, int(ab))) {
        r.roll.total = int16_t(r.roll.total + add);
        r.success = r.roll.total >= dc;
    }
    emit(ev, EventKind::CheckRolled, c.id, CombatantId(0x100u | uint32_t(ab)), r.roll.natural, r.roll.total,
         int32_t(r.success) | (int32_t(helped) << 1));
    return r;
}

bool spend_action(TurnBudget& b, const Combatant& c, ActionKind k, bool bonus) {
    if (bonus) {
        if (!b.bonus_action || !(c.bonus_action_grants & bit(k))) return false;
        b.bonus_action = false;
        return true;
    }
    if (!b.action) {
        if (b.extra_actions == 0) return false;   // Action Surge: an extra action stands in for the spent one
        --b.extra_actions;
        return true;
    }
    b.action = false;
    return true;
}

StepResult step_impl(EncounterState& s, Ctx& ctx, const ActionRequest& req);

// Resolves ONE effect (def.kind and its fields) over T. Returns the targets it affected (for rider effects).
// R-G12 rules PR 3 (D3): the Spiritual Weapon's melee spell attack (the cast's, and each later bonus action): dice + upcast
// (upcast_dice per summon_weapon.upcast_every levels above the spell's) + the spellcasting modifier, magical force (content).
bool summon_weapon_attack(EncounterState& s, Ctx& ctx, Combatant& a, const SpellDef& def, uint8_t lvl, Combatant& t,
                          std::vector<Event>& ev) {
    DiceExpr d = def.dice;
    const int every = def.summon_weapon.upcast_every == 0 ? 1 : def.summon_weapon.upcast_every;
    if (lvl > def.level) d.count = uint8_t(std::min(255, int(d.count) + int(def.upcast_dice) * ((lvl - def.level) / every)));
    if (def.add_spellcasting_modifier) d.bonus = int16_t(d.bonus + modifier(a.abilities, a.spellcasting_ability));
    Attack atk{def.content_id, int8_t(spell_attack_bonus(a, def)), d, def.damage_type, def.summon_weapon.reach_ft, false, false};
    atk.qualifiers = def.magical ? kAttackMagical : 0;
    return do_attack(s, ctx, a, t, atk, false, ev, false, def.id);
}

uint32_t spell_rounds(const SpellDef& d) {
    if (d.duration == SpellDuration::Rounds) return d.duration_amount;
    if (d.duration == SpellDuration::Minutes) return uint32_t(d.duration_amount) * 10u;
    if (d.duration == SpellDuration::Hours) return uint32_t(d.duration_amount) * 600u;
    return 0;
}

// EC spider follow-up (flagged): the giant spider bite's 0-HP clause (SpellDef::zero_hp_*): stable, then one timed effect
// (zero_hp_condition for zero_hp_rounds; 600 = 1 hour) that also holds zero_hp_while. Immunity to either skips that condition.
void zero_hp_clause(EncounterState& s, const Combatant& a, Combatant& t, const SpellDef& def, std::vector<Event>& ev) {
    if (!t.death.stable) stabilize(t, ev);
    const bool main_ok = !immune_to(s, t, def.zero_hp_condition), also_ok = !immune_to(s, t, def.zero_hp_while);
    if (!main_ok && !also_ok) return;
    for (Condition k : {def.zero_hp_condition, def.zero_hp_while}) {
        if ((k == def.zero_hp_condition ? main_ok : also_ok) && !t.has(k)) {
            add_condition(t, k);
            emit(ev, EventKind::ConditionAdded, t.id, 0, int32_t(k));
        }
    }
    ActiveSpellEffect e{def.id, a.id, t.id, def.zero_hp_condition, def.save_ability, 0, RepeatSave::None, false,
                        make_timed(Expiry::Rounds, def.zero_hp_rounds, current_actor(s) == t.id), 0, false};
    e.no_condition = !main_ok;
    e.has_also = also_ok; e.also = def.zero_hp_while;
    s.effects.push_back(e);
}

std::vector<Combatant*> resolve_effect(EncounterState& s, Ctx& ctx, Combatant& a, const SpellDef& def, uint8_t lvl,
                                       const std::vector<Combatant*>& T, bool within_5ft, std::vector<Event>& ev) {
    std::vector<Combatant*> affected;
    switch (def.kind) {
    case SpellEffectKind::SpellAttack: {
        Attack atk{def.content_id, int8_t(spell_attack_bonus(a, def)), scaled_dice(def, lvl, a.caster_level),
                   def.damage_type, uint8_t(std::min<int>(def.range_ft, 255)), !def.melee, false};
        atk.qualifiers = def.magical ? kAttackMagical : 0;   // spell damage is magical (bypasses "nonmagical" resistance)
        const int beams = scaled_beams(def, lvl, a.caster_level);
        for (int i = 0; i < beams; ++i) {   // one attack roll per beam: one target, or one per beam
            Combatant* t = T.size() == 1 ? T[0] : T[std::size_t(i)];
            // EC spider follow-up (flagged): a non-spell ranged attack_effect (Web) takes the ranged facts sim filled
            const bool rdis = !def.is_spell && !def.melee && ctx.ranged_disadvantage;
            if (!t->dead && do_attack(s, ctx, a, *t, atk, within_5ft, ev, rdis, def.id)) affected.push_back(t);   // TODO: ranged facts for spell attacks
        }
        break;
    }
    case SpellEffectKind::AutoHit: {
        const int darts = scaled_darts(def, lvl);
        std::vector<Combatant*> per_dart(std::size_t(darts), T[0]);
        if (T.size() > 1) per_dart = T;
        std::vector<Combatant*> distinct;
        for (Combatant* t : per_dart) if (std::find(distinct.begin(), distinct.end(), t) == distinct.end()) distinct.push_back(t);
        // Shield's trigger includes being targeted by Magic Missile: a BeingHit window with spell_id
        // (attack_total 0). Simultaneous darts: every target reacts before any damage.
        auto react = [&](Combatant* t) {
            if (t->dead || t->reaction_blocks_spell == def.id) return;
            ReactionWindow w = new_window(s, ctx, ReactionTrigger::BeingHit, a.id);
            w.target = t->id; w.spell_id = def.id; w.target_ac = int16_t(t->ac + t->reaction_ac_bonus);
            auto blocks = [&](const ReactionOption& o) {
                const SpellDef* sd = o.slot_level > 0 ? spell_def(s, o.option_id) : nullptr;
                return o.kind == ReactionOptionKind::AcBonus && sd && sd->blocks_spell == def.id;
            };
            being_hit_window(s, ctx, a, *t, w, ev, blocks);
        };
        if (def.simultaneous) for (Combatant* t : distinct) react(t);
        for (Combatant* t : distinct) {
            if (t->dead) continue;
            affected.push_back(t);
            if (!def.simultaneous) react(t);
            for (Combatant* d : per_dart) {
                if (d != t || t->dead) continue;
                if (t->reaction_blocks_spell == def.id) continue;   // Shield: no damage from the darts
                int dmg = std::max(0, roll_dice(s.rng, def.dice));
                { const TrigSrc ts{&a, nullptr, true}; damage_creature(s, *t, zone_apply_damage(s, *t, int16_t(dmg), def.damage_type, kAttackMagical), false, ev, &ts, &ctx); }
            }
        }
        break;
    }
    case SpellEffectKind::Save: {
        const int dc = spell_save_dc(a, def);
        const uint8_t dq = def.magical ? kAttackMagical : 0;
        const DiceExpr sdice = scaled_dice(def, lvl, a.caster_level);
        const int dmg = def.dice.count > 0 || def.dice.bonus > 0
                            ? std::max(0, ctx.empower > 0 ? roll_empowered(s.rng, sdice, false, ctx.empower)
                                                          : roll_dice(s.rng, sdice)) : 0;   // one roll for all targets
        bool applied = false;
        for (Combatant* t : T) {
            if (t->dead) continue;
            SaveSource src{def.magical, def.applies_condition ? bit(def.condition) : 0u,
                           (def.dice.count > 0 || def.dice.bonus > 0) ? bit(def.damage_type) : 0u};
            const bool spared = ctx.spared && std::find(ctx.spared->begin(), ctx.spared->end(), t->id) != ctx.spared->end();
            SaveResult r{};
            if (spared) {   // R-G14 Careful / Sculpt Spells: automatic success, no roll (natural 0, total 0)
                r.success = true;
            } else {
                r = save_roll(s, *t, def.save_ability, dc, ctx.heighten == t->id ? RollMode::Disadvantage : RollMode::Normal,
                              ev, &src);
            }
            emit(ev, EventKind::SavingThrowRolled, t->id, CombatantId(def.save_ability), r.roll.natural, r.roll.total,
                 int32_t(r.success));
            int dealt = r.success ? (def.on_save == OnSave::Half ? dmg / 2 : 0) : dmg;
            if (spared && ctx.spared_no_damage) dealt = 0;
            if (t->evasion && def.save_ability == Ability::Dex && def.on_save == OnSave::Half) dealt = r.success ? 0 : dmg / 2;   // Evasion
            const int16_t hp_before = t->hp;
            if (dealt > 0) { const TrigSrc ts{&a, nullptr, true}; damage_creature(s, *t, zone_apply_damage(s, *t, int16_t(dealt), def.damage_type, dq), false, ev, &ts, &ctx); }
            if (def.zero_hp_rounds > 0 && dealt > 0 && hp_before > 0 && t->hp <= 0 && !t->dead)   // EC spider follow-up (flagged)
                zero_hp_clause(s, a, *t, def, ev);
            if (!r.success) affected.push_back(t);
            // Petrifying Gaze: failing by escalate_margin or more applies escalate_to at once, with no repeat save
            const bool at_once = def.escalates && def.escalate_margin > 0 && !r.success && int(r.roll.total) <= dc - int(def.escalate_margin);
            const Condition cond = at_once ? def.escalate_to : def.condition;
            if (def.applies_condition && !r.success && !t->dead && !immune_to(s, *t, cond)) {
                if (!t->has(cond)) {
                    add_condition(*t, cond);
                    emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(cond));
                }
                note_source(s, cond, a.id, t->id);
                applied = true;
                if (def.condition_untracked && !at_once) continue;   // ends by its own rules (prone): no effect entry, like Shove
                s.effects.push_back(ActiveSpellEffect{def.id, a.id, t->id, cond, def.save_ability,
                                                      int16_t(dc), at_once ? RepeatSave::None : def.repeat_save, def.concentration,
                                                      at_once ? Timed{} : make_timed(Expiry(def.expiry), def.duration_rounds, current_actor(s) == t->id),
                                                      lvl, false});
                if (def.escalates && !at_once) { s.effects.back().escalates = true; s.effects.back().escalate_to = def.escalate_to; }
            }
        }
        if (def.concentration && applied) a.concentration_spell = def.id;
        ctx.heighten = 0;   // R-G14: Heightened covers only the first save against the spell
        break;
    }
    case SpellEffectKind::Heal: {
        const DiceExpr dice = scaled_dice(def, lvl, a.caster_level);
        for (Combatant* t : T) {
            if (t->dead || (def.excluded_types & bit(t->creature_type))) continue;   // no effect (Cure Wounds: undead)
            int amt = roll_dice(s.rng, dice) + (def.add_spellcasting_modifier ? modifier(a.abilities, a.spellcasting_ability) : 0);
            if (amt <= 0) continue;
            bool was_unconscious = t->has(Condition::Unconscious);
            auto h = heal_hp(s, *t, int16_t(amt));
            commit_healing(*t, h);
            emit(ev, EventKind::Healed, a.id, t->id, h.healed, h.hp_after, h.from_zero);
            affected.push_back(t);
            if (h.from_zero && was_unconscious)
                emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(Condition::Unconscious));
        }
        break;
    }
    case SpellEffectKind::ApplyCondition: {   // no save: the condition lasts with the spell
        bool applied = false;
        for (Combatant* t : T) {
            if (t->dead || immune_to(s, *t, def.condition)) continue;
            if (!t->has(def.condition)) {
                add_condition(*t, def.condition);
                emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(def.condition));
            }
            note_source(s, def.condition, a.id, t->id);
            if (!def.condition_untracked) {   // ends by its own rules (prone): no effect entry, like Shove
                s.effects.push_back(ActiveSpellEffect{def.id, a.id, t->id, def.condition, Ability::Wis, 0, RepeatSave::None,
                                                      def.concentration,
                                                      make_timed(Expiry(def.expiry), def.duration_rounds, current_actor(s) == t->id),
                                                      lvl, false});
                if (def.escape_dc > 0) { s.effects.back().dc = def.escape_dc; s.effects.back().escape_check = true; }   // EC (flagged): Web
                if (def.escape_dc > 0 && def.object_hp > 0) {   // EC spider follow-up (flagged): the web as an object
                    auto& w = s.effects.back();
                    w.object_hp = def.object_hp; w.object_ac = def.object_ac;
                    w.object_vulnerable = def.object_vulnerable; w.object_immune = def.object_immune;
                }
            }
            affected.push_back(t);
            applied = true;
        }
        if (def.concentration && applied) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::ForcedMove: {   // R-G9: queued; deferred (defer_forced) after the effect's saves and damage
        const uint16_t feet = def.forced.up_to && ctx.forced_feet > 0 ? std::min(ctx.forced_feet, def.forced.feet) : def.forced.feet;
        for (Combatant* t : T) {
            if (t->dead || t == &a) continue;
            if (def.forced.max_size >= 0 && int(t->size) > int(def.forced.max_size)) continue;   // too big: only the push is skipped
            ForcedMoveQuery q; q.mover = t->id; q.source = a.id; q.mode = def.forced.mode; q.feet = feet;
            ctx.forced_queue.push_back(q);
            affected.push_back(t);
        }
        break;
    }
    case SpellEffectKind::Teleport: {   // R-G9: the caster; the destination is sim's
        ForcedMoveQuery q; q.mover = a.id; q.source = a.id; q.mode = 0; q.feet = ctx.teleport_feet; q.teleport = true;
        q.has_dest = ctx.has_teleport_dest; q.dest_x = ctx.teleport_x; q.dest_y = ctx.teleport_y;
        ctx.forced_queue.push_back(q);
        affected.push_back(&a);
        break;
    }
    case SpellEffectKind::ApplyModifier: {   // timed modifier on each target (applies_to Self: the caster)
        ModifierSpec m = def.modifier;
        m.source = def.id; m.concentration = def.concentration;
        std::vector<Combatant*> to = T;
        if (m.applies_to == ModAppliesTo::Self) to = {&a};
        else if (m.applies_to == ModAppliesTo::AlliesWithin30 && s.hooks.area_targets) {
            SpellDef area; area.area = AreaShape::Sphere; area.area_size_ft = 30;
            to.clear();
            for (CombatantId id : s.hooks.area_targets(s, a, area)) {
                Combatant* c = find_mut(s, id);
                if (c && !c->dead && c->id != a.id && c->team == a.team) to.push_back(c);
            }
        }
        bool applied = false;
        for (Combatant* t : to) {
            if (t->dead) continue;
            if (apply_modifier(s, a.id, t->id, m, ev)) {
                affected.push_back(t); applied = true;
                for (auto& am : s.modifiers) if (am.spec.id == m.id && am.target == t->id && am.caster == a.id) {
                    am.slot_level = lvl;
                    if (am.spec.repeat_save != 0 && am.spec.repeat_dc == 0) am.spec.repeat_dc = int16_t(spell_save_dc(a, def));   // Slow
                }
            }
        }
        if (def.concentration && applied) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::HpPool: {   // Sleep / Color Spray: lowest current HP first while the pool covers them
        int pool = std::max(0, roll_dice(s.rng, scaled_dice(def, lvl, a.caster_level)));
        std::vector<Combatant*> order;
        for (Combatant* t : T) {
            if (t->dead || t->hp <= 0 || t->has(def.condition) || immune_to(s, *t, def.condition)) continue;
            if (def.excluded_types & bit(t->creature_type)) continue;
            order.push_back(t);
        }
        std::stable_sort(order.begin(), order.end(), [](const Combatant* x, const Combatant* y) {
            return x->hp != y->hp ? x->hp < y->hp : x->id < y->id; });
        for (Combatant* t : order) {
            if (t->hp > pool) break;   // the rest have at least as many hit points
            pool -= t->hp;
            add_condition(*t, def.condition);
            emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(def.condition));
            s.effects.push_back(ActiveSpellEffect{def.id, a.id, t->id, def.condition, Ability::Wis, 0, RepeatSave::None,
                                                  def.concentration,
                                                  make_timed(Expiry(def.expiry), def.duration_rounds, current_actor(s) == t->id),
                                                  lvl, def.ends_on_damage});
            affected.push_back(t);
        }
        if (def.concentration && !affected.empty()) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::Dispel: {
        for (Combatant* t : T) {
            struct OnTarget { uint32_t spell; CombatantId caster; int level; };
            std::vector<OnTarget> list;
            auto note = [&](uint32_t sp, CombatantId caster, uint8_t slot) {
                for (const auto& o : list) if (o.spell == sp && o.caster == caster) return;
                const SpellDef* d = spell_def(s, sp);
                if (!d && slot == 0) return;   // not a spell (a feature's modifier): not dispellable
                list.push_back({sp, caster, slot ? int(slot) : int(d->level)});
            };
            for (const auto& e : s.effects) if (e.target == t->id) note(e.spell_id, e.caster, e.slot_level);
            for (const auto& m : s.modifiers) if (m.target == t->id && m.spec.source != 0) note(m.spec.source, m.caster, m.slot_level);
            bool ended = false;
            for (const auto& o : list) {
                bool ends = o.level <= int(lvl);
                if (!ends) {   // ability check with the spellcasting ability, DC base + the spell's level
                    D20Roll roll = roll_d20(s.rng, modifier(a.abilities, a.spellcasting_ability), RollMode::Normal);
                    ends = roll.total >= int(def.dispel_dc_base) + o.level;
                    emit(ev, EventKind::SpellCheckRolled, a.id, t->id, roll.natural, roll.total, int32_t(ends));
                }
                if (!ends) continue;
                ended = true;
                for (std::size_t i = 0; i < s.effects.size();) {
                    const auto& e = s.effects[i];
                    if (e.target == t->id && e.spell_id == o.spell && e.caster == o.caster) end_effect(s, i, ev); else ++i;
                }
                for (std::size_t i = 0; i < s.modifiers.size();) {
                    const auto& m = s.modifiers[i];
                    if (m.target == t->id && m.spec.source == o.spell && m.caster == o.caster) remove_modifier(s, i, 5, ev); else ++i;
                }
                // the spell has nothing left anywhere: the caster's concentration on it ends too
                Combatant* c = find_mut(s, o.caster);
                if (c && c->concentration_spell == o.spell) {
                    bool left = false;
                    for (const auto& e : s.effects) left = left || (e.spell_id == o.spell && e.caster == o.caster);
                    for (const auto& m : s.modifiers) left = left || (m.spec.source == o.spell && m.caster == o.caster);
                    left = left || zone_alive(s, o.caster, o.spell);   // R-G11: its zone is still up
                    if (!left) end_concentration(s, o.caster, ev);
                }
            }
            if (ended) affected.push_back(t);
        }
        break;
    }
    case SpellEffectKind::Revive: {
        for (Combatant* t : T) {
            if (!t->dead || t->died_round == 0 || int(s.round) - int(t->died_round) > int(def.revive_within_rounds)) continue;
            t->dead = false; t->hp = 1; t->death = DeathSaves{}; t->died_round = 0; t->death_triggers_done = false;
            if (t->has(Condition::Unconscious)) {
                remove_condition(*t, Condition::Unconscious);
                emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(Condition::Unconscious));
            }
            emit(ev, EventKind::Healed, a.id, t->id, 1, 1, 1);
            affected.push_back(t);
        }
        break;
    }
    case SpellEffectKind::EndCondition: {   // Lesser Restoration: one condition, the lowest Condition value present (the chosen one
        // when ActionRequest::condition_choice narrowed ends_conditions to it, see cast_spell)
        for (Combatant* t : T) {
            if (t->dead) continue;
            const uint32_t present = t->conditions & def.ends_conditions;
            if (present == 0) continue;
            Condition c = Condition::Blinded;
            for (int k = 0; k < 32; ++k) if (present & (uint32_t(1) << k)) { c = Condition(k); break; }
            bool via_effect = false;
            for (std::size_t i = 0; i < s.effects.size();) {
                if (s.effects[i].target == t->id && !s.effects[i].no_condition && s.effects[i].condition == c) { end_effect(s, i, ev); via_effect = true; }
                else ++i;
            }
            if (t->has(c)) {   // also when an effect still claims it, or it came from elsewhere
                remove_condition(*t, c);
                emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(c));
            } else if (!via_effect) continue;
            affected.push_back(t);
        }
        break;
    }
    case SpellEffectKind::DelayedDamage: case SpellEffectKind::SustainedDamage: {
        const bool sustained = def.kind == SpellEffectKind::SustainedDamage;
        const DiceExpr dice = scaled_dice(def, lvl, a.caster_level);
        for (Combatant* t : T) {
            if (t->dead) continue;
            if (sustained) {
                const int dmg = std::max(0, roll_dice(s.rng, dice));
                damage_creature(s, *t, zone_apply_damage(s, *t, int16_t(dmg), def.damage_type, def.magical ? uint8_t(kAttackMagical) : uint8_t(0)), false, ev);
            }
            ActiveSpellEffect e{def.id, a.id, t->id, Condition::Blinded, Ability::Wis, 0, RepeatSave::None, def.concentration,
                                make_timed(Expiry(def.expiry), def.duration_rounds, current_actor(s) == t->id), lvl, false};
            e.no_condition = true; e.damage_when = sustained ? 2 : 1; e.damage = dice; e.damage_type = def.damage_type;
            e.magical = def.magical; e.cast_stamp = (uint32_t(s.round) << 16) | s.turn_index;
            s.effects.push_back(e);
            affected.push_back(t);
        }
        if (def.concentration && !affected.empty()) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::Zone: {   // R-G11: a persistent zone / aura (primary effect only), then its Appear rows
        ActiveZone z;
        z.zone_id = s.next_zone_id++;
        z.spell_id = def.id; z.owner = a.id; z.slot_level = lvl; z.dc = int16_t(spell_save_dc(a, def));
        z.concentration = def.concentration;
        z.timed = make_timed(Expiry(def.expiry), def.duration_rounds, false);
        z.cast_stamp = turn_stamp(s);
        if (def.zone.anchor == ZoneAnchor::Point && ctx.has_zone_dest) { z.has_anchor_tile = true; z.anchor_x = ctx.zone_x; z.anchor_y = ctx.zone_y; }
        // PR 2: the size after upcasting (Fog Cloud), or the ring's; a wall's end tile, side and form
        z.ring = ctx.zone_ring;
        if (z.ring) z.size_ft = def.zone.ring_size_ft;
        else z.size_ft = uint16_t(std::min(65535, int(def.zone.size_ft) + int(def.zone.upcast_size_ft) * std::max(0, int(lvl) - int(def.level))));
        if (ctx.has_zone_end) { z.has_end_tile = true; z.end_x = ctx.zone_x2; z.end_y = ctx.zone_y2; }
        z.side = ctx.zone_side;
        if (ctx.zone_excluded) {
            z.excluded = *ctx.zone_excluded;
            std::sort(z.excluded.begin(), z.excluded.end());
            z.excluded.erase(std::unique(z.excluded.begin(), z.excluded.end()), z.excluded.end());
        }
        const uint32_t id = z.zone_id;
        s.zones.push_back(std::move(z));
        if (def.concentration) a.concentration_spell = def.id;
        emit(ev, EventKind::ZoneCreated, a.id, def.zone.anchor == ZoneAnchor::Creature ? a.id : 0, int32_t(id), int32_t(def.id),
             int32_t(def.zone.flags));
        const bool appear = std::any_of(def.zone.triggers.begin(), def.zone.triggers.end(), [](const ZoneTriggerRow& r) {
            return r.on == ZoneTrigger::Appear; });
        if (appear || (def.zone.flags & kZoneSilence)) {   // creatures in it when it appears, in initiative order
            std::vector<CombatantId> ids;
            for (const auto& o : s.order) ids.push_back(o.id);
            for (CombatantId c : ids) {
                if (appear) zone_event(s, &ctx, id, c, ZoneTrigger::Appear, true, false, ev);
                silence_deafen(s, id, c, ev);   // PR 2: Silence deafens who is completely inside
            }
        }
        break;
    }
    case SpellEffectKind::Polymorph: {   // R-G12: unwilling (another team): Wisdom save; then the form (build_form)
        for (Combatant* t : T) {
            if (t->dead || t->hp <= 0 || t->shapechanger || t->form != FormKind::None || ctx.form_archetype == 0) continue;
            if (t->team != a.team) {
                SaveSource src{def.magical, 0u, 0u};
                const SaveResult r = save_roll(s, *t, def.polymorph_save, spell_save_dc(a, def), RollMode::Normal, ev, &src);
                emit(ev, EventKind::SavingThrowRolled, t->id, CombatantId(def.polymorph_save), r.roll.natural, r.roll.total, int32_t(r.success));
                if (r.success) continue;
            }
            const auto f = s.hooks.build_form ? s.hooks.build_form(s, *t, FormKind::Polymorph, def.id, uint8_t(ctx.form_archetype - 1))
                                              : std::optional<FormStats>{};
            if (!f || !apply_form(*t, FormKind::Polymorph, *f)) continue;
            t->form_source = def.id; t->form_caster = a.id; t->form_archetype = ctx.form_archetype; t->form_end_round = 0;
            ++s.transforms;
            emit(ev, EventKind::Transformed, t->id, 0, int32_t(def.id), int32_t(ctx.form_archetype - 1), int32_t(FormKind::Polymorph));
            affected.push_back(t);
        }
        if (def.concentration && !affected.empty()) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::Summon: {   // R-G12 PR 2: built now, placed in the order by attempt after the step (Summoned there)
        if (!ctx.summon_archetypes || !s.hooks.build_summon) break;
        const std::vector<CombatantId> ids = next_summon_ids(s, ctx.summon_archetypes->size());
        ctx.assigned_summon_ids = ids;
        ctx.summon_spec = def.summon;
        for (std::size_t k = 0; k < ids.size(); ++k) {
            const uint8_t arch = (*ctx.summon_archetypes)[k];
            auto c = s.hooks.build_summon(s, a, def, lvl, arch);
            if (!c) continue;   // validated before anything was spent
            c->id = ids[k]; c->team = a.team;
            c->summoner = a.id; c->summon_source = def.id;
            c->summon_group = def.summon.initiative == SummonInitiative::Group ? ids[0] : ids[k];
            c->summon_control = uint8_t(def.summon.control); c->summon_vanishes = def.summon.vanishes_at_zero_hp;
            c->summon_concentration = def.concentration; c->commanded = 0; c->gone = false; c->dead = false;
            ctx.summons_pending.emplace_back(std::move(*c), arch);
        }
        if (def.concentration && !ctx.summons_pending.empty()) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::SummonWeapon: {   // R-G12 PR 3 (D3): the token, then (a creature target) its first attack
        const CombatantId id = next_summon_ids(s, 1)[0];
        ctx.assigned_summon_ids = {id};
        const uint32_t rounds = spell_rounds(def);
        ActiveSummonWeapon w{id, a.id, def.id, lvl, rounds ? uint32_t(s.round) + rounds : 0u};
        if (ctx.summon_tiles && !ctx.summon_tiles->empty()) {
            w.has_tile = true; w.tile_x = (*ctx.summon_tiles)[0].x; w.tile_y = (*ctx.summon_tiles)[0].y;
        }
        s.summon_weapons.push_back(w);
        s.next_summon_id = id + 1;
        ++s.summons;
        emit(ev, EventKind::Summoned, a.id, id, int32_t(def.id), 0, -1);
        if (T.size() == 1 && T[0] != &a && !T[0]->dead && summon_weapon_attack(s, ctx, a, def, lvl, *T[0], ev)) affected.push_back(T[0]);
        break;
    }
    case SpellEffectKind::AcBonus: case SpellEffectKind::Counter: break;   // reactions only
    }
    return affected;
}

// Applies a resolved (not countered) spell: the first effect, then each extra effect in order. A rider
// (affected_only) resolves over the targets the previous effect affected; others over every target.
void resolve_spell(EncounterState& s, Ctx& ctx, Combatant& a, const SpellDef& def, uint8_t lvl,
                   const std::vector<Combatant*>& T, bool within_5ft, std::vector<Event>& ev) {
    std::vector<Combatant*> affected = resolve_effect(s, ctx, a, def, lvl, T, within_5ft, ev);
    for (const SpellEffect& e : def.extra_effects) {
        const std::vector<Combatant*>& targets = e.affected_only ? affected : T;
        if (targets.empty()) { affected.clear(); continue; }
        const SpellDef view = effect_view(def, e);
        affected = resolve_effect(s, ctx, a, view, lvl, std::vector<Combatant*>(targets), within_5ft, ev);
    }
}

// CastSpell for a SpellBook spell: validates, spends the action and slot, runs the SpellCast
// window, then resolves the effect.
StepResult cast_spell_impl(EncounterState& s, Ctx& ctx, Combatant& a, TurnBudget b, const SpellDef& def,
                           const ActionRequest& req, int meta_cost) {
    if (req.zone_move != 0) {   // R-G11: move the actor's zone (Moonbeam); no slot, no SpellCast window
        if (req.sustain) return fail("zone_move: not with sustain");
        const ActiveZone* z = find_zone(s, req.zone_move);
        if (!z || z->owner != a.id || z->spell_id != def.id) return fail("zone_move: not a zone of this spell of yours");
        if (def.zone.move_cost == 0 || def.zone.anchor != ZoneAnchor::Point) return fail("zone_move: this zone cannot be moved");
        if (z->cast_stamp == turn_stamp(s)) return fail("zone_move: not on the turn it was cast");
        if (is_incapacitated(a)) return fail("incapacitated");
        if (req.zone_move_ft % 5 != 0 || req.zone_move_ft > def.zone.move_ft) return fail("zone_move: beyond the zone's move distance");
        if (!req.zone_excluded.empty()) return fail("zone_move: zone_excluded is chosen at the cast");
        if (req.has_zone_end || req.zone_side != 0 || req.zone_ring) return fail("zone_move: wall choices are made at the cast");
        if (req.metamagic != 0 || !req.spared.empty() || req.innate) return fail("zone_move: not a cast");
        if (def.zone.move_cost == 2) {
            if (!b.bonus_action) return fail("no bonus action available");
            b.bonus_action = false;
        } else if (!spend_action(b, a, ActionKind::CastSpell, false)) return fail("no action available");
        s.budget = b;
        ActiveZone* zm = find_zone_mut(s, req.zone_move);
        if (req.has_zone_dest) { zm->has_anchor_tile = true; zm->anchor_x = req.zone_x; zm->anchor_y = req.zone_y; }
        StepResult out{true, {}, {}};
        emit(out.events, EventKind::ZoneMoved, a.id, 0, int32_t(req.zone_move), int32_t(req.zone_move_ft));
        zone_changes(s, &ctx, req.zone_changes, out.events);   // only the final position counts (Q10)
        check_end(s, out.events);
        return out;
    }
    if (req.zone_move_ft != 0) return fail("zone_move_ft: zone_move only");
    if (req.sustain) {   // R-G15: Heat Metal's repeat damage with a bonus action, no slot, no SpellCast window
        Combatant* t = find_mut(s, req.target);
        if (!t || t->dead) return fail("bad target");
        const uint32_t now = (uint32_t(s.round) << 16) | s.turn_index;
        const ActiveSpellEffect* e = nullptr;
        for (const auto& x : s.effects)
            if (x.spell_id == def.id && x.caster == a.id && x.target == t->id && x.damage_when == 2) e = &x;
        if (!e) return fail("no sustained effect of that spell on the target");
        if (e->cast_stamp == now) return fail("cannot sustain on the turn it was cast");
        if (!b.bonus_action) return fail("no bonus action available");
        b.bonus_action = false;
        s.budget = b;
        StepResult out{true, {}, {}};
        const ActiveSpellEffect copy = *e;
        const int dmg = std::max(0, roll_dice(s.rng, copy.damage));
        damage_creature(s, *t, zone_apply_damage(s, *t, int16_t(dmg), copy.damage_type, copy.magical ? uint8_t(kAttackMagical) : uint8_t(0)), false, out.events, nullptr, &ctx);
        emit(out.events, EventKind::SpellResolved, a.id, t->id, int32_t(def.id), copy.slot_level);
        check_end(s, out.events);
        return out;
    }
    InnateSpell* inn = nullptr;
    if (req.innate) {
        for (auto& is : a.innate) if (is.spell_id == def.id) inn = &is;
        if (!inn) return fail("not an innate spell");
        if (!inn->available()) return fail("innate spell has no uses left");
        if (req.spell_level != def.level) return fail("innate spells cannot be upcast");
    } else if (!knows_spell(a, def.id)) return fail("spell not known");
    if (def.casting_time == CastingTime::Reaction) return fail("reaction spells are cast from reaction windows");
    if (def.casting_time == CastingTime::Minute || def.casting_time == CastingTime::Hour)
        return fail("casting time too long for an encounter");
    if (is_incapacitated(a)) return fail("incapacitated");
    const uint8_t lvl = req.spell_level;
    if (def.level == 0 && lvl != 0) return fail("cantrips use no slot");
    if (def.level > 0 && (lvl < def.level || lvl > 9)) return fail("bad slot level");
    if (!inn && !has_slot(a, lvl)) return fail("no spell slot of that level");
    // Usage limits (registered rows only; no row = unlimited, like features).
    ActionUse* au = nullptr; FeatureUses* fu = nullptr;
    const uint16_t pcost = def.pool_cost ? def.pool_cost : 1;
    if (def.usage_kind == 1) {
        for (auto& u : s.action_uses) if (u.actor == a.id && u.spell_id == def.id) au = &u;
        if (au && au->expended) return fail("action has not recharged");
    } else if (def.usage_kind == 2) {
        for (auto& f : s.features) if (f.actor == a.id && f.feature_id == def.id) fu = &f;
        if (fu && fu->max > 0 && fu->used >= fu->max) return fail("no uses left");
    } else if (def.usage_kind == 3) {
        if (find_pool(s, a.id, def.pool_id) && !can_spend(s, a.id, def.pool_id, pcost)) return fail("not enough resource");
    }
    // R-G11 PR 2: no verbal component while completely inside a Silence zone (checked before sim's component hook; no roll)
    if ((!inn || inn->as_listed) && (def.components & kComponentV) && !s.zones.empty() && in_silence(s, a.id))
        return fail("silence: no verbal component inside a Silence zone");
    if ((!inn || inn->as_listed) && s.hooks.can_provide_components && !s.hooks.can_provide_components(s, a, def)) return fail("components unavailable");
    std::vector<CombatantId> ids = req.targets;
    if (ids.empty() && req.target != 0) ids.push_back(req.target);
    if (ids.empty() && (def.kind == SpellEffectKind::Teleport || def.kind == SpellEffectKind::Zone)) ids.push_back(a.id);   // R-G9 / R-G11: self
    if (ids.empty()) return fail("target required");
    std::vector<Combatant*> T;
    for (CombatantId id : ids) {
        Combatant* t = find_mut(s, id);
        if (!t || (t->dead && def.kind != SpellEffectKind::Revive)) return fail("bad target");
        if (def.kind == SpellEffectKind::Revive) {
            if (!t->dead) return fail("target is not dead");
            if (t->died_round == 0 || int(s.round) - int(t->died_round) > int(def.revive_within_rounds)) return fail("dead too long");
        }
        if (!valid_target_type(def, *t)) return fail("invalid creature type for this spell");
        if (s.hooks.spell_target_ok && !s.hooks.spell_target_ok(s, a, def, *t)) return fail("invalid spell target");
        T.push_back(t);
    }
    auto distinct = [&] {
        for (std::size_t i = 0; i < T.size(); ++i)
            for (std::size_t j = i + 1; j < T.size(); ++j) if (T[i] == T[j]) return false;
        return true;
    };
    switch (def.kind) {
    case SpellEffectKind::SpellAttack: {
        const int beams = scaled_beams(def, lvl, a.caster_level);
        if (T.size() != 1 && int(T.size()) != beams) return fail("one target, or one per beam");
        for (Combatant* t : T) if (t == &a) return fail("bad target");
        break;
    }
    case SpellEffectKind::Save: case SpellEffectKind::Heal: case SpellEffectKind::ApplyCondition: case SpellEffectKind::ApplyModifier:
    case SpellEffectKind::HpPool: case SpellEffectKind::Dispel: case SpellEffectKind::Revive:
    case SpellEffectKind::EndCondition: case SpellEffectKind::DelayedDamage: case SpellEffectKind::SustainedDamage:
    case SpellEffectKind::ForcedMove:
        if ((def.area == AreaShape::None && int(T.size()) > scaled_targets(def, lvl)) || !distinct())
            return fail("too many targets");
        break;
    case SpellEffectKind::AutoHit: {
        const int darts = scaled_darts(def, lvl);
        if (def.distribution == DartDistribution::Single && T.size() != 1) return fail("all darts at one creature");
        if (def.distribution == DartDistribution::Distinct && (int(T.size()) != darts || !distinct()))
            return fail("one different creature per dart");
        if (T.size() != 1 && int(T.size()) != darts) return fail("one target, or one per dart");
        break;
    }
    case SpellEffectKind::AcBonus: case SpellEffectKind::Counter:
        return fail("reaction spells are cast from reaction windows");
    case SpellEffectKind::Teleport:   // R-G9: self only (Dimension Door companions are deferred)
        if (T.size() != 1 || T[0] != &a) return fail("teleport: self only");
        break;
    case SpellEffectKind::Polymorph: {   // R-G12 (D13): no effect on a shapechanger or a creature with 0 hit points: refused
        if (T.size() != 1) return fail("polymorph: one target");
        const Combatant& t = *T[0];
        if (t.shapechanger) return fail("polymorph: target is a shapechanger");
        if (t.hp <= 0) return fail("polymorph: target has 0 hit points");
        if (t.form != FormKind::None) return fail("polymorph: target already transformed");
        if (req.form_archetype == 0) return fail("form_archetype required");
        if (!s.hooks.build_form || !s.hooks.build_form(s, t, FormKind::Polymorph, def.id, uint8_t(req.form_archetype - 1)))
            return fail("form not available");
        break;
    }
    case SpellEffectKind::Summon: {   // R-G12 PR 2 (D1): the caster's choice of creatures; ids come from the counter
        if (T.size() != 1 || T[0] != &a) return fail("summon: no creature targets");
        const auto& ar = req.summon_archetypes;
        if (ar.empty() || ar.size() > def.summon.max_count) return fail("summon: bad creature count");
        if (s.hooks.summon_choice_ok && !s.hooks.summon_choice_ok(s, a, def, lvl, ar)) return fail("summon: choice not allowed");
        for (uint8_t k : ar)
            if (!s.hooks.build_summon || !s.hooks.build_summon(s, a, def, lvl, k)) return fail("summon not available");
        if (!req.summon_tiles.empty() && req.summon_tiles.size() != ar.size()) return fail("summon_tiles: one per creature");
        if (!ctx.replay_only && !req.summon_ids.empty()) return fail("summon_ids: assigned by rules (leave empty)");
        if (ctx.replay_only && req.summon_ids != next_summon_ids(s, ar.size())) return fail("summon_ids: do not match the summon counter");
        break;
    }
    case SpellEffectKind::SummonWeapon:   // R-G12 PR 3: target = the creature its first attack hits, or the caster (no attack)
        if (T.size() != 1) return fail("spiritual weapon: one target");
        if (T[0] != &a && T[0]->dead) return fail("target is dead");
        if (req.summon_tiles.size() != 1) return fail("spiritual weapon: one tile");   // the spawn tile (sim checks 60 ft)
        if (!ctx.replay_only && !req.summon_ids.empty()) return fail("summon_ids: assigned by rules (leave empty)");
        if (ctx.replay_only && req.summon_ids != next_summon_ids(s, 1)) return fail("summon_ids: do not match the summon counter");
        break;
    case SpellEffectKind::Zone:   // R-G11: cast at a point (zone_x / zone_y) or on the caster; no creature targets
        if (T.size() != 1 || T[0] != &a) return fail("zone: no creature targets");
        if (req.has_zone_dest && def.zone.anchor == ZoneAnchor::Creature) return fail("zone: an aura has no destination");
        break;
    }
    for (const SpellEffect& x : def.extra_effects)
        if (x.kind == SpellEffectKind::Zone) return fail("zone: primary effect only");
    for (const SpellEffect& x : def.extra_effects)
        if (x.kind == SpellEffectKind::Polymorph) return fail("polymorph: primary effect only");
    for (const SpellEffect& x : def.extra_effects)
        if (x.kind == SpellEffectKind::Summon) return fail("summon: primary effect only");
    for (const SpellEffect& x : def.extra_effects)
        if (x.kind == SpellEffectKind::SummonWeapon) return fail("spiritual weapon: primary effect only");
    if (!req.summon_archetypes.empty() && def.kind != SpellEffectKind::Summon) return fail("summon: not a summon spell");
    if (!req.summon_tiles.empty() && def.kind != SpellEffectKind::Summon && def.kind != SpellEffectKind::SummonWeapon)
        return fail("summon: not a summon spell");
    if (!req.summon_ids.empty() && def.kind != SpellEffectKind::Summon && def.kind != SpellEffectKind::SummonWeapon)
        return fail("summon: not a summon spell");
    if (req.form_archetype != 0 && def.kind != SpellEffectKind::Polymorph) return fail("form_archetype: not a polymorph spell");
    if ((req.has_zone_dest || !req.zone_excluded.empty()) && def.kind != SpellEffectKind::Zone) return fail("zone: not a zone spell");
    if (!req.zone_excluded.empty() && !def.zone.chooses_excluded) return fail("zone_excluded: the spell does not let the caster choose");
    if ((req.has_zone_end || req.zone_side != 0 || req.zone_ring) && def.kind != SpellEffectKind::Zone) return fail("zone: not a zone spell");
    if (def.kind == SpellEffectKind::Zone) {   // R-G11 PR 2: wall choices (Wall of Fire)
        if (req.zone_ring && def.zone.ring_size_ft == 0) return fail("zone_ring: the spell has no ring form");
        if (req.has_zone_end && (def.zone.shape != AreaShape::Line || req.zone_ring)) return fail("zone_end: only a line wall has an end tile");
        if (def.zone.sided ? (req.zone_side != 1 && req.zone_side != -1) : req.zone_side != 0)
            return fail(def.zone.sided ? "zone_side: choose the damaging side (+1 or -1)" : "zone_side: the spell has no damaging side");
    }
    if (req.forced_feet != 0) {   // R-G9: the chosen distance of an "up to N ft" forced move
        int most = -1;
        if (def.kind == SpellEffectKind::ForcedMove && def.forced.up_to) most = def.forced.feet;
        for (const SpellEffect& x : def.extra_effects)
            if (x.kind == SpellEffectKind::ForcedMove && x.forced.up_to) most = std::max(most, int(x.forced.feet));
        if (most < 0) return fail("forced_feet: no 'up to' forced move in this spell");
        if (req.forced_feet % 5 != 0 || int(req.forced_feet) > most) return fail("forced_feet: a multiple of 5 up to the effect's distance");
    }
    if (req.teleport_feet != 0 || req.has_teleport_dest) {   // R-G9: sim validated the destination; rules checks the distance budget
        if (def.kind != SpellEffectKind::Teleport) return fail("teleport: not a teleport spell");
        if (req.teleport_feet % 5 != 0 || req.teleport_feet > def.teleport.max_feet) return fail("teleport: beyond the spell's range");
    }
    if (def.max_spread_ft > 0 && ids.size() > 1 && s.hooks.targets_within && !s.hooks.targets_within(s, ids, def.max_spread_ft))
        return fail("targets too far apart");
    if (def.casting_time == CastingTime::BonusAction) {
        if (!b.bonus_action) return fail("no bonus action available");
        b.bonus_action = false;
    } else if (!spend_action(b, a, ActionKind::CastSpell, false)) return fail("no action available");
    s.budget = b;
    if (inn) {   // one use; "Recharge N-6" is a single use
        if (inn->recharge_min > 0) inn->uses_left = 0;
        else if (inn->uses_max > 0) --inn->uses_left;
    } else spend_slot(a, lvl);
    // An innate block with its own ability casts with that ability for this resolution.
    struct AbilitySwap {
        Combatant& c; Ability saved; bool on;
        ~AbilitySwap() { if (on) c.spellcasting_ability = saved; }
    } swap{a, a.spellcasting_ability, inn && inn->has_ability};
    if (swap.on) a.spellcasting_ability = inn->ability;
    if (au) au->expended = true;
    if (fu && fu->max > 0) ++fu->used;
    StepResult out{true, {}, {}};
    if (def.concentration && a.concentration_spell != 0) end_concentration(s, a.id, out.events);
    if (def.is_spell) emit(out.events, EventKind::SpellCast, a.id, ids[0], int32_t(def.id), lvl);
    else emit(out.events, EventKind::ActionUsed, a.id, ids[0], int32_t(def.id), 0, def.usage_kind);   // not a spell: no SpellCast window
    if (def.usage_kind == 3 && spend_pool(s, a.id, def.pool_id, pcost))
        emit(out.events, EventKind::ResourceSpent, a.id, 0, int32_t(def.pool_id), pcost, find_pool(s, a.id, def.pool_id)->cur);
    if (meta_cost > 0 && spend_pool(s, a.id, a.sorcery_pool, uint16_t(meta_cost)))   // R-G14: sorcery points for Metamagic
        emit(out.events, EventKind::ResourceSpent, a.id, 0, int32_t(a.sorcery_pool), meta_cost, find_pool(s, a.id, a.sorcery_pool)->cur);
    bool countered = def.is_spell && spell_cast_window(s, ctx, a, def.id, lvl, ids[0], out.events);
    if (!countered) {
        const bool careful = (req.metamagic & kMetaCareful) != 0;
        if (!req.spared.empty()) { ctx.spared = &req.spared; ctx.spared_no_damage = !careful; }
        ctx.heighten = (req.metamagic & kMetaHeightened) ? req.heighten_target : 0;
        ctx.empower = (req.metamagic & kMetaEmpowered) ? std::max(1, modifier(a.abilities, Ability::Cha)) : 0;
        ctx.forced_feet = req.forced_feet; ctx.teleport_feet = req.teleport_feet;
        ctx.has_teleport_dest = req.has_teleport_dest; ctx.teleport_x = req.teleport_x; ctx.teleport_y = req.teleport_y;
        ctx.has_zone_dest = req.has_zone_dest; ctx.zone_x = req.zone_x; ctx.zone_y = req.zone_y;
        ctx.zone_excluded = req.zone_excluded.empty() ? nullptr : &req.zone_excluded;
        ctx.has_zone_end = req.has_zone_end; ctx.zone_x2 = req.zone_x2; ctx.zone_y2 = req.zone_y2;
        ctx.zone_side = req.zone_side; ctx.zone_ring = req.zone_ring;
        ctx.form_archetype = req.form_archetype;
        ctx.summon_archetypes = &req.summon_archetypes; ctx.summon_tiles = &req.summon_tiles;
        ctx.ranged_disadvantage = req.beyond_normal_range || req.ranged_hostile_within_5ft;   // EC spider follow-up (flagged)
        resolve_spell(s, ctx, a, def, lvl, T, req.target_within_5ft, out.events);
        ctx.ranged_disadvantage = false;
        ctx.spared = nullptr; ctx.spared_no_damage = false; ctx.heighten = 0; ctx.empower = 0;
        ctx.forced_feet = 0; ctx.teleport_feet = 0; ctx.has_teleport_dest = false; ctx.teleport_x = 0; ctx.teleport_y = 0;
        ctx.has_zone_dest = false; ctx.zone_x = 0; ctx.zone_y = 0; ctx.zone_excluded = nullptr;
        ctx.has_zone_end = false; ctx.zone_x2 = 0; ctx.zone_y2 = 0; ctx.zone_side = 0; ctx.zone_ring = false;
        ctx.form_archetype = 0; ctx.summon_archetypes = nullptr; ctx.summon_tiles = nullptr;
        defer_forced(s, ctx, out.events);   // R-G9: placeholders after every save and damage of the spell, before SpellResolved
    }
    emit(out.events, countered ? EventKind::SpellCountered : EventKind::SpellResolved, a.id, ids[0], int32_t(def.id), lvl);
    check_end(s, out.events);
    return out;
}

// R-G14: validates the per-cast options and resolves the spell as modified by them (a copy of `def`); no option = as before.
StepResult cast_spell(EncounterState& s, Ctx& ctx, Combatant& a, TurnBudget b, const SpellDef& def, const ActionRequest& req) {
    const bool any = req.metamagic != 0 || !req.spared.empty() || req.heighten_target != 0 || req.damage_choice != 0 ||
                     req.condition_choice != 0 || !def.damage_choices.empty() || !def.condition_choices.empty();
    if (!any || req.sustain || req.zone_move != 0) return cast_spell_impl(s, ctx, a, b, def, req, 0);
    SpellDef eff = def;
    const uint8_t lvl = req.spell_level;
    // Cast-time choices (Chromatic Orb, Blindness/Deafness).
    if (req.damage_choice != 0) {
        if (std::size_t(req.damage_choice) > def.damage_choices.size()) return fail("bad damage choice");
        eff.damage_type = def.damage_choices[std::size_t(req.damage_choice - 1)];
    }
    if (req.condition_choice != 0 && def.kind == SpellEffectKind::EndCondition) {
        // Lesser Restoration: 1 + index into ends_condition_list (the spell's JSON list order); each target must have the chosen
        // one. Refused before anything is spent. No choice = the lowest listed condition present (as before).
        std::vector<Condition> listed = def.ends_condition_list;
        if (listed.empty())   // a def built without the loader: mask order
            for (int k = 0; k < 32; ++k) if (def.ends_conditions & (uint32_t(1) << k)) listed.push_back(Condition(k));
        uint32_t chosen = 0;
        if (std::size_t(req.condition_choice) <= listed.size()) chosen = bit(listed[std::size_t(req.condition_choice - 1)]) & def.ends_conditions;
        if (chosen == 0) return fail("bad condition choice");
        auto has_it = [&](CombatantId id) { const Combatant* t = find(s, id); return t != nullptr && (t->conditions & chosen) != 0; };
        if (req.target != 0 && !has_it(req.target)) return fail("target does not have the chosen condition");
        for (CombatantId id : req.targets) if (!has_it(id)) return fail("target does not have the chosen condition");
        if (req.target == 0 && req.targets.empty()) return fail("target does not have the chosen condition");
        eff.ends_conditions = chosen;
    } else if (req.condition_choice != 0) {
        if (std::size_t(req.condition_choice) > def.condition_choices.size()) return fail("bad condition choice");
        eff.condition = def.condition_choices[std::size_t(req.condition_choice - 1)];
    }
    if (!def.damage_choices.empty() && req.damage_choice == 0) return fail("choose a damage type");
    if (!def.condition_choices.empty() && req.condition_choice == 0) return fail("choose a condition");
    const uint8_t m = req.metamagic;
    if (req.heighten_target != 0 && !(m & kMetaHeightened)) return fail("heighten_target needs Heightened Spell");
    if (m != 0) {
        if (!def.is_spell) return fail("metamagic needs a spell");
        if ((m & ~a.metamagic_known) != 0) return fail("metamagic option not known");
        const uint8_t primary = uint8_t(m & ~kMetaEmpowered);
        if ((primary & (primary - 1)) != 0) return fail("one metamagic option per spell (Empowered may be added)");
        const int cost = metamagic_cost(m, lvl);
        if (a.sorcery_pool == 0 || !can_spend(s, a.id, a.sorcery_pool, uint16_t(cost))) return fail("not enough sorcery points");
        if (m & kMetaCareful) {
            if (def.kind != SpellEffectKind::Save) return fail("Careful Spell needs a saving throw");
            if (req.spared.empty() || int(req.spared.size()) > std::max(1, modifier(a.abilities, Ability::Cha)))
                return fail("Careful Spell: 1 to Cha modifier creatures");
        }
        if (m & kMetaDistant) {
            if (def.range_ft < 5) return fail("Distant Spell needs a range of 5 ft or more");
            eff.range_ft = uint16_t(def.range_ft == 5 ? 30 : std::min<int>(def.range_ft * 2, 65535));
        }
        if (m & kMetaExtended) {
            const bool long_enough = def.duration == SpellDuration::Minutes || def.duration == SpellDuration::Hours ||
                                     def.duration_rounds >= 10;
            if (!long_enough) return fail("Extended Spell needs a duration of 1 minute or longer");
            eff.duration_amount = uint16_t(std::min<int>(def.duration_amount * 2, 65535));
            eff.duration_rounds = uint16_t(std::min<int>(def.duration_rounds * 2, 65535));
            eff.modifier.rounds = uint16_t(std::min<int>(def.modifier.rounds * 2, 65535));
            for (auto& x : eff.extra_effects) {
                x.duration_rounds = uint16_t(std::min<int>(x.duration_rounds * 2, 65535));
                x.modifier.rounds = uint16_t(std::min<int>(x.modifier.rounds * 2, 65535));
            }
        }
        if (m & kMetaHeightened) {
            if (def.kind != SpellEffectKind::Save) return fail("Heightened Spell needs a saving throw");
            if (req.heighten_target == 0) return fail("Heightened Spell needs heighten_target");
            const bool listed = req.heighten_target == req.target ||
                                std::find(req.targets.begin(), req.targets.end(), req.heighten_target) != req.targets.end();
            if (!listed) return fail("heighten_target must be a target of the spell");
        }
        if (m & kMetaQuickened) {
            if (def.casting_time != CastingTime::Action) return fail("Quickened Spell needs a casting time of 1 action");
            eff.casting_time = CastingTime::BonusAction;
        }
        if (m & kMetaSubtle) eff.components = uint8_t(def.components & ~(kComponentV | kComponentS));
        if (m & kMetaTwinned) {
            const bool single = def.area == AreaShape::None && def.range_ft > 0 &&
                (def.kind == SpellEffectKind::SpellAttack ? scaled_beams(def, lvl, a.caster_level) == 1
                                                          : scaled_targets(def, lvl) == 1);
            const bool kind_ok = def.kind == SpellEffectKind::SpellAttack || def.kind == SpellEffectKind::Save ||
                                 def.kind == SpellEffectKind::Heal || def.kind == SpellEffectKind::ApplyCondition ||
                                 def.kind == SpellEffectKind::ApplyModifier;
            if (!single || !kind_ok) return fail("Twinned Spell needs a spell that targets only one creature");
            if (req.targets.size() != 2 || req.targets[0] == req.targets[1]) return fail("Twinned Spell needs two different targets");
            if (def.kind == SpellEffectKind::SpellAttack) { eff.beams = 2; eff.upcast_beams = 0; eff.cantrip_adds_beams = false; }
            else { eff.targets = 2; eff.upcast_targets = 0; }
        }
    }
    if (!req.spared.empty() && !(m & kMetaCareful)) {   // Sculpt Spells
        if (!a.sculpt_spells || !def.evocation) return fail("spared creatures need Careful Spell or Sculpt Spells");
        if (def.kind != SpellEffectKind::Save) return fail("Sculpt Spells needs a saving throw");
        if (int(req.spared.size()) > 1 + int(lvl)) return fail("Sculpt Spells: up to 1 + spell level creatures");
    }
    for (std::size_t i = 0; i < req.spared.size(); ++i)
        for (std::size_t j = i + 1; j < req.spared.size(); ++j)
            if (req.spared[i] == req.spared[j]) return fail("spared creatures must be different");
    return cast_spell_impl(s, ctx, a, b, eff, req, m != 0 ? metamagic_cost(m, lvl) : 0);
}

} // namespace

// ---- Queries & policies ----

const Combatant* find(const EncounterState& s, CombatantId id) {
    for (const auto& c : s.combatants) if (c.id == id) return &c;
    return nullptr;
}

CombatantId current_actor(const EncounterState& s) {
    return s.order.empty() ? 0 : s.order[s.turn_index].id;
}

bool is_conscious(const Combatant& c) {
    return !c.dead && c.hp > 0 && !c.has(Condition::Unconscious);
}

PcState pc_state(const Combatant& c) {
    if (c.dead) return PcState::Dead;
    if (is_conscious(c)) return PcState::Conscious;
    if (c.hp <= 0) return c.death.stable ? PcState::Stable : PcState::Dying;
    return PcState::Unconscious;   // above 0 HP but Unconscious (e.g. magical sleep)
}

std::vector<PcStatus> pc_statuses(const EncounterState& s) {
    std::vector<PcStatus> out;
    for (const auto& c : s.combatants) if (c.is_pc) out.push_back({c.id, pc_state(c)});
    return out;
}

std::optional<EncounterOutcome> end_when_no_pc_conscious(const EncounterState& s) {
    for (const auto& c : s.combatants) if (c.is_pc && is_conscious(c)) return std::nullopt;
    return EncounterOutcome{EndReason::NoPcConscious, pc_statuses(s)};
}

std::optional<EncounterOutcome> default_end_condition(const EncounterState& s) {
    std::array<bool, 256> present{}, conscious{};
    int teams = 0;
    for (const auto& c : s.combatants) {
        if (!present[c.team]) { present[c.team] = true; ++teams; }
        if (is_conscious(c) || (c.regen_down && !c.dead)) conscious[c.team] = true;   // a regenerator at 0 HP is still in the fight
    }
    if (teams < 2) return EncounterOutcome{EndReason::InvalidSetup, pc_statuses(s)};
    std::vector<uint8_t> down;
    for (int t = 0; t < 256; ++t) if (present[std::size_t(t)] && !conscious[std::size_t(t)]) down.push_back(uint8_t(t));
    if (down.empty()) return std::nullopt;
    return EncounterOutcome{EndReason::TeamNoConscious, pc_statuses(s), std::move(down)};
}

std::optional<EncounterOutcome> end_when_no_pc_or_no_opponent(const EncounterState& s) {
    if (auto o = end_when_no_pc_conscious(s)) return o;
    for (const auto& c : s.combatants) if (!c.is_pc && is_conscious(c)) return std::nullopt;
    return EncounterOutcome{EndReason::NoOpponentConscious, pc_statuses(s)};
}

// ---- Driving ----

EncounterState begin_encounter(std::vector<Combatant> cs, uint64_t seed, std::vector<Event>* out,
                               EncounterHooks hooks) {
    EncounterState s;
    s.hooks = std::move(hooks);
    s.rng = rng::Rng(seed, 0);
    s.combatants = std::move(cs);
    s.order = roll_initiative(s.rng, s.combatants);
    std::vector<Event> ev;
    for (const auto& e : s.order) emit(ev, EventKind::InitiativeRolled, e.id, 0, e.total);
    if (!s.order.empty()) {
        if (find_mut(s, s.order[0].id)->dead) advance_turn(s, ev);
        else start_turn(s, ev);
    } else {
        check_end(s, ev);   // no combatants: InvalidSetup under the default policy
    }
    if (out) *out = std::move(ev);
    return s;
}

namespace {

// R-G12 PR 2: the creatures of this step's Summon cast join the encounter and the turn order. Initiative: d20 + Dex each (Own),
// or one roll and one slot (the first creature's Dex, carried by every member) for the cast's group, members consecutive by id (D2). A creature goes after every entry
// with a higher total, or an equal total and an equal or higher Dex modifier. Inserted at or before the current turn: the
// current actor keeps its turn (turn_index + 1, stamps rewritten); a slot that has passed acts next round (D14).
// Summoned actor=summoner target=creature a=spell b=archetype c=initiative total, in id order, after the cast's events.
void place_summons(EncounterState& s, Ctx& ctx, CombatantId summoner, std::vector<Event>& ev) {
    if (ctx.summons_pending.empty()) return;
    const bool group = ctx.summon_spec.initiative == SummonInitiative::Group;
    int16_t shared = 0;
    const int8_t group_dex = int8_t(modifier(ctx.summons_pending[0].first.abilities, Ability::Dex));
    if (group) shared = roll_d20(s.rng, group_dex, RollMode::Normal).total;
    uint32_t source = 0;
    for (auto& [c, arch] : ctx.summons_pending) {
        // D2: a group shares one slot, so every member carries the group's Dex (mixed stat blocks stay consecutive by id)
        const int8_t dex = group ? group_dex : int8_t(modifier(c.abilities, Ability::Dex));
        const int16_t total = group ? shared : roll_d20(s.rng, dex, RollMode::Normal).total;
        std::size_t pos = 0;
        while (pos < s.order.size() && (s.order[pos].total > total || (s.order[pos].total == total && s.order[pos].dex_mod >= dex))) ++pos;
        s.order.insert(s.order.begin() + std::ptrdiff_t(pos), InitiativeEntry{c.id, total, dex});
        if (pos <= s.turn_index) { ++s.turn_index; shift_turn_stamps(s, pos); }
        const CombatantId id = c.id;
        source = c.summon_source;
        s.combatants.push_back(std::move(c));
        ++s.summons;
        s.next_summon_id = id + 1;
        emit(ev, EventKind::Summoned, summoner, id, int32_t(source), arch, total);
    }
    ctx.summons_pending.clear();
}

// R-G12 PR 2: a summon that disappears at 0 HP ends (SummonEnded b = 3) once it drops (it still dies first if killed outright).
void sweep_summons(EncounterState& s, std::vector<Event>& ev) {
    if (s.summons == 0) return;
    for (auto& c : s.combatants)
        if (c.summoner != 0 && !c.gone && c.summon_vanishes && (c.dead || c.hp <= 0)) end_summon(s, c, 3, ev);
}

// One deterministic attempt at `req` with the decisions known so far. If an Ask poll has no answer,
// the state is restored to `before` and the pending decision is stored.
StepResult attempt(EncounterState& s, const ActionRequest& req, const std::vector<ReactionRecord>& decided,
                   bool replay_only, const std::vector<std::pair<uint32_t, ForcedMoveResult>>* forced_recorded = nullptr) {
    EncounterState before = s;
    std::vector<ReactionRecord> records;
    Ctx ctx{records, &decided, 0, replay_only, false, std::nullopt, 0};
    ctx.forced_recorded = forced_recorded;
    StepResult r = step_impl(s, ctx, req);
    if (r.ok) place_summons(s, ctx, req.actor, r.events);   // R-G12 PR 2: after SpellResolved, before Ready triggers
    else ctx.summons_pending.clear();
    if (r.ok) defer_forced(s, ctx, r.events);   // R-G9: forced moves queued outside a cast (on-hit riders), before Ready triggers
    else { ctx.forced_queue.clear(); ctx.forced_deferred.clear(); }
    if (r.ok && req.kind != ActionKind::Ready) fire_readies(s, ctx, r.events);
    if (!r.ok) return r;
    process_deaths(s, r.events, &ctx);
    sweep_concentration(s, r.events);
    sweep_summons(s, r.events);
    prune_sourced(s, r.events);
    for (auto& c : s.combatants) if (c.dead && c.died_round == 0) c.died_round = s.round;   // R-G15: Revivify's window
    if (!ctx.need) {
        finalize_forced(s, ctx, r.events);   // R-G9: after every decision point of the step
        if (ctx.zone_fired) {   // R-G11: zone triggers of the forced moves (damage, conditions) settle like the step's own
            process_deaths(s, r.events, &ctx);
            sweep_concentration(s, r.events);
            sweep_summons(s, r.events);
            prune_sourced(s, r.events);
            for (auto& c : s.combatants) if (c.dead && c.died_round == 0) c.died_round = s.round;
            check_end(s, r.events);
        }
    }
    if (ctx.need && !replay_only) {   // R-G11 (Q7): a zone trigger inside finalize_forced may also pause the step
        std::vector<Event> provisional = std::move(r.events);
        s = std::move(before);
        s.pending = PendingDecision{req, decided, ctx.need->window, ctx.need->reactor, ctx.need->options};
        s.pending->forced_results = std::move(ctx.forced_results);
        StepResult out{true, {}, std::move(provisional)};
        out.status = StepStatus::NeedsDecision;
        out.needs = std::move(ctx.need);
        out.reactions = decided;
        return out;
    }
    r.status = StepStatus::Done;
    if (s.ended && !before.ended) {   // R-G12 (D15): the teardown block runs up to EncounterEnded
        for (std::size_t i = r.events.size(); i-- > 0;) {
            if (r.events[i].kind != EventKind::EncounterEnded) continue;
            std::size_t k = i;
            while (k > 0 && (r.events[k - 1].kind == EventKind::Reverted || r.events[k - 1].kind == EventKind::SummonEnded) &&
                   r.events[k - 1].b == 5) --k;
            if (k < i) r.teardown_begin = k;
            break;
        }
    }
    r.reactions = std::move(records);
    r.desync = ctx.desync || ctx.replay_pos != decided.size();
    r.logged = LoggedStep{req, r.reactions};
    if (!replay_only && !ctx.assigned_summon_ids.empty()) r.logged->request.summon_ids = ctx.assigned_summon_ids;   // 0xBC
    return r;
}

// Synchronous adapter: answer NeedsDecision with hooks.decide_reaction until the request is done.
StepResult settle(EncounterState& s, StepResult r) {
    while (r.status == StepStatus::NeedsDecision && s.hooks.decide_reaction) {
        const auto& p = *s.pending;
        ReactionDecision d = s.hooks.decide_reaction(s, p.window, *find(s, p.reactor), p.options);
        r = step(s, decision_request(*r.needs, d));   // window_seq comes from the pending window
    }
    return r;
}

} // namespace

std::vector<CombatantId> default_poll_order(const EncounterState& s, const std::vector<CombatantId>& eligible) {
    std::vector<CombatantId> out;
    const std::size_t n = s.order.size();
    for (std::size_t k = 1; k <= n; ++k) {
        const CombatantId id = s.order[(s.turn_index + k) % n].id;
        if (std::find(eligible.begin(), eligible.end(), id) != eligible.end()) out.push_back(id);
    }
    return out;
}

std::optional<NeedsDecision> pending_decision(const EncounterState& s) {
    if (!s.pending) return std::nullopt;
    const auto& p = *s.pending;
    return make_needs_decision(p.window, p.reactor, p.options);
}

NeedsDecision make_needs_decision(const ReactionWindow& w, CombatantId reactor, std::vector<ReactionOption> options) {
    NeedsDecision n{w.seq, w.trigger, reactor, w.trigger_actor, std::move(options), w};
    n.target = w.target;
    if (w.trigger == ReactionTrigger::BeingHit) {
        n.attack_total = w.attack_total; n.target_ac = w.target_ac; n.attack_natural = w.attack_natural;
    }
    n.spell_id = w.spell_id;
    if (w.trigger == ReactionTrigger::SpellCast) n.spell_level = w.spell_level;
    return n;
}

ActionRequest heal_request(CombatantId target, int16_t amount) {
    ActionRequest r{0, ActionKind::Heal, target};
    r.amount = amount;
    return r;
}

ActionRequest check_request(CombatantId id, Skill k, int dc, RollMode mode) {
    ActionRequest r{id, ActionKind::AbilityCheck};
    r.skill = k; r.dc = int16_t(dc); r.check_mode = mode;
    return r;
}

ActionRequest decision_request(const NeedsDecision& n, ReactionDecision d) {
    ActionRequest r{n.reactor, ActionKind::ReactionDecision};
    r.decision = d;
    r.decision.window_seq = n.window_seq;
    return r;
}

ActionRequest decision_request(const NeedsDecision& n, bool react, uint8_t option) {
    return decision_request(n, ReactionDecision{n.window_seq, react, option});
}

StepResult step(EncounterState& s, const ActionRequest& req) {
    if (req.kind == ActionKind::ReactionDecision) {
        if (!s.pending) return fail("no pending reaction decision");
        const PendingDecision& p = *s.pending;
        if (req.actor != p.reactor || req.decision.window_seq != p.window.seq) return fail("decision does not match the pending window");
        if (req.decision.react && req.decision.option >= p.options.size()) return fail("bad option");
        auto decided = p.decided;
        decided.push_back(ReactionRecord{p.window.seq, p.window.trigger, p.reactor, req.decision});
        ActionRequest original = p.request;
        const auto recorded = p.forced_results;   // R-G11 (Q7): reused by the re-run (copied: s.pending is reset below)
        s.pending.reset();
        return settle(s, attempt(s, original, decided, false, recorded.empty() ? nullptr : &recorded));
    }
    if (s.pending) return fail("awaiting a reaction decision");
    return settle(s, attempt(s, req, {}, false));
}

StepResult step_logged(EncounterState& s, const ActionRequest& req, ActionLog& log) {
    StepResult r = step(s, req);
    if (r.status == StepStatus::Done) log.push_back(*r.logged);
    return r;
}

StepResult replay_step(EncounterState& s, const LoggedStep& e) {
    if (s.pending) return fail("awaiting a reaction decision");
    return attempt(s, e.request, e.reactions, true);
}

StepResult encounter_check(EncounterState& s, CombatantId id, Skill k, int dc, RollMode mode) {
    return step(s, check_request(id, k, dc, mode));
}

StepResult heal(EncounterState& s, CombatantId id, int16_t amount) { return step(s, heal_request(id, amount)); }

namespace {

// ---- R-G8: grapple, shove, escape ----
bool drags_grappled(const EncounterState& s, const Combatant& a) {
    for (const auto& e : s.sourced) {
        if (e.condition != Condition::Grappled || e.source != a.id) continue;
        const Combatant* t = find(s, e.target);
        if (t && !t->dead && int(a.size) - int(t->size) < 2) return true;   // no penalty against a creature two or more sizes smaller
    }
    return false;
}

Skill best_defence(const Combatant& c) {
    return skill_modifier(c, Skill::Athletics) >= skill_modifier(c, Skill::Acrobatics) ? Skill::Athletics : Skill::Acrobatics;
}

StepResult special_action(EncounterState& s, Ctx& ctx, Combatant& a, TurnBudget b, const ActionRequest& req) {
    StepResult out{true, {}, {}};
    if (req.special == 5) {   // Wake: shake a sleeper awake (ends its ends_on_damage effects); uses the action
        if (req.as_bonus_action) return fail("not a bonus action");
        Combatant* t = find_mut(s, req.target);
        if (!t || t == &a || t->dead) return fail("bad target");
        if (!req.target_within_5ft) return fail("target must be within 5 ft");
        bool asleep = false;
        for (const auto& e : s.effects) asleep = asleep || (e.target == t->id && e.ends_on_damage);
        if (!asleep) return fail("target is not under an effect that waking ends");
        if (!spend_action(b, a, ActionKind::Attack, false)) return fail("no action available");
        s.budget = b;
        for (std::size_t i = 0; i < s.effects.size();) {
            if (s.effects[i].target == t->id && s.effects[i].ends_on_damage) end_effect(s, i, out.events); else ++i;
        }
        return out;
    }
    if (req.special == 6) {   // EC spider follow-up (flagged): attack the web (an object) restraining req.target (itself too)
        Combatant* t = find_mut(s, req.target);
        if (!t || t->dead) return fail("bad target");
        if (req.as_bonus_action || req.multiattack != 0 || b.sequence != 0) return fail("an action's attack, no multiattack");
        std::size_t wi = s.effects.size();
        for (std::size_t i = 0; i < s.effects.size() && wi == s.effects.size(); ++i)
            if (s.effects[i].target == t->id && s.effects[i].object_hp > 0 && (req.spell_id == 0 || s.effects[i].spell_id == req.spell_id)) wi = i;
        if (wi == s.effects.size()) return fail("no web on that creature");
        if (req.attack_index >= a.attacks.size()) return fail("bad attack");
        const Attack& atk = a.attacks[req.attack_index];
        if (!atk.ranged && !req.target_within_5ft && t != &a) return fail("target must be within 5 ft");   // sim checks reach / range
        if (b.attacks_remaining > 0) --b.attacks_remaining;
        else if (spend_action(b, a, ActionKind::Attack, false)) {
            b.loading_fired = 0;
            b.attacks_remaining = uint8_t(a.attacks_per_action > 1 ? a.attacks_per_action - 1 : 0);
        } else return fail("no action available");
        s.budget = b;
        // an object: no target conditions, no reactions, no triggers. The attacker's own disadvantage: poisoned, restrained,
        // blinded, prone (melee too: simplified), and the ranged facts.
        const bool dis = a.has(Condition::Poisoned) || a.has(Condition::Restrained) || a.has(Condition::Blinded) || a.has(Condition::Prone) ||
                         (atk.ranged && (req.beyond_normal_range || req.ranged_hostile_within_5ft));
        const D20Roll roll = roll_d20(s.rng, atk.to_hit + (atk.ranged ? a.attack_bonus_ranged : a.attack_bonus_melee),
                                      dis ? RollMode::Disadvantage : RollMode::Normal);
        ActiveSpellEffect& w = s.effects[wi];
        const bool crit = roll.natural == 20, hit = crit || (roll.natural != 1 && roll.total >= int(w.object_ac));
        // AttackResolved c bit 2 (4): the target was the web, not the creature
        emit(out.events, EventKind::AttackResolved, a.id, t->id, roll.natural, roll.total, int32_t(hit) | (int32_t(crit) << 1) | 4);
        if (hit) {
            auto part = [&](const DiceExpr& d, DamageType ty) {
                int n = std::max(0, roll_dice(s.rng, d, crit));
                if (w.object_immune & (1u << unsigned(ty))) return 0;
                if (w.object_vulnerable & (1u << unsigned(ty))) n *= 2;
                return n;
            };
            int dmg = part(atk.damage, atk.type);
            for (const DamagePart& p : atk.extra_damage) dmg += part(p.dice, p.type);
            w.object_hp = int16_t(std::max(0, int(w.object_hp) - dmg));
            if (w.object_hp == 0) end_effect(s, wi, out.events);   // destroyed: the restraint ends
        }
        check_end(s, out.events);
        return out;
    }
    if (req.special > 4) return fail("bad special attack");
    if (req.as_bonus_action) return fail("not a bonus action");
    if (req.multiattack != 0 || b.sequence != 0) return fail("a multiattack is in progress or declared");
    const bool escape = req.special == 4;
    Combatant* t = nullptr;
    SourcedCondition held{};
    bool has_held = false;
    if (!escape) {
        t = find_mut(s, req.target);
        if (!t || t == &a) return fail("bad target");
        if (t->dead) return fail("target is dead");
        if (charmed_by(s, a.id, t->id)) return fail("a charmed creature cannot attack its charmer");
        if (!req.target_within_5ft) return fail("target must be within 5 ft");
        if (!size_within_one(a.size, t->size)) return fail("target is more than one size larger");
        if (b.attacks_remaining > 0) --b.attacks_remaining;
        else if (spend_action(b, a, ActionKind::Attack, false)) {
            b.loading_fired = 0;
            b.attacks_remaining = uint8_t(a.attacks_per_action > 1 ? a.attacks_per_action - 1 : 0);
        } else return fail("no action available");
    } else {
        // R-G11: break free of a zone's "while in the zone" condition (Web: restrained): an action, a Strength check vs the
        // zone's DC. Chosen with spell_id, or when the creature is not grappled.
        std::size_t zi = s.effects.size();
        if (req.spell_id != 0 || !a.has(Condition::Grappled))
            for (std::size_t i = 0; i < s.effects.size() && zi == s.effects.size(); ++i) {
                const ActiveSpellEffect& e = s.effects[i];
                if ((e.zone_id != 0 || e.escape_check) && e.target == a.id && e.condition == Condition::Restrained && (req.spell_id == 0 || e.spell_id == req.spell_id)) zi = i;   // EC (flagged): + escape_check
            }
        if (zi < s.effects.size()) {
            if (!spend_action(b, a, ActionKind::Attack, false)) return fail("no action available");
            s.budget = b;
            const int zdc = s.effects[zi].dc;
            const uint32_t zone = s.effects[zi].zone_id;
            if (roll_ability_check(s, a, Ability::Str, zdc, out.events).success) {
                if (zone != 0) end_zone_effects(s, zone, a.id, out.events);
                else end_effect(s, zi, out.events);   // EC (flagged): a Web escape_check effect, only that one
            }
            check_end(s, out.events);
            return out;
        }
        if (req.spell_id != 0) return fail("not restrained by that spell's zone");
        if (!a.has(Condition::Grappled)) return fail("not grappled");
        for (const auto& e : s.sourced)
            if (!has_held && e.condition == Condition::Grappled && e.target == a.id && (req.target == 0 || e.source == req.target)) { held = e; has_held = true; }
        if (!has_held && req.dc <= 0) return fail("no grappler recorded and no escape dc");
        if (has_held) t = find_mut(s, held.source);
        if (!spend_action(b, a, ActionKind::Attack, false)) return fail("no action available");
    }
    s.budget = b;
    std::vector<Event>& ev = out.events;
    if (escape) {
        const int dc = has_held ? held.escape_dc : req.dc;
        bool free = false;
        if (dc > 0) free = roll_check(s, a, best_defence(a), dc, RollMode::Normal, ev).success;
        else {   // contest: the escaper rolls first, then the grappler must match or beat it
            auto first = roll_check(s, a, best_defence(a), 0, RollMode::Normal, ev);
            free = !t || !roll_check(s, *t, Skill::Athletics, first.roll.total, RollMode::Normal, ev).success;
        }
        if (free) {
            const CombatantId src = has_held ? held.source : 0;
            for (std::size_t i = 0; i < s.sourced.size();) {
                if (s.sourced[i].condition == Condition::Grappled && s.sourced[i].target == a.id && (src == 0 || s.sourced[i].source == src))
                    s.sourced.erase(s.sourced.begin() + std::ptrdiff_t(i));
                else ++i;
            }
            bool still = false;
            for (const auto& e : s.sourced) still = still || (e.condition == Condition::Grappled && e.target == a.id);
            if (!still) { remove_condition(a, Condition::Grappled); emit(ev, EventKind::ConditionRemoved, a.id, 0, int32_t(Condition::Grappled)); }
        }
        check_end(s, ev);
        return out;
    }
    // contest: the attacker rolls Athletics, the target the better of Athletics / Acrobatics and wins ties
    auto first = roll_check(s, a, Skill::Athletics, 0, RollMode::Normal, ev);
    const bool held_off = roll_check(s, *t, best_defence(*t), first.roll.total, RollMode::Normal, ev).success;
    if (!held_off) {
        if (req.special == 1) apply_sourced_condition(s, a.id, t->id, Condition::Grappled, 0, ev);
        else if (req.special == 2) {
            if (!t->has(Condition::Prone) && !immune_to(s, *t, Condition::Prone)) {
                add_condition(*t, Condition::Prone);
                emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(Condition::Prone));
            }
        } else {   // forced movement (R-G9): a = feet, target = the shover; c = 1 only with hooks.shove_forced_flag (EC #104)
            ForcedMoveQuery q; q.mover = t->id; q.source = a.id; q.mode = 1; q.feet = 5;
            ctx.forced_queue.push_back(q);
            defer_forced(s, ctx, ev, s.hooks.shove_forced_flag ? kMovedForced : 0);
        }
    }
    check_end(s, ev);
    return out;
}

// Petrifying Gaze (VERSION_BUMP_BATCH 7.2p): the turn-start answer. Averting: no save, gazers unseen until the next turn
// starts. Otherwise each gazer (initiative order) that is alive, not incapacitated and in req.gaze_seen applies its gaze
// SpellDef (a Save with escalation) to the actor, unless the actor already carries that gaze's effect or its end condition.
StepResult gaze_response(EncounterState& s, Combatant& a, const ActionRequest& req) {
    if (!s.budget.gaze_pending) return fail("no gaze pending");
    if (req.avert_gaze && a.surprised) return fail("surprised: cannot avert the eyes");
    StepResult out{true, {}, {}};
    s.budget.gaze_pending = false;
    if (req.avert_gaze) { a.gaze_averted = true; check_end(s, out.events); return out; }
    std::vector<ReactionRecord> recs;   // no reaction windows at turn start (as zone / turn_start triggers)
    Ctx local{recs};
    local.depth = 1;
    for (const auto& slot : s.order) {
        Combatant* g = find_mut(s, slot.id);
        if (!g || g->id == a.id || g->dead || g->gazes.empty() || is_incapacitated(*g)) continue;
        if (std::find(req.gaze_seen.begin(), req.gaze_seen.end(), g->id) == req.gaze_seen.end()) continue;
        for (const auto& gz : g->gazes) {
            const SpellDef* d = spell_def(s, gz.spell_id);
            if (!d || a.dead || a.hp <= 0) continue;
            if (d->escalates && a.has(d->escalate_to)) continue;   // already petrified
            const bool carrying = std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& e) {
                return e.spell_id == d->id && e.caster == g->id && e.target == a.id; });
            if (carrying) continue;   // already turning to stone: the repeat save decides
            resolve_effect(s, local, *g, *d, 0, std::vector<Combatant*>{&a}, false, out.events);
        }
    }
    check_end(s, out.events);
    return out;
}

// Small leftovers around the per-request core: Reckless Attack's declaration, Haste's extra action and Slow's limits.
StepResult step_impl_core(EncounterState& s, Ctx& ctx, const ActionRequest& req);
StepResult step_impl(EncounterState& s, Ctx& ctx, const ActionRequest& req) {
    const bool own_turn = !s.ended && !s.order.empty() && req.actor == current_actor(s) &&
                          req.kind != ActionKind::Heal && req.kind != ActionKind::AbilityCheck &&
                          req.kind != ActionKind::ReactionDecision && req.kind != ActionKind::OpportunityAttack;
    Combatant* a = own_turn ? find_mut(s, req.actor) : nullptr;
    if ((req.forced_feet != 0 || req.teleport_feet != 0 || req.has_teleport_dest) && req.kind != ActionKind::CastSpell)
        return fail("forced_feet / teleport: CastSpell only");
    if ((req.has_zone_dest || req.zone_move != 0 || req.zone_move_ft != 0 || !req.zone_excluded.empty() || req.has_zone_end ||
         req.zone_side != 0 || req.zone_ring) && req.kind != ActionKind::CastSpell)
        return fail("zone fields: CastSpell only");
    if (req.form_archetype != 0 && req.kind != ActionKind::CastSpell && req.kind != ActionKind::UseFeature)
        return fail("form_archetype: UseFeature or CastSpell only");
    if (!a || a->dead || req.kind == ActionKind::EndTurn) {
        if (req.haste_action) return fail("haste_action: not a turn action");
        if (req.reckless) return fail("reckless: Attack only");
        return step_impl_core(s, ctx, req);
    }
    if (req.reckless) {   // Reckless Attack: declared with the first attack of the turn
        if (req.kind != ActionKind::Attack || req.as_bonus_action || req.special != 0) return fail("reckless: Attack only");
        if (!a->reckless_attack) return fail("no Reckless Attack");
        if (a->reckless_active || s.budget.attacks_used_mask != 0 || s.budget.sequence != 0 || !s.budget.action)
            return fail("reckless: only on the first attack of the turn");
    }
    const bool slowed = !s.modifiers.empty() && has_marker(s, a->id, ModStat::Slow);
    if (slowed && req.kind == ActionKind::Attack && s.budget.slow_attacked) return fail("slowed: one attack per turn");
    const TurnBudget saved = s.budget;
    if (req.haste_action) {   // Haste: one weapon attack, Dash, Disengage or Hide, beside the action
        if (!s.budget.haste_action) return fail("no Haste action");
        const bool ok_kind = (req.kind == ActionKind::Attack && req.multiattack == 0 && req.special == 0 && req.bonus_option == 0) ||
                             req.kind == ActionKind::Dash || req.kind == ActionKind::Disengage || req.kind == ActionKind::Hide;
        if (!ok_kind || req.as_bonus_action) return fail("haste_action: Attack (one weapon attack), Dash, Disengage or Hide");
        s.budget.action = true; s.budget.extra_actions = 0; s.budget.attacks_remaining = 0;   // restored after
        s.budget.sequence = 0; s.budget.sequence_pos = 0;
    }
    const bool was_reckless = a->reckless_active;
    if (req.reckless) a->reckless_active = true;   // before the roll: this attack has advantage too
    StepResult r = step_impl_core(s, ctx, req);
    a = find_mut(s, req.actor);
    if (!r.ok) {
        s.budget = saved;
        if (a) a->reckless_active = was_reckless;
        return r;
    }
    if (req.haste_action) {   // the extra action is spent; the creature's own action, surges and attack sequence are as before
        s.budget.action = saved.action; s.budget.extra_actions = saved.extra_actions;
        s.budget.attacks_remaining = saved.attacks_remaining; s.budget.sequence = saved.sequence;
        s.budget.sequence_pos = saved.sequence_pos; s.budget.attacks_used_mask = saved.attacks_used_mask;
        s.budget.haste_action = false;
    }
    if (slowed && a && !a->dead && current_actor(s) == a->id) {   // Slow: an action or a bonus action, not both; one attack
        if ((saved.action && !s.budget.action) || (saved.bonus_action && !s.budget.bonus_action) ||
            (saved.haste_action && !s.budget.haste_action)) {
            s.budget.action = false; s.budget.bonus_action = false; s.budget.extra_actions = 0; s.budget.haste_action = false;
        }
        if (req.kind == ActionKind::Attack) {
            s.budget.slow_attacked = true; s.budget.attacks_remaining = 0; s.budget.sequence = 0; s.budget.sequence_pos = 0;
            s.budget.bonus_attack_grant = 0; s.budget.bonus_option = 0; s.budget.bonus_remaining = 0;
        }
    }
    return r;
}

StepResult step_impl_core(EncounterState& s, Ctx& ctx, const ActionRequest& req) {
    if (s.ended) return fail("encounter ended");
    if (s.order.empty()) return fail("no combatants");
    // Opportunity attacks are offered through LeavingReach reaction windows (Move.leaves_reach).
    if (req.kind == ActionKind::OpportunityAttack)
        return fail("opportunity attacks come from LeavingReach reaction windows");

    if (req.kind == ActionKind::Heal) {   // any time; from spells, potions, features
        Combatant* t = find_mut(s, req.target);
        if (!t) return fail("bad target");
        if (t->dead) return fail("target is dead");
        if (req.amount <= 0) return fail("amount must be positive");
        StepResult out{true, {}, {}};
        bool was_unconscious = t->has(Condition::Unconscious);
        auto h = heal_hp(s, *t, req.amount);
        commit_healing(*t, h);
        emit(out.events, EventKind::Healed, 0, t->id, h.healed, h.hp_after, h.from_zero);
        if (h.from_zero && was_unconscious)
            emit(out.events, EventKind::ConditionRemoved, t->id, 0, int32_t(Condition::Unconscious));
        check_end(s, out.events);
        return out;
    }
    if (req.kind == ActionKind::AbilityCheck) {   // any time; content/sim driven
        Combatant* c = find_mut(s, req.actor);
        if (!c || c->dead) return fail("bad combatant");
        StepResult out{true, {}, {}};
        roll_check(s, *c, req.skill, req.dc, req.check_mode, out.events);
        return out;
    }
    if (req.kind == ActionKind::ReactionDecision) return fail("no pending reaction decision");

    if (req.actor != current_actor(s)) return fail("not this combatant's turn");
    Combatant& a = *find_mut(s, req.actor);
    if ((a.dead || a.hp <= 0) && req.kind != ActionKind::EndTurn) return fail("actor is down");
    if (req.kind == ActionKind::GazeResponse) return gaze_response(s, a, req);   // Petrifying Gaze (7.2p): also while surprised
    if (s.budget.gaze_pending) return fail("gaze pending: answer with GazeResponse first");
    if (a.surprised && req.kind != ActionKind::EndTurn) return fail("surprised: can only end the turn");
    if (req.kind != ActionKind::CommandSummons && (req.command != 0 || !req.command_ids.empty()))
        return fail("command: CommandSummons only");
    if (req.kind != ActionKind::Attack && req.familiar_id != 0) return fail("familiar_id: Attack only");
    if (req.kind != ActionKind::CastSpell && (!req.summon_archetypes.empty() || !req.summon_ids.empty() ||
                                              (!req.summon_tiles.empty() && req.kind != ActionKind::SummonWeaponAttack)))
        return fail("summon: not a summon spell");
    // R-G12 PR 3 (SRD Find Familiar): a familiar never attacks on its own turn; Pact of the Chain's trade is its only attack
    if (a.summoner != 0 && a.summon_control == uint8_t(SummonControl::Telepathic) && req.kind == ActionKind::Attack)
        return fail("familiar_attack: a familiar cannot attack on its own turn");
    // R-G12 PR 2 (D4): an uncommanded (or told to defend) summon that takes commands only Dodges: SRD "it defends itself"
    if (awaits_command(a) && req.kind != ActionKind::EndTurn && req.kind != ActionKind::Dodge)
        return fail("summon not commanded: Dodge only");
    TurnBudget b = s.budget;               // mutate a copy; commit only on success
    StepResult out{true, {}, {}};

    // Target of a dying-creature action (Stabilize / Spare the Dying).
    auto dying_target = [&](Combatant*& t) -> std::string_view {
        t = find_mut(s, req.target);
        if (!t || t == &a) return "bad target";
        if (!is_dying(*t)) return "target is not dying";
        return {};
    };

    switch (req.kind) {
    case ActionKind::EndTurn:
        if (!s.zones.empty() && !a.dead) zone_turn_triggers(s, &ctx, a.id, ZoneTrigger::EndTurn, out.events);   // R-G11
        repeat_saves(s, a.id, RepeatSave::EndOfTurn, out.events);
        if (!s.modifiers.empty()) repeat_modifier_saves(s, a.id, out.events);   // Slow
        tick_effects_turn_end(s, a.id, out.events);
        if (!a.dead && !a.triggers.empty()) {   // turn_end triggers
            TrigCtx x;
            run_event_triggers(s, a, a, TriggerOn::TurnEnd, x, out.events, nullptr, &ctx);
        }
        emit(out.events, EventKind::TurnEnded, a.id);
        a.commanded = 0;   // R-G12 PR 2: a command lasts for the summon's one turn
        a.surprised = false;   // R-G16: surprise ends with the creature's first turn
        if (a.lethargy == 2) a.lethargy = 0;   // Haste lethargy lasts until after this turn
        advance_turn(s, out.events);
        return out;
    case ActionKind::Move: {
        if (req.move_ft <= 0) return fail("move_ft must be positive");
        int cost = req.move_ft * (a.has(Condition::Prone) ? 2 : 1);   // crawling
        if (drags_grappled(s, a)) cost *= 2;                           // SRD: half speed while moving a grappled creature
        if (cost > b.movement_ft) return fail("not enough movement");
        // Pause before leaving reach; opportunity attacks may stop the move.
        if (req.leaves_reach && !leaving_reach_window(s, ctx, a, out.events)) {
            emit(out.events, EventKind::MoveInterrupted, a.id, 0, req.move_ft);
            check_end(s, out.events);
            return out;
        }
        b.movement_ft = int16_t(b.movement_ft - cost);
        b.moved_ft = uint16_t(b.moved_ft + 5 * req.move_tiles);   // only a completed move counts
        s.budget = b;
        emit(out.events, EventKind::Moved, a.id, 0, req.move_ft, b.movement_ft);
        if (!s.zones.empty()) {   // R-G11: the step's zone membership (sim's facts), then other creatures' changes (aura move)
            zone_membership(s, &ctx, a.id, req.zones_entered, req.zones_left, req.zones_within, out.events);
            zone_changes(s, &ctx, req.zone_changes, out.events);
        }
        check_end(s, out.events);
        return out;
    }
    case ActionKind::CastSpell: {
        if (req.spell_id == 0) return fail("spell_id required");
        if (a.form == FormKind::Polymorph || a.form == FormKind::WildShape) return fail("cannot cast spells in this form");   // R-G12
        if (const SpellDef* def = spell_def(s, req.spell_id)) return cast_spell(s, ctx, a, b, *def, req);
        if (req.innate) return fail("innate spells must be in the spell book");
        // Opaque spell (not in the SpellBook): only the SpellCast window; sim/content resolve it.
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        emit(out.events, EventKind::SpellCast, a.id, req.target, int32_t(req.spell_id), req.spell_level);
        bool countered = spell_cast_window(s, ctx, a, req.spell_id, req.spell_level, req.target, out.events);
        emit(out.events, countered ? EventKind::SpellCountered : EventKind::SpellResolved, a.id, req.target,
             int32_t(req.spell_id), req.spell_level);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::UseFeature: {
        if (req.feature_id == 0) return fail("feature_id required");
        FeatureUses* fu = nullptr;
        for (auto& f : s.features) if (f.actor == a.id && f.feature_id == req.feature_id) fu = &f;
        if (fu && fu->max > 0 && fu->used >= fu->max) return fail("feature has no uses left");
        if (a.form != FormKind::None && std::find(a.form_blocked_features.begin(), a.form_blocked_features.end(), req.feature_id) !=
                                            a.form_blocked_features.end())
            return fail("feature not usable in this form");   // R-G12 (D7): the form is not physically capable
        std::optional<FeatureSpec> fs;
        if (s.hooks.feature_spec) fs = s.hooks.feature_spec(s, a, req.feature_id);
        std::optional<FormStats> form_stats;   // R-G12: a transform feature (Wild Shape, shapechanger)
        if (fs && fs->form != 0) {
            if (fs->form != uint8_t(FormKind::WildShape) && fs->form != uint8_t(FormKind::Shapechanger))
                return fail("feature form: 2 (Wild Shape) or 3 (shapechanger)");
            if (a.form != FormKind::None) return fail("already transformed");
            if (is_incapacitated(a)) return fail("incapacitated");
            if (req.form_archetype == 0) return fail("form_archetype required");
            if (fs->form_choices != 0 && req.form_archetype > fs->form_choices) return fail("bad form_archetype");
            if (s.hooks.build_form) form_stats = s.hooks.build_form(s, a, FormKind(fs->form), req.feature_id, uint8_t(req.form_archetype - 1));
            if (!form_stats || (fs->form == uint8_t(FormKind::WildShape) && form_stats->hp_max <= 0)) return fail("form not available");
        } else if (req.form_archetype != 0) {
            return fail("form_archetype: not a transform feature");
        }
        uint16_t pool_spend = 0;
        Combatant* heal_to = nullptr;
        if (fs && fs->pool != 0) {   // validate everything before any state changes
            if (!find_pool(s, a.id, fs->pool)) return fail("no such resource");
            if (fs->heal_pool) {
                if (req.amount <= 0) return fail("amount required");
                heal_to = req.target == 0 ? &a : find_mut(s, req.target);
                if (!heal_to || heal_to->dead) return fail("bad target");
                pool_spend = uint16_t(req.amount);
            } else {
                pool_spend = fs->pool_cost ? fs->pool_cost : uint16_t(1);
            }
            if (!can_spend(s, a.id, fs->pool, pool_spend)) return fail("not enough resource");
        }
        const uint8_t cost = fs ? fs->cost : uint8_t(0);
        if (cost == 4) {
            // free: no action economy
        } else if (cost == 3) {
            if (!b.reaction) return fail("no reaction available");
            b.reaction = false;
        } else if (cost == 2 || (cost == 0 && req.as_bonus_action)) {
            if (!b.bonus_action) return fail("no bonus action available");
            b.bonus_action = false;
        } else if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        if (fs) b.extra_actions = uint8_t(std::min<int>(255, int(b.extra_actions) + int(fs->grant_actions)));
        s.budget = b;
        if (fu && fu->max > 0) ++fu->used;
        emit(out.events, EventKind::FeatureUsed, a.id, 0, int32_t(req.feature_id), req.as_bonus_action ? 1 : 0);
        if (form_stats && apply_form(a, FormKind(fs->form), *form_stats)) {   // R-G12: Transformed after FeatureUsed
            a.form_source = req.feature_id; a.form_caster = 0; a.form_archetype = req.form_archetype;
            a.form_end_round = fs->form_rounds != 0 ? uint32_t(s.round) + fs->form_rounds : 0u;
            ++s.transforms;
            emit(out.events, EventKind::Transformed, a.id, 0, int32_t(req.feature_id), int32_t(req.form_archetype - 1), int32_t(fs->form));
        }
        if (pool_spend > 0 && spend_pool(s, a.id, fs->pool, pool_spend))
            emit(out.events, EventKind::ResourceSpent, a.id, 0, int32_t(fs->pool), pool_spend, find_pool(s, a.id, fs->pool)->cur);
        if (heal_to) {   // Lay on Hands: the spent amount is the healing
            auto h = heal_hp(s, *heal_to, int16_t(pool_spend));
            commit_healing(*heal_to, h);
            emit(out.events, EventKind::Healed, a.id, heal_to->id, h.healed, h.hp_after, h.from_zero);
        }
        if (s.hooks.feature_modifiers) {   // Rage, Bardic Inspiration token, Leadership (row 12)
            for (ModifierSpec m : s.hooks.feature_modifiers(s, a, req.feature_id)) {
                if (m.source == 0) m.source = req.feature_id;
                std::vector<CombatantId> to;
                if (m.applies_to == ModAppliesTo::Self) to.push_back(a.id);
                else if (m.applies_to == ModAppliesTo::Target) { if (req.target != 0) to.push_back(req.target); }
                else if (s.hooks.area_targets) {
                    SpellDef area; area.area = AreaShape::Sphere; area.area_size_ft = 30;
                    for (CombatantId id : s.hooks.area_targets(s, a, area)) {
                        const Combatant* c = find(s, id);
                        if (c && !c->dead && c->id != a.id && c->team == a.team) to.push_back(id);
                    }
                }
                for (CombatantId id : to) apply_modifier(s, a.id, id, m, out.events);
            }
        }
        if (req.heal_dice.count > 0 || req.heal_dice.bonus > 0) {
            int amount = roll_dice(s.rng, req.heal_dice, false);
            if (amount > 0) {
                auto h = heal_hp(s, a, int16_t(amount));
                commit_healing(a, h);
                emit(out.events, EventKind::Healed, a.id, a.id, h.healed, h.hp_after, h.from_zero);
            }
        }
        check_end(s, out.events);
        return out;
    }
    case ActionKind::SummonWeaponAttack: {   // R-G12 PR 3 (D3): bonus action: the weapon moves (sim) and attacks
        ActiveSummonWeapon* w = nullptr;
        for (auto& x : s.summon_weapons) if (x.owner == a.id && x.spell_id == req.spell_id) { w = &x; break; }
        if (req.summon_tiles.size() > 1) return fail("spiritual weapon: one tile");
        if (!w) return fail("spiritual weapon: none active");
        if (is_incapacitated(a)) return fail("incapacitated");
        if (!b.bonus_action) return fail("no bonus action available");
        Combatant* t = find_mut(s, req.target);
        if (!t || t == &a) return fail("bad target");
        if (t->dead) return fail("target is dead");
        const SpellDef* def = spell_def(s, w->spell_id);
        if (!def) return fail("unknown spell");
        const uint8_t lvl = w->slot_level;
        if (!req.summon_tiles.empty()) { w->has_tile = true; w->tile_x = req.summon_tiles[0].x; w->tile_y = req.summon_tiles[0].y; }
        b.bonus_action = false;
        s.budget = b;
        summon_weapon_attack(s, ctx, a, *def, lvl, *t, out.events);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::CommandSummons: {   // R-G12 PR 2: verbal (conjure) / telepathic: free; Animate Dead: the bonus action
        if (req.command != 1 && req.command != 2) return fail("command: 1 act or 2 defend");
        if (is_incapacitated(a)) return fail("incapacitated");
        std::vector<Combatant*> to;
        if (req.command_ids.empty()) {
            for (auto& c : s.combatants)
                if (c.summoner == a.id && !c.gone && c.summon_control != uint8_t(SummonControl::None)) to.push_back(&c);
        } else {
            for (std::size_t i = 0; i < req.command_ids.size(); ++i) {
                Combatant* c = find_mut(s, req.command_ids[i]);
                if (!c || c->summoner != a.id || c->gone) return fail("command: not your summon");
                if (c->summon_control == uint8_t(SummonControl::None)) return fail("command: it takes no commands");
                for (std::size_t j = 0; j < i; ++j) if (req.command_ids[j] == req.command_ids[i]) return fail("command: duplicate id");
                to.push_back(c);
            }
        }
        if (to.empty()) return fail("command: no summons to command");
        const bool costs = std::any_of(to.begin(), to.end(), [](const Combatant* c) {
            return c->summon_control == uint8_t(SummonControl::BonusAction); });
        if (costs) {
            if (!b.bonus_action) return fail("no bonus action available");
            b.bonus_action = false;
        }
        s.budget = b;
        for (Combatant* c : to) c->commanded = req.command;
        if (req.command_ids.empty()) emit(out.events, EventKind::Commanded, a.id, 0, req.command);
        else for (Combatant* c : to) emit(out.events, EventKind::Commanded, a.id, c->id, req.command);
        return out;
    }
    case ActionKind::Revert: {   // R-G12: Wild Shape: a bonus action; shapechanger: an action; Polymorph: not by choice
        if (a.form == FormKind::None) return fail("not transformed");
        if (a.form == FormKind::Polymorph) return fail("polymorph ends with the spell, not by choice");
        if (is_incapacitated(a)) return fail("incapacitated");
        if (a.form == FormKind::WildShape) {
            if (!b.bonus_action) return fail("no bonus action available");
            b.bonus_action = false;
        } else if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        form_reverted(a, 3, 0, out.events);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::StandUp: {
        int speed = effective_speed(a);
        if (!a.has(Condition::Prone)) return fail("not prone");
        if (speed == 0 || b.movement_ft < speed / 2) return fail("not enough movement");
        b.movement_ft = int16_t(b.movement_ft - speed / 2);
        s.budget = b;
        remove_condition(a, Condition::Prone);
        emit(out.events, EventKind::StoodUp, a.id, 0, b.movement_ft);
        return out;
    }
    case ActionKind::FlexibleCasting: {   // Font of Magic (VERSION_BUMP_BATCH 7.2o): a bonus action, no SpellCast window
        if (is_incapacitated(a)) return fail("incapacitated");
        if (!b.bonus_action) return fail("no bonus action available");
        const FlexibleOutcome fo = flexible_casting(a, s.pools, req.flexible, req.flexible_level);
        switch (fo.result) {
        case FlexibleResult::Ok: break;
        case FlexibleResult::NoPool: return fail("no sorcery points");
        case FlexibleResult::BadDirection: return fail("flexible must be 1 (slot to points) or 2 (points to slot)");
        case FlexibleResult::BadLevel: return fail("bad flexible_level");
        case FlexibleResult::NoSlot: return fail("no spell slot of that level");
        case FlexibleResult::OverMax: return fail("sorcery points would exceed the maximum");
        case FlexibleResult::NotEnoughPoints: return fail("not enough sorcery points");
        }
        b.bonus_action = false;
        s.budget = b;
        // ResourceSpent b = points spent; negative = gained (slot to points). The level follows from b (costs are distinct).
        emit(out.events, EventKind::ResourceSpent, a.id, 0, int32_t(a.sorcery_pool), -int32_t(fo.points_delta), fo.points_after);
        return out;
    }
    case ActionKind::Dash:
        if (req.aggressive) {   // Aggressive: a bonus-action Dash (sim keeps it toward a hostile)
            if (!a.aggressive) return fail("no Aggressive");
            if (!b.bonus_action || !req.as_bonus_action) return fail("Aggressive needs the bonus action");
            b.bonus_action = false; b.aggressive_dash = true;
        } else if (!spend_action(b, a, req.kind, req.as_bonus_action)) return fail("no action available");
        b.movement_ft = int16_t(b.movement_ft + effective_speed(a));
        s.budget = b;
        emit(out.events, EventKind::Dashed, a.id, 0, b.movement_ft);
        return out;
    case ActionKind::Disengage:
        if (!spend_action(b, a, req.kind, req.as_bonus_action)) return fail("no action available");
        s.budget = b; a.disengaged = true;
        emit(out.events, EventKind::Disengaged, a.id);
        return out;
    case ActionKind::Dodge:
        if (!spend_action(b, a, req.kind, req.as_bonus_action)) return fail("no action available");
        s.budget = b; a.dodging = true;
        emit(out.events, EventKind::Dodged, a.id);
        return out;
    case ActionKind::Attack: {
        if (req.special != 0) return special_action(s, ctx, a, b, req);
        if (req.familiar_id != 0) {   // R-G12 PR 3 (D6): Pact of the Chain: one of the Attack action's attacks, made by the familiar
            if (!a.pact_of_chain) return fail("familiar_attack: needs Pact of the Chain");
            if (req.as_bonus_action || req.bonus_option != 0 || req.multiattack != 0 || b.sequence != 0)
                return fail("familiar_attack: one attack of the Attack action");
            Combatant* f = find_mut(s, req.familiar_id);
            if (!f || f->summoner != a.id || f->gone || f->summon_control != uint8_t(SummonControl::Telepathic))
                return fail("familiar_attack: not your familiar");
            if (f->reaction_used || f->dead || f->hp <= 0 || is_incapacitated(*f)) return fail("familiar_attack: the familiar cannot react");
            Combatant* t = find_mut(s, req.target);
            if (!t || t == &a || t == f) return fail("bad target");
            if (t->dead) return fail("target is dead");
            if (charmed_by(s, f->id, t->id)) return fail("a charmed creature cannot attack its charmer");
            if (req.attack_index >= f->attacks.size()) return fail("bad attack index");
            if (b.attacks_remaining > 0) --b.attacks_remaining;
            else if (spend_action(b, a, req.kind, false)) {
                b.loading_fired = 0;
                b.attacks_remaining = uint8_t(a.attacks_per_action > 1 ? a.attacks_per_action - 1 : 0);
            } else return fail("no action available");
            s.budget = b;
            f->reaction_used = true;
            do_attack(s, ctx, *f, *t, f->attacks[req.attack_index], req.target_within_5ft, out.events,
                      req.ranged_hostile_within_5ft || req.beyond_normal_range, 0, req.ally_near_target, nullptr, false);
            check_end(s, out.events);
            return out;
        }
        // A bonus-action attack is a granted trigger rider (Rampage: bonus_attack_grant = index + 1) or a compiled option
        // (bonus_option = 1 + index into bonus_attacks).
        const BonusAttack* bopt = nullptr;
        bool bonus_continue = false;
        if (req.bonus_option != 0) {
            if (!req.as_bonus_action) return fail("a bonus attack option needs as_bonus_action");
            if (req.bonus_option > a.bonus_attacks.size()) return fail("bad bonus attack option");
            bopt = &a.bonus_attacks[std::size_t(req.bonus_option - 1)];
            bonus_continue = b.bonus_option == req.bonus_option && b.bonus_remaining > 0;
            if (!bonus_continue && !bonus_attack_available(s, a, req.bonus_option)) return fail("bonus attack not available");
            if (bopt->any_melee) { if (req.attack_index >= a.attacks.size() || a.attacks[req.attack_index].ranged) return fail("bonus attack must be a melee attack"); }
            else if (req.attack_index != bopt->attack_index) return fail("bonus attack uses a different attack");
        }
        const bool granted = bopt == nullptr && req.as_bonus_action && b.bonus_attack_grant != 0 && b.bonus_action &&
                             req.attack_index + 1 == b.bonus_attack_grant;
        if (req.as_bonus_action && !granted && !bopt) return fail("attack is not a bonus action");
        Combatant* t = find_mut(s, req.target);
        if (!t || t == &a) return fail("bad target");
        if (t->dead) return fail("target is dead");
        if (charmed_by(s, a.id, t->id)) return fail("a charmed creature cannot attack its charmer");
        if (req.attack_index >= a.attacks.size()) return fail("bad attack index");
        uint32_t pending_pool = 0; uint16_t pending_cost = 0;
        if (bopt) {
            if (bonus_continue) {
                if (--b.bonus_remaining == 0) b.bonus_option = 0;
            } else {
                b.bonus_action = false;
                if (bopt->count > 1) { b.bonus_option = req.bonus_option; b.bonus_remaining = uint8_t(bopt->count - 1); }
                if (bopt->pool != 0) { pending_pool = bopt->pool; pending_cost = bopt->pool_cost ? bopt->pool_cost : uint16_t(1); }
            }
        } else if (granted) {
            b.bonus_action = false;
            b.bonus_attack_grant = 0;
        } else if (b.sequence != 0) {                                            // multiattack in progress
            if (req.multiattack != 0 && req.multiattack != b.sequence) return fail("another multiattack is in progress");
            if (b.sequence > a.multiattacks.size()) return fail("bad multiattack");
            const auto& seq = a.multiattacks[std::size_t(b.sequence - 1)].attacks;
            if (b.sequence_pos >= seq.size() || req.attack_index != seq[b.sequence_pos]) return fail("attack out of multiattack sequence");
            ++b.sequence_pos;
            if (--b.attacks_remaining == 0) { b.sequence = 0; b.sequence_pos = 0; }
        } else if (req.multiattack != 0) {                                // start a declared multiattack
            if (b.attacks_remaining > 0) return fail("attack action already in progress");
            if (req.multiattack > a.multiattacks.size()) return fail("bad multiattack");
            const auto& seq = a.multiattacks[std::size_t(req.multiattack - 1)].attacks;
            if (seq.empty()) return fail("bad multiattack");
            for (uint8_t i : seq) if (i >= a.attacks.size()) return fail("bad multiattack");
            if (req.attack_index != seq[0]) return fail("attack out of multiattack sequence");
            if (!spend_action(b, a, req.kind, false)) return fail("no action available");
            b.loading_fired = 0;
            b.attacks_remaining = uint8_t(seq.size() - 1);
            if (b.attacks_remaining > 0) { b.sequence = req.multiattack; b.sequence_pos = 1; }
        } else if (b.attacks_remaining > 0) --b.attacks_remaining;       // Extra Attack
        else if (spend_action(b, a, req.kind, false)) {
            b.loading_fired = 0;
            b.attacks_remaining = uint8_t(a.attacks_per_action > 1 ? a.attacks_per_action - 1 : 0);
        } else return fail("no action available");
        if (has_property(a.attacks[req.attack_index], kWeaponLoading) && req.attack_index < 32) {
            const uint32_t lbit = uint32_t(1) << req.attack_index;
            if (b.loading_fired & lbit) return fail("loading weapon already fired this action");
            b.loading_fired |= lbit;
        }
        if (!bopt && !granted && req.attack_index < 32) b.attacks_used_mask |= uint32_t(1) << req.attack_index;
        s.budget = b;
        if (pending_pool != 0 && spend_pool(s, a.id, pending_pool, pending_cost))
            emit(out.events, EventKind::ResourceSpent, a.id, 0, int32_t(pending_pool), pending_cost, find_pool(s, a.id, pending_pool)->cur);
        do_attack(s, ctx, a, *t, a.attacks[req.attack_index], req.target_within_5ft, out.events,
                  req.ranged_hostile_within_5ft || req.beyond_normal_range, 0, req.ally_near_target,
                  bopt && bopt->has_damage ? &bopt->damage : nullptr, req.two_handed_grip);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::Help: {
        Combatant* ally = find_mut(s, req.ally);
        if (!ally || ally == &a || !is_conscious(*ally)) return fail("bad ally");
        if (req.target == 0) {   // help with the ally's next ability check
            if (!spend_action(b, a, req.kind, false)) return fail("no action available");
            s.budget = b;
            ally->check_helped_by = a.id;
            emit(out.events, EventKind::Helped, a.id, ally->id, 0);
            return out;
        }
        Combatant* foe = find_mut(s, req.target);
        if (!foe || foe == &a || foe == ally || foe->dead) return fail("bad target");
        if (!req.target_within_5ft) return fail("target must be within 5 ft of the helper");
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        ally->helped_vs = foe->id; ally->helped_by = a.id;
        emit(out.events, EventKind::Helped, a.id, ally->id, int32_t(foe->id));
        return out;
    }
    case ActionKind::Hide: {
        if (!spend_action(b, a, req.kind, req.as_bonus_action)) return fail("no action available");
        s.budget = b;
        auto chk = roll_check(s, a, Skill::Stealth, 0, RollMode::Normal, out.events);
        a.stealth_total = int16_t(chk.roll.total > 0 ? chk.roll.total : 1);
        emit(out.events, EventKind::Hid, a.id, 0, a.stealth_total);
        return out;
    }
    case ActionKind::Ready: {
        const auto& rk = req.ready_kind;
        if (req.multiattack != 0) return fail("a readied action is a single attack");
        if (rk == ActionKind::Attack && a.summoner != 0 && a.summon_control == uint8_t(SummonControl::Telepathic))
            return fail("familiar_attack: a familiar cannot ready an attack");
        if (rk == ActionKind::Attack) {
            Combatant* t = find_mut(s, req.target);
            if (!t || t == &a || t->dead) return fail("bad target");
            if (req.attack_index >= a.attacks.size()) return fail("bad attack index");
        } else if (rk == ActionKind::Move) {
            if (req.move_ft <= 0) return fail("move_ft must be positive");
        } else return fail("only Attack or Move can be readied");
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        ActionRequest resp = req;
        resp.kind = rk;
        s.readied.push_back(ReadiedAction{a.id, resp, req.trigger_id});
        a.has_readied = true;
        emit(out.events, EventKind::Readied, a.id, req.target, int32_t(rk), int32_t(req.trigger_id));
        return out;
    }
    case ActionKind::Stabilize: {
        Combatant* t = nullptr;
        if (auto e = dying_target(t); !e.empty()) return fail(e);
        if (!req.target_within_5ft) return fail("target must be within 5 ft");
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        auto chk = roll_check(s, a, Skill::Medicine, 10, RollMode::Normal, out.events);   // SRD: DC 10 Wisdom (Medicine)
        emit(out.events, EventKind::MedicineCheck, a.id, t->id, chk.roll.natural, chk.roll.total, chk.success);
        if (chk.success) stabilize(*t, out.events);
        return out;
    }
    case ActionKind::SpareTheDying: {
        Combatant* t = nullptr;
        if (auto e = dying_target(t); !e.empty()) return fail(e);
        if (!s.hooks.spare_the_dying || !s.hooks.spare_the_dying(s, a, *t)) return fail("cannot cast Spare the Dying");
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        stabilize(*t, out.events);
        return out;
    }
    case ActionKind::OpportunityAttack: case ActionKind::ReactionDecision:
    case ActionKind::Heal: case ActionKind::AbilityCheck: case ActionKind::GazeResponse: break;  // handled above
    }
    return fail("unknown action");
}

} // namespace

namespace {
// R-G11 PR 2: one zone cast per form the caster picks at the cast: a sided zone (Wall of Fire) once per side (+1, then -1),
// and with ring_size_ft also as a ring (inside +1, outside -1). Sim adds the tiles (destination, a wall's end tile).
void push_zone_forms(std::vector<ActionRequest>& out, const ActionRequest& base, const SpellDef& d) {
    if (d.kind != SpellEffectKind::Zone || (!d.zone.sided && d.zone.ring_size_ft == 0)) { out.push_back(base); return; }
    for (int ring = 0; ring < (d.zone.ring_size_ft > 0 ? 2 : 1); ++ring) {
        for (int side = 0; side < (d.zone.sided ? 2 : 1); ++side) {
            ActionRequest r = base;
            r.zone_ring = ring == 1;
            r.zone_side = d.zone.sided ? int8_t(side == 0 ? 1 : -1) : int8_t(0);
            out.push_back(r);
        }
    }
}
} // namespace

std::vector<ActionRequest> legal_actions(const EncounterState& s) {
    std::vector<ActionRequest> out;
    if (s.ended || s.order.empty()) return out;
    const Combatant& a = *find(s, current_actor(s));
    const TurnBudget& b = s.budget;
    auto add = [&](ActionKind k, bool bonus = false, CombatantId target = 0) {
        ActionRequest r{a.id, k, target};
        r.as_bonus_action = bonus;
        out.push_back(r);
    };
    if (b.gaze_pending) {   // Petrifying Gaze: only the turn-start answer (sim fills gaze_seen)
        add(ActionKind::GazeResponse);
        if (!a.surprised) { ActionRequest r{a.id, ActionKind::GazeResponse}; r.avert_gaze = true; out.push_back(r); }
        return out;
    }
    if (a.surprised) { add(ActionKind::EndTurn); return out; }   // R-G16
    if (awaits_command(a)) {   // R-G12 PR 2 (D4): Dodge until commanded
        if ((b.action || b.extra_actions > 0) && !is_incapacitated(a)) add(ActionKind::Dodge);
        add(ActionKind::EndTurn);
        return out;
    }
    const bool can_act = b.action || b.extra_actions > 0;   // row 13: an extra action counts
    const bool familiar = a.summoner != 0 && a.summon_control == uint8_t(SummonControl::Telepathic);   // R-G12 PR 3: no attacks
    if (!familiar && (can_act || b.attacks_remaining > 0)) {
        for (const auto& t : s.combatants) {
            if (t.dead || t.id == a.id || t.team == a.team) continue;
            if (b.sequence != 0 && b.sequence <= a.multiattacks.size() &&
                b.sequence_pos < a.multiattacks[std::size_t(b.sequence - 1)].attacks.size()) {   // multiattack in progress: only the next declared attack
                ActionRequest r{a.id, ActionKind::Attack, t.id, a.multiattacks[std::size_t(b.sequence - 1)].attacks[b.sequence_pos]};
                r.multiattack = b.sequence;
                out.push_back(r);
                continue;
            }
            for (uint8_t i = 0; i < a.attacks.size(); ++i) {
                if (!can_act && i < 32 && has_property(a.attacks[i], kWeaponLoading) && (b.loading_fired >> i & 1)) continue;
                ActionRequest r{a.id, ActionKind::Attack, t.id, i};
                out.push_back(r);
            }
            if (can_act)   // start any declared multiattack (needs a fresh action)
                for (std::size_t k = 0; k < a.multiattacks.size(); ++k) {
                    const auto& seq = a.multiattacks[k].attacks;
                    if (seq.empty()) continue;
                    ActionRequest r{a.id, ActionKind::Attack, t.id, seq[0]};
                    r.multiattack = uint8_t(k + 1);
                    out.push_back(r);
                }
        }
    }
    if (can_act) {
        add(ActionKind::Dash); add(ActionKind::Disengage); add(ActionKind::Dodge); add(ActionKind::Hide);
        for (const auto& t : s.combatants)
            if (t.id != a.id && is_dying(t) && !t.regen_down && t.team == a.team) add(ActionKind::Stabilize, false, t.id);
        // Help and Ready need sim facts (adjacency, trigger); sim builds those requests.
    }
    if (b.bonus_action)
        for (ActionKind k : {ActionKind::Dash, ActionKind::Disengage, ActionKind::Dodge, ActionKind::Hide})
            if (a.bonus_action_grants & bit(k)) add(k, true);
    if (b.bonus_action && a.sorcery_pool != 0 && !is_incapacitated(a)) {   // Font of Magic: every valid conversion
        for (uint8_t dir = 1; dir <= 2; ++dir)
            for (uint8_t lvl = 1; lvl <= 9; ++lvl)
                if (check_flexible_casting(a, s.pools, dir, lvl).result == FlexibleResult::Ok) {
                    ActionRequest r{a.id, ActionKind::FlexibleCasting};
                    r.as_bonus_action = true; r.flexible = dir; r.flexible_level = lvl;
                    out.push_back(r);
                }
    }
    if (a.form == FormKind::WildShape ? b.bonus_action : (a.form == FormKind::Shapechanger && can_act)) {
        if (!is_incapacitated(a)) add(ActionKind::Revert, a.form == FormKind::WildShape);   // R-G12
    }
    if (b.bonus_action && !is_incapacitated(a))   // R-G12 PR 3: Spiritual Weapon attacks (sim moves the token first)
        for (const auto& w : s.summon_weapons) {
            if (w.owner != a.id) continue;
            for (const auto& t : s.combatants) {
                if (t.dead || t.id == a.id || t.team == a.team) continue;
                ActionRequest r{a.id, ActionKind::SummonWeaponAttack, t.id};
                r.spell_id = w.spell_id; r.as_bonus_action = true;
                out.push_back(r);
            }
            break;
        }
    if (!is_incapacitated(a)) {   // R-G12 PR 2: command every summon that takes commands (act); bonus action if one needs it
        bool any = false, costs = false;
        for (const auto& c : s.combatants)
            if (c.summoner == a.id && !c.gone && c.summon_control != uint8_t(SummonControl::None)) {
                any = true; costs = costs || c.summon_control == uint8_t(SummonControl::BonusAction);
            }
        if (any && (!costs || b.bonus_action)) { ActionRequest r{a.id, ActionKind::CommandSummons}; r.command = 1; out.push_back(r); }
    }
    if (b.bonus_action && a.aggressive) {   // Aggressive: bonus-action Dash (sim moves it toward a hostile)
        ActionRequest r{a.id, ActionKind::Dash};
        r.as_bonus_action = true; r.aggressive = true;
        out.push_back(r);
    }
    if (b.haste_action) {   // Haste: one weapon attack, Dash, Disengage or Hide
        for (const auto& t : s.combatants) {
            if (t.dead || t.id == a.id || t.team == a.team) continue;
            for (uint8_t i = 0; i < a.attacks.size(); ++i) {
                ActionRequest r{a.id, ActionKind::Attack, t.id, i};
                r.haste_action = true;
                out.push_back(r);
            }
        }
        for (ActionKind k : {ActionKind::Dash, ActionKind::Disengage, ActionKind::Hide}) {
            ActionRequest r{a.id, k};
            r.haste_action = true;
            out.push_back(r);
        }
    }
    int speed = effective_speed(a);
    if (a.has(Condition::Prone) && speed > 0 && b.movement_ft >= speed / 2) add(ActionKind::StandUp);
    if (b.movement_ft >= (a.has(Condition::Prone) ? 10 : 5)) {
        ActionRequest r{a.id, ActionKind::Move};
        r.move_ft = 5;
        out.push_back(r);
    }
    const bool silenced = !s.zones.empty() && in_silence(s, a.id);   // R-G11 PR 2: no verbal components (Silence)
    // Spells: every known non-reaction SpellBook spell, at every slot level available, at each
    // valid target (hostile for attacks/saves/darts, same team for healing). Range and sight are
    // sim's (hooks.spell_target_ok); multi-target / split-dart requests are built by sim or AI.
    const bool no_casting = a.form == FormKind::Polymorph || a.form == FormKind::WildShape;   // R-G12: can't cast in the form
    if (s.hooks.spells && !is_incapacitated(a) && !no_casting) {
        for (uint32_t id : a.spells_known) {
            const SpellDef* d = s.hooks.spells->find(id);
            if (d && (d->kind == SpellEffectKind::Polymorph || d->kind == SpellEffectKind::Summon || d->kind == SpellEffectKind::SummonWeapon)) continue;   // R-G12: sim builds Polymorph requests (form choice)
            if (!d || (d->casting_time != CastingTime::Action && d->casting_time != CastingTime::BonusAction)) continue;
            if (d->casting_time == CastingTime::BonusAction ? !b.bonus_action : !can_act) continue;
            if ((d->components & kComponentV) && silenced) continue;   // R-G11 PR 2: Silence
            if (s.hooks.can_provide_components && !s.hooks.can_provide_components(s, a, *d)) continue;
            if (d->usage_kind == 1) { const ActionUse* u = find_action_use(s, a.id, d->id); if (u && u->expended) continue; }
            if (d->usage_kind == 2 && feature_uses_left(s, a.id, d->id) == 0) continue;
            if (d->usage_kind == 3 && find_pool(s, a.id, d->pool_id) &&
                !can_spend(s, a.id, d->pool_id, d->pool_cost ? d->pool_cost : 1)) continue;
            for (uint8_t L = d->level; L <= (d->level == 0 ? 0 : 9); ++L) {
                if (!has_slot(a, L)) continue;
                if (d->kind == SpellEffectKind::Teleport || d->kind == SpellEffectKind::Zone) {   // R-G9 / R-G11: self; sim adds the destination (legal_teleport_tiles / zone tiles)
                    ActionRequest r{a.id, ActionKind::CastSpell, a.id};
                    r.spell_id = d->id; r.spell_level = L;
                    push_zone_forms(out, r, *d);
                    continue;
                }
                for (const auto& t : s.combatants) {
                    if (t.dead) continue;
                    // Heal, and Invisibility-style conditions (the only beneficial SRD condition), go on allies.
                    const bool heal = d->kind == SpellEffectKind::Heal ||
                                      (d->kind == SpellEffectKind::ApplyCondition && d->condition == Condition::Invisible);
                    if (heal ? t.team != a.team : (t.team == a.team || t.id == a.id)) continue;
                    if (!valid_target_type(*d, t) || (d->excluded_types & bit(t.creature_type))) continue;
                    if (s.hooks.spell_target_ok && !s.hooks.spell_target_ok(s, a, *d, t)) continue;
                    ActionRequest r{a.id, ActionKind::CastSpell, t.id};
                    r.spell_id = d->id; r.spell_level = L; r.damage_choice = uint8_t(d->damage_choices.empty() ? 0 : 1); r.condition_choice = uint8_t(d->condition_choices.empty() ? 0 : 1);
                    out.push_back(r);
                }
            }
        }
    }
    if (s.hooks.spells && !is_incapacitated(a) && !no_casting) {   // innate spells: no slot, own level, uses left (at will / per-day / recharged)
        for (const InnateSpell& is : a.innate) {
            const SpellDef* d = s.hooks.spells->find(is.spell_id);
            if (!d || !is.available()) continue;
            if (d->casting_time != CastingTime::Action && d->casting_time != CastingTime::BonusAction) continue;
            if (d->casting_time == CastingTime::BonusAction ? !b.bonus_action : !can_act) continue;
            if (is.as_listed && (d->components & kComponentV) && silenced) continue;   // R-G11 PR 2: Silence
            if (is.as_listed && s.hooks.can_provide_components && !s.hooks.can_provide_components(s, a, *d)) continue;
            if (d->kind == SpellEffectKind::Teleport || d->kind == SpellEffectKind::Zone) {   // R-G9 / R-G11: self (sim adds a destination)
                ActionRequest r{a.id, ActionKind::CastSpell, a.id};
                r.spell_id = d->id; r.spell_level = d->level; r.innate = true;
                push_zone_forms(out, r, *d);
                continue;
            }
            for (const auto& t : s.combatants) {
                if (t.dead) continue;
                const bool heal = d->kind == SpellEffectKind::Heal ||
                                  (d->kind == SpellEffectKind::ApplyCondition && d->condition == Condition::Invisible);
                if (heal ? t.team != a.team : (t.team == a.team || t.id == a.id)) continue;
                if (!valid_target_type(*d, t) || (d->excluded_types & bit(t.creature_type))) continue;
                if (s.hooks.spell_target_ok && !s.hooks.spell_target_ok(s, a, *d, t)) continue;
                ActionRequest r{a.id, ActionKind::CastSpell, t.id};
                r.spell_id = d->id; r.spell_level = d->level; r.innate = true; r.damage_choice = uint8_t(d->damage_choices.empty() ? 0 : 1); r.condition_choice = uint8_t(d->condition_choices.empty() ? 0 : 1);
                out.push_back(r);
            }
        }
    }
    if (s.hooks.spells && !is_incapacitated(a))   // R-G11: move a zone (Moonbeam); sim adds the destination (legal_zone_tiles)
        for (const auto& z : s.zones) {
            const SpellDef* d = s.hooks.spells->find(z.spell_id);
            if (z.owner != a.id || !d || d->zone.move_cost == 0 || d->zone.anchor != ZoneAnchor::Point || z.cast_stamp == turn_stamp(s)) continue;
            if (d->zone.move_cost == 2 ? !b.bonus_action : !can_act) continue;
            ActionRequest r{a.id, ActionKind::CastSpell, a.id};
            r.spell_id = z.spell_id; r.spell_level = z.slot_level; r.zone_move = z.zone_id;
            out.push_back(r);
        }
    add(ActionKind::EndTurn);
    return out;
}

} // namespace rules5e

namespace rules5e {

void register_feature(EncounterState& s, CombatantId actor, uint32_t feature_id, uint8_t max_uses,
                      FeatureRecharge recharge, uint8_t used) {
    for (auto& f : s.features)
        if (f.actor == actor && f.feature_id == feature_id) { f.max = max_uses; f.recharge = recharge; f.used = used; return; }
    s.features.push_back({actor, feature_id, max_uses, used, recharge});
}

const FeatureUses* find_feature(const EncounterState& s, CombatantId actor, uint32_t feature_id) {
    for (const auto& f : s.features) if (f.actor == actor && f.feature_id == feature_id) return &f;
    return nullptr;
}

uint8_t feature_uses_left(const EncounterState& s, CombatantId actor, uint32_t feature_id) {
    const FeatureUses* f = find_feature(s, actor, feature_id);
    if (!f || f->max == 0) return 255;
    return f->used >= f->max ? 0 : uint8_t(f->max - f->used);
}

void recover_features(EncounterState& s, bool long_rest) {
    for (auto& f : s.features)
        if (f.recharge == FeatureRecharge::ShortRest || (long_rest && f.recharge == FeatureRecharge::LongRest)) f.used = 0;
    for (auto& p : s.pools)
        if (p.recharge == FeatureRecharge::ShortRest || (long_rest && p.recharge == FeatureRecharge::LongRest)) p.cur = p.max;
}

uint64_t feature_uses_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    u32(uint32_t(s.features.size()));
    for (const auto& f : s.features) {   // registration order is deterministic (setup + replay)
        u32(f.actor); u32(f.feature_id); byte(f.max); byte(f.used); byte(uint8_t(f.recharge));
    }
    return h;
}

}  // namespace rules5e

namespace rules5e {

Timed make_timed(Expiry kind, uint16_t rounds, bool target_acting) {
    switch (kind) {
    case Expiry::Rounds: return {kind, int16_t(std::min<int>(rounds, 32767))};
    case Expiry::EndOfTargetNextTurn: return {kind, int16_t(target_acting ? 2 : 1)};   // the acting target's current turn does not count
    case Expiry::EndOfCasterNextTurn: return {kind, int16_t(target_acting ? 2 : 1)};   // here target_acting = the caster is acting
    case Expiry::StartOfTargetNextTurn: return {kind, 1};
    default: return {};
    }
}

bool tick_round_start(Timed& t) {
    if (t.kind != Expiry::Rounds || t.rounds_left <= 0) return false;
    return --t.rounds_left == 0;
}

bool tick_target_turn_end(Timed& t) {
    if (t.kind != Expiry::EndOfTargetNextTurn || t.rounds_left <= 0) return false;
    return --t.rounds_left == 0;
}

const ActiveZone* find_zone(const EncounterState& s, uint32_t id) {
    for (const auto& z : s.zones) if (z.zone_id == id) return &z;
    return nullptr;
}

bool has_zone_state(const EncounterState& s) { return !s.zones.empty() || s.next_zone_id != 1; }   // a zone existed

uint64_t zones_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    u32(s.next_zone_id);
    u32(uint32_t(s.zones.size()));
    for (const auto& z : s.zones) {
        u32(z.zone_id); u32(z.spell_id); u32(z.owner); byte(z.slot_level); u32(uint32_t(uint16_t(z.dc)));
        byte(uint8_t(z.concentration)); byte(uint8_t(z.timed.kind)); u32(uint32_t(uint16_t(z.timed.rounds_left)));
        u32(z.cast_stamp); byte(uint8_t(z.has_anchor_tile)); u32(uint32_t(z.anchor_x)); u32(uint32_t(z.anchor_y));
        u32(uint32_t(z.excluded.size())); for (CombatantId id : z.excluded) u32(id);
        u32(uint32_t(z.fired.size())); for (const auto& f : z.fired) { u32(f.creature); u32(f.stamp); byte(f.group); }
        // PR 2: size, wall end tile, side, ring, feature aura
        u32(z.size_ft); byte(uint8_t(z.has_end_tile)); u32(uint32_t(z.end_x)); u32(uint32_t(z.end_y));
        byte(uint8_t(z.side)); byte(uint8_t(z.ring)); byte(uint8_t(z.feature));
    }
    for (const auto& e : s.effects) {   // the zone-bound effects (Web's restraint), in application order
        if (e.zone_id == 0) continue;
        u32(e.zone_id); u32(e.spell_id); u32(e.caster); u32(e.target); byte(uint8_t(e.condition)); u32(uint32_t(uint16_t(e.dc)));
    }
    byte(uint8_t(s.budget.action_lost));   // PR 2: Stinking Cloud took this turn's action (the budget is not hashed otherwise)
    return h;
}

bool in_silence(const EncounterState& s, CombatantId id) {
    if (!s.hooks.zone_contains) return false;
    for (const auto& z : s.zones) {
        const SpellDef* d = spell_def(s, z.spell_id);
        if (d && (d->zone.flags & kZoneSilence) && !zone_suppressed(s, z, *d) && s.hooks.zone_contains(s, z.zone_id, id, true)) return true;
    }
    return false;
}

uint32_t add_feature_aura(EncounterState& s, CombatantId owner, uint32_t spell_id, std::vector<Event>* events) {
    const SpellDef* d = spell_def(s, spell_id);
    const Combatant* o = find(s, owner);
    if (!d || d->kind != SpellEffectKind::Zone || d->zone.anchor != ZoneAnchor::Creature || !o || o->dead) return 0;
    ActiveZone z;
    z.zone_id = s.next_zone_id++;
    z.spell_id = spell_id; z.owner = owner; z.slot_level = d->level; z.dc = int16_t(spell_save_dc(*o, *d));
    z.cast_stamp = turn_stamp(s); z.size_ft = d->zone.size_ft; z.feature = true;
    s.zones.push_back(z);
    if (events) emit(*events, EventKind::ZoneCreated, owner, owner, int32_t(z.zone_id), int32_t(spell_id), int32_t(d->zone.flags));
    return z.zone_id;
}

bool has_timed_effects(const EncounterState& s) {
    return std::any_of(s.effects.begin(), s.effects.end(), [](const ActiveSpellEffect& e) { return e.timed.kind != Expiry::None; });
}

uint64_t timed_effects_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    for (const auto& e : s.effects) {   // application order is deterministic
        if (e.timed.kind == Expiry::None) continue;
        u32(e.spell_id); u32(e.caster); u32(e.target); byte(uint8_t(e.condition));
        byte(uint8_t(e.timed.kind)); u32(uint32_t(uint16_t(e.timed.rounds_left)));
    }
    return h;
}

}  // namespace rules5e

namespace rules5e {

void register_action_use(EncounterState& s, CombatantId actor, uint32_t spell_id, bool expended) {
    for (auto& u : s.action_uses) if (u.actor == actor && u.spell_id == spell_id) { u.expended = expended; return; }
    s.action_uses.push_back({actor, spell_id, expended});
}

const ActionUse* find_action_use(const EncounterState& s, CombatantId actor, uint32_t spell_id) {
    for (const auto& u : s.action_uses) if (u.actor == actor && u.spell_id == spell_id) return &u;
    return nullptr;
}

void register_pool(EncounterState& s, CombatantId actor, uint32_t pool_id, uint16_t max, uint16_t cur,
                   FeatureRecharge recharge) {
    cur = std::min(cur, max);
    for (auto& p : s.pools)
        if (p.actor == actor && p.pool_id == pool_id) { p.max = max; p.cur = cur; p.recharge = recharge; return; }
    s.pools.push_back({actor, pool_id, max, cur, recharge});
}

const ResourcePool* find_pool(const EncounterState& s, CombatantId actor, uint32_t pool_id) {
    for (const auto& p : s.pools) if (p.actor == actor && p.pool_id == pool_id) return &p;
    return nullptr;
}

bool can_spend(const EncounterState& s, CombatantId actor, uint32_t pool_id, uint16_t n) {
    const ResourcePool* p = find_pool(s, actor, pool_id);
    return p && p->cur >= n;
}

bool spend_pool(EncounterState& s, CombatantId actor, uint32_t pool_id, uint16_t n) {
    for (auto& p : s.pools)
        if (p.actor == actor && p.pool_id == pool_id) {
            if (p.cur < n) return false;
            p.cur = uint16_t(p.cur - n);
            return true;
        }
    return false;
}

uint64_t action_uses_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    u32(uint32_t(s.action_uses.size()));
    for (const auto& u : s.action_uses) { u32(u.actor); u32(u.spell_id); byte(u.expended ? 1 : 0); }
    return h;
}

uint64_t pools_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    u32(uint32_t(s.pools.size()));
    for (const auto& p : s.pools) {
        u32(p.actor); u32(p.pool_id); u32(p.max); u32(p.cur); byte(uint8_t(p.recharge));
    }
    return h;
}

}  // namespace rules5e

namespace rules5e {

bool has_innate_state(const EncounterState& s) {
    for (const auto& c : s.combatants)
        if (!c.innate.empty() || c.spell_dc_override != 0 || c.spell_attack_override != 0) return true;
    return false;
}

uint64_t innate_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    for (const auto& c : s.combatants) {   // combatant order is deterministic
        if (c.innate.empty() && c.spell_dc_override == 0 && c.spell_attack_override == 0) continue;
        u32(c.id); u32(uint32_t(uint16_t(c.spell_dc_override))); u32(uint32_t(uint16_t(c.spell_attack_override)));
        u32(uint32_t(c.innate.size()));
        for (const auto& is : c.innate) {
            u32(is.spell_id); byte(is.uses_max); byte(is.uses_left); byte(is.recharge_min);
            byte(is.has_ability ? uint8_t(1 + uint8_t(is.ability)) : 0); byte(is.as_listed ? 1 : 0);
        }
    }
    return h;
}

}  // namespace rules5e

namespace rules5e {

bool has_trigger_state(const EncounterState& s) {
    for (const auto& c : s.combatants) if (!c.triggers.empty()) return true;
    return false;
}

uint64_t triggers_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    for (const auto& c : s.combatants) {
        if (c.triggers.empty()) continue;
        u32(c.id); u32(uint32_t(c.triggers.size()));
        for (const auto& t : c.triggers) { u32(t.id); byte(uint8_t(t.on)); byte(uint8_t(t.limit)); byte(t.choice); }
        byte(c.death_triggers_done ? 1 : 0);
        u32(c.trigger_turn_mask); u32(c.trigger_turn_stamp); u32(c.trigger_round_mask); u32(c.trigger_round_stamp);
    }
    return h;
}

}  // namespace rules5e

namespace rules5e {

bool apply_modifier(EncounterState& s, CombatantId caster, CombatantId target, const ModifierSpec& spec, std::vector<Event>& ev) {
    Combatant* t = find_mut(s, target);
    if (!t) return false;
    int32_t magnitude = spec.bonus;
    if (magnitude == 0 && spec.die.count > 0) magnitude = int32_t(spec.die.count) * 100 + spec.die.sides;
    else if (magnitude == 0 && spec.mode != RollMode::Normal) magnitude = int32_t(spec.mode);
    else if (magnitude == 0 && spec.stat == ModStat::Speed)
        magnitude = (spec.speed_num == spec.speed_den && spec.speed_sub > 0) ? int32_t(spec.speed_sub) : int32_t(spec.speed_num) * 100 + spec.speed_den;
    else if (magnitude == 0 && spec.stat == ModStat::Ac && spec.ac_floor > 0) magnitude = spec.ac_floor;
    if (spec.stat == ModStat::TempHp) {   // granted at once; the larger pool is kept; no record
        if (spec.bonus > t->temp_hp && !t->no_regain_hp) t->temp_hp = spec.bonus;   // EC swarm: no temp HP
        emit(ev, EventKind::ModifierApplied, caster, target, int32_t(spec.id), int32_t(spec.stat), magnitude);
        if (!spec.per_turn) return true;   // Heroism keeps a record and grants again each turn
    }
    if (!spec.stack) {
        for (std::size_t i = 0; i < s.modifiers.size();) {
            if (s.modifiers[i].spec.id == spec.id && s.modifiers[i].target == target) remove_modifier(s, i, 2, ev); else ++i;
        }
    }
    ActiveModifier m;
    m.spec = spec; m.caster = caster; m.target = target;
    m.timed = make_timed(Expiry(spec.expiry), spec.rounds, current_actor(s) == (spec.expiry == 3 ? caster : target));
    s.modifiers.push_back(m);
    if (spec.stat == ModStat::Resistance) refresh_mod_cache(s);
    if (spec.stat != ModStat::TempHp) emit(ev, EventKind::ModifierApplied, caster, target, int32_t(spec.id), int32_t(spec.stat), magnitude);
    return true;
}

int modifier_flat(const EncounterState& s, CombatantId id, ModStat st, int ability) {
    int sum = 0;
    for (const auto& m : s.modifiers)
        if (!m.gated && m.target == id && mod_in_scope(m.spec, st, ability)) sum += m.spec.bonus;
    return sum;
}

int modified_ac_bonus(const EncounterState& s, CombatantId id) {
    int bonus = modifier_flat(s, id, ModStat::Ac);
    const Combatant* c = nullptr;
    for (const auto& x : s.combatants) if (x.id == id) c = &x;
    if (!c) return bonus;
    int floor_ac = 0;   // Barkskin / Mage Armor: AC is at least this before the bonuses
    for (const auto& m : s.modifiers)
        if (!m.gated && m.target == id && m.spec.stat == ModStat::Ac && m.spec.ac_floor > 0)
            floor_ac = std::max(floor_ac, int(m.spec.ac_floor) + (m.spec.ac_floor_dex ? modifier(c->abilities, Ability::Dex) : 0));
    return bonus + std::max(0, floor_ac - int(c->ac));
}

int modified_speed(const EncounterState& s, const Combatant& c) {
    int sp = effective_speed(c);
    if (sp <= 0) return sp;
    for (const auto& m : s.modifiers)
        if (m.target == c.id && m.spec.stat == ModStat::Speed && m.spec.speed_den > 0)
            sp = std::max(0, sp * m.spec.speed_num / m.spec.speed_den - int(m.spec.speed_sub));
    return sp;
}

bool has_modifier_state(const EncounterState& s) { return !s.modifiers.empty(); }

uint64_t modifiers_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    for (const auto& m : s.modifiers) {   // application order is deterministic
        const ModifierSpec& p = m.spec;
        u32(p.id); u32(p.source); u32(m.caster); u32(m.target);
        byte(uint8_t(p.stat)); u32(uint32_t(uint16_t(p.bonus))); byte(p.die.count); byte(p.die.sides); u32(uint32_t(uint16_t(p.die.bonus)));
        byte(uint8_t(p.die_mult)); byte(uint8_t(p.mode)); byte(p.speed_num); byte(p.speed_den);
        byte(uint8_t(p.damage_type)); byte(p.has_damage_type); byte(uint8_t(p.ability)); byte(p.has_ability);
        byte(p.scope); byte(p.until_caster_turn); byte(p.concentration);
        byte(p.consume); byte(p.speed_sub); u32(uint32_t(uint16_t(p.ac_floor))); byte(p.ac_floor_dex); byte(p.per_turn);
        byte(uint8_t(p.when.size()));
        for (const Predicate& w : p.when) { byte(uint8_t(w.kind)); u32(uint32_t(uint16_t(w.a))); u32(uint32_t(uint16_t(w.b))); u32(w.mask); }
        byte(uint8_t(m.timed.kind)); u32(uint32_t(uint16_t(m.timed.rounds_left)));
        if (p.repeat_save != 0) { byte(p.repeat_save); byte(uint8_t(p.repeat_ability)); u32(uint32_t(uint16_t(p.repeat_dc))); }   // Slow
    }
    return h;
}

}  // namespace rules5e

namespace rules5e {

bool bonus_attack_available(const EncounterState& s, const Combatant& a, uint8_t option) {
    if (option == 0 || option > a.bonus_attacks.size() || !s.budget.bonus_action || s.budget.bonus_option != 0) return false;
    const BonusAttack& o = a.bonus_attacks[std::size_t(option - 1)];
    if (o.requires_attacks != 0) {
        uint32_t used = s.budget.attacks_used_mask & o.requires_attacks;
        if (o.different_weapon && o.attack_index < 32) used &= ~(uint32_t(1) << o.attack_index);
        if (used == 0) return false;
    }
    if (o.requires_modifier != 0) {
        bool on = false;
        for (const auto& m : s.modifiers) on = on || (m.target == a.id && m.spec.id == o.requires_modifier);
        if (!on) return false;
    }
    if (o.pool != 0 && !can_spend(s, a.id, o.pool, o.pool_cost ? o.pool_cost : uint16_t(1))) return false;
    return true;
}

}  // namespace rules5e

namespace rules5e {

bool size_within_one(Size g, Size t) { return int(t) <= int(g) + 1; }

bool apply_sourced_condition(EncounterState& s, CombatantId source, CombatantId target, Condition c, int escape_dc, std::vector<Event>& ev) {
    Combatant* t = find_mut(s, target);
    const Combatant* src = find(s, source);
    if (!t || !src || t->dead || t == src || immune_to(s, *t, c)) return false;
    if (c == Condition::Grappled && !size_within_one(src->size, t->size)) return false;
    if (!t->has(c)) {
        add_condition(*t, c);
        emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(c));
    }
    for (auto& e : s.sourced)
        if (e.condition == c && e.target == target && e.source == source) { e.escape_dc = int16_t(escape_dc); return true; }
    s.sourced.push_back(SourcedCondition{c, target, source, int16_t(escape_dc)});
    return true;
}

CombatantId condition_source(const EncounterState& s, CombatantId target, Condition c) {
    for (const auto& e : s.sourced) if (e.condition == c && e.target == target) return e.source;
    return 0;
}

std::vector<CombatantId> grappled_by(const EncounterState& s, CombatantId g) {
    std::vector<CombatantId> out;
    for (const auto& e : s.sourced) if (e.condition == Condition::Grappled && e.source == g) out.push_back(e.target);
    return out;
}

void prune_sourced(EncounterState& s, std::vector<Event>& ev) {
    if (s.sourced.empty()) return;
    std::vector<CombatantId> ended;   // grapple targets whose holder let go
    for (std::size_t i = 0; i < s.sourced.size();) {
        const SourcedCondition e = s.sourced[i];
        const Combatant* t = find(s, e.target);
        const Combatant* src = find(s, e.source);
        bool drop = !t || t->dead || !t->has(e.condition);
        if (!drop && e.condition == Condition::Grappled && (!src || src->dead || is_incapacitated(*src))) { drop = true; ended.push_back(e.target); }
        if (drop) s.sourced.erase(s.sourced.begin() + std::ptrdiff_t(i)); else ++i;
    }
    for (CombatantId id : ended) {
        bool still = false;
        for (const auto& e : s.sourced) still = still || (e.condition == Condition::Grappled && e.target == id);
        Combatant* t = find_mut(s, id);
        if (!still && t && t->has(Condition::Grappled)) { remove_condition(*t, Condition::Grappled); emit(ev, EventKind::ConditionRemoved, id, 0, int32_t(Condition::Grappled)); }
    }
}

bool has_sourced_conditions(const EncounterState& s) { return !s.sourced.empty(); }

uint64_t sourced_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    for (const auto& e : s.sourced) { byte(uint8_t(e.condition)); u32(e.target); u32(e.source); u32(uint32_t(uint16_t(e.escape_dc))); }
    return h;
}

}  // namespace rules5e

namespace rules5e {

std::vector<CombatantId> surprised_by_stealth(const std::vector<Combatant>& cs, const std::vector<StealthTotal>& hiders) {
    std::vector<CombatantId> out;
    for (const auto& c : cs) {
        if (c.dead) continue;
        bool any_enemy = false, noticed = false;
        const int passive = passive_perception(c);
        for (const auto& e : cs) {
            if (e.dead || e.team == c.team) continue;
            any_enemy = true;
            const StealthTotal* h = nullptr;
            for (const auto& x : hiders) if (x.id == e.id) h = &x;
            if (!h || h->total < passive) { noticed = true; break; }
        }
        if (any_enemy && !noticed) out.push_back(c.id);
    }
    return out;
}

void apply_surprise(std::vector<Combatant>& cs, const std::vector<CombatantId>& ids) {
    for (auto& c : cs)
        for (CombatantId id : ids) if (c.id == id) { c.surprised = true; c.surprised_at_start = true; }   // #158: the set kept
}

bool has_surprised(const EncounterState& s) {
    return std::any_of(s.combatants.begin(), s.combatants.end(), [](const Combatant& c) { return c.surprised; });
}

uint64_t surprised_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    for (const auto& c : s.combatants)
        if (c.surprised) for (int i = 0; i < 4; ++i) byte(uint8_t(uint32_t(c.id) >> (8 * i)));
    return h;
}

bool has_gaze_state(const EncounterState& s) {
    return std::any_of(s.combatants.begin(), s.combatants.end(), [](const Combatant& c) { return c.gaze_averted; });
}

uint64_t gaze_digest(const EncounterState& s, uint64_t h) {
    for (const auto& c : s.combatants) {
        if (!c.gaze_averted) continue;
        for (int i = 0; i < 4; ++i) { h ^= uint8_t(uint32_t(c.id) >> (8 * i)); h *= 0x100000001B3ull; }
    }
    return h;
}

// ---- R-G12 transforms ----
namespace {
FormStats own_stats(const Combatant& c) {
    FormStats f;
    f.abilities = c.abilities; f.hp = c.hp; f.hp_max = c.hp_max; f.ac = c.ac; f.proficiency_bonus = c.proficiency_bonus;
    f.speed_ft = c.speed_ft; f.save_proficiencies = c.save_proficiencies; f.skill_proficiencies = c.skill_proficiencies;
    f.resistances = c.resistances; f.immunities = c.immunities; f.vulnerabilities = c.vulnerabilities;
    f.condition_immunities = c.condition_immunities; f.resist_qualified = c.resist_qualified; f.immune_qualified = c.immune_qualified;
    f.attacks = c.attacks; f.multiattacks = c.multiattacks; f.attacks_per_action = c.attacks_per_action; f.size = c.size;
    f.creature_type = c.creature_type; f.roll_traits = c.roll_traits; f.triggers = c.triggers; f.reactions = c.reactions;
    f.bonus_attacks = c.bonus_attacks;
    return f;
}
void put_stats(Combatant& c, const FormStats& f) {   // everything but hp / hp_max
    c.abilities = f.abilities; c.ac = f.ac; c.proficiency_bonus = f.proficiency_bonus; c.speed_ft = f.speed_ft;
    c.save_proficiencies = f.save_proficiencies; c.skill_proficiencies = f.skill_proficiencies;
    c.resistances = f.resistances; c.immunities = f.immunities; c.vulnerabilities = f.vulnerabilities;
    c.condition_immunities = f.condition_immunities; c.resist_qualified = f.resist_qualified; c.immune_qualified = f.immune_qualified;
    c.attacks = f.attacks; c.multiattacks = f.multiattacks; c.attacks_per_action = f.attacks_per_action; c.size = f.size;
    c.creature_type = f.creature_type; c.roll_traits = f.roll_traits; c.triggers = f.triggers; c.reactions = f.reactions;
    c.bonus_attacks = f.bonus_attacks;
}
template <class T> void append_all(std::vector<T>& to, const std::vector<T>& from) { to.insert(to.end(), from.begin(), from.end()); }
}  // namespace

bool apply_form(Combatant& c, FormKind kind, const FormStats& f) {
    if (kind == FormKind::None || c.form != FormKind::None) return false;
    if (kind != FormKind::Shapechanger && f.hp_max <= 0) return false;
    const FormStats own = own_stats(c);
    FormStats n = f;
    if (kind == FormKind::WildShape) {   // keeps Int / Wis / Cha, its proficiencies (and the beast's) and its features (D7)
        for (std::size_t i = 3; i < 6; ++i) n.abilities.score[i] = own.abilities.score[i];
        n.save_proficiencies |= own.save_proficiencies;
        n.skill_proficiencies |= own.skill_proficiencies;
        n.proficiency_bonus = std::max(n.proficiency_bonus, own.proficiency_bonus);
        FormStats merged = own;   // own features first (their trigger bookkeeping bits stay valid), then the beast's
        append_all(merged.roll_traits, n.roll_traits); append_all(merged.triggers, n.triggers);
        append_all(merged.reactions, n.reactions); append_all(merged.bonus_attacks, n.bonus_attacks);
        n.roll_traits = merged.roll_traits; n.triggers = merged.triggers; n.reactions = merged.reactions;
        n.bonus_attacks = merged.bonus_attacks;
    }
    c.base = own;
    put_stats(c, n);
    if (kind == FormKind::Shapechanger) {   // one HP pool: hp / hp_max stay
    } else {
        c.hp_max = n.hp_max; c.hp = n.hp_max;   // a fresh pool (temp HP stay on the creature, D8)
    }
    c.form = kind;
    c.form_blocked_features = kind == FormKind::WildShape ? f.blocked_features : std::vector<uint32_t>{};
    return true;
}

bool revert_form(Combatant& c) {
    if (c.form == FormKind::None || !c.base) return false;
    const FormStats b = *c.base;
    put_stats(c, b);
    if (c.form == FormKind::Shapechanger) {
        c.hp_max = b.hp_max; c.hp = std::min(c.hp, b.hp_max);
    } else {
        c.hp_max = b.hp_max; c.hp = b.hp;
    }
    c.form = FormKind::None; c.form_source = 0; c.form_caster = 0; c.form_archetype = 0; c.form_end_round = 0;
    c.base.reset();
    c.form_blocked_features.clear();
    return true;
}

bool has_summon_state(const EncounterState& s) { return s.summons != 0 || s.transforms != 0; }

uint64_t summons_digest(const EncounterState& s, uint64_t h) {
    auto mix = [&](uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) { h ^= (v >> (8 * i)) & 0xFF; h *= 0x100000001B3ull; }
    };
    mix(s.next_summon_id, 4); mix(s.summons, 4);
    for (const auto& c : s.combatants) {   // R-G12 PR 2: summon records, combatants order
        if (c.summoner == 0) continue;
        mix(c.id, 4); mix(c.summoner, 4); mix(c.summon_source, 4); mix(c.summon_group, 4);
        mix(uint8_t(c.gone), 1); mix(c.commanded, 1);
    }
    if (!s.summon_weapons.empty()) {   // R-G12 PR 3: Spiritual Weapon rows, only while one exists (other digests unchanged)
        mix(s.summon_weapons.size(), 4);
        for (const auto& w : s.summon_weapons) {
            mix(w.token_id, 4); mix(w.owner, 4); mix(w.spell_id, 4); mix(w.slot_level, 1); mix(w.end_round, 4);
            mix(uint8_t(w.has_tile), 1); mix(uint32_t(w.tile_x), 4); mix(uint32_t(w.tile_y), 4);
        }
    }
    mix(s.transforms, 4);
    for (const auto& c : s.combatants) {
        if (c.form == FormKind::None) continue;
        mix(c.id, 4); mix(uint8_t(c.form), 1); mix(c.form_source, 4); mix(c.form_caster, 4); mix(c.form_archetype, 1);
        mix(c.form_end_round, 4);
        mix(uint16_t(c.base ? c.base->hp : 0), 2); mix(uint16_t(c.base ? c.base->hp_max : 0), 2);
    }
    return h;
}

bool has_leftover_state(const EncounterState& s) {
    return std::any_of(s.combatants.begin(), s.combatants.end(), [](const Combatant& c) {
               return c.hp_max_reduction != 0 || c.lethargy != 0 || c.reckless_active; }) ||
           std::any_of(s.effects.begin(), s.effects.end(), [](const ActiveSpellEffect& e) { return e.escalates; });
}

uint64_t leftover_digest(const EncounterState& s, uint64_t h) {
    auto byte = [&](uint8_t b) { h ^= b; h *= 0x100000001B3ull; };
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) byte(uint8_t(v >> (8 * i))); };
    for (const auto& c : s.combatants) {
        if (c.hp_max_reduction == 0 && c.lethargy == 0 && !c.reckless_active) continue;
        u32(c.id); u32(uint32_t(uint16_t(c.hp_max_reduction))); byte(c.lethargy); byte(uint8_t(c.reckless_active));
    }
    byte(0xFF);   // separator: combatant records / escalating effects
    for (const auto& e : s.effects)
        if (e.escalates) { u32(e.spell_id); u32(e.caster); u32(e.target); byte(uint8_t(e.escalate_to)); }
    return h;
}

}  // namespace rules5e
