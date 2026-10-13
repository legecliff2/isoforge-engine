#include "sim/combat.hpp"

#include <algorithm>

#include "base/canonical_hash.hpp"
#include "rules5e/conditions.hpp"

namespace eng::sim {

using rules5e::ActionKind;
using rules5e::ActionRequest;

std::vector<CombatantId> default_poll_order(const rules5e::EncounterState& s, const std::vector<CombatantId>& eligible) {
    // Initiative order starting after the current actor (the current actor last).
    std::vector<CombatantId> out;
    const std::size_t n = s.order.size();
    for (std::size_t k = 1; k <= n; ++k) {
        const CombatantId id = s.order[(s.turn_index + k) % n].id;
        if (std::find(eligible.begin(), eligible.end(), id) != eligible.end()) out.push_back(id);
    }
    return out;
}

std::optional<Combat> Combat::create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                     uint64_t seed, std::string* error) {
    Combat c(reg, chunk);
    c.seed_ = seed;
    std::vector<rules5e::Combatant> cs;
    for (const auto& sp : chunk.spawns) {
        const Archetype* a = content.find(sp.archetype);
        if (!a) continue;   // non-combatant spawn
        if (sp.stable_id == 0) { if (error) *error = "spawn stable_id 0 is reserved"; return std::nullopt; }
        if (reg.find(sp.stable_id)) { if (error) *error = "duplicate spawn stable_id"; return std::nullopt; }
        rules5e::Combatant k = a->base;
        k.id = sp.stable_id;
        k.team = a->schema_type == "character" ? 0 : 1;
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

std::optional<uint8_t> Combat::melee_attack(CombatantId id) const {
    const auto* c = rules5e::find(state_, id);
    if (!c) return std::nullopt;
    for (uint8_t i = 0; i < c->attacks.size(); ++i)
        if (!c->attacks[i].ranged) return i;
    return std::nullopt;
}

SubmitResult Combat::submit(const Command& cmd) {
    SubmitResult r;
    if (cmd.kind == CommandKind::Decide) {
        r = do_decide(cmd.decision);
    } else if (pending_) {
        r.error = "a reaction window is waiting for a decision";
        r.needs = pending_;
        return r;
    } else if (state_.ended) {
        r.error = "encounter ended";
        return r;
    } else if (cmd.kind == CommandKind::Step) {
        r = do_step(cmd.to);
    } else {
        r = do_rules(cmd.request);
    }
    if (r.ok) log_.push_back(cmd);
    return r;
}

SubmitResult Combat::do_rules(ActionRequest req) {
    SubmitResult r;
    const CombatantId actor = rules5e::current_actor(state_);
    if (req.actor != actor) { r.error = "not this combatant's turn"; return r; }
    if (req.kind == ActionKind::Move) { r.error = "use Step commands for movement (sim paths the grid)"; return r; }
    if (req.kind == ActionKind::OpportunityAttack) { r.error = "opportunity attacks come from reaction windows"; return r; }
    if (req.kind == ActionKind::Attack) {
        const auto* a = rules5e::find(state_, actor);
        if (!a || req.attack_index >= a->attacks.size() || !rules5e::find(state_, req.target)) {
            r.error = "bad attack";
            return r;
        }
        const auto& at = a->attacks[req.attack_index];
        const int d = chebyshev(pos(actor), pos(req.target));
        const int max_tiles = reach_tiles(at.reach_or_range_ft);
        if (d > max_tiles) { r.error = "target out of reach/range"; return r; }
        req.target_within_5ft = d <= 1;
    }
    rules5e::StepResult sr = rules5e::step(state_, req);
    if (!sr.ok) { r.error = std::string(sr.error); return r; }
    apply_rules_events(sr.events);
    r.ok = true;
    return r;
}

std::vector<CombatantId> Combat::leaving_reach_reactors(CombatantId mover, Vec2i from, Vec2i to) const {
    std::vector<CombatantId> eligible;   // initiative order
    const auto* m = rules5e::find(state_, mover);
    if (!m || m->disengaged) return eligible;
    for (const auto& ie : state_.order) {
        const CombatantId id = ie.id;
        if (id == mover || !hostile(id, mover)) continue;
        const auto* c = rules5e::find(state_, id);
        if (!c || c->dead || c->reaction_used || !rules5e::is_conscious(*c) || !!rules5e::is_incapacitated(*c)) continue;
        auto mi = melee_attack(id);
        if (!mi) continue;
        const int reach = reach_tiles(c->attacks[*mi].reach_or_range_ft);
        const Vec2i rp = pos(id);
        if (chebyshev(from, rp) <= reach && chebyshev(to, rp) > reach) eligible.push_back(id);
    }
    return poll_order_ ? poll_order_(state_, eligible) : default_poll_order(state_, eligible);
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
    const auto* m = rules5e::find(state_, mover);
    if (!m || !!rules5e::is_incapacitated(*m) || !rules5e::is_conscious(*m)) { r.error = "cannot move"; return r; }

    Window w{next_window_seq_, mover, from, to, cost, leaving_reach_reactors(mover, from, to), 0};
    if (w.queue.empty()) return finish_move(w);
    ++next_window_seq_;
    window_ = w;
    SimEvent se;
    se.kind = SimEvent::Kind::WindowOpened;
    se.actor = mover;
    se.window_seq = w.seq;
    events_.push_back(se);
    return advance_window();
}

SubmitResult Combat::advance_window() {
    // STUB of rules' window polling: next still-eligible reactor, else complete the move.
    Window& w = *window_;
    const auto* m = rules5e::find(state_, w.mover);
    const bool mover_ok = m && !m->dead && rules5e::is_conscious(*m) && !state_.ended;
    while (mover_ok && w.next < w.queue.size()) {
        const CombatantId rid = w.queue[w.next];
        const auto* c = rules5e::find(state_, rid);
        auto mi = melee_attack(rid);
        if (c && !c->reaction_used && rules5e::is_conscious(*c) && !rules5e::is_incapacitated(*c) && mi) {
            NeedsDecision nd{w.seq, ReactionTrigger::LeavingReach, rid, w.mover, {ReactionOption{*mi}}};
            pending_ = nd;
            SubmitResult r;
            r.ok = true;
            r.needs = nd;
            return r;
        }
        ++w.next;
    }
    const Window done = w;
    window_.reset();
    pending_.reset();
    if (!mover_ok) {
        SimEvent se;
        se.kind = SimEvent::Kind::MoveInterrupted;
        se.actor = done.mover;
        se.window_seq = done.seq;
        events_.push_back(se);
        SubmitResult r;
        r.ok = true;
        return r;
    }
    return finish_move(done);
}

SubmitResult Combat::do_decide(const ReactionDecision& d) {
    SubmitResult r;
    if (!pending_ || !window_) { r.error = "no pending reaction window"; return r; }
    if (d.window_seq != pending_->window_seq) { r.error = "decision for a different window"; r.needs = pending_; return r; }
    if (d.react && d.option >= pending_->options.size()) { r.error = "bad option"; r.needs = pending_; return r; }
    const NeedsDecision nd = *pending_;
    SimEvent se;
    se.actor = nd.reactor;
    se.window_seq = nd.window_seq;
    if (d.react) {
        se.kind = SimEvent::Kind::ReactionTaken;
        events_.push_back(se);
        ActionRequest oa{nd.reactor, ActionKind::OpportunityAttack, nd.trigger_actor};
        oa.attack_index = nd.options[d.option].attack_index;
        oa.target_within_5ft = chebyshev(window_->from, pos(nd.reactor)) <= 1;   // before leaving reach
        rules5e::StepResult sr = rules5e::step(state_, oa);
        if (!sr.ok) { r.error = std::string("opportunity attack rejected: ") + std::string(sr.error); return r; }
        apply_rules_events(sr.events);
    } else {
        se.kind = SimEvent::Kind::ReactionDeclined;
        events_.push_back(se);
    }
    ++window_->next;
    pending_.reset();
    return advance_window();
}

SubmitResult Combat::finish_move(const Window& w) {
    SubmitResult r;
    ActionRequest mv{w.mover, ActionKind::Move};
    mv.move_ft = int16_t(w.cost_ft);
    rules5e::StepResult sr = rules5e::step(state_, mv);
    if (!sr.ok) { r.error = std::string(sr.error); return r; }
    if (TilePos* p = reg_->get<TilePos>(entity(w.mover))) { p->x = w.to.x; p->y = w.to.y; }
    apply_rules_events(sr.events);
    SimEvent se;
    se.kind = SimEvent::Kind::TileMoved;
    se.actor = w.mover;
    se.tile = w.to;
    events_.push_back(se);
    r.ok = true;
    return r;
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
        h.u32(e.actor);
        h.i32(e.tile.x);
        h.i32(e.tile.y);
        h.u32(e.window_seq);
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
    for (const auto& c : log_) {
        h.byte(uint8_t(c.kind));
        h.u32(c.request.actor);
        h.byte(uint8_t(c.request.kind));
        h.u32(c.request.target);
        h.byte(c.request.attack_index);
        h.i16(c.request.move_ft);
        h.byte(c.request.as_bonus_action ? 1 : 0);
        h.i32(c.to.x);
        h.i32(c.to.y);
        h.u32(c.decision.window_seq);
        h.byte(c.decision.react ? 1 : 0);
        h.byte(c.decision.option);
    }
    return h.value();
}

std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content, uint64_t seed,
                             const std::vector<Command>& log, std::string* error) {
    auto c = Combat::create(reg, chunk, content, seed, error);
    if (!c) return std::nullopt;
    for (std::size_t i = 0; i < log.size(); ++i) {
        auto r = c->submit(log[i]);
        if (!r.ok) {
            if (error) *error = "replay desync at command " + std::to_string(i) + ": " + r.error;
            return std::nullopt;
        }
    }
    return c;
}

} // namespace eng::sim
