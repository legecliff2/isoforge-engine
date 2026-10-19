#pragma once
// Combat state machine v0: chunk spawns -> ECS entities + rules5e encounter; grid movement;
// opportunity attacks through rules5e LeavingReach reaction windows; the rules5e action log
// (plus each Move's destination tile) alone replays the fight.
//
// Reaction windows are owned by rules5e (#11/#15): sim flags `leaves_reach` on each Move and
// fills `hooks.in_range` from grid positions. Async: submit() may return NeedsDecision (the
// pending window lives in EncounterState; read it with pending()); answer with decide(), which
// sends rules5e::decision_request. settle() is the sync adapter for AI/tests.
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "base/math.hpp"
#include "ecs/registry.hpp"
#include "rules5e/encounter.hpp"
#include "sim/components.hpp"
#include "sim/content.hpp"
#include "sim/events.hpp"
#include "sim/grid.hpp"
#include "world/chunk.hpp"

namespace eng::sim {

using rules5e::CombatantId;

// ---- Reaction windows: rules5e types ------------------------------------------------------
using rules5e::NeedsDecision;
using rules5e::PollOrderFn;
using rules5e::ReactionDecision;
using rules5e::ReactionOption;
using rules5e::ReactionTrigger;
using rules5e::StepStatus;
using rules5e::default_poll_order;
class Combat;
struct CombatTestAccess;   // tests only (sim_tests): lets a test register state rows; not part of the contract
// Sync decision adapter (AI, tests). Empty = decline.
using ReactionDeciderFn = std::function<ReactionDecision(const Combat&, const NeedsDecision&)>;

// ---- Commands ---------------------------------------------------------------------------------
enum class CommandKind : uint8_t {
    Rules,    // request: any rules5e ActionRequest for the current actor. sim fills the facts rules
              // needs (target_within_5ft, ranged disadvantage facts, Ready response details);
              // Ready with ready_kind Move uses `to` as the destination.
    Step,     // to: move the current actor one tile (8-neighbour) -> rules5e Move{leaves_reach}
};
struct Command {
    CommandKind kind = CommandKind::Rules;
    rules5e::ActionRequest request{};
    Vec2i to{};
    // CastSpell of an area spell: `to` is the aim tile and sim derives the targets (the creatures inside the template)
    // when `request.targets` is empty. Opt-in: without it an area spell keeps the caller-supplied target list.
    bool aim = false;
};
// Replay log entry: rules5e's LoggedStep (request + Ask decisions) plus, for Moves, the tile.
struct LogEntry {
    rules5e::LoggedStep step;
    std::optional<Vec2i> to;
};

struct SubmitResult {
    bool ok = false;
    std::string error;
    StepStatus status = StepStatus::Rejected;
    std::optional<NeedsDecision> needs;               // status == NeedsDecision
    std::vector<rules5e::ReactionRecord> reactions;   // Done: Ask decisions of the completed request
};

// Ready triggers (sim interprets rules5e's opaque trigger_id): kind << 8 | tiles.
//   TargetWithin:  the response's target ends a move within `tiles` of the readier.
//   HostileWithin: any hostile ends a move within `tiles` of the readier.
// A readied Attack also needs, when it fires: target alive, within range, line of sight (ranged).
// A readied Move goes to the Command's `to` tile (pathed at Ready time) and fires only if that
// tile is free; leaving reach on the way opens LeavingReach windows like any move.
enum class ReadyTriggerKind : uint8_t { TargetWithin = 1, HostileWithin = 2 };
constexpr uint32_t ready_trigger_id(ReadyTriggerKind k, uint8_t tiles) { return (uint32_t(k) << 8) | tiles; }

// CastSpell (v5): submit a Command{Rules, request{actor, ActionKind::CastSpell, target, ...,
// spell_id = rules5e::spell_id_of(<spell@1 content id>), spell_level = slot level (0 = cantrip),
// targets = affected creatures when not just `target`}}. The spell must be in the loaded SpellBook
// (ContentLibrary::spells()) and known by the caster (spellcasting.spells). Sim supplies the facts
// rules5e asks for: range / line of sight (spell_target_ok: self = caster only, touch = adjacent,
// else Chebyshev range in tiles with line of sight), targets_within, can_see (line of sight, not
// Blinded), can_provide_components (V/S always; a spell with a material cost is refused: no
// inventory yet), and reaction reach (Shield: the creature hit; Counterspell: a reactor within the
// counter spell's range of the caster). Area templates are NOT computed: the caller lists the
// creatures inside the area in `targets`. Slots are spent by rules5e.

// Team ids: the distinct team names of the combatant spawns (overrides.team, else "party" for
// characters / "monsters" for monsters), byte-sorted; id = index. Spawn-order independent.
std::optional<std::string> spawn_team_name(const world::EntitySpawn& sp, const Archetype& a, std::string* error = nullptr);

class Combat {
public:
    // Spawns every chunk spawn whose archetype is in `content` as an ECS entity (StableId =
    // spawn stable_id = CombatantId). Teams: see spawn_team_name. Rolls initiative.
    static std::optional<Combat> create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                        uint64_t seed, std::string* error = nullptr);
    // Combat spanning several chunks of one level (e.g. ChunkStreamer::resident(level)). Positions
    // are world tiles (Grid::to_world); spawns are taken in chunk-coord order, then file order,
    // so the argument order does not matter. The single-chunk overload uses the chunk-local frame
    // (as if the chunk were at (0,0)); this one uses absolute world tiles.
    static std::optional<Combat> create(ecs::Registry& reg, std::span<const world::Chunk* const> chunks,
                                        const ContentLibrary& content, uint64_t seed, std::string* error = nullptr);

    // Rejected while a decision is pending.
    SubmitResult submit(const Command& cmd);
    // Answers the pending window (rules5e::decision_request). May return the next NeedsDecision.
    SubmitResult decide(const ReactionDecision& d);
    // Answers windows with `decider` until nothing is pending. Returns the final result.
    SubmitResult settle(const ReactionDeciderFn& decider);
    std::optional<NeedsDecision> pending() const { return rules5e::pending_decision(state_); }
    void set_poll_order(PollOrderFn f) { poll_order_ = std::move(f); }

    bool ended() const noexcept { return state_.ended; }
    const rules5e::EncounterState& state() const noexcept { return state_; }
    const std::vector<SimEvent>& events() const noexcept { return events_; }
    const std::vector<LogEntry>& log() const noexcept { return log_; }
    uint64_t seed() const noexcept { return seed_; }
    const Grid& grid() const noexcept { return grid_; }
    const std::vector<std::string>& team_names() const noexcept { return team_names_; }
    ecs::Registry& registry() const noexcept { return *reg_; }

    Vec2i pos(CombatantId id) const;
    ecs::Entity entity(CombatantId id) const;
    const Archetype* archetype(CombatantId id) const;
    // Footprint (Large 2x2, Huge 3x3, Gargantuan 4x4; monster@3 / character@3 only, see Archetype::footprint).
    // Convention: pos(id) is the ANCHOR, the footprint's min-x / min-y tile, and the footprint is the square
    // [pos.x, pos.x + n) x [pos.y, pos.y + n). Spawn tiles and Step destinations are anchors. n == 1 for
    // every pre-footprint creature, which then behaves exactly as before.
    int footprint_size(CombatantId id) const;
    std::vector<Vec2i> footprint_tiles(CombatantId id) const { return footprint_tiles_at(id, pos(id)); }
    std::vector<Vec2i> footprint_tiles_at(CombatantId id, Vec2i anchor) const;   // row-major (y, then x)
    // Nearest-tile distance in tiles between two creatures' footprints (chebyshev for 1x1), and line of
    // sight between them: clear from any tile of one to any tile of the other (SRD: a creature may trace
    // a line from any corner of its space). Both are what reach, range and spell targeting use.
    int distance_tiles(CombatantId a, CombatantId b) const;
    bool line_of_sight(CombatantId a, CombatantId b) const;
    std::vector<Vec2i> occupied_except(CombatantId id) const;   // tiles (every footprint tile) of other non-dead combatants
    bool hostile(CombatantId a, CombatantId b) const;
    // A declared multiattack option by id (the `multiattack` effect's option id): the value for
    // ActionRequest::multiattack (1 + option index), or nullopt if the creature has no such option. To use it,
    // submit Command{Rules, {actor, Attack, target, attack_index = first attack of the option, multiattack = value}};
    // follow-up attacks carry the sequence's next attack_index (multiattack may stay 0). Sim copies the
    // request through unchanged; rules5e checks the order and spends the action once.
    std::optional<uint8_t> multiattack_option(CombatantId id, std::string_view option_id) const;
    // Active features (UseFeature): remaining uses (255 = unlimited), and whether usable now.
    // Would stepping `mover` from `from` to `to` leave some hostile's melee reach?
    bool would_leave_reach(CombatantId mover, Vec2i from, Vec2i to) const { return leaves_reach(mover, from, to); }
    // Ranged attack legality from the current positions: in long range and line of sight.
    bool can_shoot(CombatantId actor, uint8_t attack_index, CombatantId target) const;
    // Would that ranged attack roll with disadvantage (long range or a hostile within 5 ft)?
    bool ranged_disadvantage(CombatantId actor, uint8_t attack_index, CombatantId target) const;
    // ActionRequest::ally_near_target for an attack on `target` from `attacker` (Pack Tactics, Sneak Attack):
    // a same-team creature other than the attacker, conscious and not incapacitated, within 5 ft of `target`.
    bool ally_near_target(CombatantId attacker, CombatantId target) const;
    std::optional<Vec2i> readied_destination(CombatantId id) const {
        auto it = readied_dest_.find(id);
        return it == readied_dest_.end() ? std::nullopt : std::optional<Vec2i>(it->second);
    }
    // R-G16 surprise (VERSION_BUMP_BATCH 7.2h hand-off (b)). Marks `ids` surprised (rules5e::apply_surprise) for round 1:
    // each takes only EndTurn on its first turn and cannot react until that turn ends. Setup only: refused (false, *error)
    // once any command was logged or a decision is pending, or after the encounter ended; unknown or dead ids are refused
    // too, and nothing changes on a refusal. May be called more than once before the first command (the sets add up).
    // rules5e reads `surprised` only from turn processing, never in begin_encounter, so marking right after create() is the
    // same as marking before begin_encounter. Replays must pass the same set (surprised_at_start()) to replay().
    bool apply_surprise(const std::vector<CombatantId>& ids, std::string* error = nullptr);
    // The creatures surprised by hiders' Dexterity (Stealth) totals (rules5e::surprised_by_stealth over the current
    // combatants; ties go to the hider). Pure; pass the result to apply_surprise.
    std::vector<CombatantId> surprised_by_stealth(const std::vector<rules5e::StealthTotal>& hiders) const {
        return rules5e::surprised_by_stealth(state_.combatants, hiders);
    }
    // The set given to apply_surprise (combatant order), kept after the flags clear: replay input, like the seed.
    const std::vector<CombatantId>& surprised_at_start() const noexcept { return surprised_at_start_; }
    uint8_t feature_uses_left(CombatantId id, uint32_t feature_id) const;
    bool can_use_feature(CombatantId id, uint32_t feature_id) const;

    // Canonical hash (StableHash) of the event stream + final positions. Golden material.
    uint64_t hash() const;
    // Canonical hash of the command log alone.
    uint64_t log_hash() const;

    // Area templates (VERSION_BUMP_BATCH row 7). The creatures (other than the caster) a spell's template catches when
    // aimed at `aim`, in combatant order; empty with *error set when the aim is invalid. Geometry is over footprint tiles
    // (see docs/SIM_EVENTS.md "Area templates"): range 0 is a self origin (cone / line / cube start at the caster and
    // point toward `aim`; sphere / cylinder are centred on the caster), range > 0 a point origin at `aim` (must be within
    // range and in line of sight of the caster). Creatures need line of sight from the origin to a tile of their footprint.
    std::vector<CombatantId> area_targets(CombatantId caster, const rules5e::SpellDef& def, Vec2i aim,
                                          std::string* error = nullptr) const;
    const rules5e::SpellDef* spell_def(uint32_t id) const;

private:
    bool derived_area_ = false;   // targets of the request being resolved came from area_targets (skips the per-target range check)
    struct Slot { CombatantId id; ecs::Entity entity; const Archetype* archetype; };
    Combat(ecs::Registry& reg, Grid grid) : reg_(&reg), grid_(std::move(grid)) {}
    void apply_rules_events(const std::vector<rules5e::Event>& evs);
    void sync_ecs();
    void install_hooks();
    SubmitResult finish(rules5e::StepResult sr);
    SubmitResult do_rules(rules5e::ActionRequest req, Vec2i to, bool aim = false);
    SubmitResult do_step(Vec2i to);
    void complete(const rules5e::LoggedStep& ls, const std::vector<rules5e::Event>& evs, std::optional<Vec2i> to);
    std::optional<Vec2i> moving_to(CombatantId id) const;   // destination of a move being resolved
    // A footprint at a position: anchor + side.
    struct Spot { Vec2i p; int n; };
    Spot spot(CombatantId id) const { return {pos(id), footprint_size(id)}; }
    Spot spot_at(CombatantId id, Vec2i anchor) const { return {anchor, footprint_size(id)}; }
    bool los_spots(Spot a, Spot b) const;
    // Pack Tactics / Sneak Attack fact (ActionRequest::ally_near_target): a creature on the attacker's team,
    // other than the attacker, conscious and not incapacitated, within 5 ft (nearest tile) of `target`.
    bool ally_near(CombatantId attacker, Spot target) const;
    bool ranged_attack_facts(CombatantId actor, uint8_t attack_index, Spot from, Spot target,
                             rules5e::ActionRequest& req, std::string* error) const;
    bool move_tile(CombatantId mover, Vec2i to, const std::vector<rules5e::Event>& evs);
    friend struct CombatTestAccess;
    friend std::optional<Combat> replay(ecs::Registry&, std::span<const world::Chunk* const>, const ContentLibrary&,
                                        uint64_t, const std::vector<LogEntry>&, std::string*, const std::vector<CombatantId>&);
    bool leaves_reach(CombatantId mover, Vec2i from, Vec2i to) const;
    int melee_reach_tiles(CombatantId id) const;   // 0 = no melee attack
    const Slot* slot(CombatantId id) const;
    bool spell_in_reach(const rules5e::SpellDef& def, CombatantId caster, CombatantId target) const;

    ecs::Registry* reg_;
    Grid grid_;
    uint64_t seed_ = 0;
    rules5e::EncounterState state_{};
    std::vector<Slot> slots_;              // in spawn order
    std::vector<SimEvent> events_;
    std::vector<LogEntry> log_;
    std::vector<std::string> team_names_;
    PollOrderFn poll_order_;
    std::shared_ptr<const rules5e::SpellBook> spells_;   // content spell@1 defs (null = no spells)
    std::map<CombatantId, Vec2i> readied_dest_;   // readied Move destinations (until fired/expired)
    std::optional<Vec2i> ready_to_;               // destination of the Ready being resolved
    std::optional<Vec2i> aim_to_;                 // aim tile of the area CastSpell being resolved
    std::optional<Vec2i> step_to_;   // destination of the Move being resolved / paused (sim-only)
    std::vector<CombatantId> surprised_at_start_;   // R-G16: apply_surprise input (combatant order)
};

// Replays a command log from scratch (same chunk, content and seed) into `reg`.
// Returns the rebuilt combat, or nullopt + error if any command is rejected (desync).
// `surprised` (R-G16): the original combat's surprised_at_start(), applied before the first entry (empty = no surprise).
std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                             uint64_t seed, const std::vector<LogEntry>& log, std::string* error = nullptr,
                             const std::vector<CombatantId>& surprised = {});
std::optional<Combat> replay(ecs::Registry& reg, std::span<const world::Chunk* const> chunks, const ContentLibrary& content,
                             uint64_t seed, const std::vector<LogEntry>& log, std::string* error = nullptr,
                             const std::vector<CombatantId>& surprised = {});

} // namespace eng::sim
