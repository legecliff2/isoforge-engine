#pragma once
// Encounter state machine (ARCHITECTURE.md §5.3). Pure; the engine drives it.
// Positions/distances/perception live in `sim`; requests carry the facts rules needs.
// Rules never decide win or loss: the encounter ends via an end-condition policy (hook) that
// reports a factual outcome (each PC's state); the game layer interprets it.
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include "rng/rng.hpp"
#include "rules5e/attack.hpp"
#include "rules5e/initiative.hpp"
#include "rules5e/saves.hpp"
#include "rules5e/spells.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

struct TurnBudget {
    bool action = true, bonus_action = true, reaction = true; int16_t movement_ft = 0;
    uint8_t attacks_remaining = 0;     // Extra Attack / multiattack: further attacks from the Attack action taken this turn
    // Multiattack in progress: 1 + index into Combatant::multiattacks (0 = none) and the position of the
    // next attack in that sequence. While one is active, Attack must follow the declared order.
    uint8_t sequence = 0, sequence_pos = 0;
    // Loading weapons already fired in the current Attack action (bit per Combatant::attacks index < 32):
    // a Loading weapon fires once per action regardless of Extra Attack / multiattack. Reset when an action is spent.
    uint32_t loading_fired = 0;
    // Feet moved this turn: += 5 * ActionRequest::move_tiles when an own-turn Move completes (Moved emitted).
    // An opportunity-attack-interrupted move and a readied Move fired out of turn do not count.
    uint16_t moved_ft = 0;
    // Row 9b: a trigger granted a bonus-action attack (Rampage): Combatant::attacks index + 1, 0 = none. Spent by
    // Attack with as_bonus_action; gone with the turn.
    uint8_t bonus_attack_grant = 0;
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
    uint32_t spell_id = 0;             // CastSpell: spell_id_of(content id)
    uint8_t spell_level = 0;           // CastSpell: slot level used (0 = cantrip)
    ReactionDecision decision{};       // ReactionDecision: the answer (actor = reactor)
    int16_t amount = 0;                // Heal: hit points (target = creature healed)
    Skill skill = Skill::Athletics;    // AbilityCheck: skill (actor = creature checking)
    int16_t dc = 0;                    // AbilityCheck
    RollMode check_mode = RollMode::Normal;  // AbilityCheck: extra roll-mode source
    // appended (UseFeature): content feature id (opaque to rules; sim's feature registry) and an
    // optional self-heal rolled from the encounter rng. Spends the action, or the bonus action
    // when as_bonus_action (the feature itself grants it). Limited uses: EncounterState::features.
    uint32_t feature_id = 0;
    DiceExpr heal_dice{0, 0, 0};
    // appended (Attack / readied Attack, ranged weapons): facts supplied by sim. Either one gives
    // disadvantage (SRD 5.1 "Ranged Attacks": long range; hostile within 5 ft that can see you and
    // isn't incapacitated). Ignored for melee attacks.
    bool ranged_hostile_within_5ft = false;
    bool beyond_normal_range = false;
    // CastSpell: creatures affected when more than one (Hold Person upcast), or one entry per dart
    // (Magic Missile; a single entry = all darts at it). Empty = {target}.
    std::vector<CombatantId> targets = {};
    // appended (Attack): start a declared multiattack: 1 + index into Combatant::multiattacks (0 = a plain
    // attack / legacy Extra Attack). The request's attack_index must be the sequence's first attack. Once started,
    // every further Attack this turn must carry the next attack_index of the sequence (multiattack = 0 or the
    // same value); targets may differ per attack; moving, reactions and other creatures' Ready triggers happen
    // between attacks like any other step. The sequence ends when its last attack is made or the turn ends.
    uint8_t multiattack = 0;
    // appended (Attack / readied Attack, T&C-G7): sim fact for Combatant::roll_traits, e.g. Pack Tactics:
    // an ally of the actor, not incapacitated, is within 5 ft of the target.
    bool ally_near_target = false;
    // appended (Move): sim fact, the number of tiles this Move covers (sim paths one tile per Step, so 1; 0 for
    // every other request). Unlike move_ft it is not terrain cost (difficult terrain doubles move_ft). Callers
    // that track "moved N ft this turn" add 5 * move_tiles only when the move completes. Own-turn Steps only:
    // a readied Move fired out of turn is not a request. Not part of sim's log_hash.
    uint8_t move_tiles = 0;
    // appended (CastSpell, version-bump batch row 8): cast from Combatant::innate instead of a slot. The level is the
    // spell's own (spell_level must equal it: no upcasting), no slot is spent, one use is consumed, and components
    // are skipped unless the InnateSpell is `as_listed`. The spell need not be in spells_known.
    bool innate = false;
};

enum class EventKind : uint8_t {
    InitiativeRolled, TurnStarted, AttackResolved, DamageDealt, ConditionAdded, CombatantDowned,
    DeathSaveRolled, Stabilized, RegainedHp, CombatantDied, Moved, Dashed, Disengaged, Dodged,
    StoodUp, TurnEnded, EncounterEnded,
    // appended:
    Healed, ConditionRemoved, MedicineCheck, Helped, Hid, Readied, ReadyTriggered, ReadyExpired,
    CheckRolled,
    ReactionWindowOpened, ReactionTaken, ReactionDeclined, MoveInterrupted, SpellCast, SpellCountered,
    SpellResolved, AcBonus,
    // appended:
    FeatureUsed,
    // appended (spellcasting):
    SavingThrowRolled, ConcentrationEnded, SpellEffectEnded, SpellCheckRolled,
    // appended (version-bump batch, event schema v7):
    RechargeRolled, FeatureTriggered, ResourceSpent, ModifierApplied, ModifierExpired, ActionUsed,
    // Not an event: the number of kinds. Append new kinds ABOVE this line; consumers that lock the
    // numeric layout (sim's "event layout" test) compare against kEventKindCount.
    Count_ };
inline constexpr std::size_t kEventKindCount = std::size_t(EventKind::Count_);

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
//   AcBonus a=bonus b=new AC; FeatureUsed a=feature_id b=bonus action (0/1) (Healed follows for self-heals);
//   SavingThrowRolled actor=saver target=Ability a=natural b=total c=success|concentration<<1;
//   ConcentrationEnded actor=caster a=spell_id; SpellEffectEnded actor=affected creature a=spell_id
//   b=Condition removed (-1 none); SpellCheckRolled actor=counterer a=natural b=total c=success (vs DC).
//   SpellCast/Resolved/Countered: actor=caster target=first target, b=slot level (0 = cantrip).
//   v7: RechargeRolled actor a=action/spell id b=d6 natural c=recharged(0/1); FeatureTriggered actor=owner
//   target=subject a=trigger id b=TriggerOn c=outcome (0 none, 1 applied, 2 declined, 3 failed save);
//   ResourceSpent actor a=pool id b=amount c=remaining; ModifierApplied actor=caster target a=modifier id
//   b=stat c=magnitude; ModifierExpired actor=caster target a=modifier id b=reason;
//   ActionUsed actor target=first target a=action/spell id b=is_spell(0/1) c=usage kind. A non-spell action
//   (SpellDef::is_spell == false) emits ActionUsed *instead of* SpellCast, then its save/damage events, then
//   SpellResolved. (FeatureTriggered/ModifierApplied/ModifierExpired are reserved: later rows emit them.)
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
    int16_t     target_ac = 0;        // BeingHit: AC the total was compared to (AC + cover + reaction bonus)
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
    uint8_t  slot_level = 0;          // spell options from the SpellBook: slot level spent (Counter: one option per level)
    bool     uses_reaction = true;    // appended: false = taking this option does not spend the reactor's reaction
};

// Log entry for one reaction decision made under Ask (Auto/Never are reproduced from state and are
// not logged). This is the only decision log format: completed requests carry their entries in
// LoggedStep::reactions; decisions for a paused request live in EncounterState::pending->decided.
// Sim stores rules' ActionLog as-is and has no separate Decide command.
struct ReactionDecisionEntry {
    uint32_t window_seq; ReactionTrigger trigger; CombatantId reactor; ReactionDecision decision;
    friend bool operator==(const ReactionDecisionEntry&, const ReactionDecisionEntry&) = default;
};
using ReactionRecord = ReactionDecisionEntry;   // former name (kept for source compatibility)

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

// Timed expiry shared by lasting effects (conditions from save actions / spells) and, later, modifiers (section 6).
//   Rounds:               ends at the start of a round, `rounds_left` rounds after the one it was applied in
//                         (applied in round R with n = 1: gone when round R + 1 starts; independent of who is alive).
//   EndOfTargetNextTurn:  ends at the end of the target's next turn (when applied during the target's own turn,
//                         that turn does not count; make_timed counts it).
enum class Expiry : uint8_t { None, Rounds, EndOfTargetNextTurn };
struct Timed { Expiry kind = Expiry::None; int16_t rounds_left = 0;
               friend bool operator==(const Timed&, const Timed&) = default; };
// `target_acting`: the target is the current actor when the effect is applied.
Timed make_timed(Expiry, uint16_t rounds, bool target_acting);
// Each returns true when the timer has just run out. No-ops (false) for another kind or Expiry::None.
bool tick_round_start(Timed&);
bool tick_target_turn_end(Timed&);

// A lasting spell effect on a creature (a condition from a failed save). Ends when the caster's
// concentration ends, or on a successful repeat save.
struct ActiveSpellEffect {
    uint32_t spell_id; CombatantId caster, target;
    Condition condition; Ability save_ability; int16_t dc;
    RepeatSave repeat_save; bool concentration;
    Timed timed{};                     // appended: Expiry::None = lasts until concentration ends / a repeat save succeeds
};

// Limited-use features (Second Wind, ...), keyed by (actor, feature_id). Part of the encounter state,
// so they are serialized/hashed with it and rebuilt by replay; sim registers them at setup.
enum class FeatureRecharge : uint8_t { None, ShortRest, LongRest };
struct FeatureUses {
    CombatantId actor = 0;
    uint32_t feature_id = 0;
    uint8_t max = 0;                 // uses per recharge; 0 = unlimited
    uint8_t used = 0;
    FeatureRecharge recharge = FeatureRecharge::None;
};

// A recharge action (Recharge 5-6), keyed by (actor, spell_id of the synthetic SpellDef). Registered at setup
// only for content that has one; `expended` is set when the action is used and cleared by the begin_turn
// recharge roll (the only place such a combatant draws from the encounter rng).
struct ActionUse { CombatantId actor = 0; uint32_t spell_id = 0; bool expended = false; };

// A class resource pool (ki, rage, ...), keyed by (actor, pool_id). Registered at setup only for new content.
struct ResourcePool { CombatantId actor = 0; uint32_t pool_id = 0; uint16_t max = 0, cur = 0;
                      FeatureRecharge recharge = FeatureRecharge::None; };

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
    // appended. Ready: called when a readied response fires (after the reactor accepts), with the
    // triggering event, to re-evaluate facts that depend on the moment it fires (range, hostiles
    // within 5 ft, target_within_5ft). Returns the request to resolve; nullopt = keep it as readied.
    // Must be a pure function of the state and sim's (replayed) positions. Empty = keep as readied.
    std::function<std::optional<ActionRequest>(const EncounterState&, const ReadiedAction&, const Event& e)> refresh_ready;

    // ---- Spellcasting (spells.hpp) ----
    // Spell definitions (content). A CastSpell whose spell_id isn't here is an opaque spell: rules
    // only run its SpellCast window and sim/content resolve it (no slot is spent).
    std::shared_ptr<const SpellBook> spells;
    // Can the caster provide these components (kComponentV/S/M) now? Empty = yes.
    std::function<bool(const EncounterState&, const Combatant& caster, const SpellDef&)> can_provide_components;
    // Range / line of sight / creature type (Hold Person: humanoid) for one target. Empty = valid.
    std::function<bool(const EncounterState&, const Combatant& caster, const SpellDef&, const Combatant& target)> spell_target_ok;
    // Positions (sim): are all `ids` within `feet` of each other (Hold Person upcast: 30 ft)? Empty = yes.
    std::function<bool(const EncounterState&, const std::vector<CombatantId>& ids, int feet)> targets_within;
    // Sight (sim): can `observer` see `target` (reaction spells with requires_sight)? Empty = yes.
    std::function<bool(const EncounterState&, const Combatant& observer, const Combatant& target)> can_see;
    // Row 9b (area_save riders: Death Burst, aura ticks): the creatures inside the area of `def` (a synthetic Save
    // SpellDef) centred on `source`, as sim sees positions. The source itself is ignored. Empty = nobody, and then an
    // area_save trigger does not fire.
    std::function<std::vector<CombatantId>(const EncounterState&, const Combatant& source, const SpellDef&)> area_targets;
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
    std::vector<ActiveSpellEffect> effects;   // lasting spell effects, in the order applied
    EncounterHooks hooks;                // behaviour, not state: never serialized/hashed
    std::vector<FeatureUses> features;   // appended: limited-use features (register_feature)
    std::vector<ActionUse> action_uses;  // appended (v7): recharge actions; empty for content without one
    std::vector<ResourcePool> pools;     // appended (v7): resource pools; empty for content without one
};

// Action log entry: the request plus the reaction decisions it needed. Every state-changing call
// (turn actions, Heal, AbilityCheck) is a request, so the log alone is a complete replay given
// the same seed, combatants, and hooks (decide_reaction is not needed).
struct LoggedStep { ActionRequest request; std::vector<ReactionRecord> reactions; };
using ActionLog = std::vector<LoggedStep>;

enum class StepStatus : uint8_t { Rejected, Done, NeedsDecision };

// Everything a UI needs to show the prompt (also returned by pending_decision(state)).
struct NeedsDecision {
    uint32_t window_seq = 0;
    ReactionTrigger trigger = ReactionTrigger::LeavingReach;
    CombatantId reactor = 0;
    CombatantId trigger_actor = 0;     // mover / attacker / caster / creature whose event fired the Ready
    std::vector<ReactionOption> options;
    ReactionWindow window{};           // full window details (same values as the fields below)
    // appended (prompt details; 0 when not applicable):
    CombatantId target = 0;            // BeingHit: creature hit (= reactor); SpellCast: the spell's target
    int16_t attack_total = 0;          // BeingHit: attack roll total ...
    int16_t target_ac = 0;             //   ... vs this AC (includes cover and current reaction bonus)
    int8_t  attack_natural = 0;        // BeingHit: d20 natural (20 = crit, AC bonuses can't help)
    uint32_t spell_id = 0;             // SpellCast: spell being cast; BeingHit: the spell that hits
                                       //   (Fire Bolt attack, or Magic Missile with attack_total 0)
    uint8_t spell_level = 0;           // SpellCast: its slot level (0 = cantrip); BeingHit: 0
};
NeedsDecision make_needs_decision(const ReactionWindow&, CombatantId reactor, std::vector<ReactionOption> options);

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

// Limited-use features. A UseFeature for a registered (actor, feature_id) is rejected once its uses
// are spent; an unregistered feature is unlimited (the caller validates it).
void register_feature(EncounterState&, CombatantId actor, uint32_t feature_id, uint8_t max_uses,
                      FeatureRecharge = FeatureRecharge::None, uint8_t used = 0);
const FeatureUses* find_feature(const EncounterState&, CombatantId actor, uint32_t feature_id);
// Uses left: 255 = unlimited or unregistered.
uint8_t feature_uses_left(const EncounterState&, CombatantId actor, uint32_t feature_id);
// Rest: a short rest recharges ShortRest features; a long rest recharges ShortRest and LongRest ones
// (and refills pools with the same recharge kinds).
void recover_features(EncounterState&, bool long_rest);
// Canonical FNV-1a 64 digest of the feature-use table (fold into any state hash).
uint64_t feature_uses_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);

// Recharge actions. `register_action_use` is idempotent (re-registering resets `expended`). A spell/action
// with no row is unlimited (the feature-use precedent).
void register_action_use(EncounterState&, CombatantId actor, uint32_t spell_id, bool expended = false);
const ActionUse* find_action_use(const EncounterState&, CombatantId actor, uint32_t spell_id);
// Resource pools. `can_spend` is false for an unregistered pool; spend_pool returns false (no change) when short.
void register_pool(EncounterState&, CombatantId actor, uint32_t pool_id, uint16_t max, uint16_t cur,
                   FeatureRecharge = FeatureRecharge::None);
const ResourcePool* find_pool(const EncounterState&, CombatantId actor, uint32_t pool_id);
bool can_spend(const EncounterState&, CombatantId actor, uint32_t pool_id, uint16_t n);
bool spend_pool(EncounterState&, CombatantId actor, uint32_t pool_id, uint16_t n);
// Canonical FNV-1a 64 digests of the two tables (sim folds each under its own tag byte, only when non-empty).
uint64_t action_uses_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
// Innate spells' uses and the printed DC/attack overrides, over every combatant that has any (sim folds it under tag 0xA4,
// only when `has_innate_state` is true).
// Triggers' once-per-turn/round bookkeeping over every combatant that has triggers (sim folds it under tag 0xA6, only
// when `has_trigger_state`).
bool has_trigger_state(const EncounterState&);
uint64_t triggers_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
bool has_innate_state(const EncounterState&);
uint64_t innate_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
uint64_t pools_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);

// Timed lasting effects (sim folds the digest under tag 0xA5, only when `has_timed_effects`).
bool has_timed_effects(const EncounterState&);
uint64_t timed_effects_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);

std::vector<ActionRequest> legal_actions(const EncounterState&);   // for AI + UI (current actor)
CombatantId                current_actor(const EncounterState&);
const Combatant*           find(const EncounterState&, CombatantId);

} // namespace rules5e
