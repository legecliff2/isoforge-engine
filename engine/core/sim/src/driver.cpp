#include "sim/driver.hpp"

#include "rules5e/conditions.hpp"

namespace eng::sim {

using rules5e::ActionKind;

std::optional<CombatantId> NearestEnemyPolicy::choose_target(const Combat& c, CombatantId self) {
    std::optional<CombatantId> best;
    int best_d = 0;
    for (const auto& k : c.state().combatants) {   // combatant order; tie -> lowest id below
        if (k.id == self || !c.hostile(self, k.id) || k.dead || !rules5e::is_conscious(k)) continue;
        const int d = chebyshev(c.pos(self), c.pos(k.id));
        if (!best || d < best_d || (d == best_d && k.id < *best)) { best = k.id; best_d = d; }
    }
    return best;
}

Command next_command(const Combat& c, std::optional<CombatantId> target) {
    const auto& s = c.state();
    const CombatantId self = rules5e::current_actor(s);
    Command end;
    end.request = {self, ActionKind::EndTurn};
    const auto* me = rules5e::find(s, self);
    const auto* t = target ? rules5e::find(s, *target) : nullptr;
    if (!me || !t || t->dead || !rules5e::is_conscious(*me) || rules5e::is_incapacitated(*me)) return end;

    // Active features (data-driven: whatever the archetype's hooks registered). v0 tactic: a
    // self-heal when at or below its threshold.
    if (const Archetype* arch = c.archetype(self))
        for (const auto& f : arch->active)
            if (f.heal.count > 0 && c.can_use_feature(self, f.feature_id) &&
                int(me->hp) * int(f.use_below_den) <= int(me->hp_max) * int(f.use_below_num)) {
                Command u;
                u.request = {self, ActionKind::UseFeature};
                u.request.feature_id = f.feature_id;
                return u;
            }
    // Prefer the first melee attack; fall back to the first attack.
    uint8_t ai = 0;
    for (uint8_t i = 0; i < me->attacks.size(); ++i)
        if (!me->attacks[i].ranged) { ai = i; break; }
    if (me->attacks.empty()) return end;
    const int reach = reach_tiles(me->attacks[ai].reach_or_range_ft);
    const Vec2i mp = c.pos(self), tp = c.pos(t->id);
    const bool can_attack = s.budget.action || s.budget.attacks_remaining > 0;
    if (chebyshev(mp, tp) <= reach) {
        if (can_attack && rules5e::is_conscious(*t)) {
            Command a;
            a.request = {self, ActionKind::Attack, t->id};
            a.request.attack_index = ai;
            return a;
        }
        return end;
    }
    // Goals: tiles within reach of the target.
    std::vector<Vec2i> goals;
    for (int dy = -reach; dy <= reach; ++dy)
        for (int dx = -reach; dx <= reach; ++dx)
            if (dx || dy) goals.push_back({tp.x + dx, tp.y + dy});
    const auto path = c.grid().path(mp, goals, c.occupied_except(self));
    if (path.empty() || c.grid().step_cost_ft(path[0]) > s.budget.movement_ft) return end;
    // About to leave a hostile's reach: Disengage first if a bonus Disengage is available
    // (granted by content, e.g. a bonus_action_grant feature).
    if (s.budget.bonus_action && !me->disengaged &&
        (me->bonus_action_grants & rules5e::bit(ActionKind::Disengage)) && c.would_leave_reach(self, mp, path[0])) {
        Command d;
        d.request = {self, ActionKind::Disengage};
        d.request.as_bonus_action = true;
        return d;
    }
    Command st;
    st.kind = CommandKind::Step;
    st.to = path[0];
    return st;
}

RunResult run_to_end(Combat& c, const PolicyFor& policy_for, uint32_t max_commands) {
    RunResult rr;
    uint16_t round = 0, turn = 0xFFFF;
    std::optional<CombatantId> target;
    // Sync adapter: answer each NeedsDecision with the reactor's policy.
    const ReactionDeciderFn decider = [&](const Combat& cc, const NeedsDecision& n) {
        IActorPolicy* p = policy_for(n.reactor);
        return p ? p->decide_reaction(cc, n) : ReactionDecision{n.window_seq, false, 0};
    };
    while (!c.ended() && rr.commands < max_commands) {
        const CombatantId self = rules5e::current_actor(c.state());
        if (c.state().round != round || c.state().turn_index != turn) {
            round = c.state().round;
            turn = c.state().turn_index;
            IActorPolicy* p = policy_for(self);
            target = p ? p->choose_target(c, self) : std::nullopt;
        }
        const Command cmd = next_command(c, target);
        auto r = c.submit(cmd);
        if (r.ok && r.status == StepStatus::NeedsDecision) r = c.settle(decider);
        ++rr.commands;
        if (!r.ok) {
            rr.error = r.error;
            return rr;
        }
    }
    rr.ended = c.ended();
    if (!rr.ended && rr.error.empty()) rr.error = "command limit reached";
    return rr;
}

} // namespace eng::sim
