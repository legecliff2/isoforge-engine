#pragma once
// Encounter state machine (ARCHITECTURE.md §5.3). Pure; the engine drives it.
// Positions/distances/perception live in `sim`; requests carry the facts rules needs.
// Rules never decide win or loss: the encounter ends via an end-condition policy (hook) that
// reports a factual outcome (each PC's state); the game layer interprets it.
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include "rng/rng.hpp"
#include "rules5e/attack.hpp"
#include "rules5e/initiative.hpp"
#include "rules5e/saves.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

struct TurnBudget {
    bool action = true, bonus_action = true, reaction = true; int16_t movement_ft = 0;
    uint8_t attacks_remaining = 0;     // Extra Attack: further attacks from the Attack action taken this turn
};

// Answer to a reaction window (names/layout shared with sim::Combat).
struct ReactionDecision {
    uint32_t window_seq = 0;           // the window being answered (ignored when returned by decide_reaction)
    bool react = false;
    uint8_t option = 0;                // index into NeedsDecision::options
    friend bool operator==(const ReactionDecision&, const ReactionDecision&) = default;
};

struct ActionRequest {
    CombatantId actor; ActionKind kind; CombatantId target = 0;
    uint8_t attack_index = 0; int16_t move_ft = 0;
    bool as_bonus_action = false;      // Dash/Disengage/Hide via bonus_action_grants
    bool target_within_5ft = false;    // supplied by sim
    // appended:
    CombatantId ally = 0;              // Help: the creature that gains advantage.
                                       //   target != 0: next attack vs target; target == 0: next ability check
    ActionKind ready_kind = ActionKind::EndTurn;  // Ready: response (Attack or Move) using target/attack_index/move_ft
    uint32_t trigger_id = 0;           // Ready: opaque trigger, interpreted by hooks.ready_trigger (sim)
    bool leaves_reach = false;         // Move: sim says this move leaves some creature's reach (LeavingReach)
    uint32_t spell_id = 0;             // CastSpell (stub): content id of the spell
    uint8_t spell_level = 0;           // CastSpell (stub): slot level (0 = cantrip)
    ReactionDecision decision{};       // ReactionDecision: the answer (actor = reactor)
    int16_t amount = 0;                // Heal: hit points (target = creature healed)
    Skill skill = Skill::Athletics;    // AbilityCheck: skill (actor = creature checking)
    int16_t dc = 0;                    // AbilityCheck
    RollMode check_mode = RollMode::Normal;  // AbilityCheck: extra roll-mode source
};

enum class EventKind : uint8_t {
    InitiativeRolled, TurnStarted, AttackResolved, DamageDealt, ConditionAdded, CombatantDowned,
    DeathSaveRolled, Stabilized, RegainedHp, CombatantDied, Moved, Dashed, Disengaged, Dodged,
    StoodUp, TurnEnded, EncounterEnded,
    // appended:
    Healed, ConditionRemoved, MedicineCheck, Helped, Hid, Readied, ReadyTriggered, ReadyExpired,
    CheckRolled,
    ReactionWindowOpened, ReactionTaken, ReactionDeclined, MoveInterrupted, SpellCast, SpellCountered,
    SpellResolved, AcBonus };

// Flat tagged event; meaning of a/b/c per kind:
//   InitiativeRolled a=total; TurnStarted a=round; AttackResolved a=natural b=total c=hit|crit<<1;
//   DamageDealt a=dealt b=hp_after; ConditionAdded/Removed a=Condition; DeathSaveRolled a=natural
//   b=successes c=failures; Moved a=feet b=movement left; Dashed a=movement left;
//   EncounterEnded a=EndReason; Healed a=healed b=hp_after c=from_zero;
//   MedicineCheck a=natural b=total c=success; Helped target=ally a=vs; Hid a=stealth total;
//   Readied a=ready_kind b=trigger_id; ReadyTriggered a=trigger_id;
//   CheckRolled a=natural b=total c=success|helped<<1 (target = Skill);
//   ReactionWindowOpened actor=trigger actor a=ReactionTrigger b=window seq;
//   ReactionTaken/Declined actor=reactor a=ReactionTrigger b=window seq c=option index;
//   MoveInterrupted a=feet requested; SpellCast/SpellResolved/SpellCountered a=spell_id b=level;
//   AcBonus a=bonus b=new AC.
// reaction_depth: 0 for the action itself, 1+ for events produced while resolving a reaction.
struct Event { EventKind kind; CombatantId actor = 0, target = 0; int32_t a = 0, b = 0, c = 0;
               uint8_t reaction_depth = 0; };

// ---- Reaction windows ----
// When an action reaches a trigger point, rules pause it and open a window. Eligible reactors
// (reaction unused, not incapacitated, policy != Never, in range, has an option) are polled in
// initiative order starting after the current actor (current actor last). Each decision may
// change the outcome before the original action continues.
struct ReactionWindow {
    ReactionTrigger trigger;
    uint32_t    seq;                  // per-encounter counter; identifies the window in logs
    CombatantId trigger_actor;        // mover / attacker / caster / (Ready: creature whose event fired)
    CombatantId target = 0;           // BeingHit: creature hit
    int16_t     attack_total = 0;     // BeingHit
    int8_t      attack_natural = 0;   // BeingHit
    uint32_t    spell_id = 0;         // SpellCast
    uint8_t     spell_level = 0;      // SpellCast
    uint8_t     depth = 0;            // 0 = opened by an action; 1+ = opened inside a reaction (chain)
};

enum class ReactionOptionKind : uint8_t { Attack, AcBonus, Counter, FireReady };
struct ReactionOption {
    uint8_t  attack_index = 0;        // Attack: index into reactor.attacks (first, so {idx} = an attack option)
    ReactionOptionKind kind = ReactionOptionKind::Attack;
    int8_t   ac_bonus = 0;            // AcBonus: e.g. +5 for Shield
    uint32_t option_id = 0;           // content id (e.g. the Shield / Counterspell spell id)
};

// A decision made under Ask. Auto/Never are reproduced from state and are not logged.
struct ReactionRecord { uint32_t window_seq; ReactionTrigger trigger; CombatantId reactor; ReactionDecision decision; };

// ---- End condition (policy, overridable) ----
enum class PcState : uint8_t { Conscious, Dying, Stable, Dead, Unconscious /* >0 HP, e.g. sleep */ };
enum class EndReason : uint8_t { NoPcConscious, NoOpponentConscious, Custom,
                                 // appended:
                                 TeamNoConscious, InvalidSetup };
struct PcStatus { CombatantId id; PcState state; };
struct EncounterOutcome {
    EndReason reason;
    std::vector<PcStatus> pcs;           // in combatant order
    std::vector<uint8_t> teams_without_conscious = {};  // ascending; set by the team-based default
};

struct EncounterState;
using EndConditionFn = std::function<std::optional<EncounterOutcome>(const EncounterState&)>;

// A paused action. The state is left as it was *before* the action; when the decision arrives the
// action is re-run deterministically from that state with all decisions so far, so the async and
// synchronous paths are bit-identical by construction.
struct PendingDecision {
    ActionRequest request;                 // the paused request (any loggable kind)
    std::vector<ReactionRecord> decided;   // decisions already given for this request, in order
    ReactionWindow window;                 // the window waiting for an answer
    CombatantId reactor;
    std::vector<ReactionOption> options;
};

struct ReadiedAction { CombatantId actor; ActionRequest response; uint32_t trigger_id; };

using PollOrderFn = std::function<std::vector<CombatantId>(const EncounterState&,
                                                           const std::vector<CombatantId>& eligible_in_initiative_order)>;

struct EncounterHooks {
    // Checked after every state change. Empty = default_end_condition.
    EndConditionFn end_condition;
    // Spare the Dying: may `caster` cast it on `target` now (knows the cantrip, in touch range)?
    // Empty = action unavailable. The rules apply the effect (target becomes stable).
    std::function<bool(const EncounterState&, const Combatant& caster, const Combatant& target)> spare_the_dying;
    // Ready: does `e` satisfy the trigger the readier chose? Filled in by sim. Empty = never fires.
    std::function<bool(const EncounterState&, const ReadiedAction&, const Event& e)> ready_trigger;

    // ---- Reaction windows (all optional; filled by sim / content / AI / UI) ----
    // Synchronous decider (convenience adapter for AI and tests). When set, step() answers each
    // NeedsDecision itself by calling this and resubmitting, so it never returns NeedsDecision.
    // Empty = step() returns NeedsDecision and the caller answers with a ReactionDecision request.
    std::function<ReactionDecision(const EncounterState&, const ReactionWindow&, const Combatant& reactor,
                                   std::span<const ReactionOption>)> decide_reaction;
    // Range/reach/sight test for a would-be reactor. Empty = LeavingReach/SpellCast: nobody in range;
    // BeingHit: the creature hit is in range of itself.
    std::function<bool(const EncounterState&, const ReactionWindow&, const Combatant& reactor)> in_range;
    // Content-provided options (Shield-style AC bumps, Counterspell-style counters). Empty = none.
    std::function<std::vector<ReactionOption>(const EncounterState&, const ReactionWindow&, const Combatant&)> being_hit_options;
    std::function<std::vector<ReactionOption>(const EncounterState&, const ReactionWindow&, const Combatant&)> spell_cast_options;
    // Order in which eligible reactors are polled; receives them in initiative order and returns
    // the order to ask (may drop ids; unknown ids are ignored). Empty = default_poll_order.
    PollOrderFn poll_order;
    // Reactions don't chain: a window that would open while resolving a reaction (depth >= 1), and
    // Ready triggers on events produced by reactions, are skipped unless this returns true.
    std::function<bool(const EncounterState&, const ReactionWindow& nested)> allow_chain;
};

struct EncounterState {
    std::vector<Combatant>       combatants;
    std::vector<InitiativeEntry> order;
    uint16_t round = 1; uint16_t turn_index = 0;
    TurnBudget budget;
    rng::Rng rng{0, 0};                  // owned -> state fully serializable & replayable
    bool ended = false;
    std::optional<EncounterOutcome> outcome;
    std::vector<ReadiedAction> readied;  // in the order they were readied
    uint32_t next_window_seq = 0;        // reaction windows opened so far
    std::optional<PendingDecision> pending;   // set while an action waits for a reaction decision
    EncounterHooks hooks;                // behaviour, not state: never serialized/hashed
};

// Action log entry: the request plus the reaction decisions it needed. Every state-changing call
// (turn actions, Heal, AbilityCheck) is a request, so the log alone is a complete replay given
// the same seed, combatants, and hooks (decide_reaction is not needed).
struct LoggedStep { ActionRequest request; std::vector<ReactionRecord> reactions; };
using ActionLog = std::vector<LoggedStep>;

enum class StepStatus : uint8_t { Rejected, Done, NeedsDecision };

struct NeedsDecision {
    uint32_t window_seq = 0;
    ReactionTrigger trigger = ReactionTrigger::LeavingReach;
    CombatantId reactor = 0;
    CombatantId trigger_actor = 0;
    std::vector<ReactionOption> options;
    ReactionWindow window{};           // full window details (attack total, spell id, ...)
};

struct StepResult {
    bool ok; std::string_view error; std::vector<Event> events;
    std::vector<ReactionRecord> reactions = {};   // Done: all Ask decisions of this request, in order
    bool desync = false;                          // replay_step: logged decisions didn't match
    StepStatus status = StepStatus::Rejected;     // ok == (status != Rejected)
    std::optional<NeedsDecision> needs = {};      // NeedsDecision: what to ask; `events` are provisional
    std::optional<LoggedStep> logged = {};        // Done: the entry to append to the action log
};

bool is_conscious(const Combatant&);
PcState pc_state(const Combatant&);
std::vector<PcStatus> pc_statuses(const EncounterState&);

// Default policy (team-based): ends when any team has no conscious member
// (reason TeamNoConscious, listing those teams). Fewer than two teams among the combatants
// means a team has zero members: ends immediately with InvalidSetup.
std::optional<EncounterOutcome> default_end_condition(const EncounterState&);
// Built-in alternative: ends only when no PC (is_pc) is conscious.
std::optional<EncounterOutcome> end_when_no_pc_conscious(const EncounterState&);
// Built-in alternative: no PC conscious, or every non-PC unconscious/dead.
std::optional<EncounterOutcome> end_when_no_pc_or_no_opponent(const EncounterState&);

// Rolls initiative from Pcg32(seed, 0) and starts the first turn.
EncounterState begin_encounter(std::vector<Combatant>, uint64_t seed,
                               std::vector<Event>* opening_events = nullptr, EncounterHooks hooks = {});

// Validates and applies one action. Invalid requests change nothing (ok = false).
StepResult step(EncounterState&, const ActionRequest&);

// step() and, when a request completes (Done), append {request, decisions} to `log`.
// Use it for every request, including ReactionDecision answers; only completed requests are logged.
StepResult step_logged(EncounterState&, const ActionRequest&, ActionLog& log);
// step() answering Ask polls from the logged decisions instead of hooks.decide_reaction.
StepResult replay_step(EncounterState&, const LoggedStep&);

// Convenience wrappers: build a Heal / AbilityCheck request and step() it. Prefer step_logged with
// the request so the call is logged.
StepResult heal(EncounterState&, CombatantId target, int16_t amount);
ActionRequest heal_request(CombatantId target, int16_t amount);
ActionRequest check_request(CombatantId id, Skill, int dc, RollMode = RollMode::Normal);
ActionRequest decision_request(const NeedsDecision&, bool react, uint8_t option = 0);
ActionRequest decision_request(const NeedsDecision&, ReactionDecision);   // window_seq is taken from `needs`
// The pending window as a NeedsDecision (nullopt if nothing is pending).
std::optional<NeedsDecision> pending_decision(const EncounterState&);

// Default poll order: `eligible` reordered to initiative order starting after the current actor
// (current actor last).
std::vector<CombatantId> default_poll_order(const EncounterState&, const std::vector<CombatantId>& eligible);

// An ability (skill) check made during the encounter (content/sim driven). Consumes Help's
// "next check" advantage. Wrapper over step(check_request(...)).
StepResult encounter_check(EncounterState&, CombatantId, Skill, int dc, RollMode = RollMode::Normal);

std::vector<ActionRequest> legal_actions(const EncounterState&);   // for AI + UI (current actor)
CombatantId                current_actor(const EncounterState&);
const Combatant*           find(const EncounterState&, CombatantId);

} // namespace rules5e
