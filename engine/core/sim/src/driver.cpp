#include "sim/driver.hpp"

#include "rules5e/conditions.hpp"

#include <algorithm>
#include <cstdlib>

namespace eng::sim {

using rules5e::ActionKind;

std::optional<CombatantId> NearestEnemyPolicy::choose_target(const Combat& c, CombatantId self) {
    std::optional<CombatantId> best;
    int best_d = 0;
    for (const auto& k : c.state().combatants) {   // combatant order; tie -> lowest id below
        if (k.id == self || !c.hostile(self, k.id) || k.dead || !rules5e::is_conscious(k)) continue;
        const int d = c.distance_tiles(self, k.id);
        if (!best || d < best_d || (d == best_d && k.id < *best)) { best = k.id; best_d = d; }
    }
    return best;
}

namespace {
// Anchors from which a creature of side `mn` stands within `reach` tiles of the `tn`-side creature at
// `tp`, excluding anchors that overlap it. For 1x1 vs 1x1 this is the 8 * reach-ring around `tp`.
std::vector<Vec2i> goal_anchors(Vec2i tp, int tn, int mn, int reach) {
    std::vector<Vec2i> g;
    for (int y = tp.y - reach - mn + 1; y <= tp.y + tn - 1 + reach; ++y)
        for (int x = tp.x - reach - mn + 1; x <= tp.x + tn - 1 + reach; ++x)
            if (rect_distance({x, y}, mn, tp, tn) > 0) g.push_back({x, y});
    return g;
}
} // namespace

Command next_command(const Combat& c, std::optional<CombatantId> target) {
    const auto& s = c.state();
    const CombatantId self = rules5e::current_actor(s);
    Command end;
    end.request = {self, ActionKind::EndTurn};
    const auto* me = rules5e::find(s, self);
    const auto* t = target ? rules5e::find(s, *target) : nullptr;
    if (!me || !t || t->dead || !rules5e::is_conscious(*me) || rules5e::is_incapacitated(*me)) return end;
    // R-G8 Charmed: the charmer is never attacked (rules refuse it) or targeted with a harmful spell (sim refuses it).
    if (rules5e::condition_source(s, self, rules5e::Condition::Charmed) == t->id) return end;

    // Pool features (row 13). Lay on Hands (heal_pool): at or below half hp, heal self for min(pool, missing hp); the caller's
    // `amount` and (default self) target come from here. Action Surge (grant_action): once the action is spent with a hostile
    // adjacent, take the extra action. The AI never picks a pool feature whose pool is short.
    if (const Archetype* arch = c.archetype(self))
        for (const auto& f : arch->active) {
            const auto sp = arch->feature_specs.find(f.feature_id);
            if (sp == arch->feature_specs.end() || !c.can_use_feature(self, f.feature_id)) continue;
            const rules5e::FeatureSpec& spec = sp->second;
            const rules5e::ResourcePool* pool = spec.pool ? rules5e::find_pool(s, self, spec.pool) : nullptr;
            if (spec.heal_pool) {
                if (!pool || pool->cur == 0 || int(me->hp) * 2 > int(me->hp_max)) continue;
                Command u;
                u.request = {self, ActionKind::UseFeature};
                u.request.feature_id = f.feature_id;
                u.request.amount = int16_t(std::min<int>(pool->cur, me->hp_max - me->hp));
                return u;
            }
            if (spec.grant_actions > 0 && !s.budget.action && s.budget.extra_actions == 0 && s.budget.attacks_remaining == 0 &&
                c.distance_tiles(self, t->id) <= 1 && (!spec.pool || rules5e::can_spend(s, self, spec.pool, spec.pool_cost ? spec.pool_cost : 1))) {
                Command u;
                u.request = {self, ActionKind::UseFeature};
                u.request.feature_id = f.feature_id;
                return u;
            }
        }
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
    // Modifier features (row 12: Rage-like, applies_to self): use one when its cost is available, a hostile is within
    // reach or one move + reach, and none of its modifiers from this source is already on the creature. The AI never
    // casts modifier spells (Bless, Haste, ...) and never targets allies with features: that stays a caller decision.
    if (const Archetype* arch = c.archetype(self))
        for (const auto& f : arch->active) {
            const auto mods = arch->feature_modifiers.find(f.feature_id);
            if (mods == arch->feature_modifiers.end() || !c.can_use_feature(self, f.feature_id)) continue;
            bool self_only = true, active = false;
            for (const auto& m : mods->second) {
                self_only = self_only && m.applies_to == rules5e::ModAppliesTo::Self;
                for (const auto& am : s.modifiers) active = active || (am.target == self && am.spec.id == m.id);
            }
            const int reach_now = c.distance_tiles(self, t->id) * kFeetPerTile;
            if (!self_only || active || reach_now > int(me->speed_ft) + 10) continue;
            Command u;
            u.request = {self, ActionKind::UseFeature};
            u.request.feature_id = f.feature_id;
            return u;
        }
    // Save actions (monster@4 / character@4, VERSION_BUMP_BATCH row 6): a single-target one (no area template) that
    // is available and in reach goes before a multiattack or a plain attack. Availability is read from the rules
    // state (recharge row expended, uses left); the AI never rolls or keeps its own flag. Area actions are the
    // area-template row's business.
    if (const Archetype* arch = c.archetype(self); arch && s.budget.action && s.budget.sequence == 0 &&
                                                   s.budget.attacks_remaining == 0 && rules5e::is_conscious(*t))
        for (const auto& sa : arch->save_actions) {
            if (sa.usage_kind == 1) {
                const auto* u = rules5e::find_action_use(s, self, sa.spell_id);
                if (u && u->expended) continue;
            } else if (sa.usage_kind == 2 && rules5e::feature_uses_left(s, self, sa.spell_id) == 0) continue;
            else if (sa.usage_kind == 3) {
                if (!rules5e::can_spend(s, self, sa.pool_id, sa.pool_cost ? sa.pool_cost : 1)) continue;
            }
            if (sa.area) {   // area template: aim at a hostile; never catch an ally; >= 2 hostiles (1 if no attack is legal)
                const auto* def = c.spell_def(sa.spell_id);
                if (!def) continue;
                bool attack_legal = false;
                for (uint8_t i = 0; i < me->attacks.size() && !attack_legal; ++i)
                    attack_legal = me->attacks[i].ranged ? c.can_shoot(self, i, t->id)
                                                         : c.distance_tiles(self, t->id) <= reach_tiles(me->attacks[i].reach_or_range_ft);
                std::optional<Command> best;
                std::size_t best_n = 0;
                for (const auto& k : s.combatants) {
                    if (k.id == self || !c.hostile(self, k.id) || k.dead || !rules5e::is_conscious(k)) continue;
                    const auto hit = c.area_targets(self, *def, c.pos(k.id));
                    std::size_t hostile_n = 0;
                    bool ally = false;
                    for (CombatantId id : hit) {
                        if (c.hostile(self, id)) ++hostile_n; else ally = true;
                    }
                    if (hit.empty() || ally || hostile_n <= best_n) continue;   // first aim wins a tie
                    best_n = hostile_n;
                    Command cast;
                    cast.request = {self, ActionKind::CastSpell, hit[0]};
                    cast.request.spell_id = sa.spell_id;
                    cast.request.spell_level = 0;
                    cast.aim = true;
                    cast.to = c.pos(k.id);
                    best = cast;
                }
                if (best && (best_n >= 2 || !attack_legal)) return *best;
                continue;
            }
            if (sa.range_ft == 0 || c.distance_tiles(self, t->id) * kFeetPerTile > int(sa.range_ft) ||
                !c.line_of_sight(self, t->id))
                continue;
            Command cast;
            cast.request = {self, ActionKind::CastSpell, t->id};
            cast.request.spell_id = sa.spell_id;
            cast.request.spell_level = 0;
            cast.request.targets = {t->id};
            return cast;
        }
    // Rampage (trigger rider bonus_attack): while a grant is present, the bonus-action attack is the granted attack index
    // (grant - 1) against the target if it is in reach (melee) or legal to shoot; otherwise the grant is left unused.
    if (s.budget.bonus_attack_grant != 0 && s.budget.bonus_action && rules5e::is_conscious(*t)) {
        const uint8_t gi = uint8_t(s.budget.bonus_attack_grant - 1);
        if (gi < me->attacks.size()) {
            const bool ok = me->attacks[gi].ranged ? c.can_shoot(self, gi, t->id)
                                                   : c.distance_tiles(self, t->id) <= reach_tiles(me->attacks[gi].reach_or_range_ft);
            if (ok) {
                Command b;
                b.request = {self, ActionKind::Attack, t->id};
                b.request.attack_index = gi;
                b.request.as_bonus_action = true;
                return b;
            }
        }
    }
    // R-G8 grapple / shove / escape (only arise once something grapples; no existing content does):
    //  * held (Grappled with a recorded holder) and the target is out of reach: Escape (takes the action);
    //  * the target holds one of my allies and is within 5 ft: Shove it 5 ft away (breaks the hold when the push lands);
    //  * I hold the target and it is not prone: Shove it prone.
    {
        const bool fresh_action = s.budget.action && s.budget.attacks_remaining == 0 && s.budget.sequence == 0;
        const bool attack_open = s.budget.action || s.budget.attacks_remaining > 0;
        const bool adjacent = c.distance_tiles(self, t->id) <= 1;
        auto special = [&](uint8_t kind, CombatantId who) {
            Command x;
            x.request = {self, ActionKind::Attack, who};
            x.request.special = kind;
            return x;
        };
        const CombatantId holder = rules5e::condition_source(s, self, rules5e::Condition::Grappled);
        if (holder != 0 && fresh_action && (me->conditions & rules5e::bit(rules5e::Condition::Grappled))) {
            bool reach_ok = false;
            for (const auto& at : me->attacks)
                reach_ok = reach_ok || (at.ranged ? false : c.distance_tiles(self, t->id) <= reach_tiles(at.reach_or_range_ft));
            if (!reach_ok) return special(4, holder);
        }
        if (attack_open && adjacent && rules5e::is_conscious(*t) && s.budget.sequence == 0) {
            for (CombatantId held : rules5e::grappled_by(s, t->id))
                if (held != self && !c.hostile(self, held)) return special(3, t->id);
            if (rules5e::condition_source(s, t->id, rules5e::Condition::Grappled) == self &&
                !(t->conditions & rules5e::bit(rules5e::Condition::Prone)))
                return special(2, t->id);
        }
    }
    // Prefer the first melee attack; fall back to the first attack.
    uint8_t ai = 0;
    for (uint8_t i = 0; i < me->attacks.size(); ++i)
        if (!me->attacks[i].ranged) { ai = i; break; }
    if (me->attacks.empty()) return end;
    // Declared multiattack (T&C-G1): continue the sequence in progress, else start the first option made
    // only of melee attacks (declaration order; options with a ranged attack are never picked by the AI).
    uint8_t multi = 0;   // ActionRequest::multiattack to send (0 = continue / plain attack)
    if (s.budget.sequence != 0 && s.budget.sequence <= me->multiattacks.size()) {
        const auto& seq = me->multiattacks[std::size_t(s.budget.sequence - 1)].attacks;
        if (s.budget.sequence_pos < seq.size()) ai = seq[s.budget.sequence_pos];
    } else {
        for (std::size_t i = 0; i < me->multiattacks.size() && i < 255; ++i) {
            const auto& seq = me->multiattacks[i].attacks;
            bool melee_only = !seq.empty();
            for (uint8_t k : seq) melee_only = melee_only && k < me->attacks.size() && !me->attacks[k].ranged;
            if (!melee_only) continue;
            ai = seq[0];
            multi = uint8_t(i + 1);
            break;
        }
    }
    const int reach = reach_tiles(me->attacks[ai].reach_or_range_ft);
    const Vec2i mp = c.pos(self), tp = c.pos(t->id);
    const int mn = c.footprint_size(self), tn = c.footprint_size(t->id);
    const bool can_attack = s.budget.action || s.budget.extra_actions > 0 || s.budget.attacks_remaining > 0;
    if (c.distance_tiles(self, t->id) <= reach) {
        if (can_attack && rules5e::is_conscious(*t)) {
            Command a;
            a.request = {self, ActionKind::Attack, t->id};
            a.request.attack_index = ai;
            if (s.budget.attacks_remaining == 0) a.request.multiattack = multi;   // only the opener names the option
            return a;
        }
        return end;
    }
    // Can't get into melee reach this turn: shoot if a ranged attack is legal (range + LOS),
    // preferring one without disadvantage.
    auto melee_path_cost = [&]() {
        const auto p = c.grid().path(mp, goal_anchors(tp, tn, mn, reach), c.occupied_except(self), mn);
        if (p.empty()) return 1 << 20;
        int cost = 0;
        Vec2i prev = mp;
        for (Vec2i q : p) { cost += c.grid().move_cost_ft(prev, q, mn); prev = q; }
        return cost;
    };
    if (can_attack && melee_path_cost() > s.budget.movement_ft) {
        std::optional<uint8_t> best;
        bool best_clean = false;
        for (uint8_t i = 0; i < me->attacks.size(); ++i) {
            if (!me->attacks[i].ranged || !c.can_shoot(self, i, t->id)) continue;
            const bool clean = !c.ranged_disadvantage(self, i, t->id);
            if (!best || (clean && !best_clean)) { best = i; best_clean = clean; }
        }
        if (best) {
            Command a;
            a.request = {self, ActionKind::Attack, t->id};
            a.request.attack_index = *best;
            return a;
        }
    }
    // Goals: tiles within reach of the target.
    const auto path = c.grid().path(mp, goal_anchors(tp, tn, mn, reach), c.occupied_except(self), mn);
    if (path.empty() || c.grid().move_cost_ft(mp, path[0], mn) > s.budget.movement_ft) return end;
    // About to leave a hostile's reach: Disengage first if a bonus Disengage is available
    // (granted by content, e.g. a bonus_action_grant feature).
    if (s.budget.bonus_action && !me->disengaged &&
        (me->bonus_action_grants & rules5e::bit(ActionKind::Disengage)) && c.would_leave_reach(self, mp, path[0])) {
        Command d;
        d.request = {self, ActionKind::Disengage};
        d.request.as_bonus_action = true;
        return d;
    }
    // R-G8 Frightened: a step that ends closer to the source of the fear is refused by the sim; stop instead.
    if (const CombatantId fear = rules5e::condition_source(s, self, rules5e::Condition::Frightened))
        if (c.distance_tiles(self, fear) > 0) {
            const Vec2i fp = c.pos(fear);
            const int before = std::max(std::abs(fp.x - mp.x), std::abs(fp.y - mp.y));
            const int after = std::max(std::abs(fp.x - path[0].x), std::abs(fp.y - path[0].y));
            if (after < before) return end;
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
