#include "sim/driver.hpp"

#include "rules5e/conditions.hpp"
#include "rules5e/equipment.hpp"
#include "sim/equipment.hpp"
#include "sim/light_driver.hpp"

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
// The built-in target scoring (NearestEnemyPolicy) over a candidate list sorted by id: conscious hostile, nearest Chebyshev; the
// strict < keeps the earliest (lowest id) of equally near candidates.
CombatantId best_scored(const Combat& c, CombatantId self, const std::vector<CombatantId>& ids, CombatantId skip = 0) {
    CombatantId best = 0;
    int best_d = 0;
    for (CombatantId id : ids) {
        const auto* k = rules5e::find(c.state(), id);
        if (!k || id == self || id == skip || !c.hostile(self, id) || k->dead || k->gone || !rules5e::is_conscious(*k)) continue;
        const int d = c.distance_tiles(self, id);
        if (best == 0 || d < best_d) { best = id; best_d = d; }
    }
    return best;
}
bool warded_against(const rules5e::EncounterState& s, const rules5e::Combatant& t, CombatantId actor) {   // rules' ward_index
    if (actor == t.id || !t.has(rules5e::Condition::Warded)) return false;
    for (const auto& e : s.effects)
        if (e.target == t.id && !e.no_condition && e.condition == rules5e::Condition::Warded && e.dc > 0) return true;
    return false;
}
}  // namespace

CombatantId ward_target_of(const Combat& c, const rules5e::ActionRequest& q) {
    const auto& s = c.state();
    const auto* a = rules5e::find(s, q.actor);
    if (!a || q.move_mark) return 0;
    if (q.kind == ActionKind::Attack) {
        const auto* t = rules5e::find(s, q.target);
        return t && !t->dead && warded_against(s, *t, q.actor) ? t->id : 0;
    }
    if (q.kind != ActionKind::CastSpell || q.sustain) return 0;
    const auto* def = c.spell_def(q.spell_id);
    if (!def || def->area != rules5e::AreaShape::None) return 0;
    const std::vector<CombatantId> ids = q.targets.empty() ? std::vector<CombatantId>{q.target} : q.targets;
    for (CombatantId id : ids) {
        const auto* t = rules5e::find(s, id);
        if (t && t->id != a->id && t->team != a->team && warded_against(s, *t, q.actor)) return t->id;
    }
    return 0;
}

CombatantId choose_ward_alternate(const Combat& c, const rules5e::ActionRequest& q) {
    const CombatantId warded = ward_target_of(c, q);
    if (warded == 0) return 0;
    const bool spell = q.kind == ActionKind::CastSpell;
    std::vector<CombatantId> alts = c.ward_alternates(q.actor, warded, spell ? q.spell_id : 0u, spell ? uint8_t(0) : q.attack_index);
    if (spell) {   // rules refuse an alternate that is already one of the spell's targets
        const std::vector<CombatantId> ids = q.targets.empty() ? std::vector<CombatantId>{q.target} : q.targets;
        std::erase_if(alts, [&](CombatantId id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); });
    }
    return best_scored(c, q.actor, alts);
}

std::vector<rules5e::HealShare> plan_heal_split(const Combat& c, CombatantId user, const rules5e::FeatureSpec& spec,
                                                const HealSplitValidator& validate) {
    const auto& s = c.state();
    const auto* u = rules5e::find(s, user);
    if (!u || spec.split_pool == 0) return {};
    struct Cand { CombatantId id; int hp; int cap; };
    std::vector<Cand> cands;
    for (const auto& k : s.combatants) {
        if (k.dead || k.gone || (k.id != user && c.hostile(user, k.id))) continue;
        const int hp = std::max(0, int(k.hp));
        const int room = spec.split_cap_half ? int(k.hp_max) / 2 - hp : int(k.hp_max) - hp;
        if (room > 0) cands.push_back({k.id, hp, std::min(room, 32767)});
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.hp != y.hp ? x.hp < y.hp : x.id < y.id; });
    int budget = int(spec.split_pool);
    std::vector<rules5e::HealShare> split;
    // Each round drops a candidate, lowers a cap or lowers the budget, so the loop ends; the bound is a backstop only.
    for (int round = 0; round < 1 << 16; ++round) {
        split.clear();
        int left = budget;
        for (const Cand& k : cands) {
            if (left <= 0) break;
            const int amt = std::min(k.cap, left);
            if (amt < 1) continue;
            split.push_back({k.id, int16_t(amt)});
            left -= amt;
        }
        if (split.empty()) return split;
        const rules5e::HealSplitCheck r = validate ? validate(split) : c.heal_split_check(user, spec, split);
        if (r.index < 0) return split;
        if (std::size_t(r.index) >= split.size()) return {};
        const rules5e::HealShare bad = split[std::size_t(r.index)];
        auto it = std::find_if(cands.begin(), cands.end(), [&](const Cand& k) { return k.id == bad.target; });
        if (it == cands.end()) return {};
        if (r.reason == rules5e::HealSplitReason::OverCap) {
            it->cap = int(bad.amount) - 1;
            if (it->cap < 1) cands.erase(it);
        } else if (r.reason == rules5e::HealSplitReason::OverPool) {
            int before = 0;
            for (int i = 0; i < r.index; ++i) before += int(split[std::size_t(i)].amount);
            budget = std::min(budget - 1, before + int(bad.amount) - 1);
            if (budget < 1) return {};
        } else {
            cands.erase(it);   // OutOfRange, ExcludedType, BadTarget (and an unknown reason): that ally cannot take any
        }
    }
    return {};
}

bool spell_needs_mark_ability(const rules5e::SpellDef& def) {
    bool chosen = def.kind == rules5e::SpellEffectKind::ApplyModifier && def.modifier.chosen_ability;
    for (const auto& x : def.extra_effects)
        chosen = chosen || (x.kind == rules5e::SpellEffectKind::ApplyModifier && x.modifier.chosen_ability);
    return chosen;
}

uint8_t choose_mark_ability(const Combat& c, CombatantId target) {
    const auto* t = rules5e::find(c.state(), target);
    if (!t) return 1;   // fixed default: Strength
    int best = 0;
    for (int i = 1; i < 6; ++i)
        if (t->abilities.score[std::size_t(i)] < t->abilities.score[std::size_t(best)]) best = i;   // strict <: earlier ability wins ties
    return uint8_t(best + 1);
}

std::optional<Command> move_mark_command(const Combat& c, CombatantId self, std::optional<CombatantId> preferred) {
    const auto& s = c.state();
    const auto* me = rules5e::find(s, self);
    if (!me || me->concentration_spell == 0 || !s.budget.bonus_action || s.budget.sequence != 0) return std::nullopt;
    if (!rules5e::is_conscious(*me) || rules5e::is_incapacitated(*me)) return std::nullopt;
    const auto* def = c.spell_def(me->concentration_spell);
    if (!def || def->move_mark == 0) return std::nullopt;
    CombatantId old_mark = 0;
    for (const auto& m : s.modifiers)
        if (m.caster == self && m.spec.source == def->id && m.spec.mark_scope != 0 && m.mark != 0) { old_mark = m.mark; break; }
    if (old_mark == 0) return std::nullopt;
    if (const auto* om = rules5e::find(s, old_mark); om && !om->dead && om->hp > 0) return std::nullopt;
    CombatantId to = 0;
    if (preferred) to = best_scored(c, self, {*preferred}, old_mark);
    if (to == 0) {
        std::vector<CombatantId> ids;
        for (const auto& k : s.combatants) ids.push_back(k.id);
        std::sort(ids.begin(), ids.end());
        to = best_scored(c, self, ids, old_mark);
    }
    if (to == 0) return std::nullopt;
    Command x;
    x.request = {self, ActionKind::CastSpell, to};
    x.request.spell_id = def->id;
    x.request.move_mark = true;
    return x;
}

void complete_request(const Combat& c, rules5e::ActionRequest& q) {
    if (q.kind == ActionKind::CastSpell && !q.move_mark && !q.sustain && q.mark_ability == 0)
        if (const auto* def = c.spell_def(q.spell_id); def && spell_needs_mark_ability(*def)) {
            const CombatantId t = q.targets.empty() ? q.target : q.targets[0];
            q.mark_ability = choose_mark_ability(c, t);
        }
    if ((q.kind == ActionKind::Attack || q.kind == ActionKind::CastSpell) && q.ward_alternate == 0 && q.special == 0)
        q.ward_alternate = choose_ward_alternate(c, q);
}

namespace {
// 7.2zb: a costed bonus-action grant (Step of the Wind) is only taken when its pool can pay (else the step would be refused).
bool bonus_cost_affordable(const rules5e::EncounterState& s, const rules5e::Combatant& me, rules5e::ActionKind k) {
    for (const auto& bc : me.bonus_action_costs)
        if (bc.kind == k) return rules5e::can_spend(s, me.id, bc.pool, bc.cost);
    return true;
}
// Anchors from which a creature of side `mn` stands within `reach` tiles of the `tn`-side creature at
// `tp`, excluding anchors that overlap it. For 1x1 vs 1x1 this is the 8 * reach-ring around `tp`.
// Reach 0 (a swarm's bite, SRD: "one target in the swarm's space"): only the anchors whose footprint overlaps the target's
// (row-major, so the path search stays deterministic). They are reachable only for a mover that may share spaces
// (Combat::occupied_except: a swarm, or a target that is one); for anyone else the path is empty and the AI ends its turn.
std::vector<Vec2i> goal_anchors(Vec2i tp, int tn, int mn, int reach) {
    std::vector<Vec2i> g;
    for (int y = tp.y - reach - mn + 1; y <= tp.y + tn - 1 + reach; ++y)
        for (int x = tp.x - reach - mn + 1; x <= tp.x + tn - 1 + reach; ++x)
            if (reach == 0 ? rect_distance({x, y}, mn, tp, tn) == 0 : rect_distance({x, y}, mn, tp, tn) > 0) g.push_back({x, y});
    return g;
}
} // namespace

// ---- Equipment AI (CombatOptions::equipment only) ----------------------------------------------------------------------------
bool equipment_ai_active(const Combat& c, CombatantId self) {
    return c.options().equipment && c.loadout(self) != nullptr && c.item_defs() != nullptr;
}

namespace {
int mean2(rules5e::DiceExpr d) {   // 2 x the mean of a dice expression (count 0 = flat)
    return d.count == 0 ? 2 * int(d.bonus) : int(d.count) * (int(d.sides) + 1) + 2 * int(d.bonus);
}
}  // namespace

int attack_score(const rules5e::Combatant& me, uint8_t attack_index, const rules5e::Combatant& target, bool disadvantage) {
    if (attack_index >= me.attacks.size()) return 0;
    const rules5e::Attack& a = me.attacks[attack_index];
    int dmg = mean2(a.damage);
    for (const auto& p : a.extra_damage) dmg += mean2(p.dice);
    if (dmg < 1) dmg = 1;   // a hit is still worth something (flat 0 / negative modifiers)
    const int faces = std::clamp(21 - (int(target.ac) - int(a.to_hit)), 1, 19);
    return dmg * (disadvantage ? faces * faces : faces * 20);
}

namespace {
bool attack_usable(const rules5e::EncounterState& st, CombatantId self, uint8_t i) {
    return rules5e::attack_refusal(st, self, i).empty();
}

// Movement cost to stand within `reach` tiles of `t` (1 << 20 = no path).
int path_cost_to(const Combat& c, CombatantId self, CombatantId t, int reach) {
    const Vec2i mp = c.pos(self);
    const auto p = c.grid().path(mp, goal_anchors(c.pos(t), c.footprint_size(t), c.footprint_size(self), reach),
                                 c.occupied_except(self), c.footprint_size(self), c.caps_of(self));
    if (p.empty()) return 1 << 20;
    int cost = 0;
    Vec2i prev = mp;
    for (Vec2i q : p) { cost += c.step_cost_ft(self, prev, q); prev = q; }
    return cost;
}

// (attackable this turn, next turn): the best attack_score over the attacks the loadout in `st` allows. Melee counts this turn
// when in reach or reachable with the movement left; ranged when legal to shoot from here (disadvantage priced in).
struct PlanValue {
    int now = 0, later = 0;
    bool better_than(const PlanValue& o) const { return now != o.now ? now > o.now : later > o.later; }
};
PlanValue weapon_value(const Combat& c, const rules5e::EncounterState& st, const rules5e::Combatant& me,
                       const rules5e::Combatant& t, bool action_left) {
    PlanValue v;
    const int d = c.distance_tiles(me.id, t.id);
    for (std::size_t k = 0; k < me.attacks.size() && k < 255; ++k) {
        const uint8_t i = uint8_t(k);
        if (!attack_usable(st, me.id, i)) continue;
        const rules5e::Attack& a = me.attacks[k];
        v.later = std::max(v.later, attack_score(me, i, t, false));
        if (!action_left) continue;
        int now = 0;
        if (!a.ranged) {
            const int reach = reach_tiles(a.reach_or_range_ft);
            if (d <= reach || path_cost_to(c, me.id, t.id, reach) <= int(st.budget.movement_ft)) now = attack_score(me, i, t, false);
        } else if (c.can_shoot(me.id, i, t.id)) {
            now = attack_score(me, i, t, c.ranged_disadvantage(me.id, i, t.id));
        }
        v.now = std::max(v.now, now);
    }
    return v;
}

bool def_linked(const rules5e::Loadout& l, uint32_t def) {
    for (const auto& a : l.view.attacks) if (a.def == def && def != 0) return true;
    return false;
}

// Applies one equip op to the lookahead copy (as Combat::apply_equip would, minus events / derived stats). Returns the cost, or
// Refused when rules5e::equip_legal refuses it on that state.
rules5e::EquipCost::Kind lookahead_apply(rules5e::EncounterState& st, const rules5e::ActionRequest& r) {
    const rules5e::EquipCost cost = rules5e::equip_legal(st, r);
    if (cost.kind == rules5e::EquipCost::Refused) return cost.kind;
    rules5e::Loadout* l = find_loadout(st, r.actor);
    if (!l) return rules5e::EquipCost::Refused;
    rules5e::LoadoutView& v = l->view;
    auto into_hand = [&](const rules5e::HeldItem& h) {
        if (r.equip_hand == 2 || (r.equip_hand == 0 && v.main.instance != 0)) v.off = h; else v.main = h;
    };
    const auto op = rules5e::EquipOp(r.equip_op);
    if (op == rules5e::EquipOp::Drop || op == rules5e::EquipOp::Stow) {
        for (rules5e::HeldItem* h : {&v.main, &v.off})
            if (h->instance == r.equip_item) {
                if (op == rules5e::EquipOp::Stow) v.carried.push_back(*h);
                *h = rules5e::HeldItem{};
            }
    } else if (op == rules5e::EquipOp::Draw) {
        for (auto it = v.carried.begin(); it != v.carried.end(); ++it)
            if (it->instance == r.equip_item) { const rules5e::HeldItem h = *it; v.carried.erase(it); into_hand(h); break; }
    } else if (op == rules5e::EquipOp::PickUp) {
        for (auto it = st.ground_items.begin(); it != st.ground_items.end(); ++it)
            if (it->item.instance == r.equip_item) { const rules5e::HeldItem h = it->item; st.ground_items.erase(it); into_hand(h); break; }
    }
    if (cost.kind == rules5e::EquipCost::Interaction) v.interaction_used = true;
    return cost.kind;
}

rules5e::ActionRequest equip_req(CombatantId a, rules5e::EquipOp op, uint32_t item, uint8_t hand) {
    rules5e::ActionRequest r{a, ActionKind::UseObject, 0};
    r.equip_op = uint8_t(op);
    r.equip_item = item;
    r.equip_hand = hand;
    r.equip_in_reach = op == rules5e::EquipOp::PickUp;
    return r;
}

// The first legal_actions() entry with this op and item (the exact request rules listed), if any.
std::optional<rules5e::ActionRequest> listed(const std::vector<rules5e::ActionRequest>& legal, rules5e::EquipOp op, uint32_t item) {
    for (const auto& r : legal)
        if (r.kind == ActionKind::UseObject && r.equip_op == uint8_t(op) && r.equip_item == item) return r;
    return std::nullopt;
}
}  // namespace

std::optional<Command> equipment_command(const Combat& c, CombatantId self, CombatantId target) {
    if (!equipment_ai_active(c, self)) return std::nullopt;
    const auto& s = c.state();
    const auto* me = rules5e::find(s, self);
    const auto* t = rules5e::find(s, target);
    if (!me || !t || t->dead || !rules5e::is_conscious(*t) || s.budget.sequence != 0) return std::nullopt;
    const bool action_left = s.budget.action || s.budget.extra_actions > 0 || s.budget.attacks_remaining > 0;
    if (!action_left) return std::nullopt;
    const rules5e::Loadout* lo = c.loadout(self);
    const ItemDefTable& items = *c.item_defs();
    const PlanValue cur = weapon_value(c, s, *me, *t, true);
    const std::vector<rules5e::ActionRequest> legal = c.legal_actions();

    // Candidate first ops on held items (instance order): stow (keeps the item; spends the interaction) before drop (free).
    std::vector<uint32_t> held;
    for (const rules5e::HeldItem* h : {&lo->view.main, &lo->view.off}) if (h->instance) held.push_back(h->instance);
    std::sort(held.begin(), held.end());
    // Acquisitions: carried weapons the attacks use (Draw, instance order), then such ground weapons within 5 ft (PickUp).
    struct Acq { rules5e::EquipOp op; uint32_t item; };
    std::vector<Acq> acq;
    for (const auto& h : lo->view.carried)
        if (const auto* d = items.find(h.def); d && rules5e::is_weapon(*d) && def_linked(*lo, h.def)) acq.push_back({rules5e::EquipOp::Draw, h.instance});
    const Vec2i mp = c.pos(self);
    const int mn = c.footprint_size(self);
    for (const auto& g : s.ground_items)
        if (const auto* d = items.find(g.item.def);
            d && rules5e::is_weapon(*d) && def_linked(*lo, g.item.def) && rect_distance(mp, mn, {g.x, g.y}, 1) <= 1)
            acq.push_back({rules5e::EquipOp::PickUp, g.item.instance});
    std::sort(acq.begin(), acq.end(), [](const Acq& a, const Acq& b) {
        return a.op != b.op ? uint8_t(a.op) < uint8_t(b.op) : a.item < b.item;
    });
    if (acq.empty()) return std::nullopt;

    // Plans: up to two emptying ops (each held item stowed or dropped), then one acquisition into the first empty hand.
    struct Plan { std::vector<rules5e::ActionRequest> ops; PlanValue v; };
    std::optional<Plan> best;
    std::vector<std::vector<rules5e::ActionRequest>> prefixes{{}};
    for (const uint32_t inst : held)
        for (const auto op : {rules5e::EquipOp::Stow, rules5e::EquipOp::Drop}) prefixes.push_back({equip_req(self, op, inst, 0)});
    if (held.size() == 2)
        for (const auto op0 : {rules5e::EquipOp::Stow, rules5e::EquipOp::Drop})
            for (const auto op1 : {rules5e::EquipOp::Stow, rules5e::EquipOp::Drop})
                prefixes.push_back({equip_req(self, op0, held[0], 0), equip_req(self, op1, held[1], 0)});
    for (const auto& pre : prefixes)
        for (const Acq& a : acq) {
            rules5e::EncounterState st = s;   // lookahead copy: hooks read the copy they are given
            bool ok = true, spends_action = false;
            std::vector<rules5e::ActionRequest> ops;
            for (const auto& r : pre) {
                const auto k = lookahead_apply(st, r);
                ok = ok && k != rules5e::EquipCost::Refused;
                spends_action = spends_action || k == rules5e::EquipCost::Action;
                ops.push_back(r);
            }
            if (!ok) continue;
            const rules5e::Loadout* ll = find_loadout(st, self);
            const uint8_t hand = ll->view.main.instance == 0 ? uint8_t(1) : uint8_t(2);
            const rules5e::ActionRequest ar = equip_req(self, a.op, a.item, hand);
            const auto k = lookahead_apply(st, ar);
            if (k == rules5e::EquipCost::Refused) continue;
            spends_action = spends_action || k == rules5e::EquipCost::Action;
            ops.push_back(ar);
            // The first op is the one submitted now: an Action-cost op is only worth it with no usable weapon at all.
            if (rules5e::equip_legal(s, ops[0]).kind == rules5e::EquipCost::Action && cur.later > 0) continue;
            const PlanValue v = weapon_value(c, st, *me, *t, !spends_action);
            if (!v.better_than(cur) || (best && !v.better_than(best->v))) continue;
            best = Plan{ops, v};
        }
    if (!best) return std::nullopt;
    const rules5e::ActionRequest& first = best->ops[0];
    const auto entry = listed(legal, rules5e::EquipOp(first.equip_op), first.equip_item);
    if (!entry) return std::nullopt;   // never bypass rules' listing
    Command x;
    x.request = *entry;
    return x;
}

std::optional<Light> ai_light_at(const Combat& c, Vec2i tile) {
    const LightDriver* d = c.light_driver();
    if (!d) return std::nullopt;
    const auto f = d->latest();
    if (!f) return std::nullopt;
    const int cs = world::kChunkSize;
    const int cx = tile.x >= 0 ? tile.x / cs : -((-tile.x + cs - 1) / cs);
    const int cy = tile.y >= 0 ? tile.y / cs : -((-tile.y + cs - 1) / cs);
    for (const auto& ch : f->chunks)
        if (ch.coord.x == cx && ch.coord.y == cy) {
            const int lx = tile.x - cx * cs, ly = tile.y - cy * cs;
            return ch.samples[std::size_t(ly * cs + lx)].level;
        }
    return std::nullopt;
}

std::optional<Command> light_command(const Combat& c, CombatantId self, std::optional<CombatantId> target) {
    if (!equipment_ai_active(c, self)) return std::nullopt;
    const auto& s = c.state();
    const auto* me = rules5e::find(s, self);
    if (!me || s.budget.sequence != 0) return std::nullopt;
    const rules5e::Loadout* lo = c.loadout(self);
    const ItemDefTable& items = *c.item_defs();
    const std::vector<rules5e::ActionRequest> legal = c.legal_actions();
    std::vector<const rules5e::HeldItem*> held;
    for (const rules5e::HeldItem* h : {&lo->view.main, &lo->view.off}) if (h->instance) held.push_back(h);
    std::sort(held.begin(), held.end(), [](const rules5e::HeldItem* a, const rules5e::HeldItem* b) { return a->instance < b->instance; });
    for (const rules5e::HeldItem* h : held) {
        const rules5e::ItemDef* d = items.find(h->def);
        if (!d || d->light.states.empty()) continue;
        const bool lit = (h->flags & rules5e::kHeldLit) != 0;
        std::optional<rules5e::ActionRequest> want;
        if (!lit && me->darkvision_ft == 0) {
            bool dark = ai_light_at(c, c.pos(self)) == Light::Dark;
            if (const auto* t = target ? rules5e::find(s, *target) : nullptr; t && !t->dead)
                dark = dark || ai_light_at(c, c.pos(t->id)) == Light::Dark;
            if (dark) {
                want = equip_req(self, rules5e::EquipOp::SetLightState, h->instance, 0);
                want->equip_state = 0;
            }
        } else if (lit && me->darkvision_ft > 0 && !def_linked(*lo, h->def)) {   // a torch it fights with stays lit
            const rules5e::LightState r = rules5e::light_radii(*d, *h);
            const int radius = (int(r.bright_ft) + int(r.dim_ft)) / kFeetPerTile;
            bool needed = false;
            for (const auto& k : s.combatants)
                needed = needed || (k.id != self && !k.dead && !k.gone && k.team == me->team && k.darkvision_ft == 0 &&
                                    c.distance_tiles(self, k.id) <= radius);
            if (!needed) {
                want = equip_req(self, rules5e::EquipOp::SetLightState, h->instance, 0);
                want->equip_state = rules5e::kLightDouse;
            }
        }
        if (!want) continue;
        for (const auto& r : legal)
            if (r.kind == ActionKind::UseObject && r.equip_op == want->equip_op && r.equip_item == want->equip_item &&
                r.equip_state == want->equip_state && rules5e::equip_legal(s, r).kind == rules5e::EquipCost::Interaction) {
                Command x;
                x.request = r;
                return x;
            }
    }
    return std::nullopt;
}

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
        const int speed = rules5e::modified_speed(s, *me);   // 7.2u: half the speed after modifiers (Ray of Frost, exhaustion)
        if (speed > 0 && s.budget.movement_ft >= speed / 2) {
            Command up;
            up.request = {self, ActionKind::StandUp};
            return up;
        }
    }
    // R-G12 (SUMMONS_TRANSFORMS §3.4): a Polymorph / Wild Shape form cannot cast (rules refuse it), so no spell or save-action
    // casts from the creature's own block while in it. Wild Shape is an engine heuristic (Director ruling (b): no content key).
    // At the start of the turn (nothing spent or moved yet) the AI takes Wild Shape when ALL hold:
    //   1. usable: can_use_feature (pool has a use, the feature's action / bonus action is free, not blocked);
    //   2. not concentrating at all (concentration_spell == 0): every concentration spell counts as worth keeping, so the
    //      simple rule is "never trade a running concentration spell for a form";
    //   3. in or near melee (a living hostile it can see, not invisible, within kWildShapeMeleeTiles = 2 tiles Chebyshev),
    //      OR no spell slots left (every spell_slots[1..9] and pact_slots 0; a creature with no slots at all counts as spent).
    // Form: the highest CR among the forms build_form allows here; ties go to the earlier entry in the feature's forms[] list
    // (ids are unique in that list, so the order is total). Shapechanger features are never used by the AI (spawn form kept).
    // Never revert by choice: 0 HP, the timer, concentration or the encounter's end revert it.
    const bool beast_form = me->form == rules5e::FormKind::Polymorph || me->form == rules5e::FormKind::WildShape;
    // Equipment AI: only with CombatOptions::equipment and a loadout; `usable` is always true otherwise (pre-equipment choices).
    const bool eq = equipment_ai_active(c, self);
    auto usable = [&](std::size_t i) { return !eq || (i < 255 && attack_usable(s, self, uint8_t(i))); };
    if (me->form == rules5e::FormKind::None && me->concentration_spell == 0 && s.budget.sequence == 0 && s.budget.moved_ft == 0 &&
        s.budget.attacks_used_mask == 0)
        if (const Archetype* arch = c.archetype(self))
            for (const auto& [fid, ff] : arch->form_features) {
                if (ff.kind != uint8_t(rules5e::FormKind::WildShape) || !c.can_use_feature(self, fid)) continue;
                const auto sp = arch->feature_specs.find(fid);
                if (sp == arch->feature_specs.end() || (sp->second.cost == 2 ? !s.budget.bonus_action : !s.budget.action)) continue;
                constexpr int kWildShapeMeleeTiles = 2;
                bool near_melee = false;
                for (const auto& o : s.combatants)
                    if (o.id != self && o.team != me->team && !o.dead && !o.gone && !o.has(rules5e::Condition::Invisible) &&
                        c.distance_tiles(self, o.id) <= kWildShapeMeleeTiles && c.line_of_sight(self, o.id))
                        near_melee = true;
                bool slots_left = me->pact_slots > 0;
                for (int l = 1; l <= 9; ++l) slots_left = slots_left || me->spell_slots[std::size_t(l)] > 0;
                if (!near_melee && slots_left) continue;
                int best = -1, best_cr = -1;
                for (std::size_t i = 0; i < ff.forms.size() && i < 255; ++i) {
                    const Archetype* fa = c.form_archetype(self, rules5e::FormKind(ff.kind), fid, uint8_t(i));
                    if (!fa || !c.build_form(*me, rules5e::FormKind(ff.kind), fid, uint8_t(i))) continue;
                    if (fa->cr_x8 > best_cr) { best_cr = fa->cr_x8; best = int(i); }   // strict >: the earlier entry wins ties
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
    // 7.2zc: a movable mark whose creature dropped to 0 hp moves (bonus action) before anything else spends the bonus action.
    if (!beast_form)
        if (auto mm = move_mark_command(c, self, t->id)) return *mm;

    // Pool features (row 13). Lay on Hands (heal_pool): at or below half hp, heal self for min(pool, missing hp); the caller's
    // `amount` and (default self) target come from here. Action Surge (grant_action): once the action is spent with a hostile
    // adjacent, take the extra action. The AI never picks a pool feature whose pool is short.
    if (const Archetype* arch = c.archetype(self))
        for (const auto& f : arch->active) {
            const auto sp = arch->feature_specs.find(f.feature_id);
            if (sp == arch->feature_specs.end() || !c.can_use_feature(self, f.feature_id)) continue;
            const rules5e::FeatureSpec& spec = sp->second;
            const rules5e::ResourcePool* pool = spec.pool ? rules5e::find_pool(s, self, spec.pool) : nullptr;
            if (spec.split_pool > 0) {   // 7.2zd Preserve Life (AI policy): someone planned is at 0 hp, or the plan uses half the pool
                if (spec.pool && !rules5e::can_spend(s, self, spec.pool, spec.pool_cost ? spec.pool_cost : 1)) continue;
                if (spec.cost == 2 ? !s.budget.bonus_action : !s.budget.action) continue;
                auto split = plan_heal_split(c, self, spec);
                int total = 0;
                bool downed = false;
                for (const auto& sh : split) {
                    total += sh.amount;
                    const auto* k = rules5e::find(s, sh.target);
                    downed = downed || (k && k->hp <= 0);
                }
                if (split.empty() || (!downed && total * 2 < int(spec.split_pool))) continue;
                Command u;
                u.request = {self, ActionKind::UseFeature};
                u.request.feature_id = f.feature_id;
                u.request.heal_split = std::move(split);
                return u;
            }
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
        if (gi < me->attacks.size() && usable(gi)) {
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
            if (bi < 0 || std::size_t(bi) >= me->attacks.size() || !usable(std::size_t(bi))) return std::nullopt;
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
    // Equipment AI: a weapon plan (draw / switch / drop / pick up), then held lights, before choosing an attack.
    if (eq) {
        if (auto x = equipment_command(c, self, t->id)) return *x;
        if (auto x = light_command(c, self, t->id)) return *x;
    }
    // Prefer the first melee attack; fall back to the first attack.
    uint8_t ai = 0;
    bool no_weapon = false;   // equipment: no attack the loadout allows (move / end instead of a refused attack)
    if (!eq) {
        for (uint8_t i = 0; i < me->attacks.size(); ++i)
            if (!me->attacks[i].ranged) { ai = i; break; }
    } else {   // the usable melee attack with the best attack_score (earliest on ties), else the first usable attack
        int best_score = -1;
        for (std::size_t i = 0; i < me->attacks.size() && i < 255; ++i) {
            if (me->attacks[i].ranged || !usable(i)) continue;
            const int sc = attack_score(*me, uint8_t(i), *t, false);
            if (sc > best_score) { best_score = sc; ai = uint8_t(i); }
        }
        if (best_score < 0) {
            no_weapon = true;
            for (std::size_t i = 0; i < me->attacks.size() && i < 255 && no_weapon; ++i)
                if (usable(i)) { ai = uint8_t(i); no_weapon = false; }
        }
    }
    if (me->attacks.empty()) return end;
    // Declared multiattack (T&C-G1): continue the sequence in progress, else start the first option made
    // only of melee attacks (declaration order; options with a ranged attack are never picked by the AI).
    uint8_t multi = 0;   // ActionRequest::multiattack to send (0 = continue / plain attack)
    if (s.budget.sequence != 0 && s.budget.sequence <= me->multiattacks.size()) {
        if (const int next = rules5e::multiattack_next(me->multiattacks, s.budget.sequence, s.budget.sequence_pos); next >= 0)
            ai = uint8_t(next);
    } else {
        for (std::size_t i = 0; i < me->multiattacks.size() && i < 255; ++i) {
            const auto& seq = me->multiattacks[i].attacks;
            bool melee_only = !seq.empty();
            for (uint8_t k : seq) melee_only = melee_only && k < me->attacks.size() && !me->attacks[k].ranged && usable(k);
            if (!melee_only) continue;
            ai = seq[0];
            multi = uint8_t(i + 1);
            break;
        }
    }
    const int reach = reach_tiles(me->attacks[ai].reach_or_range_ft);
    const Vec2i mp = c.pos(self), tp = c.pos(t->id);
    const int mn = c.footprint_size(self), tn = c.footprint_size(t->id);
    const bool can_attack = (s.budget.action || s.budget.extra_actions > 0 || s.budget.attacks_remaining > 0) && !no_weapon;
    // Equipment: with no usable melee attack the chosen attack may be ranged; it is never "in reach" (shoot or close in below).
    const bool eq_ranged = eq && me->attacks[ai].ranged;
    if (!eq_ranged && c.distance_tiles(self, t->id) <= reach) {
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
        const auto p = c.grid().path(mp, goal_anchors(tp, tn, mn, reach), c.occupied_except(self), mn, c.caps_of(self));
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
            if (!can_attack || !shoot || need <= s.budget.movement_ft + rules5e::modified_speed(s, *me)) {   // 7.2u: what the Dash adds
                Command d;
                d.request = {self, ActionKind::Dash};
                d.request.as_bonus_action = true;
                d.request.aggressive = true;
                return d;
            }
        }
    }
    if (eq && can_attack && (eq_ranged || melee_path_cost() > s.budget.movement_ft)) {
        // Equipment: the usable ranged attack (thrown included) with the best attack_score, disadvantage priced in (earliest on
        // ties). With a hostile within 5 ft (disadvantage) and a usable melee attack, hit that hostile in melee instead.
        std::optional<uint8_t> best;
        int best_score = 0;
        bool best_dis = false;
        for (std::size_t i = 0; i < me->attacks.size() && i < 255; ++i) {
            const uint8_t ii = uint8_t(i);
            if (!me->attacks[i].ranged || !usable(i) || !c.can_shoot(self, ii, t->id)) continue;
            const bool dis = c.ranged_disadvantage(self, ii, t->id);
            const int sc = attack_score(*me, ii, *t, dis);
            if (!best || sc > best_score) { best = ii; best_score = sc; best_dis = dis; }
        }
        if (!best || best_dis) {
            CombatantId near = 0;
            for (const auto& k : s.combatants)   // combatant order, lowest id wins
                if (k.id != self && c.hostile(self, k.id) && !k.dead && !k.gone && rules5e::is_conscious(k) &&
                    c.distance_tiles(self, k.id) <= 1 && (near == 0 || k.id < near))
                    near = k.id;
            if (near != 0) {
                const auto* nk = rules5e::find(s, near);
                std::optional<uint8_t> mi;
                int msc = 0;
                for (std::size_t i = 0; i < me->attacks.size() && i < 255; ++i) {
                    if (me->attacks[i].ranged || !usable(i) || reach_tiles(me->attacks[i].reach_or_range_ft) < 1) continue;
                    const int sc = attack_score(*me, uint8_t(i), *nk, false);
                    if (!mi || sc > msc) { mi = uint8_t(i); msc = sc; }
                }
                if (mi && (!best || msc > best_score)) {
                    Command a;
                    a.request = {self, ActionKind::Attack, near};
                    a.request.attack_index = *mi;
                    return a;
                }
            }
        }
        if (best) {
            Command a;
            a.request = {self, ActionKind::Attack, t->id};
            a.request.attack_index = *best;
            return a;
        }
    } else if (can_attack && melee_path_cost() > s.budget.movement_ft) {
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
    const auto path = c.grid().path(mp, goal_anchors(tp, tn, mn, reach), c.occupied_except(self), mn, c.caps_of(self));
    if (path.empty()) return end;
    if (const uint8_t m = c.step_mode(self, mp, path[0], c.step_cost_ft(self, mp, path[0])); m >= 0xFE) return end;   // feat/move-modes
    // About to leave a hostile's reach: Disengage first if a bonus Disengage is available
    // (granted by content, e.g. a bonus_action_grant feature).
    if (s.budget.bonus_action && !me->disengaged &&
        (me->bonus_action_grants & rules5e::bit(ActionKind::Disengage)) && bonus_cost_affordable(s, *me, ActionKind::Disengage) &&
        c.would_leave_reach(self, mp, path[0])) {
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
    if (cmd.kind == CommandKind::Rules) complete_request(c, q);   // 7.2zc mark_ability / 7.2zd ward_alternate
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

namespace {
// R-G12 summons (§3.4, built-in, before the policy; default approved by the Director, SUMMONS_TRANSFORMS Q4): an uncommanded summon that takes commands (Verbal / BonusAction, D4)
// Dodges, then ends its turn; a summoner commands its uncommanded Verbal summons to act (free) and, with its bonus action
// still free, its BonusAction ones (Animate Dead). Telepathic / None summons act on their own. Nothing without summons.
std::optional<Command> summon_turn_command(const Combat& c, CombatantId self) {
    const auto& s = c.state();
    const auto* me = rules5e::find(s, self);
    if (!me || me->dead) return std::nullopt;
    using rules5e::SummonControl;
    const bool takes = me->summon_control == uint8_t(SummonControl::Verbal) || me->summon_control == uint8_t(SummonControl::BonusAction);
    if (me->summoner != 0 && takes && me->commanded != 1) {
        Command x;
        x.request = {self, s.budget.action ? ActionKind::Dodge : ActionKind::EndTurn};
        return x;
    }
    if (rules5e::is_incapacitated(*me)) return std::nullopt;
    std::vector<CombatantId> verbal, bonus;
    for (const auto& k : s.combatants) {
        if (k.summoner != self || k.gone || k.dead || k.commanded == 1) continue;
        if (k.summon_control == uint8_t(SummonControl::Verbal)) verbal.push_back(k.id);
        if (k.summon_control == uint8_t(SummonControl::BonusAction) && s.budget.bonus_action && s.budget.sequence == 0) bonus.push_back(k.id);
    }
    for (const auto* ids : {&verbal, &bonus}) {
        if (ids->empty()) continue;
        Command x;
        x.request = {self, ActionKind::CommandSummons};
        x.request.command = 1;
        x.request.command_ids = *ids;
        return x;
    }
    return std::nullopt;
}
}  // namespace

RunResult run_to_end(Combat& c, const PolicyFor& policy_for, uint32_t max_commands) {
    RunResult rr;
    std::optional<Combat::TurnKey> turn;   // R-G12 (§3.4): (round, current actor, TurnStarted count), not (round, turn_index)
    std::optional<CombatantId> target;
    // Sync adapter: answer each NeedsDecision with the reactor's policy.
    const ReactionDeciderFn decider = [&](const Combat& cc, const NeedsDecision& n) {
        IActorPolicy* p = policy_for(n.reactor);
        return p ? p->decide_reaction(cc, n) : ReactionDecision{n.window_seq, false, 0};
    };
    while (!c.ended() && rr.commands < max_commands) {
        const CombatantId self = rules5e::current_actor(c.state());
        if (!turn || !(*turn == c.turn_key())) {
            turn = c.turn_key();
            IActorPolicy* p = policy_for(self);
            target = p ? p->choose_target(c, self) : std::nullopt;
        }
        const auto sc = summon_turn_command(c, self);
        const Command cmd = sc ? *sc : next_command(c, target);
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
