#include "rules5e/encounter.hpp"

#include <algorithm>
#include <array>

#include "rules5e/checks.hpp"
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
    if (d.instant_death) { kill(t, ev); return; }
    if (d.dropped_to_zero) {
        if (!t.uses_death_saves) { kill(t, ev); return; }
        emit(ev, EventKind::CombatantDowned, t.id);
        t.death = DeathSaves{};
        for (Condition k : {Condition::Unconscious, Condition::Prone}) {
            if (!t.has(k)) { add_condition(t, k); emit(ev, EventKind::ConditionAdded, t.id, 0, int32_t(k)); }
        }
        return;
    }
    if (was_at_zero && d.dealt > 0 && t.uses_death_saves) {
        t.death = death_save_failures_from_damage(t.death, critical);
        emit(ev, EventKind::DeathSaveRolled, t.id, 0, 0, t.death.successes, t.death.failures);
        if (t.death.failures >= 3) kill(t, ev);
    }
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
            ctx.need = NeedsDecision{w.seq, w.trigger, r.id, w.trigger_actor,
                                     std::vector<ReactionOption>(opts.begin(), opts.end()), w};
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
void do_attack(EncounterState& s, Ctx& ctx, Combatant& a, Combatant& t, uint8_t attack_index,
               bool within_5ft, std::vector<Event>& ev, bool ranged_disadvantage = false) {
    bool adv = false;
    if (a.helped_vs == t.id) { adv = true; a.helped_vs = 0; a.helped_by = 0; }
    if (a.stealth_total > 0) { adv = true; a.stealth_total = 0; }   // unseen attacker; now revealed
    AttackContext actx;
    actx.target_within_5ft = within_5ft;
    const Attack& atk = a.attacks[attack_index];
    actx.mode = combine_modes(adv, ranged_disadvantage && atk.ranged);
    D20Roll roll = roll_d20(s.rng, atk.to_hit, attack_roll_mode(a, t, actx));
    bool crit = false;
    bool hit = evaluate_hit(roll, t, actx, crit);
    if (hit) {
        ReactionWindow w = new_window(s, ctx, ReactionTrigger::BeingHit, a.id);
        w.target = t.id; w.attack_total = roll.total; w.attack_natural = roll.natural;
        if (may_open(s, w) && s.hooks.being_hit_options && may_react(s, t, w.trigger) && in_range(s, w, t)) {
            auto opts = s.hooks.being_hit_options(s, w, t);
            if (!opts.empty()) {
                ++s.next_window_seq;
                emit(ev, EventKind::ReactionWindowOpened, a.id, t.id, int32_t(w.trigger), int32_t(w.seq));
                // Auto only spends the reaction when option 0 would turn the hit into a miss.
                bool would_miss = false;
                if (opts[0].kind == ReactionOptionKind::AcBonus && roll.natural != 20) {
                    would_miss = roll.total < int(t.ac) + int(actx.cover_ac_bonus) + int(t.reaction_ac_bonus) + opts[0].ac_bonus;
                }
                auto d = poll(s, ctx, w, t, opts, ev, would_miss);
                if (d.react) {
                    t.reaction_used = true;
                    const auto& o = opts[d.option];
                    if (o.kind == ReactionOptionKind::AcBonus) {
                        t.reaction_ac_bonus = int8_t(t.reaction_ac_bonus + o.ac_bonus);
                        emit(ev, EventKind::AcBonus, t.id, 0, o.ac_bonus, t.ac + t.reaction_ac_bonus);
                        hit = evaluate_hit(roll, t, actx, crit);
                    }
                }
            }
        }
    }
    if (hit && within_5ft && (t.has(Condition::Paralyzed) || t.has(Condition::Unconscious))) crit = true;
    emit(ev, EventKind::AttackResolved, a.id, t.id, roll.natural, roll.total,
         int32_t(hit) | (int32_t(crit) << 1));
    if (hit) {
        int dmg = roll_dice(s.rng, atk.damage, crit);
        if (dmg < 0) dmg = 0;
        after_damage(t, apply_damage(t, int16_t(dmg), atk.type), crit, ev);
    }
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
        r.reaction_used = true;
        std::size_t from = ev.size();
        ++ctx.depth;
        do_attack(s, ctx, r, mover, opts[d.option].attack_index, true, ev);   // reach: within 5 ft
        --ctx.depth;
        mark_depth(ev, from, uint8_t(ctx.depth + 1));
        if (mover.dead || mover.hp <= 0 || effective_speed(mover) == 0) return false;
    }
    return true;
}

// SpellCast: polls hostile counters. Returns true if the spell was countered. A counter is itself a spell,
// so it opens a nested SpellCast window when allow_chain permits (counter the counter).
bool spell_cast_window(EncounterState& s, Ctx& ctx, Combatant& caster, uint32_t spell_id, uint8_t level,
                       std::vector<Event>& ev) {
    ReactionWindow w = new_window(s, ctx, ReactionTrigger::SpellCast, caster.id);
    w.spell_id = spell_id; w.spell_level = level;
    if (!may_open(s, w) || !s.hooks.spell_cast_options) return false;
    auto eligible = [&](const Combatant& r) {
        return r.id != caster.id && r.team != caster.team && may_react(s, r, w.trigger) && in_range(s, w, r) &&
               !s.hooks.spell_cast_options(s, w, r).empty();
    };
    std::vector<CombatantId> cands;
    for (CombatantId id : initiative_ids(s)) if (eligible(*find_mut(s, id))) cands.push_back(id);
    bool opened = false;
    for (CombatantId rid : poll_order(s, cands)) {
        Combatant& r = *find_mut(s, rid);
        if (!eligible(r)) continue;
        auto opts = s.hooks.spell_cast_options(s, w, r);
        if (!opened) {
            opened = true; ++s.next_window_seq;
            emit(ev, EventKind::ReactionWindowOpened, caster.id, 0, int32_t(w.trigger), int32_t(w.seq));
        }
        auto d = poll(s, ctx, w, r, opts, ev);
        if (!d.react) continue;
        r.reaction_used = true;
        const auto& o = opts[d.option];
        std::size_t from = ev.size();
        ++ctx.depth;
        emit(ev, EventKind::SpellCast, r.id, caster.id, int32_t(o.option_id), 3);
        bool counter_countered = spell_cast_window(s, ctx, r, o.option_id, 3, ev);
        --ctx.depth;
        mark_depth(ev, from, uint8_t(ctx.depth + 1));
        if (counter_countered) {
            emit(ev, EventKind::SpellCountered, r.id, 0, int32_t(o.option_id), 3);
            continue;   // the counter failed; later reactors may still try
        }
        // TODO(spellcasting milestone, ARCHITECTURE.md §7 "Later"): Counterspell vs a spell above
        // the counter's slot level needs an ability check (DC 10 + spell level). Stub: always succeeds.
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
    if (dies_of_exhaustion(c) && !c.dead) kill(c, ev);
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
    s.budget = TurnBudget{act, act, act, int16_t(c.hp > 0 ? effective_speed(c) : 0), 0};
    check_end(s, ev);
}

void advance_turn(EncounterState& s, std::vector<Event>& ev) {
    const auto n = s.order.size();
    for (std::size_t k = 0; k < n; ++k) {
        if (++s.turn_index >= n) { s.turn_index = 0; ++s.round; }
        if (!find_mut(s, s.order[s.turn_index].id)->dead) break;
    }
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
            const ReadiedAction r = s.readied[k];
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
            emit(ev, EventKind::ReadyTriggered, a->id, r.response.target, int32_t(r.trigger_id));
            ++ctx.depth;
            if (r.response.kind == ActionKind::Attack) {
                Combatant* t = find_mut(s, r.response.target);
                if (t && !t->dead && r.response.attack_index < a->attacks.size())
                    do_attack(s, ctx, *a, *t, r.response.attack_index, r.response.target_within_5ft, ev,
                              r.response.ranged_hostile_within_5ft || r.response.beyond_normal_range);
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
        if (is_conscious(c)) conscious[c.team] = true;
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
    return NeedsDecision{p.window.seq, p.window.trigger, p.reactor, p.window.trigger_actor, p.options, p.window};
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
        s.budget = b;
        emit(out.events, EventKind::Moved, a.id, 0, req.move_ft, b.movement_ft);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::CastSpell: {   // stub: no effect beyond the SpellCast reaction window
        if (req.spell_id == 0) return fail("spell_id required");
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        emit(out.events, EventKind::SpellCast, a.id, req.target, int32_t(req.spell_id), req.spell_level);
        bool countered = spell_cast_window(s, ctx, a, req.spell_id, req.spell_level, out.events);
        emit(out.events, countered ? EventKind::SpellCountered : EventKind::SpellResolved, a.id, req.target,
             int32_t(req.spell_id), req.spell_level);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::UseFeature: {
        if (req.feature_id == 0) return fail("feature_id required");
        if (req.as_bonus_action) {
            if (!b.bonus_action) return fail("no bonus action available");
            b.bonus_action = false;
        } else if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
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
        if (b.attacks_remaining > 0) --b.attacks_remaining;               // Extra Attack
        else if (spend_action(b, a, req.kind, false))
            b.attacks_remaining = uint8_t(a.attacks_per_action > 1 ? a.attacks_per_action - 1 : 0);
        else return fail("no action available");
        s.budget = b;
        do_attack(s, ctx, a, *t, req.attack_index, req.target_within_5ft, out.events,
                  req.ranged_hostile_within_5ft || req.beyond_normal_range);
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
            for (uint8_t i = 0; i < a.attacks.size(); ++i) {
                ActionRequest r{a.id, ActionKind::Attack, t.id, i};
                out.push_back(r);
            }
        }
    }
    if (b.action) {
        add(ActionKind::Dash); add(ActionKind::Disengage); add(ActionKind::Dodge); add(ActionKind::Hide);
        for (const auto& t : s.combatants)
            if (t.id != a.id && is_dying(t) && t.team == a.team) add(ActionKind::Stabilize, false, t.id);
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
    add(ActionKind::EndTurn);
    return out;
}

} // namespace rules5e
