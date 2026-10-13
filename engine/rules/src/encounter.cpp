#include "rules5e/encounter.hpp"

#include "rules5e/conditions.hpp"
#include "rules5e/damage.hpp"

namespace rules5e {

namespace {

Combatant* find_mut(EncounterState& s, CombatantId id) {
    for (auto& c : s.combatants) if (c.id == id) return &c;
    return nullptr;
}

bool out_of_fight(const Combatant& c) { return c.dead; }
bool can_act(const Combatant& c) { return !c.dead && c.hp > 0 && !is_incapacitated(c); }

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
    if (auto w = winner_team(s)) {
        s.ended = true;
        emit(ev, EventKind::EncounterEnded, 0, 0, *w);
    }
}

// Handles everything that follows damage: dropping to 0, massive damage, death-save failures.
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

void start_turn(EncounterState& s, std::vector<Event>& ev) {
    Combatant& c = *find_mut(s, s.order[s.turn_index].id);
    c.reaction_used = false;
    c.dodging = false;
    c.disengaged = false;
    emit(ev, EventKind::TurnStarted, c.id, 0, s.round);
    if (dies_of_exhaustion(c) && !c.dead) kill(c, ev);
    if (!c.dead && c.hp <= 0 && c.uses_death_saves && !c.death.stable) {
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
                break;
            case DeathSaveOutcome::Pending: break;
        }
    }
    bool act = can_act(c);
    s.budget.action = act;
    s.budget.bonus_action = act;
    s.budget.reaction = act;
    s.budget.movement_ft = int16_t(act || c.hp > 0 ? effective_speed(c) : 0);
    check_end(s, ev);
}

void advance_turn(EncounterState& s, std::vector<Event>& ev) {
    const auto n = s.order.size();
    for (std::size_t k = 0; k < n; ++k) {
        if (++s.turn_index >= n) { s.turn_index = 0; ++s.round; }
        if (!out_of_fight(*find_mut(s, s.order[s.turn_index].id))) break;
    }
    start_turn(s, ev);
}

StepResult fail(std::string_view why) { return {false, why, {}}; }

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

} // namespace

const Combatant* find(const EncounterState& s, CombatantId id) {
    for (const auto& c : s.combatants) if (c.id == id) return &c;
    return nullptr;
}

CombatantId current_actor(const EncounterState& s) {
    return s.order.empty() ? 0 : s.order[s.turn_index].id;
}

std::optional<uint8_t> winner_team(const EncounterState& s) {
    std::optional<uint8_t> team;
    for (const auto& c : s.combatants) {
        if (c.dead || c.hp <= 0) continue;  // dying / stable at 0 HP doesn't hold the field
        if (team && *team != c.team) return std::nullopt;
        team = c.team;
    }
    return team.value_or(uint8_t(0xFF));   // 0xFF: nobody left standing
}

EncounterState begin_encounter(std::vector<Combatant> cs, uint64_t seed, std::vector<Event>* out) {
    EncounterState s;
    s.rng = rng::Rng(seed, 0);
    s.combatants = std::move(cs);
    s.order = roll_initiative(s.rng, s.combatants);
    std::vector<Event> ev;
    for (const auto& e : s.order) emit(ev, EventKind::InitiativeRolled, e.id, 0, e.total);
    if (!s.order.empty()) {
        if (out_of_fight(*find_mut(s, s.order[0].id))) advance_turn(s, ev);
        else start_turn(s, ev);
    }
    if (out) *out = std::move(ev);
    return s;
}

StepResult step(EncounterState& s, const ActionRequest& req) {
    if (s.ended) return fail("encounter ended");
    if (s.order.empty()) return fail("no combatants");

    // Opportunity attacks are the only off-turn action.
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
        AttackContext ctx; ctx.target_within_5ft = req.target_within_5ft;
        auto res = resolve_attack(s.rng, *a, a->attacks[req.attack_index], *t, ctx);
        emit(out.events, EventKind::AttackResolved, a->id, t->id, res.roll.natural, res.roll.total,
             int32_t(res.hit) | (int32_t(res.critical) << 1));
        if (res.hit) after_damage(*t, apply_damage(*t, res.damage, res.type), res.critical, out.events);
        check_end(s, out.events);
        return out;
    }

    if (req.actor != current_actor(s)) return fail("not this combatant's turn");
    Combatant& a = *find_mut(s, req.actor);
    if ((a.dead || a.hp <= 0) && req.kind != ActionKind::EndTurn) return fail("actor is down");
    TurnBudget b = s.budget;               // mutate a copy; commit only on success
    StepResult out{true, {}, {}};

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
    case ActionKind::Dash: {
        if (!spend_action(b, a, req.kind, req.as_bonus_action)) return fail("no action available");
        b.movement_ft = int16_t(b.movement_ft + effective_speed(a));
        s.budget = b;
        emit(out.events, EventKind::Dashed, a.id, 0, b.movement_ft);
        return out;
    }
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
        if (!spend_action(b, a, req.kind, false)) return fail("no action available");
        s.budget = b;
        AttackContext ctx; ctx.target_within_5ft = req.target_within_5ft;
        auto res = resolve_attack(s.rng, a, a.attacks[req.attack_index], *t, ctx);
        emit(out.events, EventKind::AttackResolved, a.id, t->id, res.roll.natural, res.roll.total,
             int32_t(res.hit) | (int32_t(res.critical) << 1));
        if (res.hit) after_damage(*t, apply_damage(*t, res.damage, res.type), res.critical, out.events);
        check_end(s, out.events);
        return out;
    }
    case ActionKind::Help: case ActionKind::Hide: case ActionKind::Ready:
        return fail("action not implemented yet");
    case ActionKind::OpportunityAttack: break;  // handled above
    }
    return fail("unknown action");
}

std::vector<ActionRequest> legal_actions(const EncounterState& s) {
    std::vector<ActionRequest> out;
    if (s.ended || s.order.empty()) return out;
    const Combatant& a = *find(s, current_actor(s));
    const TurnBudget& b = s.budget;
    auto add = [&](ActionKind k, bool bonus = false) { out.push_back(ActionRequest{a.id, k, 0, 0, 0, bonus, false}); };
    if (b.action) {
        for (const auto& t : s.combatants) {
            if (t.dead || t.id == a.id || t.team == a.team) continue;
            for (uint8_t i = 0; i < a.attacks.size(); ++i)
                out.push_back(ActionRequest{a.id, ActionKind::Attack, t.id, i, 0, false, false});
        }
        add(ActionKind::Dash); add(ActionKind::Disengage); add(ActionKind::Dodge);
    }
    if (b.bonus_action)
        for (ActionKind k : {ActionKind::Dash, ActionKind::Disengage, ActionKind::Dodge})
            if (a.bonus_action_grants & bit(k)) add(k, true);
    int speed = effective_speed(a);
    if (a.has(Condition::Prone) && speed > 0 && b.movement_ft >= speed / 2) add(ActionKind::StandUp);
    if (b.movement_ft >= (a.has(Condition::Prone) ? 10 : 5))
        out.push_back(ActionRequest{a.id, ActionKind::Move, 0, 0, 5, false, false});
    add(ActionKind::EndTurn);
    return out;
}

} // namespace rules5e
