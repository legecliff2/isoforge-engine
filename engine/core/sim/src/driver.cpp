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

namespace {
// R-G15 hand-off (VERSION_BUMP_BATCH 7.2g b), only once such an effect is on the board (the AI never casts these spells itself):
//  * Heat Metal again: a bonus action CastSpell{sustain} on a living creature (not my charmer) holding my sustained-damage
//    effect, from the turn after the cast;
//  * Wake: the action (special 5) on an ally within 5 ft held by an ends-on-damage effect (Sleep).
std::optional<Command> spell_kind_command(const Combat& c, const rules5e::Combatant& me) {
    const auto& s = c.state();
    const CombatantId charmer = rules5e::condition_source(s, me.id, rules5e::Condition::Charmed);
    if (s.budget.bonus_action && s.budget.sequence == 0) {
        const uint32_t now = (uint32_t(s.round) << 16) | s.turn_index;
        for (const auto& e : s.effects) {
            if (e.caster != me.id || e.damage_when != 2 || e.cast_stamp == now || e.target == charmer || !c.spell_def(e.spell_id)) continue;
            const auto* v = rules5e::find(s, e.target);
            if (!v || v->dead) continue;
            Command x;
            x.request = {me.id, ActionKind::CastSpell, e.target};
            x.request.spell_id = e.spell_id;
            x.request.sustain = true;
            return x;
        }
    }
    if (s.budget.action && s.budget.attacks_remaining == 0 && s.budget.sequence == 0) {
        for (const auto& e : s.effects) {
            if (!e.ends_on_damage || e.target == me.id || c.hostile(me.id, e.target)) continue;
            const auto* v = rules5e::find(s, e.target);
            if (!v || v->dead || c.distance_tiles(me.id, e.target) > 1) continue;
            Command x;
            x.request = {me.id, ActionKind::Attack, e.target};
            x.request.special = 5;
            return x;
        }
    }
    return std::nullopt;
}

Command tactical_command(const Combat& c, std::optional<CombatantId> target) {
    const auto& s = c.state();
    const CombatantId self = rules5e::current_actor(s);
    Command end;
    end.request = {self, ActionKind::EndTurn};
    if (s.budget.gaze_pending) {   // Petrifying Gaze (7.2p): answer first, even surprised or incapacitated; AI default: look
        Command look;
        look.request = {self, ActionKind::GazeResponse};   // avert_gaze false; submit fills gaze_seen
        return look;
    }
    const auto* me = rules5e::find(s, self);
    if (!me || !rules5e::is_conscious(*me) || rules5e::is_incapacitated(*me)) return end;
    if (me->surprised) return end;   // R-G16: a surprised creature's first turn accepts only EndTurn
    // Stand up (Director-approved AI policy): a prone creature, at the start of its turn (nothing spent or moved yet), stands with
    // rules5e StandUp (half its speed, StoodUp) when it is not grappled, its speed is not 0 and its remaining movement is at least
    // half its speed. Otherwise it stays prone and acts as before (crawls, attacks with disadvantage).
    if (me->has(rules5e::Condition::Prone) && !me->has(rules5e::Condition::Grappled) && s.budget.action && s.budget.bonus_action &&
        s.budget.moved_ft == 0 && s.budget.attacks_used_mask == 0 && s.budget.sequence == 0) {
        const int speed = rules5e::effective_speed(*me);
        if (speed > 0 && s.budget.movement_ft >= speed / 2) {
            Command up;
            up.request = {self, ActionKind::StandUp};
            return up;
        }
    }
    // R-G12 (SUMMONS_TRANSFORMS §3.4): a Polymorph / Wild Shape form cannot cast (rules refuse it), so no spell or save-action
    // casts from the creature's own block while in it. Transform only when content asks (wild_shape ai: prefer_beast), at the
    // start of the turn, into the allowed form with the highest CR (ties: lowest form index). Never revert by choice: 0 HP,
    // the timer, concentration or the encounter's end revert it.
    const bool beast_form = me->form == rules5e::FormKind::Polymorph || me->form == rules5e::FormKind::WildShape;
    if (me->form == rules5e::FormKind::None && s.budget.sequence == 0 && s.budget.moved_ft == 0 && s.budget.attacks_used_mask == 0)
        if (const Archetype* arch = c.archetype(self))
            for (const auto& [fid, ff] : arch->form_features) {
                if (!ff.ai_prefer || !c.can_use_feature(self, fid)) continue;
                const auto sp = arch->feature_specs.find(fid);
                if (sp == arch->feature_specs.end() || (sp->second.cost == 2 ? !s.budget.bonus_action : !s.budget.action)) continue;
                int best = -1, best_cr = -1;
                for (std::size_t i = 0; i < ff.forms.size() && i < 255; ++i) {
                    const Archetype* fa = c.form_archetype(self, rules5e::FormKind(ff.kind), fid, uint8_t(i));
                    if (!fa || !c.build_form(*me, rules5e::FormKind(ff.kind), fid, uint8_t(i))) continue;
                    if (fa->cr_x8 > best_cr) { best_cr = fa->cr_x8; best = int(i); }
                }
                if (best < 0) continue;
                Command w;
                w.request = {self, ActionKind::UseFeature};
                w.request.feature_id = fid;
                w.request.form_archetype = uint8_t(best + 1);
                return w;
            }
    if (!beast_form)
        if (auto x = spell_kind_command(c, *me)) return *x;
    const auto* t = target ? rules5e::find(s, *target) : nullptr;
    if (!t || t->dead) return end;
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
    if (const Archetype* arch = c.archetype(self); arch && !beast_form && s.budget.action && s.budget.sequence == 0 &&
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
    // R-G6 bonus-action attack options: finish the option in progress (Flurry's second strike), else start the first one that is
    // available (rules check the bonus action, the prerequisite attacks, the modifier and the pool) with a legal target.
    {
        auto bonus_cmd = [&](std::size_t k) -> std::optional<Command> {
            const rules5e::BonusAttack& o = me->bonus_attacks[k];
            int bi = o.attack_index;
            if (o.any_melee) {
                bi = -1;
                for (std::size_t i = 0; i < me->attacks.size() && bi < 0; ++i) if (!me->attacks[i].ranged) bi = int(i);
            }
            if (bi < 0 || std::size_t(bi) >= me->attacks.size()) return std::nullopt;
            const bool ok = me->attacks[std::size_t(bi)].ranged
                                ? c.can_shoot(self, uint8_t(bi), t->id)
                                : c.distance_tiles(self, t->id) <= reach_tiles(me->attacks[std::size_t(bi)].reach_or_range_ft);
            if (!ok || !rules5e::is_conscious(*t)) return std::nullopt;
            Command b;
            b.request = {self, ActionKind::Attack, t->id};
            b.request.attack_index = uint8_t(bi);
            b.request.as_bonus_action = true;
            b.request.bonus_option = uint8_t(k + 1);
            return b;
        };
        if (s.budget.bonus_option != 0 && s.budget.bonus_remaining > 0) {
            if (std::size_t(s.budget.bonus_option) <= me->bonus_attacks.size())
                if (auto b = bonus_cmd(std::size_t(s.budget.bonus_option - 1))) return *b;
        } else {
            for (std::size_t k = 0; k < me->bonus_attacks.size(); ++k) {
                if (!rules5e::bonus_attack_available(s, *me, uint8_t(k + 1))) continue;
                if (auto b = bonus_cmd(k)) return *b;
            }
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
        for (Vec2i q : p) { cost += c.step_cost_ft(self, prev, q); prev = q; }
        return cost;
    };
    // Aggressive (rules #111, VERSION_BUMP_BATCH 7.2j c): out of reach with too little movement left, take the bonus-action
    // Dash toward the target when it is visible and reachable by a path, if that Dash gets it into reach this turn or no
    // ranged attack is legal now (otherwise shoot, as before). Only creatures with Combatant::aggressive (content kind
    // `aggressive`) do this, so no other combat changes.
    if (me->aggressive && s.budget.bonus_action && !me->has(rules5e::Condition::Blinded) && c.line_of_sight(self, t->id)) {
        const int need = melee_path_cost();
        if (need > s.budget.movement_ft && need < (1 << 20)) {
            bool shoot = false;
            for (uint8_t i = 0; i < me->attacks.size() && !shoot; ++i) shoot = me->attacks[i].ranged && c.can_shoot(self, i, t->id);
            if (!can_attack || !shoot || need <= s.budget.movement_ft + rules5e::effective_speed(*me)) {
                Command d;
                d.request = {self, ActionKind::Dash};
                d.request.as_bonus_action = true;
                d.request.aggressive = true;
                return d;
            }
        }
    }
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
    if (path.empty() || c.step_cost_ft(self, mp, path[0]) > s.budget.movement_ft) return end;
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
    // Aggressive: the Dash's movement only goes toward a hostile it can see (sim refuses other steps); stop instead.
    if (!c.aggressive_step_ok(self, path[0])) return end;
    Command st;
    st.kind = CommandKind::Step;
    st.to = path[0];
    return st;
}
}  // namespace

// The tactical choice, plus two declarations on the Attack action's weapon attacks:
//  * Reckless (Director-approved AI policy, rules #111 / sim #129): a creature with Combatant::reckless_attack declares
//    ActionRequest::reckless on its turn's first melee attack (the first attack of the Attack action: nothing attacked yet, no
//    sequence, the action unspent; rules refuse it later). A ranged attack never declares it, so a turn that opens with a ranged
//    attack stays un-reckless.
//  * R-G7's grip fact: a versatile melee weapon held in two hands (Archetype::two_handed_grip, the v0 wield model; never with a
//    two-weapon bonus option) is attacked with ActionRequest::two_handed_grip so Great Weapon Fighting applies. Only rules5e's
//    Great Weapon Fighting reads the flag.
Command next_command(const Combat& c, std::optional<CombatantId> target) {
    Command cmd = tactical_command(c, target);
    rules5e::ActionRequest& q = cmd.request;
    if (cmd.kind != CommandKind::Rules || q.kind != ActionKind::Attack || q.special != 0 || q.as_bonus_action) return cmd;
    const auto* me = rules5e::find(c.state(), q.actor);
    if (!me || q.attack_index >= me->attacks.size()) return cmd;
    const rules5e::TurnBudget& b = c.state().budget;
    if (me->reckless_attack && !me->reckless_active && !me->attacks[q.attack_index].ranged && !q.haste_action && b.action &&
        b.attacks_used_mask == 0 && b.sequence == 0)
        q.reckless = true;
    const Archetype* arch = c.attack_archetype(q.actor);   // R-G12: a form's natural weapons are its archetype's
    if (!arch || q.attack_index >= 32) return cmd;
    if (!(arch->two_handed_grip >> q.attack_index & 1u)) return cmd;
    for (const auto& bo : me->bonus_attacks)
        if (bo.different_weapon) return cmd;
    q.two_handed_grip = true;
    return cmd;
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
