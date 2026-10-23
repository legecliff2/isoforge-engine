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
class LightDriver;   // sim/light_driver.hpp

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
    // Step: request.squeeze == 1 opts in to SRD Squeezing (a Large+ fits a space one size smaller; feat/size-space).
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
// Blinded), can_provide_components (V/S and unpriced M always; a priced material, cost_cp > 0,
// needs the first combat-local equipment entry whose id equals components.m.item exactly and whose
// value_gp * 100 >= cost_cp; no item key = refused; a `consumed` material removes that one entry when
// the cast completes, CONTENT_CONVENTIONS §7, hashed under 0xAD once something was consumed), and reaction reach (Shield: the creature hit; Counterspell: a reactor within the
// counter spell's range of the caster). Area templates are NOT computed: the caller lists the
// creatures inside the area in `targets`. Slots are spent by rules5e.

// Team ids: the distinct team names of the combatant spawns (overrides.team, else "party" for
// characters / "monsters" for monsters), byte-sorted; id = index. Spawn-order independent.
std::optional<std::string> spawn_team_name(const world::EntitySpawn& sp, const Archetype& a, std::string* error = nullptr);

// docs/proposals/EQUIPMENT.md 7.2 / 9 (Engine Core): per-combat options. Default = every pre-equipment behaviour and hash.
//  equipment: build loadouts from character@4 equipment[] entries naming a loaded item@1 (setup order: combatant, then entry),
//    install the rules hooks loadout_of / item_def / apply_equip, recompute derived stats, keep ground items, and list PickUp /
//    HandOver in legal_actions(). Off: none of it (no loadout, no hook, no new fold).
struct CombatOptions {
    bool equipment = false;
};

// An equipped light item as a light producer (EQUIPMENT.md 6, LIGHTING 10.1 / #233): the light id is allocated once at setup
// (item instance order) and never changes; holder_id is the creature holding or carrying it, 0 on the ground (tile = where it
// lies; carrier stays Item). lit: kHeldLit while held or on the ground (stowed lights are covered).
struct EquipmentLight {
    uint32_t light_id = 0;
    uint32_t item = 0;          // carrier_id (Carrier::Item)
    uint32_t def = 0;
    CombatantId holder_id = 0;
    Vec2i tile{};
    bool lit = false;
    // Light state (EQUIPMENT.md 6): rules5e::LightDef::states index (0 = top-level radii; a hooded lantern: 1 = hooded), from
    // HeldItem::flags bits 4-6 (rules5e::light_state_index). bright_ft / dim_ft: rules5e::light_radii (0 / 0 while unlit).
    uint8_t state = 0;
    uint16_t bright_ft = 0, dim_ft = 0;
    rules5e::LightShape shape = rules5e::LightShape::Radius;
};

// Ground item ECS entities (EQUIPMENT.md 4.4): StableId kGroundItemIdBase + instance, with TilePos and this component.
inline constexpr uint8_t kHeldRelight = 0x80;   // sim-only HeldItem flag: a lit light stowed (covered), relit when drawn
inline constexpr uint64_t kGroundItemIdBase = uint64_t(1) << 40;
struct GroundItemLink { uint32_t instance = 0, def = 0; };

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
    // As above with options (CombatOptions{} is identical to the overloads without them).
    static std::optional<Combat> create(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content,
                                        uint64_t seed, CombatOptions options, std::string* error = nullptr);
    static std::optional<Combat> create(ecs::Registry& reg, std::span<const world::Chunk* const> chunks,
                                        const ContentLibrary& content, uint64_t seed, CombatOptions options,
                                        std::string* error = nullptr);
    const CombatOptions& options() const noexcept { return options_; }

    // ---- Equipment (CombatOptions::equipment; EQUIPMENT.md 9) ----
    // rules5e::legal_actions plus, with equipment on, the Engine Core entries appended after them: PickUp (ground items within
    // 5 ft of the actor, instance order, hand 1 then 2), HandOver (held instance order, recipient combatant order, to
    // creatures with a loadout within 5 ft), then SetLightState (held lights in instance order: each state index, then douse).
    // Each is filtered through rules5e::equip_legal.
    std::vector<rules5e::ActionRequest> legal_actions();   // re-binds the hooks first (like submit)
    std::vector<rules5e::ActionRequest> legal_actions() const;   // the same list for AI queries (re-binds like ward_alternates)
    const rules5e::Loadout* loadout(CombatantId id) const;   // nullptr: no loadout (always with equipment off)
    const ItemDefTable* item_defs() const noexcept { return items_.get(); }
    std::vector<EquipmentLight> equipment_lights() const;    // light id order
    uint32_t recomputes() const noexcept { return recomputes_; }   // recompute_derived calls (diagnostics, never hashed)
    // Setup diagnostics (never hashed): rules5e::hands_overfull_error lines for creatures whose equipped holdables need more
    // hands than they have (the extra items start carried).
    const std::vector<std::string>& equipment_errors() const noexcept { return equipment_errors_; }

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
    // Priced materials: the combatant's combat-local equipment (copied from content at create; consumed entries removed).
    const std::vector<EquipmentEntry>& equipment(CombatantId id) const;
    struct MaterialUse { CombatantId caster; uint32_t spell_id; uint32_t entry; uint32_t value_gp; };
    const std::vector<MaterialUse>& materials_consumed() const noexcept { return materials_used_; }

    Vec2i pos(CombatantId id) const;
    ecs::Entity entity(CombatantId id) const;
    const Archetype* archetype(CombatantId id) const;
    // Footprint (Large 2x2, Huge 3x3, Gargantuan 4x4; monster@3 / character@3 only, see Archetype::footprint).
    // Convention: pos(id) is the ANCHOR, the footprint's min-x / min-y tile, and the footprint is the square
    // [pos.x, pos.x + n) x [pos.y, pos.y + n). Spawn tiles and Step destinations are anchors. n == 1 for
    // every pre-footprint creature, which then behaves exactly as before.
    int footprint_size(CombatantId id) const;
    // feat/size-space: the size's side ignoring squeezing; may `mover` pass through `other`'s space; who covers a tile.
    int natural_footprint(CombatantId id) const;
    bool may_pass(CombatantId mover, CombatantId other) const;
    CombatantId occupant_at(Vec2i t, CombatantId except) const;
    bool overlaps_other(CombatantId id, Vec2i anchor, int n) const;
    struct StepPlan { int n = 1; int cost = 5; uint8_t squeeze = 0; bool ends_in_other = false; };
    std::optional<std::string> plan_step(CombatantId mover, Vec2i to, StepPlan& plan, bool allow_squeeze = false) const;
    MoveCaps caps_of(CombatantId id) const;   // feat/move-modes: fly / swim / climb speeds present
    // feat/move-modes: the speed a step from `from` to `to` costing `cost` ft uses (0 walk, 1 fly, 2 swim, 3 climb);
    // 0xFF: no fitting speed has enough left; 0xFE: only flight fits and the mover is prone.
    uint8_t step_mode(CombatantId mover, Vec2i from, Vec2i to, int cost, int fp = 0) const;   // fp 0: the current footprint
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
    // Aggressive (rules #111, VERSION_BUMP_BATCH 7.2j b): after the current actor's bonus-action Dash with
    // ActionRequest::aggressive, the movement it had before that Dash is spent first; a Step that needs the Dash's movement
    // must end nearer (footprint distance) to a hostile, not dead, that it can see from where it stands (not Blinded, line of
    // sight). False only for such a step (submit refuses it); true for every other step.
    bool aggressive_step_ok(CombatantId mover, Vec2i to) const;
    bool aggressive_step_ok(CombatantId mover, Vec2i to, int cost, int footprint) const;   // a step already priced by plan_step
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
    // Petrifying Gaze (VERSION_BUMP_BATCH 7.2p): the gazers `viewer` can see now, in combatant order: every other living
    // creature with a gaze that is within that gaze's range_ft (footprint distance), in line of sight, not Invisible, while
    // the viewer is not Blinded. One visibility fact covers both directions (gazer sees viewer, viewer sees gazer) until
    // R-G13. While TurnBudget::gaze_pending the turn accepts only Command{Rules, {actor, ActionKind::GazeResponse}} with
    // avert_gaze as the caller's choice; submit fills ActionRequest::gaze_seen from this (a caller's list is replaced).
    // A UI calls it to show which gazers threaten the actor before asking whether to avert.
    std::vector<CombatantId> gaze_seen(CombatantId viewer) const;
    // R-G12 summons (7.2r, SUMMONS_TRANSFORMS.md §3.4): the key of the current turn, (round, current actor, TurnStarted
    // count). Unlike (round, turn_index) it does not change when a summon joins the order before the current actor.
    struct TurnKey {
        uint16_t round = 0; CombatantId actor = 0; uint32_t starts = 0;
        friend bool operator==(const TurnKey&, const TurnKey&) = default;
    };
    TurnKey turn_key() const { return {state_.round, rules5e::current_actor(state_), turns_started_}; }
    // R-G12 summons: is `id` a summoned creature (live or gone), and has it ended (SummonEnded; dead, its token removed)?
    bool summoned(CombatantId id) const { const Slot* s = slot(id); return s && s->summoned; }
    bool gone(CombatantId id) const { const Slot* s = slot(id); return s && s->gone; }
    // The anchors a summon cast's creatures would be placed on now (§3.1 deterministic choice), one per archetype index, or
    // empty with *error ("no unoccupied space", ...). Submit fills ActionRequest::summon_tiles from this when it is empty.
    std::vector<Vec2i> summon_tiles_for(CombatantId caster, const rules5e::SpellDef& def, const std::vector<uint8_t>& archetypes,
                                        std::string* error = nullptr) const;
    // AI queries of rules helpers that read positions through EncounterHooks (targets_within, spell_target_ok): the hooks are
    // re-bound to this Combat first (as submit does), so the answer matches what rules will check on submit.
    std::vector<CombatantId> ward_alternates(CombatantId actor, CombatantId warded, uint32_t spell_id, uint8_t attack_index) const;
    rules5e::HealSplitCheck heal_split_check(CombatantId user, const rules5e::FeatureSpec& spec,
                                             const std::vector<rules5e::HealShare>& split) const;
    uint8_t feature_uses_left(CombatantId id, uint32_t feature_id) const;
    bool can_use_feature(CombatantId id, uint32_t feature_id) const;

    // R-G13 phase 2 (LIGHTING.md §7, §10): the chunks of this combat in its own tile frame (ascending), and the offset to
    // world chunk coords (world = frame + offset; zero except for the single-chunk overload, which keeps the chunk-local frame).
    const std::vector<world::ChunkCoord>& frame_chunks() const noexcept { return frame_chunks_; }
    world::ChunkCoord frame_offset() const noexcept { return frame_offset_; }
    // Attach a light driver (not owned; null detaches). It publishes a frame now (the create-time frame) and after every
    // committed step. While its last step left a wanted halo Pending or Failed, submit() re-evaluates once and refuses
    // ("light halo pending" / "light halo failed") until the halo completes. Copies of a Combat never carry the driver.
    void attach_light_driver(LightDriver* d);
    LightDriver* light_driver() const noexcept { return light_.d; }

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
    // R-G9 teleport (FORCED_MOVEMENT.md §3, Q8): the anchors `actor` may teleport to with `def` (a Teleport spell), row-major
    // (y, then x): footprint walkable and free, not its own tile, at most max_feet away (anchor-to-anchor Chebyshev x 5 ft), in
    // line of sight when needs_sight, and not closer to a creature it is frightened of. Callers pick one and set
    // ActionRequest::has_teleport_dest / teleport_x / teleport_y; submit re-validates it and fills teleport_feet.
    std::vector<Vec2i> legal_teleport_tiles(CombatantId actor, const rules5e::SpellDef& def) const;
    bool teleport_dest_ok(CombatantId actor, const rules5e::SpellDef& def, Vec2i dest) const;
    struct Spot { Vec2i p; int n; };   // a creature's footprint: anchor tile and side
    // ---- Cover (VERSION_BUMP_BATCH 7.2t hand-off; EncounterHooks::cover; #187 + feat/cover-from-grid) ----
    // Pure, integer only. DMG grid rule: lines from one corner of the origin footprint to every corner of the target's
    // footprint; the origin corner that gives the LEAST cover is used. A line is blocked by a tile whose interior it crosses
    // (grazing an edge or a corner does not): a blocks_sight tile is a full obstruction, another living creature's footprint
    // (not except_a / except_b) half. Degree: no line blocked = None; every line blocked by full obstructions = Total; at
    // least 3/4 of the lines blocked by full obstructions = ThreeQuarters; any line blocked (wall or creature) = Half.
    // Read-only query for R&P debug overlays and tile probes (no render coupling). origin_n / target_n are footprint sides
    // (1 = Medium, 2 = Large). except_a / except_b are combatant ids whose footprints do not count as half-cover bodies.
    rules5e::Cover cover_between(Vec2i origin, int origin_n, Vec2i target, int target_n, CombatantId except_a = 0,
                                 CombatantId except_b = 0) const;
    // CoverQuery form used by EncounterHooks::cover: has_origin = that single tile; otherwise the source's footprint.
    // Returns None when the target (or source, when has_origin is false) is missing, or source == target.
    rules5e::Cover cover_of(const rules5e::CoverQuery& q) const;
    // ---- Vision (VERSION_BUMP_BATCH 7.2x hand-off; EncounterHooks::sight_facts) ----
    // Per-pair facts for viewer -> subject. Pure and integer-only: LightDriver::latest() samples (or Bright / clear
    // without a driver), LOS, zone obscurement on the line (`between`), distance, Invisible, grounded, audible.
    // Rules derive Sight via derive_sight; answers are not hashed. Empty hook = Sight{} (everyone sees everyone).
    rules5e::SightFacts sight_facts_of(CombatantId viewer, CombatantId subject) const;
    // ---- R-G11 zones (VERSION_BUMP_BATCH 7.2l hand-off, ZONES_AURAS.md §3) ----
    // Geometry: a point zone is centred on its anchor tile; an aura (anchor Creature) on its owner's footprint. Sphere /
    // cylinder: Chebyshev radius size_ft (tiles); cube: side size_ft centred on the anchor (an aura: half the side around the
    // footprint). Walls (shape Line / rings) are not modelled yet: they cover no tile. `owner_at` overrides the owner's
    // footprint and `anchor_at` the anchor tile (a move being evaluated).
    bool zone_covers(const rules5e::ActiveZone& z, Vec2i tile, const Spot* owner_at = nullptr, const Vec2i* anchor_at = nullptr) const;
    // Is creature `id` standing at `at` in zone `z` (any footprint tile, or every tile when `completely`)?
    bool zone_holds(const rules5e::ActiveZone& z, CombatantId id, Spot at, bool completely, const Spot* owner_at = nullptr,
                    const Vec2i* anchor_at = nullptr) const;
    // Anchor tiles a point zone may be cast at (in the spell's range, in sight, in bounds) or moved to (zone_move: within
    // ZoneSpec::move_ft of its anchor), row-major (y, then x). Empty for auras and non-zone spells.
    std::vector<Vec2i> legal_zone_tiles(CombatantId actor, const rules5e::SpellDef& def, uint32_t zone_move = 0) const;
    // Cost in feet of one step of `mover` from `from` to `to`: the grid's cost, at least 10 when it enters a kZoneDifficult zone
    // and doubled in a kZoneHalveSpeed zone (the mover not excluded), at most 20 ft per tile.
    int step_cost_ft(CombatantId mover, Vec2i from, Vec2i to) const;
    // through_creature: the step enters another creature's space (difficult terrain, SRD Moving Around Other Creatures).
    int step_cost_ft(CombatantId mover, Vec2i from, Vec2i to, int footprint, bool through_creature = false) const;
    // ---- R-G12 transforms (VERSION_BUMP_BATCH 7.2q hand-off, SUMMONS_TRANSFORMS.md §3) ----
    // The archetype form choice `index` (ActionRequest::form_archetype - 1) of transform `kind` from `source` resolves to:
    // Wild Shape: the feature's forms[index]; Polymorph (source = spell id): the index-th beast of the library (byte order of
    // ids, beasts with a challenge rating). nullptr for an unknown id / index or a shapechanger (its forms are its own block).
    const Archetype* form_archetype(CombatantId creature, rules5e::FormKind kind, uint32_t source, uint8_t index) const;
    // EncounterHooks::build_form, also used by the AI: the form's statistics, or nullopt when the form is not allowed (unknown
    // archetype, not a beast, above the CR limit, fly / swim below the druid level, or a bigger footprint that does not fit
    // at the creature's anchor). A function of content, the arguments and the board (positions are replayed identically).
    std::optional<rules5e::FormStats> build_form(const rules5e::Combatant& creature, rules5e::FormKind kind, uint32_t source,
                                                 uint8_t index, std::string* why = nullptr) const;
    // The archetype whose attack-indexed tables (ranges, grapple riders, two-handed grip) match the creature's current
    // attacks: the form's archetype in a Polymorph / Wild Shape form, else its own.
    const Archetype* attack_archetype(CombatantId id) const;
    // Polymorph's choice list (beasts with a rating, id byte order), built at create.
    const std::vector<const Archetype*>& polymorph_forms() const noexcept { return beasts_; }

private:
    bool derived_area_ = false;   // targets of the request being resolved came from area_targets (skips the per-target range check)
    // summoned / gone (R-G12): a summon slot (appended after the create-time slots) and whether it ended; `last` = its anchor
    // when it ended (its entity is destroyed at the end of that step).
    struct Slot { CombatantId id; ecs::Entity entity; const Archetype* archetype; bool summoned = false; bool gone = false; Vec2i last{}; };
    Combat(ecs::Registry& reg, Grid grid, CombatOptions options = {}) : reg_(&reg), grid_(std::move(grid)) { options_ = options; }
    // Equipment (EQUIPMENT.md 9): options, the item table, light ids per light item, recompute inputs per loadout (sim-only,
    // derived from the replayed state, not hashed).
    CombatOptions options_{};
    std::shared_ptr<const ItemDefTable> items_;
    std::vector<std::pair<uint32_t, uint32_t>> item_lights_;   // (item instance, light id), instance order
    std::map<CombatantId, DeriveInputs> derive_seen_;
    uint32_t recomputes_ = 0;
    std::vector<std::string> equipment_errors_;
    std::vector<SimEvent> item_events_;   // ItemMoved of the step being resolved (apply_equip on state_ only), pushed by complete
    void item_moved(CombatantId actor, Vec2i tile, uint32_t instance, uint8_t reason, CombatantId holder);
    void ground_drop(rules5e::EncounterState& st, const rules5e::HeldItem& h, Vec2i at) const;
    void setup_equipment(const ContentLibrary& content);
    void apply_equip(rules5e::EncounterState& st, const rules5e::ActionRequest& req, uint8_t cost_kind);
    void equipment_after_step(const rules5e::LoggedStep& ls, const std::vector<rules5e::Event>& evs);
    void refresh_interactions(rules5e::EncounterState& st) const;
    void sync_ground_entities();
    void recompute(rules5e::EncounterState& st, CombatantId id);
    bool within_5ft(CombatantId a, Vec2i tile) const;
    // Non-owning; a copy (lookahead, replay) starts detached so only the attached Combat drives the light frames.
    struct LightLink {
        LightDriver* d = nullptr;
        LightLink() = default;
        LightLink(const LightLink&) noexcept {}
        LightLink& operator=(const LightLink&) noexcept { d = nullptr; return *this; }
        LightLink(LightLink&& o) noexcept : d(o.d) { o.d = nullptr; }
        LightLink& operator=(LightLink&& o) noexcept { d = o.d; o.d = nullptr; return *this; }
    };
    LightLink light_;
    std::vector<world::ChunkCoord> frame_chunks_;
    world::ChunkCoord frame_offset_{0, 0, 0};
    void apply_rules_events(const std::vector<rules5e::Event>& evs);
    void sync_ecs();
    void install_hooks();
    // R-G9: the rules forced_move hook (path walk / teleport, committed at once) and its farthest-first order.
    rules5e::ForcedMoveResult resolve_forced(const rules5e::ForcedMoveQuery& q);
    void set_step_context(const rules5e::ActionRequest& q) {   // which step the hook runs in (shove detection)
        shove_actor_ = q.kind == rules5e::ActionKind::Attack && q.special == 3 ? q.actor : 0;
        shove_target_ = shove_actor_ ? q.target : 0;
    }
    CombatantId shove_actor_ = 0, shove_target_ = 0;
    // R-G11: membership lists of a move of `mover` from `from` to `to` (ascending zone ids; its own auras skipped), and the
    // membership changes of other creatures when `mover` owns auras (aura owner's move) or zone `moved_zone` moves to `anchor_to`.
    void zone_lists(CombatantId mover, Spot from, Spot to, rules5e::ActionRequest& q) const;
    bool zone_spec_anchor_creature(const rules5e::ActiveZone& z) const;   // an aura (moves with its owner)
    std::vector<rules5e::ZoneChange> zone_changes_for(CombatantId mover, Spot from, Spot to, uint32_t moved_zone = 0,
                                                      const Vec2i* anchor_to = nullptr) const;
    std::vector<std::pair<CombatantId, Vec2i>> forced_placed_;   // hook commits of the step being resolved, in order
    SubmitResult finish(rules5e::StepResult sr);
    SubmitResult do_rules(rules5e::ActionRequest req, Vec2i to, bool aim = false);
    SubmitResult do_step(Vec2i to, bool allow_squeeze = false);
    void complete(const rules5e::LoggedStep& ls, const std::vector<rules5e::Event>& evs, std::optional<Vec2i> to,
                  std::optional<std::size_t> teardown_begin = std::nullopt);
    // R-G12 summons: push one rules event (counting TurnStarted), spawning the summon of a Summoned event (+ TokenSpawned).
    void push_rules_event(const rules5e::Event& e, const rules5e::LoggedStep* ls);
    void spawn_summon(const rules5e::Event& summoned, const rules5e::LoggedStep& ls);
    void register_rows(const Slot& sl);
    SimEvent token_removed(const rules5e::Event& ended);
    // R-G12: footprint bookkeeping of one Transformed / Reverted event (side change: Footprint event, a growing revert that
    // does not fit is moved to the nearest free anchor first); the events go to `out`.
    void form_footprint(const rules5e::Event& e, std::vector<SimEvent>& out);
    std::optional<Vec2i> moving_to(CombatantId id) const;   // destination of a move being resolved
    // A footprint at a position: anchor + side.
    Spot spot(CombatantId id) const { return {pos(id), footprint_size(id)}; }
    Spot spot_at(CombatantId id, Vec2i anchor) const { return {anchor, footprint_size(id)}; }
    bool los_spots(Spot a, Spot b) const;
    bool summon_spot_ok(CombatantId caster, const rules5e::SpellDef& def, Spot sp, const std::vector<Spot>& placed) const;
    // Pack Tactics / Sneak Attack fact (ActionRequest::ally_near_target): a creature on the attacker's team,
    // other than the attacker, conscious and not incapacitated, within 5 ft (nearest tile) of `target`.
    bool ally_near(CombatantId attacker, Spot target) const;
    bool ranged_attack_facts(CombatantId actor, uint8_t attack_index, Spot from, Spot target,
                             rules5e::ActionRequest& req, std::string* error) const;
    bool move_tile(CombatantId mover, Vec2i to, const std::vector<rules5e::Event>& evs);
    friend struct CombatTestAccess;
    friend std::optional<Combat> replay(ecs::Registry&, std::span<const world::Chunk* const>, const ContentLibrary&,
                                        uint64_t, CombatOptions, const std::vector<LogEntry>&, std::string*,
                                        const std::vector<CombatantId>&);
    bool leaves_reach(CombatantId mover, Vec2i from, Vec2i to) const;
    int melee_reach_tiles(CombatantId id) const;   // 0 = no melee attack
    const Slot* slot(CombatantId id) const;
    bool spell_in_reach(const rules5e::SpellDef& def, CombatantId caster, CombatantId target) const;

    ecs::Registry* reg_;
    Grid grid_;
    uint64_t seed_ = 0;
    rules5e::EncounterState state_{};
    std::vector<Slot> slots_;              // in spawn order
    const ContentLibrary* content_ = nullptr;      // R-G12: form archetypes (Wild Shape / Polymorph) resolve against it; summon choices too
    std::vector<const Archetype*> beasts_;         // R-G12: Polymorph choices (see polymorph_forms)
    std::map<CombatantId, int> form_side_;         // R-G12: footprint side of each creature in a form (derived, not hashed)
    std::vector<SimEvent> events_;
    std::vector<LogEntry> log_;
    std::vector<std::string> team_names_;
    PollOrderFn poll_order_;
    std::shared_ptr<const rules5e::SpellBook> spells_;   // content spell@1 defs (null = no spells)
    std::map<CombatantId, Vec2i> readied_dest_;   // readied Move destinations (until fired/expired)
    std::optional<Vec2i> ready_to_;               // destination of the Ready being resolved
    std::optional<Vec2i> aim_to_;                 // aim tile of the area CastSpell being resolved
    std::optional<Vec2i> step_to_;   // destination of the Move being resolved / paused (sim-only)
    // Aggressive Dash of the current turn (sim-only, derived from the log, not hashed): the turn stamp (round << 16 |
    // turn_index), the dasher, and the movement left from before the Dash (spent first by its Moves).
    // R-G12: the stamp is the TurnKey (a summon inserted before the current actor shifts turn_index, not the key).
    struct AggressiveDash { TurnKey stamp{}; CombatantId actor = 0; int free_ft = 0; };
    std::optional<AggressiveDash> aggressive_;
    std::map<CombatantId, std::vector<EquipmentEntry>> equipment_;   // combat-local (priced materials)
    // R-G12 summons: the content (build_summon / summon options; outlives the combat like the archetypes), the number of
    // create-time slots (the untagged per-slot hash fold covers only those), TurnStarted events so far, and whether the
    // spawn ids reached UINT32_MAX (every summon refused).
    std::size_t created_count_ = 0;
    uint32_t turns_started_ = 0;
    bool summon_ids_exhausted_ = false;
    std::vector<rules5e::Event> pending_removed_;   // non-teardown SummonEnded of the step being completed (TokenRemoved after)
    std::vector<MaterialUse> materials_used_;   // consumed materials, in cast order (0xAD)
    int material_entry(CombatantId caster, const rules5e::SpellDef& d) const;   // -1 = no entry provides it
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
// Replays with the original combat's options (equipment on needs them: loadouts are built at create).
std::optional<Combat> replay(ecs::Registry& reg, const world::Chunk& chunk, const ContentLibrary& content, uint64_t seed,
                             CombatOptions options, const std::vector<LogEntry>& log, std::string* error = nullptr,
                             const std::vector<CombatantId>& surprised = {});
std::optional<Combat> replay(ecs::Registry& reg, std::span<const world::Chunk* const> chunks, const ContentLibrary& content,
                             uint64_t seed, CombatOptions options, const std::vector<LogEntry>& log,
                             std::string* error = nullptr, const std::vector<CombatantId>& surprised = {});

} // namespace eng::sim
