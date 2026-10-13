#pragma once
// Combat state machine v0: chunk spawns -> ECS entities + rules5e encounter; grid movement;
// LeavingReach reaction windows (pause/resume); a command log that alone replays the fight.
//
// Reaction windows: the API below mirrors the Director-approved Rules reaction API
// (step() may return NeedsDecision{window_seq, reactor, options}; the pending window is
// state; resumption is a ReactionDecision request; polling order is a policy hook whose default
// starts after the current actor; every window-opening action is logged). Until that Rules PR
// lands, sim implements LeavingReach itself on top of rules5e's direct OpportunityAttack
// request ("STUB" below). When it lands, Combat forwards to rules5e::step / ReactionDecision and
// the public surface stays the same.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "base/math.hpp"
#include "ecs/registry.hpp"
#include "rules5e/encounter.hpp"
#include "sim/components.hpp"
#include "sim/content.hpp"
#include "sim/grid.hpp"
#include "world/chunk.hpp"

namespace eng::sim {

using rules5e::CombatantId;

// ---- Reaction windows (mirrors upcoming rules5e API) -----------------------------------------
enum class ReactionTrigger : uint8_t { LeavingReach };
struct ReactionOption { uint8_t attack_index = 0; };            // Attack option (opportunity attack)
struct NeedsDecision {
    uint32_t window_seq = 0;
    ReactionTrigger trigger = ReactionTrigger::LeavingReach;
    CombatantId reactor = 0;
    CombatantId trigger_actor = 0;
    std::vector<ReactionOption> options;
};
struct ReactionDecision {
    uint32_t window_seq = 0; bool react = false; uint8_t option = 0;
    friend bool operator==(const ReactionDecision&, const ReactionDecision&) = default;
};
// Orders eligible reactors (given in initiative order). Default: start after the current actor.
using PollOrderFn = std::function<std::vector<CombatantId>(const rules5e::EncounterState&,
                                                           const std::vector<CombatantId>& eligible_in_initiative_order)>;
std::vector<CombatantId> default_poll_order(const rules5e::EncounterState&, const std::vector<CombatantId>&);

// ---- Commands (the replayable action log) ----------------------------------------------------
enum class CommandKind : uint8_t {
    Rules,    // request: any rules5e ActionRequest for the current actor (sim fills target_within_5ft)
    Step,     // to: move the current actor one tile (8-neighbour); may open a LeavingReach window
    Decide,   // decision: answer the pending window
};
struct Command {
    CommandKind kind = CommandKind::Rules;
    rules5e::ActionRequest request{};
    Vec2i to{};
    ReactionDecision decision{};
    friend bool operator==(const Command& a, const Command& b) {
        return a.kind == b.kind && a.request.actor == b.request.actor && a.request.kind == b.request.kind &&
               a.request.target == b.request.target && a.request.attack_index == b.request.attack_index &&
               a.request.move_ft == b.request.move_ft && a.to == b.to && a.decision == b.decision;
    }
};

struct SimEvent {
    enum class Kind : uint8_t { Rules, TileMoved, WindowOpened, ReactionTaken, ReactionDeclined, MoveInterrupted };
    Kind kind = Kind::Rules;
    rules5e::Event rules{};       // Kind::Rules
    CombatantId actor = 0;        // mover / reactor
    Vec2i tile{};                 // TileMoved: new tile
    uint32_t window_seq = 0;
};

struct SubmitResult {
    bool ok = false;
    std::string error;
    std::optional<NeedsDecision> needs;   // set while a window is waiting for a decision
};

class Combat {
public:
    // Spawns every chunk spawn whose archetype is in `content` as an ECS entity (StableId =
    // spawn stable_id = CombatantId). Teams v0: characters 0, monsters 1. Rolls initiative.
    static std::optional<Combat> create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                        uint64_t seed, std::string* error = nullptr);

    SubmitResult submit(const Command& cmd);

    const std::optional<NeedsDecision>& pending() const noexcept { return pending_; }
    bool ended() const noexcept { return state_.ended; }
    const rules5e::EncounterState& state() const noexcept { return state_; }
    const std::vector<SimEvent>& events() const noexcept { return events_; }
    const std::vector<Command>& log() const noexcept { return log_; }
    uint64_t seed() const noexcept { return seed_; }
    const Grid& grid() const noexcept { return grid_; }
    ecs::Registry& registry() const noexcept { return *reg_; }

    Vec2i pos(CombatantId id) const;
    ecs::Entity entity(CombatantId id) const;
    const Archetype* archetype(CombatantId id) const;
    std::vector<Vec2i> occupied_except(CombatantId id) const;   // tiles of other non-dead combatants
    bool hostile(CombatantId a, CombatantId b) const;

    // Canonical hash (StableHash) of the event stream + final positions. Golden material.
    uint64_t hash() const;
    // Canonical hash of the command log alone.
    uint64_t log_hash() const;

    void set_poll_order(PollOrderFn f) { poll_order_ = std::move(f); }

private:
    struct Slot { CombatantId id; ecs::Entity entity; const Archetype* archetype; };
    struct Window {   // STUB pending window (moves into rules5e::EncounterState with the Rules PR)
        uint32_t seq;
        CombatantId mover;
        Vec2i from, to;
        int cost_ft;
        std::vector<CombatantId> queue;
        std::size_t next = 0;
    };

    Combat(ecs::Registry& reg, const world::Chunk& chunk) : reg_(&reg), grid_(chunk) {}
    void apply_rules_events(const std::vector<rules5e::Event>& evs);
    void sync_ecs();
    SubmitResult do_rules(rules5e::ActionRequest req);
    SubmitResult do_step(Vec2i to);
    SubmitResult do_decide(const ReactionDecision& d);
    SubmitResult advance_window();
    SubmitResult finish_move(const Window& w);
    std::vector<CombatantId> leaving_reach_reactors(CombatantId mover, Vec2i from, Vec2i to) const;
    std::optional<uint8_t> melee_attack(CombatantId id) const;
    const Slot* slot(CombatantId id) const;

    ecs::Registry* reg_;
    Grid grid_;
    uint64_t seed_ = 0;
    rules5e::EncounterState state_{};
    std::vector<Slot> slots_;              // in spawn order
    std::vector<SimEvent> events_;
    std::vector<Command> log_;
    std::optional<NeedsDecision> pending_;
    std::optional<Window> window_;
    uint32_t next_window_seq_ = 0;
    PollOrderFn poll_order_;
};

// Replays a command log from scratch (same chunk, content and seed) into `reg`.
// Returns the rebuilt combat, or nullopt + error if any command is rejected (desync).
std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                             uint64_t seed, const std::vector<Command>& log, std::string* error = nullptr);

} // namespace eng::sim
