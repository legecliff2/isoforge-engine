#include "sim/combat.hpp"

#include <algorithm>

#include "base/canonical_hash.hpp"
#include "rules5e/conditions.hpp"

namespace eng::sim {

using rules5e::ActionKind;
using rules5e::ActionRequest;

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
    state_.hooks.in_range = [this](const rules5e::EncounterState&, const rules5e::ReactionWindow& w,
                                   const rules5e::Combatant& r) {
        if (w.trigger != ReactionTrigger::LeavingReach || !step_to_) return false;
        const int reach = melee_reach_tiles(r.id);
        const Vec2i rp = pos(r.id);
        return reach > 0 && chebyshev(pos(w.trigger_actor), rp) <= reach && chebyshev(*step_to_, rp) > reach;
    };
    state_.hooks.decide_reaction = [this](const rules5e::EncounterState&, const rules5e::ReactionWindow& w,
                                          const rules5e::Combatant& r, std::span<const ReactionOption> opts) {
        if (!decider_) return rules5e::ReactionDecision{};
        NeedsDecision nd{w.seq, w.trigger, r.id, w.trigger_actor, {opts.begin(), opts.end()}, w};
        const ReactionDecision d = decider_(*this, nd);
        if (d.window_seq != w.seq || (d.react && d.option >= opts.size())) return rules5e::ReactionDecision{};
        return rules5e::ReactionDecision{d.react, d.option};
    };
}

SubmitResult Combat::submit(const Command& cmd) { return run(cmd, nullptr); }
SubmitResult Combat::submit_logged(const LoggedCommand& e) { return run(e.command, &e.reactions); }

SubmitResult Combat::run(const Command& cmd, const std::vector<rules5e::ReactionRecord>* replay) {
    SubmitResult r;
    if (state_.ended) { r.error = "encounter ended"; return r; }
    install_hooks();
    r = cmd.kind == CommandKind::Step ? do_step(cmd.to, replay) : do_rules(cmd.request, replay);
    if (r.ok) log_.push_back({cmd, r.reactions});
    return r;
}

SubmitResult Combat::rules_step(const ActionRequest& req, const std::vector<rules5e::ReactionRecord>* replay) {
    SubmitResult r;
    rules5e::StepResult sr = replay ? rules5e::replay_step(state_, rules5e::LoggedStep{req, *replay})
                                    : rules5e::step(state_, req);
    if (!sr.ok) { r.error = std::string(sr.error); return r; }
    if (sr.desync) { r.error = "reaction decisions do not match the log"; return r; }
    apply_rules_events(sr.events);
    r.reactions = std::move(sr.reactions);
    r.ok = true;
    return r;
}

SubmitResult Combat::do_rules(ActionRequest req, const std::vector<rules5e::ReactionRecord>* replay) {
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
        const int d = chebyshev(pos(actor), pos(req.target));
        if (d > reach_tiles(a->attacks[req.attack_index].reach_or_range_ft)) { r.error = "target out of reach/range"; return r; }
        req.target_within_5ft = d <= 1;
    }
    return rules_step(req, replay);
}

SubmitResult Combat::do_step(Vec2i to, const std::vector<rules5e::ReactionRecord>* replay) {
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
    r = rules_step(mv, replay);
    step_to_.reset();
    if (!r.ok) return r;
    bool interrupted = false;
    for (std::size_t i = events_.size(); i-- > 0 && events_[i].kind == SimEvent::Kind::Rules;)
        if (events_[i].rules.kind == rules5e::EventKind::MoveInterrupted && events_[i].rules.actor == mover) interrupted = true;
    if (!interrupted) {
        if (TilePos* p = reg_->get<TilePos>(entity(mover))) { p->x = to.x; p->y = to.y; }
        SimEvent se;
        se.kind = SimEvent::Kind::TileMoved;
        se.actor = mover;
        se.tile = to;
        events_.push_back(se);
    }
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
        const Command& c = e.command;
        h.byte(uint8_t(c.kind));
        h.u32(c.request.actor);
        h.byte(uint8_t(c.request.kind));
        h.u32(c.request.target);
        h.byte(c.request.attack_index);
        h.i16(c.request.move_ft);
        h.byte(c.request.as_bonus_action ? 1 : 0);
        h.i32(c.to.x);
        h.i32(c.to.y);
        h.u32(uint32_t(e.reactions.size()));
        for (const auto& rr : e.reactions) {
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
                             const std::vector<LoggedCommand>& log, std::string* error) {
    auto c = Combat::create(reg, chunk, content, seed, error);
    if (!c) return std::nullopt;
    for (std::size_t i = 0; i < log.size(); ++i) {
        auto r = c->submit_logged(log[i]);
        if (!r.ok) {
            if (error) *error = "replay desync at command " + std::to_string(i) + ": " + r.error;
            return std::nullopt;
        }
    }
    return c;
}

} // namespace eng::sim
