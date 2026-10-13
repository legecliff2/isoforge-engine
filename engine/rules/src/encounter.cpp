#include "rules5e/encounter.hpp"

#include <array>

#include "rules5e/checks.hpp"
#include "rules5e/conditions.hpp"
#include "rules5e/damage.hpp"
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

// One attack roll, including Help and hidden-attacker advantage. Attacking reveals the attacker.
void do_attack(EncounterState& s, Combatant& a, Combatant& t, uint8_t attack_index, bool within_5ft,
               std::vector<Event>& ev) {
    bool adv = false;
    if (a.helped_vs == t.id) { adv = true; a.helped_vs = 0; a.helped_by = 0; }
    if (a.stealth_total > 0) { adv = true; a.stealth_total = 0; }   // unseen attacker; now revealed
    AttackContext ctx;
    ctx.target_within_5ft = within_5ft;
    ctx.mode = adv ? RollMode::Advantage : RollMode::Normal;
    auto res = resolve_attack(s.rng, a, a.attacks[attack_index], t, ctx);
    emit(ev, EventKind::AttackResolved, a.id, t.id, res.roll.natural, res.roll.total,
         int32_t(res.hit) | (int32_t(res.critical) << 1));
    if (res.hit) after_damage(t, apply_damage(t, res.damage, res.type), res.critical, ev);
}

void start_turn(EncounterState& s, std::vector<Event>& ev) {
    Combatant& c = *find_mut(s, s.order[s.turn_index].id);
    c.reaction_used = false;
    c.dodging = false;
    c.disengaged = false;
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

// Ready: offer each event of this step to each readied action (readied order, then event order).
// Responses' own events are not re-offered (no reaction chains).
void fire_readies(EncounterState& s, std::vector<Event>& ev) {
    if (!s.hooks.ready_trigger || s.readied.empty()) return;
    const std::size_t n = ev.size();
    for (std::size_t i = 0; i < n && !s.ended; ++i) {
        const Event e = ev[i];
        for (std::size_t k = 0; k < s.readied.size();) {
            const ReadiedAction r = s.readied[k];
            Combatant* a = find_mut(s, r.actor);
            if (!a || !can_act(*a) || a->reaction_used || !s.hooks.ready_trigger(s, r, e)) { ++k; continue; }
            s.readied.erase(s.readied.begin() + std::ptrdiff_t(k));
            a->has_readied = false;
            a->reaction_used = true;
            emit(ev, EventKind::ReadyTriggered, a->id, r.response.target, int32_t(r.trigger_id));
            if (r.response.kind == ActionKind::Attack) {
                Combatant* t = find_mut(s, r.response.target);
                if (t && !t->dead && r.response.attack_index < a->attacks.size())
                    do_attack(s, *a, *t, r.response.attack_index, r.response.target_within_5ft, ev);
            } else if (r.response.kind == ActionKind::Move) {
                int16_t ft = r.response.move_ft;
                int sp = effective_speed(*a);
                if (ft > sp) ft = int16_t(sp);
                emit(ev, EventKind::Moved, a->id, 0, ft, 0);
            }
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

StepResult step_impl(EncounterState& s, const ActionRequest& req);

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

StepResult step(EncounterState& s, const ActionRequest& req) {
    StepResult r = step_impl(s, req);
    if (r.ok && req.kind != ActionKind::Ready) fire_readies(s, r.events);
    return r;
}

StepResult encounter_check(EncounterState& s, CombatantId id, Skill k, int dc, RollMode mode) {
    if (s.ended) return fail("encounter ended");
    Combatant* c = find_mut(s, id);
    if (!c || c->dead) return fail("bad combatant");
    StepResult out{true, {}, {}};
    roll_check(s, *c, k, dc, mode, out.events);
    fire_readies(s, out.events);
    return out;
}

StepResult heal(EncounterState& s, CombatantId id, int16_t amount) {
    if (s.ended) return fail("encounter ended");
    Combatant* t = find_mut(s, id);
    if (!t) return fail("bad target");
    if (t->dead) return fail("target is dead");
    if (amount <= 0) return fail("amount must be positive");
    StepResult out{true, {}, {}};
    bool was_unconscious = t->has(Condition::Unconscious);
    auto h = apply_healing(*t, amount);
    commit_healing(*t, h);
    emit(out.events, EventKind::Healed, 0, t->id, h.healed, h.hp_after, h.from_zero);
    if (h.from_zero && was_unconscious)
        emit(out.events, EventKind::ConditionRemoved, t->id, 0, int32_t(Condition::Unconscious));
    check_end(s, out.events);
    fire_readies(s, out.events);
    return out;
}

namespace {

StepResult step_impl(EncounterState& s, const ActionRequest& req) {
    if (s.ended) return fail("encounter ended");
    if (s.order.empty()) return fail("no combatants");

    // Opportunity attacks are the only directly requested off-turn action.
    if (req.kind == ActionKind::OpportunityAttack) {
        Combatant* a = find_mut(s, req.actor);
        Combatant* t = find_mut(s, req.target);
        if (!a || !t || a == t) return fail("bad actor/target");
        if (!can_act(*a) || a->reaction_used) return fail("no reaction available");
        if (t->dead) return fail("target is dead");
        if (t->disengaged) return fail("target disengaged");
        if (req.attack_index >= a->attacks.size()) return fail("bad attack index");
        if (req.actor == current_actor(s)) return fail("cannot react on own turn");
        StepResult out{true, {}, {}};
        a->reaction_used = true;
        do_attack(s, *a, *t, req.attack_index, req.target_within_5ft, out.events);
        check_end(s, out.events);
        return out;
    }

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
        b.movement_ft = int16_t(b.movement_ft - cost);
        s.budget = b;
        emit(out.events, EventKind::Moved, a.id, 0, req.move_ft, b.movement_ft);
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
        do_attack(s, a, *t, req.attack_index, req.target_within_5ft, out.events);
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
    case ActionKind::OpportunityAttack: break;  // handled above
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
