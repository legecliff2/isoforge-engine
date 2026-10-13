#include "sim/combat.hpp"

#include <algorithm>

#include "assets/schema_registry.hpp"
#include "base/canonical_hash.hpp"
#include "rules5e/conditions.hpp"

namespace eng::sim {

using rules5e::ActionKind;
using rules5e::ActionRequest;

std::optional<std::string> spawn_team_name(const world::EntitySpawn& sp, const Archetype& a, std::string* error) {
    std::string name = a.schema_type == "character" ? "party" : "monsters";
    if (!sp.overrides_json.empty()) {
        const assets::Json ov = assets::Json::parse(sp.overrides_json, nullptr, false);
        if (ov.is_object()) {
            if (auto t = ov.find("team"); t != ov.end()) {
                bool ok = t->is_string() && !t->get_ref<const std::string&>().empty();
                if (ok) {
                    const auto& v = t->get_ref<const std::string&>();
                    ok = v[0] >= 'a' && v[0] <= 'z';
                    for (char ch : v) ok = ok && ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_');
                    if (ok) name = v;
                }
                if (!ok) {
                    if (error) *error = "spawn overrides.team must match ^[a-z][a-z0-9_]*$";
                    return std::nullopt;
                }
            }
        }
    }
    return name;
}

std::optional<Combat> Combat::create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                     uint64_t seed, std::string* error) {
    Combat c(reg, chunk);
    c.seed_ = seed;
    // Team names -> ids by byte order of the distinct names (spawn-order independent).
    std::vector<std::string> spawn_teams;
    for (const auto& sp : chunk.spawns) {
        const Archetype* a = content.find(sp.archetype);
        if (!a) continue;
        auto t = spawn_team_name(sp, *a, error);
        if (!t) return std::nullopt;
        spawn_teams.push_back(*t);
    }
    c.team_names_ = spawn_teams;
    std::sort(c.team_names_.begin(), c.team_names_.end());
    c.team_names_.erase(std::unique(c.team_names_.begin(), c.team_names_.end()), c.team_names_.end());
    if (c.team_names_.size() > 255) { if (error) *error = "too many teams"; return std::nullopt; }
    std::vector<rules5e::Combatant> cs;
    std::size_t ti = 0;
    for (const auto& sp : chunk.spawns) {
        const Archetype* a = content.find(sp.archetype);
        if (!a) continue;   // non-combatant spawn
        const std::string& team_name = spawn_teams[ti++];
        if (sp.stable_id == 0) { if (error) *error = "spawn stable_id 0 is reserved"; return std::nullopt; }
        if (reg.find(sp.stable_id)) { if (error) *error = "duplicate spawn stable_id"; return std::nullopt; }
        rules5e::Combatant k = a->base;
        k.id = sp.stable_id;
        k.team = uint8_t(std::lower_bound(c.team_names_.begin(), c.team_names_.end(), team_name) - c.team_names_.begin());
        const ecs::Entity e = reg.create(sp.stable_id);
        reg.emplace(e, Health{k.hp, k.hp_max});
        reg.emplace(e, Team{k.team});
        reg.emplace(e, TilePos{sp.tile.x, sp.tile.y});
        reg.emplace(e, CombatantLink{k.id});
        c.slots_.push_back({k.id, e, a});
        cs.push_back(std::move(k));
    }
    std::vector<rules5e::Event> opening;
    c.state_ = rules5e::begin_encounter(std::move(cs), seed, &opening);
    // Limited-use features live in the rules state (hashed, rebuilt by replay).
    for (const auto& sl : c.slots_)
        for (const auto& f : sl.archetype->active)
            if (f.uses > 0)
                rules5e::register_feature(c.state_, sl.id, f.feature_id, f.uses,
                                          f.recharge == Recharge::ShortRest  ? rules5e::FeatureRecharge::ShortRest
                                          : f.recharge == Recharge::LongRest ? rules5e::FeatureRecharge::LongRest
                                                                             : rules5e::FeatureRecharge::None);
    c.apply_rules_events(opening);
    return c;
}

const Combat::Slot* Combat::slot(CombatantId id) const {
    for (const auto& s : slots_) if (s.id == id) return &s;
    return nullptr;
}
ecs::Entity Combat::entity(CombatantId id) const { const Slot* s = slot(id); return s ? s->entity : ecs::kNullEntity; }
const Archetype* Combat::archetype(CombatantId id) const { const Slot* s = slot(id); return s ? s->archetype : nullptr; }
Vec2i Combat::pos(CombatantId id) const {
    const Slot* s = slot(id);
    if (!s) return {};
    const TilePos* p = reg_->get<TilePos>(s->entity);
    return p ? Vec2i{p->x, p->y} : Vec2i{};
}
bool Combat::hostile(CombatantId a, CombatantId b) const {
    const auto* ca = rules5e::find(state_, a);
    const auto* cb = rules5e::find(state_, b);
    return ca && cb && ca->team != cb->team;
}
std::vector<Vec2i> Combat::occupied_except(CombatantId id) const {
    std::vector<Vec2i> out;
    for (const auto& s : slots_) {
        const auto* c = rules5e::find(state_, s.id);
        if (s.id != id && c && !c->dead) out.push_back(pos(s.id));
    }
    return out;
}

void Combat::apply_rules_events(const std::vector<rules5e::Event>& evs) {
    for (const auto& e : evs) {
        SimEvent se;
        se.kind = SimEvent::Kind::Rules;
        se.rules = e;
        events_.push_back(se);
    }
    sync_ecs();
}

void Combat::sync_ecs() {
    for (const auto& s : slots_)
        if (const auto* c = rules5e::find(state_, s.id))
            if (Health* h = reg_->get<Health>(s.entity)) { h->hp = c->hp; h->hp_max = c->hp_max; }
}

int Combat::melee_reach_tiles(CombatantId id) const {
    const auto* c = rules5e::find(state_, id);
    int best = 0;
    if (c)
        for (const auto& at : c->attacks)
            if (!at.ranged) best = std::max(best, reach_tiles(at.reach_or_range_ft));
    return best;
}

bool Combat::leaves_reach(CombatantId mover, Vec2i from, Vec2i to) const {
    // Facts only; rules5e decides eligibility (reaction used, incapacitated, policy, Disengage).
    for (const auto& s : slots_) {
        if (s.id == mover || !hostile(s.id, mover)) continue;
        const auto* c = rules5e::find(state_, s.id);
        if (!c || c->dead) continue;
        const int reach = melee_reach_tiles(s.id);
        if (reach == 0) continue;
        const Vec2i rp = pos(s.id);
        if (chebyshev(from, rp) <= reach && chebyshev(to, rp) > reach) return true;
    }
    return false;
}

void Combat::install_hooks() {
    // Re-bound before every rules call: Combat is movable, so hooks must not outlive `this`.
    // No decide_reaction: rules5e returns NeedsDecision and sim pauses (settle() is the adapter).
    state_.hooks.decide_reaction = nullptr;
    state_.hooks.poll_order = poll_order_;
    state_.hooks.in_range = [this](const rules5e::EncounterState&, const rules5e::ReactionWindow& w,
                                   const rules5e::Combatant& r) {
        if (w.trigger != ReactionTrigger::LeavingReach) return false;
        const auto to = moving_to(w.trigger_actor);
        if (!to) return false;
        const int reach = melee_reach_tiles(r.id);
        const Vec2i rp = pos(r.id);
        return reach > 0 && chebyshev(pos(w.trigger_actor), rp) <= reach && chebyshev(*to, rp) > reach;
    };
    // A readied ranged attack's facts are taken when it fires, not when it was readied.
    state_.hooks.refresh_ready = [this](const rules5e::EncounterState&, const rules5e::ReadiedAction& ra,
                                        const rules5e::Event& e) -> std::optional<ActionRequest> {
        if (ra.response.kind != ActionKind::Attack) return std::nullopt;
        const auto* a = rules5e::find(state_, ra.actor);
        if (!a || ra.response.attack_index >= a->attacks.size() || !a->attacks[ra.response.attack_index].ranged)
            return std::nullopt;
        const Vec2i rp = pos(ra.actor);
        const Vec2i tp = ra.response.target == e.actor ? moving_to(e.actor).value_or(pos(e.actor)) : pos(ra.response.target);
        ActionRequest q = ra.response;
        if (!ranged_attack_facts(ra.actor, ra.response.attack_index, rp, tp, q, nullptr)) return std::nullopt;
        q.target_within_5ft = chebyshev(rp, tp) <= 1;
        return q;
    };
    state_.hooks.ready_trigger = [this](const rules5e::EncounterState&, const rules5e::ReadiedAction& ra,
                                        const rules5e::Event& e) {
        if (e.kind != rules5e::EventKind::Moved || e.actor == ra.actor) return false;
        const auto kind = ReadyTriggerKind(ra.trigger_id >> 8);
        const int tiles = int(ra.trigger_id & 0xFF);
        const auto* mover = rules5e::find(state_, e.actor);
        if (!mover || mover->dead) return false;
        if (kind == ReadyTriggerKind::TargetWithin && e.actor != ra.response.target) return false;
        if (kind == ReadyTriggerKind::HostileWithin && !hostile(e.actor, ra.actor)) return false;
        const Vec2i mp = moving_to(e.actor).value_or(pos(e.actor));
        const Vec2i rp = pos(ra.actor);
        if (chebyshev(mp, rp) > tiles) return false;
        if (ra.response.kind == ActionKind::Attack) {
            const auto* a = rules5e::find(state_, ra.actor);
            const auto* t = rules5e::find(state_, ra.response.target);
            if (!a || !t || t->dead || ra.response.attack_index >= a->attacks.size()) return false;
            const Vec2i tp = ra.response.target == e.actor ? mp : pos(ra.response.target);
            if (a->attacks[ra.response.attack_index].ranged) {
                const Archetype* arch = archetype(ra.actor);
                const int lt = arch ? reach_tiles(arch->long_range_ft[ra.response.attack_index]) : 0;
                return chebyshev(rp, tp) <= lt && grid_.line_of_sight(rp, tp);
            }
            return chebyshev(rp, tp) <= reach_tiles(a->attacks[ra.response.attack_index].reach_or_range_ft);
        }
        if (ra.response.kind == ActionKind::Move) {
            auto it = readied_dest_.find(ra.actor);
            if (it == readied_dest_.end()) return false;
            if (it->second == mp) return false;
            for (const auto& sl : slots_) {
                const auto* c = rules5e::find(state_, sl.id);
                if (sl.id != ra.actor && sl.id != e.actor && c && !c->dead && pos(sl.id) == it->second) return false;
            }
            return true;
        }
        return false;
    };
}

bool Combat::move_tile(CombatantId mover, Vec2i to, const std::vector<rules5e::Event>& evs) {
    for (const auto& e : evs)
        if (e.kind == rules5e::EventKind::MoveInterrupted && e.actor == mover) return false;
    if (TilePos* p = reg_->get<TilePos>(entity(mover))) { p->x = to.x; p->y = to.y; }
    SimEvent se;
    se.kind = SimEvent::Kind::TileMoved;
    se.actor = mover;
    se.tile = to;
    events_.push_back(se);
    return true;
}

static const ActiveFeature* find_active(const Archetype* a, uint32_t fid) {
    if (!a) return nullptr;
    for (const auto& f : a->active)
        if (f.feature_id == fid) return &f;
    return nullptr;
}

uint8_t Combat::feature_uses_left(CombatantId id, uint32_t fid) const {
    const ActiveFeature* f = find_active(archetype(id), fid);
    if (!f) return 0;
    return rules5e::feature_uses_left(state_, id, fid);
}

bool Combat::can_use_feature(CombatantId id, uint32_t fid) const {
    const ActiveFeature* f = find_active(archetype(id), fid);
    if (!f || state_.ended || state_.pending || rules5e::current_actor(state_) != id) return false;
    if (feature_uses_left(id, fid) == 0) return false;
    return f->cost == FeatureCost::BonusAction ? state_.budget.bonus_action : state_.budget.action;
}

std::optional<Vec2i> Combat::moving_to(CombatantId id) const {
    if (step_to_ && id == rules5e::current_actor(state_)) return step_to_;
    // A readied Move resolving inside another creature's request.
    if (id != rules5e::current_actor(state_)) return readied_destination(id);
    return std::nullopt;
}

// Applies a completed request: rules events, then sim events (tiles) in the order of their
// rules Moved events, then sim bookkeeping (readied destinations).
void Combat::complete(const rules5e::LoggedStep& ls, const std::vector<rules5e::Event>& evs, std::optional<Vec2i> to) {
    const auto& q = ls.request;
    apply_rules_events(evs);
    std::vector<CombatantId> fired;
    for (const auto& e : evs) {
        if (e.kind == rules5e::EventKind::Moved) {
            if (e.reaction_depth == 0 && q.kind == ActionKind::Move && e.actor == q.actor && to) move_tile(q.actor, *to, evs);
            else if (e.reaction_depth > 0 && std::find(fired.begin(), fired.end(), e.actor) != fired.end())
                if (auto d = readied_destination(e.actor)) {
                    if (TilePos* p = reg_->get<TilePos>(entity(e.actor))) { p->x = d->x; p->y = d->y; }
                    SimEvent se;
                    se.kind = SimEvent::Kind::TileMoved;
                    se.actor = e.actor;
                    se.tile = *d;
                    events_.push_back(se);
                }
        }
        if (e.kind == rules5e::EventKind::ReadyTriggered) fired.push_back(e.actor);
    }
    for (const auto& e : evs)
        if (e.kind == rules5e::EventKind::ReadyTriggered || e.kind == rules5e::EventKind::ReadyExpired) readied_dest_.erase(e.actor);
    if (q.kind == ActionKind::Ready && q.ready_kind == ActionKind::Move && to) readied_dest_[q.actor] = *to;
    step_to_.reset();
    ready_to_.reset();
}

bool Combat::ranged_attack_facts(CombatantId actor, uint8_t ai, Vec2i from, Vec2i tp, ActionRequest& req,
                                 std::string* error) const {
    const Archetype* arch = archetype(actor);
    if (!arch || ai >= arch->long_range_ft.size()) { if (error) *error = "bad attack"; return false; }
    const int d = chebyshev(from, tp);
    if (d > reach_tiles(arch->long_range_ft[ai])) { if (error) *error = "target beyond long range"; return false; }
    if (!grid_.line_of_sight(from, tp)) { if (error) *error = "no line of sight"; return false; }
    req.beyond_normal_range = d > reach_tiles(arch->normal_range_ft[ai]);
    req.ranged_hostile_within_5ft = false;
    for (const auto& sl : slots_) {
        const auto* c = rules5e::find(state_, sl.id);
        if (sl.id == actor || !c || c->dead || !hostile(sl.id, actor) || !rules5e::is_conscious(*c) ||
            rules5e::is_incapacitated(*c))
            continue;
        if (chebyshev(from, pos(sl.id)) <= 1) req.ranged_hostile_within_5ft = true;
    }
    return true;
}

bool Combat::ranged_disadvantage(CombatantId actor, uint8_t ai, CombatantId target) const {
    ActionRequest q{};
    if (!ranged_attack_facts(actor, ai, pos(actor), pos(target), q, nullptr)) return false;
    return q.beyond_normal_range || q.ranged_hostile_within_5ft;
}

bool Combat::can_shoot(CombatantId actor, uint8_t ai, CombatantId target) const {
    const auto* a = rules5e::find(state_, actor);
    if (!a || ai >= a->attacks.size() || !a->attacks[ai].ranged || !rules5e::find(state_, target)) return false;
    ActionRequest scratch{};
    return ranged_attack_facts(actor, ai, pos(actor), pos(target), scratch, nullptr);
}

SubmitResult Combat::finish(rules5e::StepResult sr) {
    SubmitResult r;
    r.status = sr.status;
    if (sr.status == StepStatus::Rejected) {
        r.error = std::string(sr.error);
        if (!state_.pending) { step_to_.reset(); ready_to_.reset(); }   // nothing paused
        return r;
    }
    r.ok = true;
    if (sr.status == StepStatus::NeedsDecision) {   // events are provisional; state is pre-request
        r.needs = std::move(sr.needs);
        return r;
    }
    const rules5e::LoggedStep& ls = *sr.logged;
    std::optional<Vec2i> to;
    if (ls.request.kind == ActionKind::Move) to = step_to_;
    if (ls.request.kind == ActionKind::Ready) to = ready_to_;
    complete(ls, sr.events, to);
    log_.push_back({ls, to});
    r.reactions = std::move(sr.reactions);
    return r;
}

SubmitResult Combat::submit(const Command& cmd) {
    SubmitResult r;
    if (state_.pending) { r.error = "a reaction window is waiting for a decision"; r.needs = pending(); return r; }
    if (state_.ended) { r.error = "encounter ended"; return r; }
    install_hooks();
    return cmd.kind == CommandKind::Step ? do_step(cmd.to) : do_rules(cmd.request, cmd.to);
}

SubmitResult Combat::decide(const ReactionDecision& d) {
    SubmitResult r;
    const auto nd = pending();
    if (!nd) { r.error = "no pending reaction window"; return r; }
    if (d.window_seq != nd->window_seq) { r.error = "decision for a different window"; r.needs = nd; return r; }
    install_hooks();
    return finish(rules5e::step(state_, rules5e::decision_request(*nd, d)));
}

SubmitResult Combat::settle(const ReactionDeciderFn& decider) {
    SubmitResult r;
    r.ok = true;
    r.status = StepStatus::Done;
    while (auto nd = pending()) {
        ReactionDecision d = decider ? decider(*this, *nd) : ReactionDecision{nd->window_seq, false, 0};
        d.window_seq = nd->window_seq;
        if (d.react && d.option >= nd->options.size()) d.react = false;
        r = decide(d);
        if (!r.ok) return r;
    }
    return r;
}

SubmitResult Combat::do_rules(ActionRequest req, Vec2i to) {
    SubmitResult r;
    const CombatantId actor = rules5e::current_actor(state_);
    if (req.actor != actor) { r.error = "not this combatant's turn"; return r; }
    if (req.kind == ActionKind::Move) { r.error = "use Step commands for movement (sim paths the grid)"; return r; }
    if (req.kind == ActionKind::OpportunityAttack) { r.error = "opportunity attacks come from reaction windows"; return r; }
    if (req.kind == ActionKind::ReactionDecision) { r.error = "use decide() for reaction decisions"; return r; }
    if (req.kind == ActionKind::UseFeature) {   // parameters come from content, not the caller
        const ActiveFeature* f = find_active(archetype(actor), req.feature_id);
        if (!f) { r.error = "no such active feature"; return r; }
        if (feature_uses_left(actor, req.feature_id) == 0) { r.error = "feature has no uses left"; return r; }
        req.as_bonus_action = f->cost == FeatureCost::BonusAction;
        req.heal_dice = f->heal;   // zero unless the hook set a heal
    }
    if (req.kind == ActionKind::Attack) {
        const auto* a = rules5e::find(state_, actor);
        if (!a || req.attack_index >= a->attacks.size() || !rules5e::find(state_, req.target)) {
            r.error = "bad attack";
            return r;
        }
        const int d = chebyshev(pos(actor), pos(req.target));
        if (a->attacks[req.attack_index].ranged) {
            if (!ranged_attack_facts(actor, req.attack_index, pos(actor), pos(req.target), req, &r.error)) return r;
        } else if (d > reach_tiles(a->attacks[req.attack_index].reach_or_range_ft)) {
            r.error = "target out of reach";
            return r;
        }
        req.target_within_5ft = d <= 1;
    }
    ready_to_.reset();
    if (req.kind == ActionKind::Ready) {
        const auto kind = ReadyTriggerKind(req.trigger_id >> 8);
        const int tiles = int(req.trigger_id & 0xFF);
        if ((kind != ReadyTriggerKind::TargetWithin && kind != ReadyTriggerKind::HostileWithin) || tiles < 1 ||
            (req.trigger_id >> 16) != 0) {
            r.error = "bad ready trigger";
            return r;
        }
        const auto* a = rules5e::find(state_, actor);
        if (req.ready_kind == ActionKind::Attack) {
            const auto* t = rules5e::find(state_, req.target);
            if (!t || req.attack_index >= a->attacks.size() || !hostile(actor, req.target)) { r.error = "bad readied attack"; return r; }
            if (kind == ReadyTriggerKind::HostileWithin) { r.error = "a readied attack needs a TargetWithin trigger"; return r; }
            const auto& at = a->attacks[req.attack_index];
            const Archetype* arch = archetype(actor);
            if (at.ranged) {
                if (tiles > reach_tiles(arch->long_range_ft[req.attack_index])) { r.error = "trigger beyond long range"; return r; }
                req.beyond_normal_range = tiles > reach_tiles(arch->normal_range_ft[req.attack_index]);
                req.ranged_hostile_within_5ft = false;   // re-evaluated when it fires (hooks.refresh_ready)
                req.target_within_5ft = tiles <= 1;
            } else {
                if (tiles > reach_tiles(at.reach_or_range_ft)) { r.error = "trigger beyond reach"; return r; }
                req.target_within_5ft = tiles <= 1;
            }
        } else if (req.ready_kind == ActionKind::Move) {
            const Vec2i from = pos(actor);
            if (!grid_.walkable(to) || to == from) { r.error = "bad readied move destination"; return r; }
            const auto path = grid_.path(from, {to}, occupied_except(actor));
            if (path.empty()) { r.error = "readied move destination unreachable"; return r; }
            int cost = 0;
            for (Vec2i p : path) cost += grid_.step_cost_ft(p);
            if (cost > int(a->speed_ft)) { r.error = "readied move longer than speed"; return r; }
            req.move_ft = int16_t(cost);
            req.leaves_reach = true;   // rules polls; sim's in_range decides who is actually left
            ready_to_ = to;
        } else {
            r.error = "only Attack or Move can be readied";
            return r;
        }
    }
    step_to_.reset();
    return finish(rules5e::step(state_, req));
}

SubmitResult Combat::do_step(Vec2i to) {
    SubmitResult r;
    const CombatantId mover = rules5e::current_actor(state_);
    const Vec2i from = pos(mover);
    if (chebyshev(from, to) != 1) { r.error = "step must be to an adjacent tile"; return r; }
    if (!grid_.walkable(to)) { r.error = "tile not walkable"; return r; }
    const auto occ = occupied_except(mover);
    if (std::find(occ.begin(), occ.end(), to) != occ.end()) { r.error = "tile occupied"; return r; }
    const int cost = grid_.step_cost_ft(to);
    if (cost > state_.budget.movement_ft) { r.error = "not enough movement"; return r; }
    ActionRequest mv{mover, ActionKind::Move};
    mv.move_ft = int16_t(cost);
    mv.leaves_reach = leaves_reach(mover, from, to);
    step_to_ = to;
    return finish(rules5e::step(state_, mv));
}

uint64_t Combat::hash() const {
    CanonicalHasher h;
    h.u64(seed_);
    h.u32(uint32_t(events_.size()));
    for (const auto& e : events_) {
        h.byte(uint8_t(e.kind));
        h.byte(uint8_t(e.rules.kind));
        h.u32(e.rules.actor);
        h.u32(e.rules.target);
        h.i32(e.rules.a);
        h.i32(e.rules.b);
        h.i32(e.rules.c);
        h.byte(e.rules.reaction_depth);
        h.u32(e.actor);
        h.i32(e.tile.x);
        h.i32(e.tile.y);
    }
    for (const auto& s : slots_) {
        h.u32(s.id);
        const Vec2i p = pos(s.id);
        h.i32(p.x);
        h.i32(p.y);
        const auto* c = rules5e::find(state_, s.id);
        h.i16(c ? c->hp : int16_t(0));
        h.byte(c && c->dead ? 1 : 0);
    }
    // Feature uses (rules state). Folded only when any are registered, so feature-less combats
    // keep their hashes.
    if (!state_.features.empty()) h.u64(rules5e::feature_uses_digest(state_));
    return h.value();
}

uint64_t Combat::log_hash() const {
    CanonicalHasher h;
    h.u32(uint32_t(log_.size()));
    for (const auto& e : log_) {
        const ActionRequest& q = e.step.request;
        h.u32(q.actor);
        h.byte(uint8_t(q.kind));
        h.u32(q.target);
        h.byte(q.attack_index);
        h.i16(q.move_ft);
        h.byte(q.as_bonus_action ? 1 : 0);
        h.byte(q.target_within_5ft ? 1 : 0);
        h.byte(q.leaves_reach ? 1 : 0);
        h.u32(q.feature_id);
        h.byte(uint8_t(q.ready_kind));
        h.u32(q.trigger_id);
        h.byte(q.ranged_hostile_within_5ft ? 1 : 0);
        h.byte(q.beyond_normal_range ? 1 : 0);
        h.byte(q.heal_dice.count);
        h.byte(q.heal_dice.sides);
        h.i16(q.heal_dice.bonus);
        h.byte(e.to ? 1 : 0);
        h.i32(e.to ? e.to->x : 0);
        h.i32(e.to ? e.to->y : 0);
        h.u32(uint32_t(e.step.reactions.size()));
        for (const auto& rr : e.step.reactions) {
            h.u32(rr.window_seq);
            h.byte(uint8_t(rr.trigger));
            h.u32(rr.reactor);
            h.byte(rr.decision.react ? 1 : 0);
            h.byte(rr.decision.option);
        }
    }
    return h.value();
}

std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content, uint64_t seed,
                             const std::vector<LogEntry>& log, std::string* error) {
    auto c = Combat::create(reg, chunk, content, seed, error);
    if (!c) return std::nullopt;
    for (std::size_t i = 0; i < log.size(); ++i) {
        const LogEntry& e = log[i];
        c->install_hooks();
        c->step_to_.reset();
        c->ready_to_.reset();
        if (e.step.request.kind == ActionKind::Move) c->step_to_ = e.to;
        if (e.step.request.kind == ActionKind::Ready) c->ready_to_ = e.to;
        rules5e::StepResult sr = rules5e::replay_step(c->state_, e.step);   // rules' replay; no decider
        if (!sr.ok || sr.desync || sr.status != StepStatus::Done) {
            if (error) *error = "replay desync at entry " + std::to_string(i) + ": " +
                                (sr.ok ? std::string("logged decisions do not match") : std::string(sr.error));
            return std::nullopt;
        }
        c->complete(*sr.logged, sr.events, e.to);
        c->log_.push_back({*sr.logged, e.to});
    }
    return c;
}

} // namespace eng::sim
