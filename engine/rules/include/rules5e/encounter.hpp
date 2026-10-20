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

struct SpellDef;

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
    // Row 13: further actions granted this turn (Action Surge). spend_action takes one when the action is spent.
    uint8_t extra_actions = 0;
    // R-G6: bitmask over Combatant::attacks indices used by the Attack action this turn (not bonus attacks), and a bonus
    // attack option in progress (1 + index into bonus_attacks) with the attacks it still owes (Flurry's second strike).
    uint32_t attacks_used_mask = 0;
    uint8_t bonus_option = 0, bonus_remaining = 0;
    // Small leftovers (appended): Haste's extra action is available (granted at turn start while hasted, not lethargic);
    // Slow: an attack was made this turn; Aggressive: this turn's bonus Dash was Aggressive (movement toward a hostile, sim's).
    bool haste_action = false, slow_attacked = false, aggressive_dash = false;
    // R-G11 rules PR 2 (appended): a zone row with loses_action took this turn's action (Stinking Cloud, D9); set together
    // with action = false, gone with the turn.
    bool action_lost = false;
    // Petrifying Gaze (appended): the turn started with a gazer present; the actor must answer with GazeResponse first.
    bool gaze_pending = false;
};

// Answer to a reaction window (names/layout shared with sim::Combat).
struct ReactionDecision {
    uint32_t window_seq = 0;           // the window being answered (ignored when returned by decide_reaction)
    bool react = false;
    uint8_t option = 0;                // index into NeedsDecision::options
    friend bool operator==(const ReactionDecision&, const ReactionDecision&) = default;
};

// R-G11: a membership change of another creature caused by this step (an aura owner's move, a zone move; Q8). Only the final
// position counts (Q10). Filled by sim inside Combat::submit, like the Move membership lists.
struct ZoneChange { CombatantId creature = 0; uint32_t zone_id = 0; bool entered = false; };

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
    // appended (R-G6, Attack with as_bonus_action): 1 + index into Combatant::bonus_attacks (0 = none / the Rampage grant).
    uint8_t bonus_option = 0;
    // appended (R-G8, Attack): a special attack that replaces one attack of the Attack action (or, for Escape, takes the action):
    // 1 Grapple (target), 2 Shove prone (target), 3 Shove 5 ft away (target), 4 Escape a grapple (target = the grappler, 0 = any;
    // `dc` > 0 is the escape DC when the grapple has none recorded). attack_index / multiattack are ignored (multiattack must be 0).
    uint8_t special = 0;   // R-G15: 5 Wake (target): the action, ends the target's ends_on_damage effects (shake a sleeper awake)
    // appended (R-G7, Attack): the weapon is held in two hands (a versatile weapon); Great Weapon Fighting applies.
    bool two_handed_grip = false;
    // appended (R-G15, CastSpell): sustain the actor's SustainedDamage spell `spell_id` on `target` (Heat Metal): a bonus action,
    // no slot, not on the turn it was cast.
    bool sustain = false;
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
    // appended (R-G14, CastSpell per-cast options; all log-relevant, see SIM_EVENTS.md tag 0xB1):
    //   metamagic: kMeta* options (Combatant::metamagic_known, paid from Combatant::sorcery_pool);
    //   spared: Careful Spell (up to max(1, Cha mod)) or, without Careful, Sculpt Spells (up to 1 + slot level, evocation):
    //     those creatures succeed on the spell's save automatically (Sculpt: and take no damage on a half-on-save spell);
    //   heighten_target: Heightened Spell's target (disadvantage on its first save against the spell);
    //   damage_choice / condition_choice: 1 + index into SpellDef::damage_choices / condition_choices (0 = none).
    uint8_t metamagic = 0;
    std::vector<CombatantId> spared = {};
    CombatantId heighten_target = 0;
    uint8_t damage_choice = 0, condition_choice = 0;
    // appended (small leftovers; log-relevant, SIM_EVENTS.md tag 0xB3):
    //   haste_action: take Attack (one weapon attack), Dash, Disengage or Hide with Haste's extra action;
    //   reckless: Attack, declare Reckless Attack (first attack of the turn, Combatant::reckless_attack);
    //   aggressive: Dash as a bonus action through Combatant::aggressive.
    bool haste_action = false, reckless = false, aggressive = false;
    // appended (R-G9, CastSpell; log-relevant, SIM_EVENTS.md tag 0xB4): forced_feet = the chosen distance of an "up to N ft"
    // forced move (multiple of 5, at most N; 0 = N). teleport_feet = distance to the teleport destination when sim knows
    // positions (multiple of 5, at most TeleportSpec::max_feet; 0 = not supplied, no distance check).
    uint16_t forced_feet = 0, teleport_feet = 0;
    // appended (R-G9): the teleport destination, a sim tile that rules never interprets (passed to EncounterHooks::forced_move;
    // sim validates it before submitting and folds it into log_hash under 0xB2, claimed by the R-G9 sim PR).
    bool has_teleport_dest = false; int32_t teleport_x = 0, teleport_y = 0;
    // appended (R-G11, docs/proposals/ZONES_AURAS.md §3 Q2): membership facts of a Move tile step (and of a readied Move's
    // response), ascending zone ids. Sim fills them inside Combat::submit from the from / to tiles; caller values are
    // overwritten; not folded into log_hash (Q11). zones_entered: in after, not before; zones_left: in before, not after;
    // zones_within: in before and after. zone_changes: other creatures' changes caused by this step (aura owner's move, zone move).
    std::vector<uint32_t> zones_entered = {}, zones_left = {}, zones_within = {};
    std::vector<ZoneChange> zone_changes = {};
    // appended (R-G11, CastSpell; log-relevant, SIM_EVENTS.md log_hash tag 0xB7): a point zone's anchor tile (opaque to rules,
    // validated by sim) and the creatures a chooses_excluded zone leaves unaffected (Spirit Guardians). zone_move = id of the
    // actor's zone to move instead of casting (Moonbeam: ZoneSpec::move_cost), to has_zone_dest / zone_x / zone_y;
    // zone_move_ft = the distance when sim knows positions (0 = not supplied, no distance check; sim-derived, not folded).
    bool has_zone_dest = false; int32_t zone_x = 0, zone_y = 0;
    std::vector<CombatantId> zone_excluded = {};
    uint32_t zone_move = 0; uint16_t zone_move_ft = 0;
    // appended (R-G11 rules PR 2, CastSpell of a zone; log-relevant, SIM_EVENTS.md log_hash tag 0xB9): a wall's end tile
    // (shape Line: from zone_x / zone_y to zone_x2 / zone_y2, opaque to rules), its damaging side (ZoneSpec::sided: +1 / -1,
    // required then; 0 otherwise) and the ring form (ZoneSpec::ring_size_ft > 0).
    bool has_zone_end = false; int32_t zone_x2 = 0, zone_y2 = 0;
    int8_t zone_side = 0;
    bool zone_ring = false;
    // appended (Font of Magic, ActionKind::FlexibleCasting; log-relevant, SIM_EVENTS.md log_hash tag 0xBA): flexible = 1
    // expend a spell slot of flexible_level (1..9) for that many sorcery points, 2 spend flexible_casting_cost(level)
    // points for a slot of flexible_level (1..5). Paid from / into Combatant::sorcery_pool. A bonus action.
    uint8_t flexible = 0, flexible_level = 0;
    // appended (Petrifying Gaze, ActionKind::GazeResponse, VERSION_BUMP_BATCH 7.2p): the turn-start answer while
    // TurnBudget::gaze_pending. avert_gaze (caller input, log_hash 0xBB): avert the eyes (refused while surprised): no save,
    // gazers unseen until its next turn starts. gaze_seen (sim fact, not folded): the gazers it can see now (line of sight,
    // within the gaze's range_ft, not Blinded, the gazer not invisible to it); rules treats the list as opaque.
    bool avert_gaze = false;
    std::vector<CombatantId> gaze_seen = {};
    // appended (R-G12 rules PR 1; log-relevant, SIM_EVENTS.md log_hash tag 0xBD): the form, 1 + index into the choices sim's
    // EncounterHooks::build_form resolves (Wild Shape / shapechanger: UseFeature of a FeatureSpec with `form`; Polymorph:
    // CastSpell). 0 = none.
    uint8_t form_archetype = 0;
    // appended (R-G12 rules PR 2; log-relevant, SIM_EVENTS.md log_hash tag 0xBC). Summon cast: summon_archetypes = the creatures
    // (indices for build_summon, the caster's choice, D1); summon_ids = their ids, filled by rules from the counter (a live caller
    // must leave it empty; a replayed entry must match the counter); summon_tiles = sim's anchors (opaque). CommandSummons:
    // command (1 act, 2 defend) for command_ids (empty = every summon of the actor that takes commands).
    struct SummonTile { int32_t x = 0, y = 0; friend bool operator==(const SummonTile&, const SummonTile&) = default; };
    std::vector<CombatantId> summon_ids = {};
    std::vector<uint8_t> summon_archetypes = {};
    std::vector<SummonTile> summon_tiles = {};
    uint8_t command = 0;
    std::vector<CombatantId> command_ids = {};
    // appended (R-G12 rules PR 3, D6; log_hash 0xBE): Attack by a Pact of the Chain warlock: the familiar (its summon) makes
    // this attack with its reaction (attack_index into the familiar's attacks); it costs one of the warlock's attacks.
    CombatantId familiar_id = 0;
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
    // appended (R-G11 zones, event schema v9):
    ZoneCreated, ZoneMoved, ZoneEnded,
    // appended (R-G12 summons and transforms, event schema v10; SUMMONS_TRANSFORMS.md §2.7, numbered 48-52). Rules PR 1 emits
    // Transformed / Reverted; Summoned, SummonEnded and Commanded are reserved for rules PR 2 (never emitted yet).
    Summoned, SummonEnded, Transformed, Reverted, Commanded,
    // Not an event: the number of kinds. Append new kinds ABOVE this line; consumers that lock the
    // numeric layout (sim's "event layout" test) compare against kEventKindCount.
    Count_ };
inline constexpr std::size_t kEventKindCount = std::size_t(EventKind::Count_);

// Flat tagged event; meaning of a/b/c per kind:
//   InitiativeRolled a=total; TurnStarted a=round; AttackResolved a=natural b=total c=hit|crit<<1;
//   DamageDealt a=dealt b=hp_after; ConditionAdded/Removed a=Condition; DeathSaveRolled a=natural
//   b=successes c=failures; Moved a=feet b=movement left (0 when c != 0) c=flags (R-G9: bit0 forced, bit1 teleport, bit2 drag; target = source); Dashed a=movement left;
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
//   v10 (R-G12): Transformed actor=creature a=source id b=form archetype index c=FormKind; Reverted actor=creature a=source id
//   b=reason (0 duration, 1 concentration, 2 0 HP / unconscious, 3 chose, 4 died, 5 encounter ended) c=overflow damage carried.
//   Summoned actor=summoner target=creature a=spell id b=archetype index c=initiative total; SummonEnded actor=summoner target=creature
//   a=spell id b=reason (0 duration, 1 concentration, 3 0 HP, 5 encounter ended); a Spiritual Weapon: Summoned target=token id
//   b=0 c=-1, SummonEnded b=0 / 5; Commanded actor=summoner target=creature (0 = all) a=command.
//   v7: RechargeRolled actor a=action/spell id b=d6 natural c=recharged(0/1); FeatureTriggered actor=owner
//   target=subject a=trigger id b=TriggerOn c=outcome (0 none, 1 applied, 2 declined, 3 failed save);
//   ResourceSpent actor a=pool id b=amount c=remaining; ModifierApplied actor=caster target a=modifier id
//   b=stat c=magnitude; ModifierExpired actor=caster target a=modifier id b=reason;
//   ActionUsed actor target=first target a=action/spell id b=is_spell(0/1) c=usage kind. A non-spell action
//   (SpellDef::is_spell == false) emits ActionUsed *instead of* SpellCast, then its save/damage events, then
//   SpellResolved. (FeatureTriggered/ModifierApplied/ModifierExpired are reserved: later rows emit them.)
//   v9 (R-G11): ZoneCreated actor=owner target=anchor creature (0 = a point zone) a=zone id b=spell id c=ZoneSpec flags;
//   ZoneMoved actor=owner a=zone id b=feet moved (0 = not supplied); ZoneEnded actor=owner a=zone id b=reason (0 timer,
//   1 concentration, 2 owner died / feature ended, 3 dispelled). A zone trigger emits FeatureTriggered{actor=owner,
//   target=subject, a=spell id, b=TriggerOn::Zone*, c=outcome (0 saved, 1 no save, 2 automatic success: immune, no roll, 3 failed save)} before its save /
//   damage.
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
    // appended (row 11). Damaged: target = creature damaged (the reactor), trigger_actor = the damager (0 unknown),
    // damage = amount after mitigation, before temporary HP. AllyAttacked: target = the ally attacked.
    // `ranged`: the attack that caused the window was ranged.
    int16_t     damage = 0;
    bool        ranged = false;
};

enum class ReactionOptionKind : uint8_t { Attack, AcBonus, Counter, FireReady, Rider, ReduceDamage, ImposeDisadvantage, Retaliate, SubtractRoll /* R-G7: AttackRolled, `dice` subtracted from the roll */ };   // Rider (9c): option_id = trigger id, slot_level = slot spent (0 none)
struct ReactionOption {
    uint8_t  attack_index = 0;        // Attack: index into reactor.attacks (first, so {idx} = an attack option)
    ReactionOptionKind kind = ReactionOptionKind::Attack;
    int8_t   ac_bonus = 0;            // AcBonus: e.g. +5 for Shield
    uint32_t option_id = 0;           // content id (e.g. the Shield / Counterspell spell id)
    uint8_t  slot_level = 0;          // spell options from the SpellBook: slot level spent (Counter: one option per level)
    bool     uses_reaction = true;    // appended: false = taking this option does not spend the reactor's reaction
    // appended (row 11): ReduceDamage: `half` or `dice` is the reduction (option_id = reaction id); Retaliate:
    // option_id = the synthetic SpellDef resolved against the damager; ImposeDisadvantage: option_id = reaction id.
    bool     half = false;
    DiceExpr dice{0, 0, 0};
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

// The hook's answer: feet actually moved (Moved.a), whether it stopped short, and every creature the mover was grappling or
// grappled by that is no longer within reach (rules ends those grapples). hazard: sim's "entered a zone or hazard tile" (R-G11;
// rules reads the membership lists, not this bool); fell_ft stays reserved until elevation exists.
struct ForcedMoveResult {
    uint16_t feet_moved = 0; bool stopped_short = false; bool hazard = false; uint16_t fell_ft = 0;
    std::vector<CombatantId> out_of_reach_of{};
    // appended (R-G11, Q2): the move's zone membership, as on a Move step (ascending zone ids). A forced move covers several
    // tiles, so zones_within repeats a zone id once per further 5 ft moved inside it (Spike Growth's per-5-ft damage:
    // MoveWithin fires once per occurrence in zones_entered and zones_within). A teleport reports entered / left only.
    std::vector<uint32_t> zones_entered{}, zones_left{}, zones_within{};
};

// A paused action. The state is left as it was *before* the action; when the decision arrives the
// action is re-run deterministically from that state with all decisions so far, so the async and
// synchronous paths are bit-identical by construction.
struct PendingDecision {
    ActionRequest request;                 // the paused request (any loggable kind)
    std::vector<ReactionRecord> decided;   // decisions already given for this request, in order
    ReactionWindow window;                 // the window waiting for an answer
    CombatantId reactor;
    std::vector<ReactionOption> options;
    // appended (R-G11, Q7): forced-move results the paused run already got from hooks.forced_move, by step_seq. The re-run
    // reuses them instead of calling the hook again, so it is still called once per (step, step_seq).
    std::vector<std::pair<uint32_t, ForcedMoveResult>> forced_results = {};
};

struct ReadiedAction { CombatantId actor; ActionRequest response; uint32_t trigger_id; };

// Timed expiry shared by lasting effects (conditions from save actions / spells) and, later, modifiers (section 6).
//   Rounds:               ends at the start of a round, `rounds_left` rounds after the one it was applied in
//                         (applied in round R with n = 1: gone when round R + 1 starts; independent of who is alive).
//   EndOfTargetNextTurn:  ends at the end of the target's next turn (when applied during the target's own turn,
//                         that turn does not count; make_timed counts it).
//   EndOfCasterNextTurn (7.2u): ends at the end of the caster's next turn (applied during the caster's turn: that turn
//                         does not count). StartOfTargetNextTurn (7.2u): ends when the target's next turn starts.
//                         Both are used by modifiers only (ActiveModifier::timed; the caster is ActiveModifier::caster).
enum class Expiry : uint8_t { None, Rounds, EndOfTargetNextTurn, EndOfCasterNextTurn, StartOfTargetNextTurn };
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
    uint8_t slot_level = 0;            // appended (R-G15): level the spell was cast at (Dispel Magic); 0 = the spell's level
    bool ends_on_damage = false;       // appended (R-G15): ends when the target takes damage (Sleep)
    // appended (R-G15, delayed / sustained damage): the effect carries no condition (`condition` is unused), and deals
    // `damage` of `damage_type`: damage_when 1 when its timer runs out (Acid Arrow), 2 each time the caster sustains it
    // (Heat Metal: ActionRequest::sustain). cast_stamp = round << 16 | turn_index of the cast (no sustain on that turn).
    bool no_condition = false;
    uint8_t damage_when = 0;
    DiceExpr damage{0, 0, 0}; DamageType damage_type = DamageType::Force; bool magical = true;
    uint32_t cast_stamp = 0;
    // appended (Petrifying Gaze): a failed repeat save turns `condition` into `escalate_to` for good (no further saves).
    bool escalates = false; Condition escalate_to = Condition::Petrified;
    // appended (R-G11): the zone that applied this effect "while in the zone" (Web's restraint), 0 = none. It ends when the
    // target leaves the zone, escapes (special 4: Strength check vs `dc`) or the zone ends.
    uint32_t zone_id = 0;
    // appended (EC deferred monsters, flagged): ends when the target escapes (special 4: Strength check vs `dc`), a giant
    // spider's Web. Derived from the SpellDef (escape_dc): not hashed.
    bool escape_check = false;
    // appended (EC spider follow-up, flagged): a destructible restraint (the web, SpellDef::object_*): AC, the hit points left
    // (Combat::hash 0xBF while > 0) and its damage-type masks. ActionRequest::special 6 attacks it; 0 HP ends the effect.
    int16_t object_hp = 0; int8_t object_ac = 0; uint32_t object_vulnerable = 0, object_immune = 0;
    // appended (EC spider follow-up, flagged): a second condition held for as long as this effect lasts (the spider bite's
    // "paralyzed while poisoned in this way"). Derived from the SpellDef: not hashed.
    bool has_also = false; Condition also = Condition::Paralyzed;
};

// A modifier in force on `target` (row 12). `timed` reuses the condition-duration machinery (Rounds / EndOfTargetNextTurn).
struct ActiveModifier {
    ModifierSpec spec; CombatantId caster = 0, target = 0;
    Timed timed{};
    bool gated = false;   // transient: a `when` that does not hold for the roll in progress (never set between steps)
    uint8_t slot_level = 0;   // appended (R-G15): level of the spell that applied it (Dispel Magic); 0 = unknown / not a spell
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

// Row 13: what UseFeature does for one feature (content: usage.pool, effect.cost, grant_action, heal_pool).
struct FeatureSpec {
    uint32_t pool = 0;          // spend from this pool (0 = none); the pool must be registered for the actor
    uint16_t pool_cost = 0;     // 0 = 1; ignored for heal_pool (the spend is ActionRequest::amount)
    uint8_t cost = 0;           // 0 follow ActionRequest::as_bonus_action, 1 action, 2 bonus action, 3 reaction, 4 free
    uint8_t grant_actions = 0;  // grant_action: TurnBudget::extra_actions += this (Action Surge: 1)
    bool heal_pool = false;     // Lay on Hands: spend `amount` from `pool`, heal ActionRequest::target (0 = self) by that much
    // appended (R-G12 rules PR 1): a transform feature. form = FormKind (2 Wild Shape, 3 shapechanger; 0 = not a transform);
    // form_rounds = how long it lasts (Wild Shape: 600 * (druid level / 2); 0 = until reverted); form_choices = how many forms
    // build_form offers (legal_actions lists form_archetype 1..form_choices). The request needs ActionRequest::form_archetype.
    uint8_t form = 0;
    uint16_t form_rounds = 0;
    uint8_t form_choices = 0;
};

// v8 Moved.c flag bits (the one definition; sim's kMoved* alias these).
inline constexpr int32_t kMovedForced = 1;     // Moved.c bit0: forced movement (push / pull)
inline constexpr int32_t kMovedTeleport = 2;   // Moved.c bit1: teleport
inline constexpr int32_t kMovedDrag = 4;       // Moved.c bit2: dragged by a grappler's own Move (sim emits it)
// R-G9 forced movement / teleport (docs/proposals/FORCED_MOVEMENT.md §2.4, §3). One query per creature moved by an effect.
// mode: 1 push, 2 pull, 3 line (ForcedMoveSpec), 0 teleport. feet: requested distance (teleport: ActionRequest::teleport_feet).
// step_seq: index of the query among the forced moves of this step (0, 1, ...). The destination fields are the request's.
struct ForcedMoveQuery {
    CombatantId mover = 0, source = 0; uint8_t mode = 0; uint16_t feet = 0; bool teleport = false; uint32_t step_seq = 0;
    bool has_dest = false; int32_t dest_x = 0, dest_y = 0;
};
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
    // Row 11: extra options for the Damaged / AllyAttacked windows, appended after the options rules build from
    // Combatant::reactions. Empty = none.
    std::function<std::vector<ReactionOption>(const EncounterState&, const ReactionWindow&, const Combatant&)> damaged_options;
    std::function<std::vector<ReactionOption>(const EncounterState&, const ReactionWindow&, const Combatant&)> ally_attacked_options;
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
    // Row 12: modifiers a feature grants when UseFeature resolves (Rage, Bardic Inspiration token, Leadership). Empty = none.
    // `applies_to` Self = the actor, Target = ActionRequest::target, AlliesWithin30 = area_targets with a 30 ft sphere
    // (the actor excluded).
    std::function<std::vector<ModifierSpec>(const EncounterState&, const Combatant& actor, uint32_t feature_id)> feature_modifiers;
    // Row 13: the mechanics of a feature beyond its uses and modifiers (pool spend, cost kind, grant_action, heal_pool).
    // nullopt = the plain pre-row-13 UseFeature (as_bonus_action decides the cost).
    std::function<std::optional<FeatureSpec>(const EncounterState&, const Combatant& actor, uint32_t feature_id)> feature_spec;
    // ---- R-G9 forced movement / teleport (appended) ----
    // Synchronous: resolves the move on sim's grid and commits it at once (no rollback; a forced move never pauses), as a pure
    // function of the state and sim's positions. Rules calls it only when no decision is pending in the run. Empty = no
    // positions: the move is an intent only (Moved.a = requested feet; no grapple ends on distance).
    std::function<ForcedMoveResult(const EncounterState&, const ForcedMoveQuery&)> forced_move;
    // Order in which one effect's forced moves resolve (sim: farthest from the origin first, ties to the lower id). Returns a
    // permutation of indices into `queries`; anything else = the given order. Empty = the given (target) order.
    std::function<std::vector<std::size_t>(const EncounterState&, std::span<const ForcedMoveQuery> queries)> forced_move_order;
    // Shove "push 5 ft away" sets Moved.c bit 0 (forced) only when this is true. Default false keeps today's c = 0 shove event
    // so no locked digest moves; sim turns it on together with its shove support (EC #104).
    bool shove_forced_flag = false;
    // ---- R-G11 zones (appended; docs/proposals/ZONES_AURAS.md §3) ----
    // Is `creature` in zone `zone_id` now (any footprint tile covered), or completely within it (every tile) when `completely`?
    // Pure and synchronous, from sim's positions. Answers Appear, StartTurn and EndTurn. Empty = nobody is in any zone.
    std::function<bool(const EncounterState&, uint32_t zone_id, CombatantId creature, bool completely)> zone_contains;
    // R-G11 rules PR 2 (appended): is `creature` within the damaging side of a sided zone (Wall of Fire: within side_ft of a
    // wall tile on the chosen side, wall tiles excluded; Q4)? Asked only by region 1 rows. Empty = nobody is.
    std::function<bool(const EncounterState&, uint32_t zone_id, CombatantId creature)> zone_side_contains;
    // ---- R-G12 transforms (appended; SUMMONS_TRANSFORMS.md §2.3, §3.3) ----
    // The statistics of form `archetype_index` (ActionRequest::form_archetype - 1) of transform `kind` from `source` (the spell or
    // feature id) for `creature`: the beast's stat block (Polymorph / Wild Shape: sim checks the CR / level limits and returns
    // nullopt for a form not allowed) or a shapechanger trait's form. Pure (content only, no rng). Rules decides what is
    // replaced and what is kept. Empty = no forms: every transform is refused.
    std::function<std::optional<FormStats>(const EncounterState&, const Combatant& creature, FormKind kind, uint32_t source,
                                           uint8_t archetype_index)> build_form;
    // R-G12 rules PR 2: the stat block of creature `archetype_index` (ActionRequest::summon_archetypes) of summon spell `def`
    // cast at `slot_level` by `summoner` (pure, content only; nullopt = not allowed). Rules sets the id, team and summon fields.
    // Empty = nothing can be summoned.
    std::function<std::optional<Combatant>(const EncounterState&, const Combatant& summoner, const SpellDef& def, uint8_t slot_level,
                                           uint8_t archetype_index)> build_summon;
    // Is this set of creatures a valid choice (Conjure Animals: one CR 2, two CR 1, four CR 1/2 or eight CR 1/4, by slot level)?
    // Empty = any 1..SummonSpec::max_count.
    std::function<bool(const EncounterState&, const Combatant& summoner, const SpellDef& def, uint8_t slot_level,
                       const std::vector<uint8_t>& archetypes)> summon_choice_ok;
};

// R-G11: a live zone or aura (ZONES_AURAS.md §2.6). anchor_x / anchor_y: a point zone's sim tile, opaque to rules (Q1).
// fired: once-per-turn bookkeeping, (creature, turn stamp = round << 16 | turn_index, group); entries of earlier turns are
// pruned at each turn start.
struct ZoneFired { CombatantId creature = 0; uint32_t stamp = 0; uint8_t group = 0;
                   friend bool operator==(const ZoneFired&, const ZoneFired&) = default; };
struct ActiveZone {
    uint32_t zone_id = 0;
    uint32_t spell_id = 0;
    CombatantId owner = 0;
    uint8_t slot_level = 0; int16_t dc = 0;
    bool concentration = false; Timed timed{};
    uint32_t cast_stamp = 0;                   // round << 16 | turn_index of the cast (no move on that turn)
    bool has_anchor_tile = false; int32_t anchor_x = 0, anchor_y = 0;
    std::vector<CombatantId> excluded{};       // ascending; unaffected creatures (Spirit Guardians)
    std::vector<ZoneFired> fired{};
    // appended (R-G11 rules PR 2): size_ft after upcasting (ZoneSpec::size_ft + upcast_size_ft per slot level above; a
    // ring's ring_size_ft); a wall's end tile (opaque, like the anchor), its damaging side (+1 / -1, 0 = none) and ring form;
    // feature: created by add_feature_aura (no spell cast; ends when its owner dies, ZoneEnded reason 2).
    uint16_t size_ft = 0;
    bool has_end_tile = false; int32_t end_x = 0, end_y = 0;
    int8_t side = 0;
    bool ring = false;
    bool feature = false;
};

// R-G8: a condition with a source (Grappled: the grappler and the escape DC; Charmed / Frightened: the caster).
struct SourcedCondition {
    Condition condition = Condition::Grappled; CombatantId target = 0, source = 0;
    int16_t escape_dc = 0;   // Grappled: 0 = escape by a contest against the grappler's Athletics
};

// R-G12 rules PR 3 (D3): a Spiritual Weapon: not a combatant (no HP, not a target, no turn). token_id comes from the summon
// counter (sim's token); end_round 0 = no limit, else it ends at the start of the owner's turn in that round.
struct ActiveSummonWeapon {
    CombatantId token_id = 0, owner = 0;
    uint32_t spell_id = 0;
    uint8_t slot_level = 0;
    uint32_t end_round = 0;
    // the token's tile (ActionRequest::summon_tiles[0] of the cast, then of each SummonWeaponAttack that moves it): stored and
    // logged by rules (0xBC, 0xAF), validated by sim (20 ft move, reach). has_tile false = no tile given yet.
    bool has_tile = false;
    int32_t tile_x = 0, tile_y = 0;
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
    std::vector<ActiveModifier> modifiers;    // row 12 (appended): timed modifiers in application order; empty for old content
    EncounterHooks hooks;                // behaviour, not state: never serialized/hashed
    std::vector<FeatureUses> features;   // appended: limited-use features (register_feature)
    std::vector<ActionUse> action_uses;  // appended (v7): recharge actions; empty for content without one
    std::vector<ResourcePool> pools;     // appended (v7): resource pools; empty for content without one
    std::vector<SourcedCondition> sourced;   // appended (R-G8): who a condition came from; empty for old content
    std::vector<ActiveZone> zones;            // appended (R-G11): live zones in creation order; empty unless a zone exists
    uint32_t next_zone_id = 1;                // appended (R-G11): id of the next zone (ids start at 1)
    uint32_t transforms = 0;                  // appended (R-G12): transforms begun this encounter (0xAF folds while non-zero)
    // appended (R-G12 PR 2): the next summon id (rules-owned; sim seeds it after begin_encounter above every chunk spawn id; 0 =
    // unseeded: above every combatant id) and the creatures summoned so far. Ids are never reused.
    uint32_t next_summon_id = 0;
    uint32_t summons = 0;
    std::vector<ActiveSummonWeapon> summon_weapons{};   // appended (R-G12 PR 3), creation order; counted in `summons`
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
    // appended (R-G12, D15 / SUMMONS_TRANSFORMS.md §3.5): in the step that ends the encounter, the index into `events` of the
    // first teardown event (Reverted{b = 5}; SummonEnded{b = 5} with rules PR 2), which run up to EncounterEnded. nullopt = no
    // teardown (nothing transformed or summoned, or the encounter did not end in this step).
    std::optional<std::size_t> teardown_begin = {};
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
// ---- R-G8: conditions with a source, grapple and shove ----
// SRD 5.1: a target can be grappled / shoved only if it is at most one size larger than the creature.
bool size_within_one(Size grappler, Size target);
// Applies `c` to `target` and records `source` (replaces an entry for the same condition, target and source). False (nothing
// happens) when the target is missing, dead, immune, or (Grappled) too large.
bool apply_sourced_condition(EncounterState&, CombatantId source, CombatantId target, Condition c, int escape_dc, std::vector<Event>&);
inline bool grapple(EncounterState& s, CombatantId grappler, CombatantId target, int escape_dc, std::vector<Event>& ev) {
    return apply_sourced_condition(s, grappler, target, Condition::Grappled, escape_dc, ev);
}
CombatantId condition_source(const EncounterState&, CombatantId target, Condition);   // 0 = none recorded
std::vector<CombatantId> grappled_by(const EncounterState&, CombatantId grappler);     // creatures the grappler is holding
// Drops entries whose condition is gone; a grapple also ends when the grappler is dead or incapacitated. Called after every step.
void prune_sourced(EncounterState&, std::vector<Event>&);
bool has_sourced_conditions(const EncounterState&);
uint64_t sourced_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);   // sim folds under tag 0xA7 when non-empty

// ---- R-G16: surprise ----
// A creature is surprised when it notices no threat: it has at least one living enemy (another team), every living enemy is
// hiding (listed in `hiders` with its Dexterity (Stealth) total), and each such total is >= the creature's passive Perception
// (passive_perception + `perception_bonus`, 0 here). Pure; returns ids in `combatants` order. Dead creatures are never listed.
struct StealthTotal { CombatantId id = 0; int16_t total = 0; };
std::vector<CombatantId> surprised_by_stealth(const std::vector<Combatant>&, const std::vector<StealthTotal>& hiders);
// Sets Combatant::surprised on the listed creatures (call before begin_encounter). Unknown ids are ignored.
void apply_surprise(std::vector<Combatant>&, const std::vector<CombatantId>& surprised);
bool has_surprised(const EncounterState&);
// FNV-1a 64 over the ids of surprised creatures (combatants order); sim folds it under tag 0xA8 only when has_surprised.
uint64_t surprised_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
// Small leftovers: Life Drain max-HP reductions, Haste lethargy, active Reckless Attack, and escalating (Petrifying Gaze)
// effects. sim folds leftover_digest under tag 0xAA only when has_leftover_state (nothing for content that uses none).
// Haste / Slow themselves are modifiers (modifiers_digest, 0xA2, which folds repeat_save fields only when set).
// Petrifying Gaze (Combat::hash 0xAE): some creature has its eyes averted / their ids.
bool has_gaze_state(const EncounterState&);
uint64_t gaze_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
// R-G12 (Combat::hash 0xAF): summon / transform state. has_summon_state: something was transformed (or, rules PR 2, summoned)
// this encounter. summons_digest: summon id counter u32, summon count u32 (0 / 0 until rules PR 2), transforms u32, then per
// combatant with a form (combatants order): id, FormKind, form_source, form_caster, form_archetype, form_end_round, base hp,
// base hp_max.
bool has_summon_state(const EncounterState&);
uint64_t summons_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
// R-G12 pure helpers (also used by the encounter). apply_form saves `c`'s own statistics in `base` and puts on form `f` of
// `kind` (what is replaced vs kept: VERSION_BUMP_BATCH 7.2q). revert_form restores them; Polymorph / Wild Shape come back with
// the saved hp, a shapechanger keeps its one HP pool. Both are no-ops on a wrong state (apply_form returns false when already
// transformed or kind is None; revert_form returns false when not transformed). They emit no events.
bool apply_form(Combatant& c, FormKind kind, const FormStats& f);
bool revert_form(Combatant& c);
bool has_leftover_state(const EncounterState&);
uint64_t leftover_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);

// R-G6: could `actor` start bonus attack option `option` (1 + index) now? Checks the bonus action, the prerequisite (Attack action
// used a listed attack, a different weapon, a modifier in force) and the pool. Pure.
bool bonus_attack_available(const EncounterState&, const Combatant& actor, uint8_t option);
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

// ---- Timed modifiers (row 12) ----
// Applies `spec` to `target` (replacing a same-id modifier unless spec.stack). TempHp is granted at once (the larger
// pool is kept) and leaves no record. Emits ModifierApplied (actor = caster, target, a = id, b = ModStat,
// c = magnitude: bonus, else die count*100+sides, else the RollMode, else speed num*100+den, else 0). Returns false
// if the target does not exist.
bool apply_modifier(EncounterState&, CombatantId caster, CombatantId target, const ModifierSpec& spec, std::vector<Event>& ev);
// Pure queries over the active modifiers of `id` (ability < 0: no ability scope check).
int modifier_flat(const EncounterState&, CombatantId id, ModStat, int ability = -1);   // sum of flat bonuses
int modified_ac_bonus(const EncounterState& s, CombatantId id);
int modified_speed(const EncounterState&, const Combatant&);                          // effective_speed scaled by Speed modifiers
bool has_modifier_state(const EncounterState&);
uint64_t modifiers_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);                      // fold under tag 0xA2 when has_modifier_state
uint64_t timed_effects_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);

// ---- R-G11 zones ----
const ActiveZone* find_zone(const EncounterState&, uint32_t zone_id);
// Zone state (sim folds zones_digest under Combat::hash tag 0xAB, only when has_zone_state: a zone exists or has existed):
// next_zone_id, every ActiveZone field in order, and the zone-bound effects (ActiveSpellEffect::zone_id != 0).
bool has_zone_state(const EncounterState&);
uint64_t zones_digest(const EncounterState&, uint64_t seed = 0xCBF29CE484222325ull);
// R-G11 rules PR 2 (D7 model): a feature aura (no cast, no concentration, no timer) of `spell_id`'s ZoneSpec centred on `owner`
// (anchor Creature), e.g. a Paladin aura at encounter start. Appends ZoneCreated to `events` when given. Returns the zone id,
// 0 when the spell is unknown, not a Zone with anchor Creature, or the owner is missing or dead.
uint32_t add_feature_aura(EncounterState&, CombatantId owner, uint32_t spell_id, std::vector<Event>* events = nullptr);
// R-G11 rules PR 2: is `id` completely inside a live Silence zone (kZoneSilence; hooks.zone_contains)?
bool in_silence(const EncounterState&, CombatantId id);

std::vector<ActionRequest> legal_actions(const EncounterState&);   // for AI + UI (current actor)
CombatantId                current_actor(const EncounterState&);
const Combatant*           find(const EncounterState&, CombatantId);

} // namespace rules5e
