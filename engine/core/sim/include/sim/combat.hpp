#pragma once
// Combat state machine v0: chunk spawns -> ECS entities + rules5e encounter; grid movement;
// opportunity attacks through rules5e LeavingReach reaction windows (#11); a command log that
// alone replays the fight.
//
// Reaction windows: rules5e owns the window (eligibility, polling order = initiative after the
// current actor, options, resolution, MoveInterrupted). sim supplies `leaves_reach` on each Move
// and `hooks.in_range` from grid positions. Decisions use rules5e's sync `decide_reaction` hook,
// adapted to sim's NeedsDecision -> ReactionDecision shape so the planned async step
// (step() returns NeedsDecision, resumed by a ReactionDecision request) swaps in behind
// `submit()` without changing callers: today every NeedsDecision is answered inline by the
// decider; later submit() returns `needs` and callers send a Decide command.
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

// ---- Reaction windows (sim view of rules5e windows; async-ready shape) -----------------------
using rules5e::ReactionTrigger;
using rules5e::ReactionOption;
struct NeedsDecision {
    uint32_t window_seq = 0;
    ReactionTrigger trigger = ReactionTrigger::LeavingReach;
    CombatantId reactor = 0;
    CombatantId trigger_actor = 0;
    std::vector<ReactionOption> options;
    rules5e::ReactionWindow window{};   // full window (as in Rules #15's NeedsDecision)
};
struct ReactionDecision {
    uint32_t window_seq = 0; bool react = false; uint8_t option = 0;
    friend bool operator==(const ReactionDecision&, const ReactionDecision&) = default;
};
class Combat;
// Sync decision adapter (AI, tests). Empty = decline.
using ReactionDeciderFn = std::function<ReactionDecision(const Combat&, const NeedsDecision&)>;

// ---- Commands (the replayable action log) ----------------------------------------------------
enum class CommandKind : uint8_t {
    Rules,    // request: any rules5e ActionRequest for the current actor (sim fills target_within_5ft)
    Step,     // to: move the current actor one tile (8-neighbour); may open a LeavingReach window
    // Decide: reserved for the async Rules API (answer a pending window).
};
struct Command {
    CommandKind kind = CommandKind::Rules;
    rules5e::ActionRequest request{};
    Vec2i to{};
    friend bool operator==(const Command& a, const Command& b) {
        return a.kind == b.kind && a.request.actor == b.request.actor && a.request.kind == b.request.kind &&
               a.request.target == b.request.target && a.request.attack_index == b.request.attack_index &&
               a.request.move_ft == b.request.move_ft && a.to == b.to;
    }
};
// Log entry: the command plus the Ask decisions rules recorded while it ran (rules5e::LoggedStep).
struct LoggedCommand {
    Command command;
    std::vector<rules5e::ReactionRecord> reactions;
};

struct SimEvent {
    enum class Kind : uint8_t { Rules, TileMoved };
    Kind kind = Kind::Rules;
    rules5e::Event rules{};       // Kind::Rules (incl. ReactionWindowOpened/Taken/Declined, MoveInterrupted)
    CombatantId actor = 0;        // TileMoved: mover
    Vec2i tile{};                 // TileMoved: new tile
};

struct SubmitResult {
    bool ok = false;
    std::string error;
    std::vector<rules5e::ReactionRecord> reactions;   // Ask decisions made during this command
    // Async API (next Rules PR): set while a window waits for a decision. Always empty today.
    std::optional<NeedsDecision> needs;
};

class Combat {
public:
    // Spawns every chunk spawn whose archetype is in `content` as an ECS entity (StableId =
    // spawn stable_id = CombatantId). Teams v0: characters 0, monsters 1. Rolls initiative.
    static std::optional<Combat> create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                        uint64_t seed, std::string* error = nullptr);

    SubmitResult submit(const Command& cmd);
    // Replays one log entry: Ask polls are answered from the recorded decisions (desync = error).
    SubmitResult submit_logged(const LoggedCommand& entry);

    void set_reaction_decider(ReactionDeciderFn f) { decider_ = std::move(f); }
    // Async API placeholder: always empty until rules5e exposes pending windows.
    const std::optional<NeedsDecision>& pending() const noexcept { return pending_; }
    bool ended() const noexcept { return state_.ended; }
    const rules5e::EncounterState& state() const noexcept { return state_; }
    const std::vector<SimEvent>& events() const noexcept { return events_; }
    const std::vector<LoggedCommand>& log() const noexcept { return log_; }
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

private:
    struct Slot { CombatantId id; ecs::Entity entity; const Archetype* archetype; };
    Combat(ecs::Registry& reg, const world::Chunk& chunk) : reg_(&reg), grid_(chunk) {}
    void apply_rules_events(const std::vector<rules5e::Event>& evs);
    void sync_ecs();
    void install_hooks();
    SubmitResult run(const Command& cmd, const std::vector<rules5e::ReactionRecord>* replay);
    SubmitResult rules_step(const rules5e::ActionRequest& req, const std::vector<rules5e::ReactionRecord>* replay);
    SubmitResult do_rules(rules5e::ActionRequest req, const std::vector<rules5e::ReactionRecord>* replay);
    SubmitResult do_step(Vec2i to, const std::vector<rules5e::ReactionRecord>* replay);
    bool leaves_reach(CombatantId mover, Vec2i from, Vec2i to) const;
    int melee_reach_tiles(CombatantId id) const;   // 0 = no melee attack
    const Slot* slot(CombatantId id) const;

    ecs::Registry* reg_;
    Grid grid_;
    uint64_t seed_ = 0;
    rules5e::EncounterState state_{};
    std::vector<Slot> slots_;              // in spawn order
    std::vector<SimEvent> events_;
    std::vector<LoggedCommand> log_;
    std::optional<NeedsDecision> pending_;   // async placeholder
    ReactionDeciderFn decider_;
    std::optional<Vec2i> step_to_;           // destination of the Step being resolved (in_range)
};

// Replays a command log from scratch (same chunk, content and seed) into `reg`.
// Returns the rebuilt combat, or nullopt + error if any command is rejected (desync).
std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                             uint64_t seed, const std::vector<LoggedCommand>& log, std::string* error = nullptr);

} // namespace eng::sim
