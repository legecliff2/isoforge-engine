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
        if (w.trigger != ReactionTrigger::LeavingReach || !step_to_) return false;
        const int reach = melee_reach_tiles(r.id);
        const Vec2i rp = pos(r.id);
        return reach > 0 && chebyshev(pos(w.trigger_actor), rp) <= reach && chebyshev(*step_to_, rp) > reach;
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
    if (f->uses == 0) return 255;
    auto it = feature_used_.find({id, fid});
    const uint8_t used = it == feature_used_.end() ? 0 : it->second;
    return used >= f->uses ? 0 : uint8_t(f->uses - used);
}

bool Combat::can_use_feature(CombatantId id, uint32_t fid) const {
    const ActiveFeature* f = find_active(archetype(id), fid);
    if (!f || state_.ended || state_.pending || rules5e::current_actor(state_) != id) return false;
    if (feature_uses_left(id, fid) == 0) return false;
    return f->cost == FeatureCost::BonusAction ? state_.budget.bonus_action : state_.budget.action;
}

void Combat::on_completed(const rules5e::LoggedStep& ls) {
    if (ls.request.kind == ActionKind::UseFeature) ++feature_used_[{ls.request.actor, ls.request.feature_id}];
}

SubmitResult Combat::finish(rules5e::StepResult sr) {
    SubmitResult r;
    r.status = sr.status;
    if (sr.status == StepStatus::Rejected) {
        r.error = std::string(sr.error);
        if (!state_.pending) step_to_.reset();   // a rejected new request leaves no paused Step
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
    step_to_.reset();
    apply_rules_events(sr.events);
    if (to) move_tile(ls.request.actor, *to, sr.events);
    on_completed(ls);
    log_.push_back({ls, to});
    r.reactions = std::move(sr.reactions);
    return r;
}

SubmitResult Combat::submit(const Command& cmd) {
    SubmitResult r;
    if (state_.pending) { r.error = "a reaction window is waiting for a decision"; r.needs = pending(); return r; }
    if (state_.ended) { r.error = "encounter ended"; return r; }
    install_hooks();
    return cmd.kind == CommandKind::Step ? do_step(cmd.to) : do_rules(cmd.request);
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

SubmitResult Combat::do_rules(ActionRequest req) {
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
        if (d > reach_tiles(a->attacks[req.attack_index].reach_or_range_ft)) { r.error = "target out of reach/range"; return r; }
        req.target_within_5ft = d <= 1;
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
        c->step_to_ = e.to;
        rules5e::StepResult sr = rules5e::replay_step(c->state_, e.step);   // rules' replay; no decider
        c->step_to_.reset();
        if (!sr.ok || sr.desync || sr.status != StepStatus::Done) {
            if (error) *error = "replay desync at entry " + std::to_string(i) + ": " +
                                (sr.ok ? std::string("logged decisions do not match") : std::string(sr.error));
            return std::nullopt;
        }
        c->apply_rules_events(sr.events);
        if (e.to) c->move_tile(e.step.request.actor, *e.to, sr.events);
        c->on_completed(*sr.logged);
        c->log_.push_back({*sr.logged, e.to});
    }
    return c;
}

} // namespace eng::sim
