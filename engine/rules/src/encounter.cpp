#include "rules5e/encounter.hpp"

#include <algorithm>
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

void kill(Combatant& c, std::vector<Event>& ev) {
    c.dead = true;
    c.hp = 0;
    emit(ev, EventKind::CombatantDied, c.id);
}

void check_end(EncounterState& s, std::vector<Event>& ev) {
    if (s.ended) return;
    auto o = s.hooks.end_condition ? s.hooks.end_condition(s) : default_end_condition(s);
    if (!o) return;
    s.ended = true;
    emit(ev, EventKind::EncounterEnded, 0, 0, int32_t(o->reason));
    s.outcome = std::move(*o);
}

void stabilize(Combatant& t, std::vector<Event>& ev) {
    t.death = DeathSaves{0, 0, true};
    emit(ev, EventKind::Stabilized, t.id);
}

// Everything that follows damage: dropping to 0, massive damage, death-save failures.
void after_damage(Combatant& t, const DamageApplied& d, bool critical, std::vector<Event>& ev) {
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
    bool still = std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
        return o.target == e.target && o.condition == e.condition; });
    if (!still && t->has(e.condition)) {
        remove_condition(*t, e.condition);
        emit(ev, EventKind::SpellEffectEnded, t->id, 0, int32_t(e.spell_id), int32_t(e.condition));
        emit(ev, EventKind::ConditionRemoved, t->id, 0, int32_t(e.condition));
    } else {
        emit(ev, EventKind::SpellEffectEnded, t->id, 0, int32_t(e.spell_id), -1);
    }
}

// Ends `c`'s concentration and every effect that depends on it.
void end_concentration(EncounterState& s, CombatantId caster, std::vector<Event>& ev) {
    Combatant* c = find_mut(s, caster);
    if (!c || c->concentration_spell == 0) return;
    const uint32_t sp = c->concentration_spell;
    c->concentration_spell = 0;
    emit(ev, EventKind::ConcentrationEnded, caster, 0, int32_t(sp));
    for (std::size_t i = 0; i < s.effects.size();) {
        const auto& e = s.effects[i];
        if (e.caster == caster && e.concentration && e.spell_id == sp) end_effect(s, i, ev);
        else ++i;
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
        auto r = saving_throw(s.rng, *c, e.save_ability, e.dc, RollMode::Normal, SaveSource{true, 0, 0});   // an ongoing spell effect
        emit(ev, EventKind::SavingThrowRolled, cid, CombatantId(e.save_ability), r.roll.natural, r.roll.total, int32_t(r.success));
        if (!r.success) continue;
        end_effect(s, std::size_t(it - s.effects.begin()), ev);
        for (;;) {   // a multi-effect spell ends all its effects on this target together
            auto more = std::find_if(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
                return o.spell_id == e.spell_id && o.caster == e.caster && o.target == e.target; });
            if (more == s.effects.end()) break;
            end_effect(s, std::size_t(more - s.effects.begin()), ev);
        }
        bool remaining = std::any_of(s.effects.begin(), s.effects.end(), [&](const ActiveSpellEffect& o) {
            return o.caster == e.caster && o.spell_id == e.spell_id; });
        Combatant* caster = find_mut(s, e.caster);
        if (!remaining && caster && caster->concentration_spell == e.spell_id) end_concentration(s, e.caster, ev);
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
            return o.caster == g.second.first && o.spell_id == g.first; });
        Combatant* caster = find_mut(s, g.second.first);
        if (!remaining && caster && caster->concentration_spell == g.first) end_concentration(s, g.second.first, ev);
    }
}

// New round: Rounds timers tick (all effects, in application order).
void tick_effects_round_start(EncounterState& s, std::vector<Event>& ev) {
    std::vector<GoneEffect> gone;
    for (auto& e : s.effects)
        if (tick_round_start(e.timed)) gone.push_back({e.spell_id, {e.caster, e.target}});
    expire_effects(s, gone, ev);
}

// End of `cid`'s turn: EndOfTargetNextTurn timers on `cid` tick.
void tick_effects_turn_end(EncounterState& s, CombatantId cid, std::vector<Event>& ev) {
    std::vector<GoneEffect> gone;
    for (auto& e : s.effects)
        if (e.target == cid && tick_target_turn_end(e.timed)) gone.push_back({e.spell_id, {e.caster, e.target}});
    expire_effects(s, gone, ev);
}

// SRD: concentration ends when the caster is incapacitated or dies.
void sweep_concentration(EncounterState& s, std::vector<Event>& ev) {
    for (std::size_t i = 0; i < s.combatants.size(); ++i) {
        const Combatant& c = s.combatants[i];
        if (c.concentration_spell != 0 && (c.dead || c.hp <= 0 || is_incapacitated(c)))
            end_concentration(s, c.id, ev);
    }
}

// Damage plus the concentration check (SRD: CON save, DC max(10, half the damage taken)).
void damage_creature(EncounterState& s, Combatant& t, const DamageApplied& d, bool critical, std::vector<Event>& ev) {
    after_damage(t, d, critical, ev);
    if (d.dealt <= 0) return;
    if (t.concentration_spell != 0) {
        if (t.dead || t.hp <= 0) end_concentration(s, t.id, ev);
        else {
            const int dc = std::max(10, d.dealt / 2);
            auto r = saving_throw(s.rng, t, Ability::Con, dc);
            emit(ev, EventKind::SavingThrowRolled, t.id, CombatantId(Ability::Con), r.roll.natural, r.roll.total,
                 int32_t(r.success) | 2);
            if (!r.success) end_concentration(s, t.id, ev);
        }
    }
    repeat_saves(s, t.id, RepeatSave::OnDamage, ev);
}

const SpellDef* spell_def(const EncounterState& s, uint32_t id) {
    return s.hooks.spells ? s.hooks.spells->find(id) : nullptr;
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
};

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
    (void)s;
    return !c.dead && can_act(c) && !c.reaction_used &&
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
    return w.trigger == ReactionTrigger::BeingHit && r.id == w.target;
}

bool evaluate_hit(const D20Roll& roll, const Combatant& t, const AttackContext& ctx, bool& crit) {
    crit = false;
    if (roll.natural == 20) { crit = true; return true; }
    if (roll.natural == 1) return false;
    return roll.total >= int(t.ac) + int(ctx.cover_ac_bonus) + int(t.reaction_ac_bonus);
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

// ---- Triggers (row 9a): Hit / Crit events with extra_damage riders ----

bool predicate_holds(const EncounterState& s, const Combatant& owner, const Combatant& target, const Attack& atk,
                     bool is_spell, RollMode mode, bool ally_near, bool crit, const Predicate& p) {
    switch (p.kind) {
    case PredicateKind::Weapon: {
        if (is_spell) return false;
        switch (WeaponIs(p.a)) {
        case WeaponIs::Melee: return !atk.ranged;
        case WeaponIs::Ranged: return atk.ranged;
        case WeaponIs::Finesse: return is_finesse(atk);
        case WeaponIs::FinesseOrRanged: return is_finesse(atk) || atk.ranged;
        }
        return false;
    }
    case PredicateKind::AdvantageOrAllyAdjacent: return mode == RollMode::Advantage || (ally_near && mode != RollMode::Disadvantage);
    case PredicateKind::TargetCreatureType: return (p.mask & bit(target.creature_type)) != 0;
    case PredicateKind::NotCreatureType: return (p.mask & bit(target.creature_type)) == 0;
    case PredicateKind::DamageTypeIs: return (p.mask & bit(atk.type)) != 0;
    case PredicateKind::DamageTypeNot: return (p.mask & bit(atk.type)) == 0;
    case PredicateKind::NotCrit: return !crit;
    case PredicateKind::MovedAtLeastFt: return owner.id == current_actor(s) && s.budget.moved_ft >= p.a;   // own turn only
    case PredicateKind::SelfHpBelow: return int(owner.hp) * int(p.b) < int(owner.hp_max) * int(p.a);
    case PredicateKind::Save: return false;   // reserved (9b)
    }
    return false;
}

// Fires every Hit / Crit trigger of `a` whose predicates hold and whose limit allows it; emits FeatureTriggered
// (actor = owner, target = the creature hit, a = trigger id, b = TriggerOn, c = 1 applied) in trigger order and
// returns the extra_damage riders to roll with the attack's damage. No triggers, no events, no rng.
std::vector<const RiderEffect*> fire_hit_triggers(EncounterState& s, Combatant& a, const Combatant& t, const Attack& atk,
                                                  bool is_spell, RollMode mode, bool ally_near, bool crit,
                                                  std::vector<Event>& ev) {
    std::vector<const RiderEffect*> out;
    if (a.triggers.empty()) return out;
    const uint32_t turn_stamp = uint32_t(s.round) << 16 | s.turn_index;
    if (a.trigger_turn_stamp != turn_stamp) { a.trigger_turn_stamp = turn_stamp; a.trigger_turn_mask = 0; }
    if (a.trigger_round_stamp != s.round) { a.trigger_round_stamp = s.round; a.trigger_round_mask = 0; }
    for (std::size_t i = 0; i < a.triggers.size() && i < 32; ++i) {
        const Trigger& tr = a.triggers[i];
        if (tr.choice != 0) continue;                                  // ask (9c) / never
        if (!(tr.on == TriggerOn::Hit || (tr.on == TriggerOn::Crit && crit))) continue;
        const uint32_t m = uint32_t(1) << i;
        if (tr.limit == TriggerLimit::OncePerTurn && (a.trigger_turn_mask & m)) continue;
        if (tr.limit == TriggerLimit::OncePerRound && (a.trigger_round_mask & m)) continue;
        bool ok = true;
        for (const Predicate& p : tr.when)
            if (!predicate_holds(s, a, t, atk, is_spell, mode, ally_near, crit, p)) { ok = false; break; }
        if (!ok) continue;
        bool any = false;
        for (const RiderEffect& e : tr.effects)
            if (e.kind == RiderKind::ExtraDamage) { out.push_back(&e); any = true; }
        if (!any) continue;                                            // no 9a effect: reserved kinds do not fire yet
        if (tr.limit == TriggerLimit::OncePerTurn) a.trigger_turn_mask |= m;
        if (tr.limit == TriggerLimit::OncePerRound) a.trigger_round_mask |= m;
        emit(ev, EventKind::FeatureTriggered, a.id, t.id, int32_t(tr.id), int32_t(tr.on), 1);
    }
    return out;
}

bool do_attack(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, const Attack& atk,
               bool within_5ft, std::vector<Event>& ev, bool ranged_disadvantage = false,
               uint32_t spell_id = 0, bool ally_near_target = false) {
    bool adv = false;
    if (a.helped_vs == t.id) { adv = true; a.helped_vs = 0; a.helped_by = 0; }
    if (a.stealth_total > 0) { adv = true; a.stealth_total = 0; }   // unseen attacker; now revealed
    AttackContext actx;
    actx.target_within_5ft = within_5ft;
    actx.ally_near_target = ally_near_target;
    actx.ranged = atk.ranged;
    actx.mode = adv ? RollMode::Advantage : RollMode::Normal;
    actx.extra_disadvantage = (ranged_disadvantage && atk.ranged) || weapon_disadvantage(a, atk, within_5ft);   // combined with conditions in one place
    const RollMode atk_mode = attack_roll_mode(a, t, actx);
    D20Roll roll = roll_d20(s.rng, atk.to_hit, atk_mode);
    bool crit = false;
    bool hit = evaluate_hit(roll, t, actx, crit);
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
        if (being_hit_window(s, ctx, a, t, w, ev, would_miss)) hit = evaluate_hit(roll, t, actx, crit);
    }
    if (hit && within_5ft && (t.has(Condition::Paralyzed) || t.has(Condition::Unconscious))) crit = true;
    emit(ev, EventKind::AttackResolved, a.id, t.id, roll.natural, roll.total,
         int32_t(hit) | (int32_t(crit) << 1));
    if (hit) {
        // Triggers that fire on this hit (Sneak Attack-style): their FeatureTriggered events come before the damage
        // dice; extra_damage riders are rolled with the attack's damage so a crit doubles them.
        const auto riders = fire_hit_triggers(s, a, t, atk, spell_id != 0, atk_mode, ally_near_target, crit, ev);
        int dmg = roll_dice(s.rng, atk.damage, crit);
        if (dmg < 0) dmg = 0;
        if (!riders.empty()) {
            std::vector<DamagePiece> parts{{int16_t(dmg), atk.type}};
            for (const DamagePart& p : atk.extra_damage)
                parts.push_back({int16_t(std::max(0, roll_dice(s.rng, p.dice, crit))), p.type});
            for (const RiderEffect* r : riders)
                parts.push_back({int16_t(std::max(0, roll_dice(s.rng, r->dice, crit))), r->same_as_weapon ? atk.type : r->type});
            damage_creature(s, t, apply_damage_parts(t, parts, atk.qualifiers), crit, ev);
        } else if (atk.extra_damage.empty()) {
            damage_creature(s, t, apply_damage(t, int16_t(dmg), atk.type, atk.qualifiers), crit, ev);
        } else {   // multi-component hit: one DamageDealt carrying the per-type-modified sum
            std::vector<DamagePiece> parts{{int16_t(dmg), atk.type}};
            for (const DamagePart& p : atk.extra_damage)
                parts.push_back({int16_t(std::max(0, roll_dice(s.rng, p.dice, crit))), p.type});
            damage_creature(s, t, apply_damage_parts(t, parts, atk.qualifiers), crit, ev);
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

void start_turn(EncounterState& s, std::vector<Event>& ev) {
    Combatant& c = *find_mut(s, s.order[s.turn_index].id);
    c.reaction_used = false;
    c.dodging = false;
    c.disengaged = false;
    c.reaction_ac_bonus = 0;       // Shield-style bonus lasts until the start of its next turn
    c.reaction_blocks_spell = 0;
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
    repeat_saves(s, c.id, RepeatSave::StartOfTurn, ev);
    if (dies_of_exhaustion(c) && !c.dead) kill(c, ev);
    if (!c.dead && c.regeneration.amount > 0) {   // Regeneration (troll, vampire spawn)
        const bool suppressed = c.regen_suppressed;
        c.regen_suppressed = false;
        if (suppressed) {
            if (c.regen_down) { c.regen_down = false; kill(c, ev); }
        } else if (c.regen_down || c.hp < effective_hp_max(c)) {
            c.regen_down = false;
            auto h = apply_healing(c, c.regeneration.amount);
            commit_healing(c, h);
            emit(ev, EventKind::Healed, c.id, c.id, h.healed, h.hp_after, h.from_zero);
        }
    }
    if (!c.dead) {   // Recharge: one d6 per expended recharge action of this creature (draws only when one exists)
        for (auto& u : s.action_uses) {
            if (u.actor != c.id || !u.expended) continue;
            const SpellDef* d = spell_def(s, u.spell_id);
            const int need = d && d->recharge_min ? d->recharge_min : 6;
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
    if (is_dying(c) && c.uses_death_saves) {
        auto r = roll_death_save(s.rng, c.death);
        c.death = r.after;
        emit(ev, EventKind::DeathSaveRolled, c.id, 0, r.natural, c.death.successes, c.death.failures);
        switch (r.outcome) {
            case DeathSaveOutcome::Died: kill(c, ev); break;
            case DeathSaveOutcome::Stabilized: emit(ev, EventKind::Stabilized, c.id); break;
            case DeathSaveOutcome::RegainedHp:
                c.hp = 1;
                remove_condition(c, Condition::Unconscious);   // stays Prone
                emit(ev, EventKind::RegainedHp, c.id, 0, 1);
                emit(ev, EventKind::ConditionRemoved, c.id, 0, int32_t(Condition::Unconscious));
                break;
            case DeathSaveOutcome::Pending: break;
        }
    }
    bool act = can_act(c);
    s.budget = TurnBudget{act, act, act, int16_t(c.hp > 0 ? effective_speed(c) : 0), 0, 0, 0};
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
                if (go) emit(ev, EventKind::Moved, a->id, 0, ft, 0);
                else emit(ev, EventKind::MoveInterrupted, a->id, 0, ft);
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
    auto r = skill_check(s.rng, c, k, dc, mode);
    emit(ev, EventKind::CheckRolled, c.id, CombatantId(k), r.roll.natural, r.roll.total,
         int32_t(r.success) | (int32_t(helped) << 1));
    return r;
}

bool spend_action(TurnBudget& b, const Combatant& c, ActionKind k, bool bonus) {
    if (bonus) {
        if (!b.bonus_action || !(c.bonus_action_grants & bit(k))) return false;
        b.bonus_action = false;
        return true;
    }
    if (!b.action) return false;
    b.action = false;
    return true;
}

StepResult step_impl(EncounterState& s, Ctx& ctx, const ActionRequest& req);

// Resolves ONE effect (def.kind and its fields) over T. Returns the targets it affected (for rider effects).
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
            if (!t->dead && do_attack(s, ctx, a, *t, atk, within_5ft, ev, false, def.id)) affected.push_back(t);   // TODO: ranged facts for spell attacks
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
                damage_creature(s, *t, apply_damage(*t, int16_t(dmg), def.damage_type, kAttackMagical), false, ev);
            }
        }
        break;
    }
    case SpellEffectKind::Save: {
        const int dc = spell_save_dc(a, def);
        const uint8_t dq = def.magical ? kAttackMagical : 0;
        const int dmg = def.dice.count > 0 || def.dice.bonus > 0
                            ? std::max(0, roll_dice(s.rng, scaled_dice(def, lvl, a.caster_level))) : 0;   // one roll for all targets
        bool applied = false;
        for (Combatant* t : T) {
            if (t->dead) continue;
            SaveSource src{def.magical, def.applies_condition ? bit(def.condition) : 0u,
                           (def.dice.count > 0 || def.dice.bonus > 0) ? bit(def.damage_type) : 0u};
            auto r = saving_throw(s.rng, *t, def.save_ability, dc, RollMode::Normal, src);
            emit(ev, EventKind::SavingThrowRolled, t->id, CombatantId(def.save_ability), r.roll.natural, r.roll.total,
                 int32_t(r.success));
            int dealt = r.success ? (def.on_save == OnSave::Half ? dmg / 2 : 0) : dmg;
            if (dealt > 0) damage_creature(s, *t, apply_damage(*t, int16_t(dealt), def.damage_type, dq), false, ev);
            if (!r.success) affected.push_back(t);
            if (def.applies_condition && !r.success && !t->dead && !(t->condition_immunities & bit(def.condition))) {
                if (!t->has(def.condition)) {
                    add_condition(*t, def.condition);
                    emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(def.condition));
                }
                s.effects.push_back(ActiveSpellEffect{def.id, a.id, t->id, def.condition, def.save_ability,
                                                      int16_t(dc), def.repeat_save, def.concentration,
                                                      make_timed(Expiry(def.expiry), def.duration_rounds, current_actor(s) == t->id)});
                applied = true;
            }
        }
        if (def.concentration && applied) a.concentration_spell = def.id;
        break;
    }
    case SpellEffectKind::Heal: {
        const DiceExpr dice = scaled_dice(def, lvl, a.caster_level);
        for (Combatant* t : T) {
            if (t->dead || (def.excluded_types & bit(t->creature_type))) continue;   // no effect (Cure Wounds: undead)
            int amt = roll_dice(s.rng, dice) + (def.add_spellcasting_modifier ? modifier(a.abilities, a.spellcasting_ability) : 0);
            if (amt <= 0) continue;
            bool was_unconscious = t->has(Condition::Unconscious);
            auto h = apply_healing(*t, int16_t(amt));
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
            if (t->dead || (t->condition_immunities & bit(def.condition))) continue;
            if (!t->has(def.condition)) {
                add_condition(*t, def.condition);
                emit(ev, EventKind::ConditionAdded, t->id, 0, int32_t(def.condition));
            }
            s.effects.push_back(ActiveSpellEffect{def.id, a.id, t->id, def.condition, Ability::Wis, 0, RepeatSave::None,
                                                  def.concentration,
                                                  make_timed(Expiry(def.expiry), def.duration_rounds, current_actor(s) == t->id)});
            affected.push_back(t);
            applied = true;
        }
        if (def.concentration && applied) a.concentration_spell = def.id;
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
StepResult cast_spell(EncounterState& s, Ctx& ctx, Combatant& a, TurnBudget b, const SpellDef& def,
                      const ActionRequest& req) {
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
    if ((!inn || inn->as_listed) && s.hooks.can_provide_components && !s.hooks.can_provide_components(s, a, def)) return fail("components unavailable");
    std::vector<CombatantId> ids = req.targets;
    if (ids.empty() && req.target != 0) ids.push_back(req.target);
    if (ids.empty()) return fail("target required");
    std::vector<Combatant*> T;
    for (CombatantId id : ids) {
        Combatant* t = find_mut(s, id);
        if (!t || t->dead) return fail("bad target");
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
    case SpellEffectKind::Save: case SpellEffectKind::Heal: case SpellEffectKind::ApplyCondition:
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
    bool countered = def.is_spell && spell_cast_window(s, ctx, a, def.id, lvl, ids[0], out.events);
    if (!countered) resolve_spell(s, ctx, a, def, lvl, T, req.target_within_5ft, out.events);
    emit(out.events, countered ? EventKind::SpellCountered : EventKind::SpellResolved, a.id, ids[0], int32_t(def.id), lvl);
    check_end(s, out.events);
    return out;
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

// One deterministic attempt at `req` with the decisions known so far. If an Ask poll has no answer,
// the state is restored to `before` and the pending decision is stored.
StepResult attempt(EncounterState& s, const ActionRequest& req, const std::vector<ReactionRecord>& decided,
                   bool replay_only) {
    EncounterState before = s;
    std::vector<ReactionRecord> records;
    Ctx ctx{records, &decided, 0, replay_only, false, std::nullopt, 0};
    StepResult r = step_impl(s, ctx, req);
    if (r.ok && req.kind != ActionKind::Ready) fire_readies(s, ctx, r.events);
    if (!r.ok) return r;
    sweep_concentration(s, r.events);
    if (ctx.need && !replay_only) {
        std::vector<Event> provisional = std::move(r.events);
        s = std::move(before);
        s.pending = PendingDecision{req, decided, ctx.need->window, ctx.need->reactor, ctx.need->options};
        StepResult out{true, {}, std::move(provisional)};
        out.status = StepStatus::NeedsDecision;
        out.needs = std::move(ctx.need);
        out.reactions = decided;
        return out;
    }
    r.status = StepStatus::Done;
    r.reactions = std::move(records);
    r.desync = ctx.desync || ctx.replay_pos != decided.size();
    r.logged = LoggedStep{req, r.reactions};
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
        s.pending.reset();
        return settle(s, attempt(s, original, decided, false));
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

StepResult step_impl(EncounterState& s, Ctx& ctx, const ActionRequest& req) {
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
        auto h = apply_healing(*t, req.amount);
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
        repeat_saves(s, a.id, RepeatSave::EndOfTurn, out.events);
        tick_effects_turn_end(s, a.id, out.events);
        emit(out.events, EventKind::TurnEnded, a.id);
        advance_turn(s, out.events);
        return out;
    case ActionKind::Move: {
        if (req.move_ft <= 0) return fail("move_ft must be positive");
        int cost = req.move_ft * (a.has(Condition::Prone) ? 2 : 1);   // crawling
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
        check_end(s, out.events);
        return out;
    }
    case ActionKind::CastSpell: {
        if (req.spell_id == 0) return fail("spell_id required");
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
        if (req.as_bonus_action) {
            if (!b.bonus_action) return fail("no bonus action available");
            b.bonus_action = false;
        } else if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        if (fu && fu->max > 0) ++fu->used;
        emit(out.events, EventKind::FeatureUsed, a.id, 0, int32_t(req.feature_id), req.as_bonus_action ? 1 : 0);
        if (req.heal_dice.count > 0 || req.heal_dice.bonus > 0) {
            int amount = roll_dice(s.rng, req.heal_dice, false);
            if (amount > 0) {
                auto h = apply_healing(a, int16_t(amount));
                commit_healing(a, h);
                emit(out.events, EventKind::Healed, a.id, a.id, h.healed, h.hp_after, h.from_zero);
            }
        }
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
    case ActionKind::Dash:
        if (!spend_action(b, a, req.kind, req.as_bonus_action)) return fail("no action available");
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
        if (req.as_bonus_action) return fail("attack is not a bonus action");
        Combatant* t = find_mut(s, req.target);
        if (!t || t == &a) return fail("bad target");
        if (t->dead) return fail("target is dead");
        if (req.attack_index >= a.attacks.size()) return fail("bad attack index");
        if (b.sequence != 0) {                                            // multiattack in progress
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
        s.budget = b;
        do_attack(s, ctx, a, *t, a.attacks[req.attack_index], req.target_within_5ft, out.events,
                  req.ranged_hostile_within_5ft || req.beyond_normal_range, 0, req.ally_near_target);
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
    case ActionKind::Heal: case ActionKind::AbilityCheck: break;  // handled above
    }
    return fail("unknown action");
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
    if (b.action || b.attacks_remaining > 0) {
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
                if (!b.action && i < 32 && has_property(a.attacks[i], kWeaponLoading) && (b.loading_fired >> i & 1)) continue;
                ActionRequest r{a.id, ActionKind::Attack, t.id, i};
                out.push_back(r);
            }
            if (b.action)   // start any declared multiattack (needs a fresh action)
                for (std::size_t k = 0; k < a.multiattacks.size(); ++k) {
                    const auto& seq = a.multiattacks[k].attacks;
                    if (seq.empty()) continue;
                    ActionRequest r{a.id, ActionKind::Attack, t.id, seq[0]};
                    r.multiattack = uint8_t(k + 1);
                    out.push_back(r);
                }
        }
    }
    if (b.action) {
        add(ActionKind::Dash); add(ActionKind::Disengage); add(ActionKind::Dodge); add(ActionKind::Hide);
        for (const auto& t : s.combatants)
            if (t.id != a.id && is_dying(t) && !t.regen_down && t.team == a.team) add(ActionKind::Stabilize, false, t.id);
        // Help and Ready need sim facts (adjacency, trigger); sim builds those requests.
    }
    if (b.bonus_action)
        for (ActionKind k : {ActionKind::Dash, ActionKind::Disengage, ActionKind::Dodge, ActionKind::Hide})
            if (a.bonus_action_grants & bit(k)) add(k, true);
    int speed = effective_speed(a);
    if (a.has(Condition::Prone) && speed > 0 && b.movement_ft >= speed / 2) add(ActionKind::StandUp);
    if (b.movement_ft >= (a.has(Condition::Prone) ? 10 : 5)) {
        ActionRequest r{a.id, ActionKind::Move};
        r.move_ft = 5;
        out.push_back(r);
    }
    // Spells: every known non-reaction SpellBook spell, at every slot level available, at each
    // valid target (hostile for attacks/saves/darts, same team for healing). Range and sight are
    // sim's (hooks.spell_target_ok); multi-target / split-dart requests are built by sim or AI.
    if (s.hooks.spells && !is_incapacitated(a)) {
        for (uint32_t id : a.spells_known) {
            const SpellDef* d = s.hooks.spells->find(id);
            if (!d || (d->casting_time != CastingTime::Action && d->casting_time != CastingTime::BonusAction)) continue;
            if (d->casting_time == CastingTime::BonusAction ? !b.bonus_action : !b.action) continue;
            if (s.hooks.can_provide_components && !s.hooks.can_provide_components(s, a, *d)) continue;
            if (d->usage_kind == 1) { const ActionUse* u = find_action_use(s, a.id, d->id); if (u && u->expended) continue; }
            if (d->usage_kind == 2 && feature_uses_left(s, a.id, d->id) == 0) continue;
            if (d->usage_kind == 3 && find_pool(s, a.id, d->pool_id) &&
                !can_spend(s, a.id, d->pool_id, d->pool_cost ? d->pool_cost : 1)) continue;
            for (uint8_t L = d->level; L <= (d->level == 0 ? 0 : 9); ++L) {
                if (!has_slot(a, L)) continue;
                for (const auto& t : s.combatants) {
                    if (t.dead) continue;
                    // Heal, and Invisibility-style conditions (the only beneficial SRD condition), go on allies.
                    const bool heal = d->kind == SpellEffectKind::Heal ||
                                      (d->kind == SpellEffectKind::ApplyCondition && d->condition == Condition::Invisible);
                    if (heal ? t.team != a.team : (t.team == a.team || t.id == a.id)) continue;
                    if (!valid_target_type(*d, t) || (d->excluded_types & bit(t.creature_type))) continue;
                    if (s.hooks.spell_target_ok && !s.hooks.spell_target_ok(s, a, *d, t)) continue;
                    ActionRequest r{a.id, ActionKind::CastSpell, t.id};
                    r.spell_id = d->id; r.spell_level = L;
                    out.push_back(r);
                }
            }
        }
    }
    if (s.hooks.spells && !is_incapacitated(a)) {   // innate spells: no slot, own level, uses left (at will / per-day / recharged)
        for (const InnateSpell& is : a.innate) {
            const SpellDef* d = s.hooks.spells->find(is.spell_id);
            if (!d || !is.available()) continue;
            if (d->casting_time != CastingTime::Action && d->casting_time != CastingTime::BonusAction) continue;
            if (d->casting_time == CastingTime::BonusAction ? !b.bonus_action : !b.action) continue;
            if (is.as_listed && s.hooks.can_provide_components && !s.hooks.can_provide_components(s, a, *d)) continue;
            for (const auto& t : s.combatants) {
                if (t.dead) continue;
                const bool heal = d->kind == SpellEffectKind::Heal ||
                                  (d->kind == SpellEffectKind::ApplyCondition && d->condition == Condition::Invisible);
                if (heal ? t.team != a.team : (t.team == a.team || t.id == a.id)) continue;
                if (!valid_target_type(*d, t) || (d->excluded_types & bit(t.creature_type))) continue;
                if (s.hooks.spell_target_ok && !s.hooks.spell_target_ok(s, a, *d, t)) continue;
                ActionRequest r{a.id, ActionKind::CastSpell, t.id};
                r.spell_id = d->id; r.spell_level = d->level; r.innate = true;
                out.push_back(r);
            }
        }
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
        u32(c.trigger_turn_mask); u32(c.trigger_turn_stamp); u32(c.trigger_round_mask); u32(c.trigger_round_stamp);
    }
    return h;
}

}  // namespace rules5e
