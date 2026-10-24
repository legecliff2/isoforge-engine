#pragma once
// Core SRD 5.1 value types (ARCHITECTURE.md §5.3). Pure data; no engine headers.
#include <array>
#include <cstdint>
#include <string_view>
#include <optional>
#include <vector>

namespace rules5e {

enum class Ability : uint8_t { Str, Dex, Con, Int, Wis, Cha };
enum class DamageType : uint8_t { Acid, Bludgeoning, Cold, Fire, Force, Lightning,
                                  Necrotic, Piercing, Poison, Psychic, Radiant, Slashing, Thunder };
enum class Condition : uint8_t { Blinded, Charmed, Deafened, Frightened, Grappled, Incapacitated,
                                 Invisible, Paralyzed, Petrified, Poisoned, Prone, Restrained,
                                 Stunned, Unconscious, Exhaustion,
                                 // appended (7.2zd Sanctuary): spell@1 apply_condition `warded` with a `ward`. Not an SRD condition:
                                 // creatures targeting the warded one with an attack or a harmful spell save first (ward_alternate_targets).
                                 Warded };
// SRD 5.1 skills (Chapter 7). Append-only.
enum class Skill : uint8_t { Acrobatics, AnimalHandling, Arcana, Athletics, Deception, History, Insight,
                             Intimidation, Investigation, Medicine, Nature, Perception, Performance,
                             Persuasion, Religion, SleightOfHand, Stealth, Survival };
constexpr Ability skill_ability(Skill k) {
    switch (k) {
    case Skill::Athletics: return Ability::Str;
    case Skill::Acrobatics: case Skill::SleightOfHand: case Skill::Stealth: return Ability::Dex;
    case Skill::Arcana: case Skill::History: case Skill::Investigation: case Skill::Nature:
    case Skill::Religion: return Ability::Int;
    case Skill::AnimalHandling: case Skill::Insight: case Skill::Medicine: case Skill::Perception:
    case Skill::Survival: return Ability::Wis;
    default: return Ability::Cha;   // Deception, Intimidation, Performance, Persuasion
    }
}

// SRD 5.1 creature types (same order as common@1 creature_type). Append-only.
enum class CreatureType : uint8_t { Aberration, Beast, Celestial, Construct, Dragon, Elemental, Fey, Fiend,
                                    Giant, Humanoid, Monstrosity, Ooze, Plant, Undead };
constexpr uint32_t bit(CreatureType t) { return uint32_t(1) << uint32_t(t); }

enum class RollMode : uint8_t { Normal, Advantage, Disadvantage };

using CombatantId = uint32_t;

constexpr uint32_t bit(Condition c) { return uint32_t(1) << uint32_t(c); }
constexpr uint32_t bit(DamageType t) { return uint32_t(1) << uint32_t(t); }
constexpr uint32_t bit(Ability a) { return uint32_t(1) << uint32_t(a); }
constexpr uint32_t bit(Skill k) { return uint32_t(1) << uint32_t(k); }

struct DiceExpr {                                  // "2d6+3"; count == 0 means a flat value
    uint8_t count; uint8_t sides; int16_t bonus;
    friend constexpr bool operator==(const DiceExpr&, const DiceExpr&) = default;
};

struct AbilityScores {
    std::array<int8_t, 6> score;
    constexpr int8_t operator[](Ability a) const { return score[std::size_t(a)]; }
};

// One extra damage component of an attack (SRD "damage dice + type"; e.g. a flame tongue's +2d6 fire).
// Attack::damage/type stay the first component; extra_damage holds components 2..n. Each is rolled
// (and doubled on a critical hit) separately and gets immunity/resistance/vulnerability for ITS type.
struct DamagePart {
    DiceExpr dice; DamageType type;
    // appended (class feature hooks, 7.2y; content `bonus_vs`): extra dice of the same type against a target whose creature type
    // is in `vs_types` (bitmask over CreatureType; 0 = none). Rolled right after `dice` (doubled on a crit).
    uint32_t vs_types = 0; DiceExpr vs_dice{0, 0, 0};
};

// Facts about an attack's damage source that qualified resistances/immunities care about (SRD 5.1:
// "bludgeoning, piercing, and slashing from nonmagical attacks [that aren't silvered / adamantine]").
// Bit flags in Attack::qualifiers; they apply to every damage component of the attack. Spell damage is
// always magical; weapon attacks are magical for magic weapons or creatures whose attacks count as
// magical ("Magic Weapons", Ki-Empowered Strikes). kAttackSilvered / kAttackAdamantine describe the weapon.
enum AttackQualifier : uint8_t { kAttackMagical = 1, kAttackSilvered = 2, kAttackAdamantine = 4 };

// Damage types that only resist (or are immune to) damage from a nonmagical source:
//   nonmagical                 - any nonmagical source ("from nonmagical attacks")
//   nonmagical_not_silvered    - nonmagical and not silvered
//   nonmagical_not_adamantine  - nonmagical and not adamantine
// Unqualified Combatant::resistances / immunities are unaffected and apply to every source.
struct QualifiedDamageMask { uint32_t nonmagical = 0, nonmagical_not_silvered = 0, nonmagical_not_adamantine = 0; };

// Creature size category (SRD 5.1). Rules only reads it for the Heavy weapon property.
enum class Size : uint8_t { Tiny, Small, Medium, Large, Huge, Gargantuan };

struct Attack {
    std::string_view id;
    int8_t   to_hit;                               // total bonus
    DiceExpr damage; DamageType type;
    uint8_t  reach_or_range_ft;
    bool     ranged, finesse;
    // appended: ranged weapons' normal / long range in feet (SRD 5.1 "Range"; longbow 150/600 needs
    // more than reach_or_range_ft's byte). 0 = use reach_or_range_ft as the normal range, no long range.
    uint16_t range_ft = 0, long_range_ft = 0;
    // appended: damage components 2..n (empty = single-component attack, the pre-existing behaviour).
    std::vector<DamagePart> extra_damage{};
    uint8_t qualifiers = 0;   // AttackQualifier bits (magic / silvered / adamantine); 0 = plain nonmagical
    uint16_t properties = 0;  // WeaponProperty bits (weapon.hpp): heavy, loading, finesse, light, ...; 0 = none
    // appended (Life Drain: wight, specter, vampire spawn): on a hit, the target's hit point maximum is reduced by the damage it
    // took (Combatant::hp_max_reduction), after a Con save vs drain_save_dc when non-zero (success: no reduction).
    bool drain_max_hp = false;
    int8_t drain_save_dc = 0;
    // appended (EC swarm, flagged): SRD swarm bites ("2d6, or 1d6 if the swarm has half of its hit points or fewer"): when
    // half_hp_damage.count > 0 and the attacker's hp * 2 <= hp_max, it replaces `damage` (component 1). Default: unused.
    DiceExpr half_hp_damage{};
    // appended (7.2y `bonus_vs` on damage[0]): as DamagePart::vs_types / vs_dice for component 1.
    uint32_t vs_types = 0; DiceExpr vs_dice{0, 0, 0};
    // appended (batch 3, mummy Rotting Fist): on a hit, a Con save vs curse_no_regain_dc when non-zero; on a failure the target
    // is cursed: "The cursed target can't regain hit points" (sets Combatant::no_regain_hp; the 24-hour maximum loss and
    // Remove Curse are not modelled). Sim Combat::hash 0xDC folds no_regain_hp while any attack carries this.
    int8_t curse_no_regain_dc = 0;
    // appended (batch 3, vampire spawn Bite): drain_damage_type >= 0 (a DamageType) limits the Life Drain amount to the damage of
    // that type the target took (after immunity / resistance / vulnerability, capped at the hit points actually lost); -1 = all.
    // drain_heal_self: "the vampire regains hit points equal to that amount" (only when a reduction applies).
    int8_t drain_damage_type = -1;
    bool drain_heal_self = false;
    // appended (batch 3, vampire spawn Bite: "one willing creature, or a creature that is grappled by the vampire, incapacitated,
    // or restrained"): kTargetGate* bits; non-zero = the target must meet at least one (0 = any target).
    uint8_t target_requires_any = 0;
    // appended (batch 3, shadow Strength Drain, SRD 5.1: "the target's Strength score is reduced by 1d4. The target dies if this
    // reduces its Strength to 0. Otherwise, the reduction lasts until the target finishes a short or long rest."): on a hit,
    // after the damage, when count > 0 (strength_drain). Static content: not hashed (the reduction itself folds in 0xDF).
    DiceExpr strength_drain{0, 0, 0};
};
inline constexpr uint8_t kTargetGateWilling = 1, kTargetGateGrappledBySelf = 2, kTargetGateIncapacitated = 4,
                         kTargetGateRestrained = 8;
constexpr int normal_range_ft(const Attack& a) { return a.range_ft ? a.range_ft : a.reach_or_range_ft; }
constexpr int long_range_ft(const Attack& a) { return a.long_range_ft ? a.long_range_ft : normal_range_ft(a); }
// Range facts for a ranged attack at `dist_ft`: out of reach entirely, or beyond normal range (disadvantage).
constexpr bool out_of_range(const Attack& a, int dist_ft) { return dist_ft > long_range_ft(a); }
constexpr bool beyond_normal_range(const Attack& a, int dist_ft) { return a.ranged && dist_ft > normal_range_ft(a); }

enum class ActionKind : uint8_t { Attack, Dash, Disengage, Dodge, Help, Hide, Ready, Move, EndTurn,
                                  // appended (append-only):
                                  StandUp, OpportunityAttack, Stabilize, SpareTheDying,
                                  CastSpell,
                                  // not turn actions: answered/allowed at any time (logged like actions)
                                  ReactionDecision, Heal, AbilityCheck,
                                  // turn action (appended): a content feature (Second Wind, ...)
                                  UseFeature,
                                  // appended: Font of Magic, Flexible Casting (bonus action; ActionRequest::flexible)
                                  FlexibleCasting,
                                  // appended: Petrifying Gaze turn-start response (ActionRequest::avert_gaze / gaze_seen)
                                  GazeResponse,
                                  // appended (R-G12 rules PR 1): end the actor's own transform (Wild Shape: bonus action; shapechanger: action)
                                  Revert,
                                  // appended (R-G12 rules PR 2): command summons (ActionRequest::command / command_ids)
                                  CommandSummons,
                                  // appended (R-G12 rules PR 3): the bonus-action attack of the actor's Spiritual Weapon (spell_id)
                                  SummonWeaponAttack,
                                  // appended (7.2y Fast Hands): Use an Object, a Dexterity (Sleight of Hand) check, or thieves' tools
                                  // (disarm a trap / open a lock): ActionRequest::object_use 1 / 2 / 3, with `dc` for 2 and 3. An
                                  // action, or the bonus action when bonus_action_grants has it (Fast Hands).
                                  UseObject };
constexpr uint32_t bit(ActionKind k) { return uint32_t(1) << uint32_t(k); }

// Reaction windows (encounter.hpp). Append-only.
// HitRider (row 9c): the attacker is the reactor; opened after a hit for each `choice: ask` trigger (Divine Smite).
// Damaged (row 11): damage to the reactor has been computed (after mitigation) but is not committed yet, so a
// reduce_damage reaction can act; AllyAttacked: an attack roll against an ally of the reactor is about to be made.
// AttackRolled (appended, R-G7): an attack roll was made, before hit or miss is decided (Cutting Words).
enum class ReactionTrigger : uint8_t { LeavingReach, BeingHit, SpellCast, ReadiedTrigger, HitRider, Damaged, AllyAttacked, AttackRolled };
inline constexpr std::size_t kReactionTriggerCount = 8;
enum class ReactionPolicy : uint8_t { Ask, Never, Auto };

struct DeathSaves { uint8_t successes = 0, failures = 0; bool stable = false; };

// A declared multiattack option (SRD "Multiattack"; also Thirsting Blade, ...): an ordered list of
// indices into Combatant::attacks, all made with ONE Attack action. A creature may declare several
// (a dragon's "Bite, Claw, Claw" vs "Tail"). Each attack resolves as its own step, so reaction windows,
// Ready triggers and movement can happen between attacks (see ActionRequest::multiattack).
struct AttackSequence {
    std::string_view id;                  // option id; storage owned by the caller (like Attack::id)
    std::vector<uint8_t> attacks;         // indices into Combatant::attacks, in order (1..255)
    // Appended (batch-3, violet fungus "makes 1d4 Rotting Touch attacks"): count > 0 = the option names ONE
    // attack, made this many times; the count is rolled from the encounter RNG when the multiattack starts.
    DiceExpr count_dice{0, 0, 0};
    // Appended (batch-3, blink dog Teleport: "Before or after teleporting, the dog can make one bite attack."): nonzero = this
    // sequence is not a Multiattack option; it is armed (sequence_pos 0) when the creature uses the non-spell action with this
    // SpellDef id, so its attack(s) follow that action without spending another (teleport-then-bite; "before" is deferred).
    uint32_t after_action = 0;
};
// The attack index the multiattack in progress requires next, or -1 (none in progress / sequence done).
inline int multiattack_next(const std::vector<AttackSequence>& ms, uint8_t sequence, uint8_t pos) {
    if (sequence == 0 || sequence > ms.size()) return -1;
    const AttackSequence& m = ms[std::size_t(sequence - 1)];
    if (m.attacks.empty()) return -1;
    if (m.after_action != 0) return pos < m.attacks.size() ? m.attacks[pos] : -1;
    if (m.count_dice.count > 0) return m.attacks[0];   // attacks_remaining bounds a rolled count
    return pos < m.attacks.size() ? m.attacks[pos] : -1;
}

// SRD "Regeneration" (troll, vampire spawn): regains `amount` HP at the start of its turn unless it took damage of
// a `suppressed_by` type since its last turn started (troll: acid, fire; vampire spawn: radiant). A creature with
// amount > 0 that is reduced to 0 HP is not dead until its next turn starts without regenerating (SRD troll:
// "dies only if it starts its turn with 0 hit points and doesn't regenerate").
struct Regeneration { int16_t amount = 0; uint32_t suppressed_by = 0; };   // suppressed_by: bitmask over DamageType
// Conditional advantage/disadvantage trait (T&C-G7); semantics and examples in traits.hpp.
enum class TraitRoll : uint8_t {
    Attack,       // this creature's own attack rolls
    AttackedBy,   // attack rolls made AGAINST this creature
    Save,         // this creature's saving throws
};

enum TraitWhen : uint16_t {
    kWhenAllyNearTarget  = 1,   // Attack: caller fact "an ally of the attacker is within 5 ft of the target, not incapacitated"
    kWhenTargetDamaged   = 2,   // Attack: the target is below its maximum hit points
    kWhenMeleeAttack     = 4,   // Attack / AttackedBy: the attack is a melee attack (not Attack::ranged)
    kWhenRangedAttack    = 8,   // Attack / AttackedBy: the attack is ranged
    kWhenSourceMagical   = 16,  // Save: the effect is a spell or other magical effect
};

struct RollTrait {
    TraitRoll roll = TraitRoll::Attack;
    RollMode  mode = RollMode::Advantage;   // Advantage or Disadvantage (Normal = no effect)
    uint16_t  when = 0;                     // TraitWhen bits, all must hold
    uint32_t  target_conditions = 0;        // Attack: target must have at least one of these Conditions (bitmask); 0 = any
    uint32_t  target_types = 0;             // Attack: target CreatureType must be in this mask; 0 = any
    uint8_t   save_abilities = 0;           // Save: only these abilities (bitmask over Ability); 0 = any
    uint32_t  save_vs_conditions = 0;       // Save: the effect imposes one of these Conditions ...
    uint32_t  save_vs_damage_types = 0;     //       ... or deals one of these damage types (either listed set matches); both 0 = any
};

// One innate spell (SRD "Innate Spellcasting"): cast without a slot (and without components unless
// `as_listed`), at the spell's own level. uses_max 0 = at will; recharge_min > 0 = "Recharge N-6" (one use,
// uses_left 0 = expended, recharged by a d6 at the start of the owner's turn); otherwise per-day (uses_max).
struct InnateSpell {
    uint32_t spell_id = 0;
    uint8_t uses_max = 0;
    uint8_t uses_left = 0;
    uint8_t recharge_min = 0;
    // appended: the innate block's own spellcasting ability (monsters often use Cha innately and Int for slots),
    // and whether the spell's listed components are required (content `innate.components: "as_listed"`).
    bool has_ability = false; Ability ability = Ability::Cha;
    bool as_listed = false;
    constexpr bool at_will() const { return uses_max == 0 && recharge_min == 0; }
    constexpr bool available() const { return at_will() || uses_left > 0; }
};

// ---- Triggers (version-bump batch, row 9; docs/proposals/VERSION_BUMP_BATCH.md section 3) ----
// An effect that happens on an event (Sneak Attack, Rampage, Undead Fortitude, Divine Smite, ...). Rows 9a-9c
// evaluate every TriggerOn; `choice: ask` is offered only for hit/crit triggers (the HitRider window). Other
// ask triggers and any effect kind that makes no sense for the event never fire.
enum class TriggerOn : uint8_t { Hit, Crit, DamageDealt, ReduceToZero, DropToZero, Death, TurnStart, TurnEnd,
                                 // appended (R-G11): FeatureTriggered.b of a zone trigger (never a Combatant::triggers row)
                                 ZoneAppear, ZoneEnter, ZoneStartTurn, ZoneEndTurn, ZoneMoveWithin };
enum class TriggerLimit : uint8_t { None, OncePerTurn, OncePerRound, OncePerAttack };
enum class PredicateKind : uint8_t {
    Weapon,                 // a: WeaponIs; false for spell attacks
    AdvantageOrAllyAdjacent,// the attack had advantage, or an ally fact and no disadvantage
    TargetCreatureType,     // mask: bitmask over CreatureType; the target is one of them
    NotCreatureType,        // mask: the target is none of them
    DamageTypeIs,           // mask: bitmask over DamageType; the attack's damage type is one of them
    DamageTypeNot,
    NotCrit,
    MovedAtLeastFt,         // a: feet. TurnBudget::moved_ft of the owner's own current turn (Q4: no path direction)
    SelfHpBelow,            // a/b = num/den: hp * den < hp_max * num
    Save,                   // the owner rolls a save: a = Ability, b = DC (mask bit 0: dc_from_damage, DC = b + damage taken)
    AttackIs,               // appended (sim loader, `on_hit[]` riders): a = index into the owner's `attacks`; false for spell attacks
    // appended (EC monster predicates: bugbear, boar, hobgoblin; flagged rules5e edit, VERSION_BUMP_BATCH hand-off pending):
    TargetSurprised,        // round 1 and the subject (the creature hit) is in the surprised set (Combatant::surprised_at_start): Surprise Attack
    AllyNearTarget,         // the sim fact ActionRequest::ally_near_target (no disadvantage clause): Martial Advantage
    DamageAtMost,           // a: the event's damage (TrigCtx dmg, after resistances) <= a; drop_to_zero / damage_dealt: Relentless
    WeaponAttack,           // appended (Director #158): a weapon attack (melee or ranged); false for spell attacks and no attack
    // appended (EC deferred monsters, flagged): mask = spell_id_of(tag); the subject's Combatant::tags has / lacks it (ghoul: elves)
    TargetTag, NotTargetTag,
    // appended (7.2u spell riders): SubjectIsCaster = the subject is the creature that applied the modifier (only meaningful
    // in a modifier's `when`; false elsewhere); OwnerCreatureType = mask over CreatureType, the owner is one of them.
    SubjectIsCaster, OwnerCreatureType,
    // appended (EC loader temp HP, flagged): the subject is on another team than the owner (a hostile creature): Dark One's Blessing
    SubjectHostile,
    // appended (7.2y, T&C Colossus Slayer): the subject (the creature hit) is below its hit point maximum. Hit triggers run
    // before the hit's damage, so this reads the hp the target had when it was hit.
    TargetBelowMaxHp,
    // appended (7.2zb, T&C Open Hand Technique): the hit comes from the compiled bonus attack whose BonusAttack::id is `mask`
    // (Flurry of Blows: spell_id_of("<archetype>#bonus#flurry_of_blows")). False for every other attack and outside hits.
    AttackFromFeature,
};
enum class WeaponIs : uint8_t { Melee, Ranged, Finesse, FinesseOrRanged };
struct Predicate { PredicateKind kind = PredicateKind::NotCrit; int16_t a = 0, b = 0; uint32_t mask = 0; };
enum class RiderKind : uint8_t { ExtraDamage, SaveCondition, ApplyCondition, HealSelf, TempHp, SetHp, BonusAttack, AreaSave, Spend,
                                Effect /* 7.2zb: any synthetic SpellDef `spell_id`, resolved with its extra effects (forced_move, apply_modifier riders) */ };
struct RiderEffect {
    RiderKind kind = RiderKind::ExtraDamage;
    DiceExpr dice{0, 0, 0};               // ExtraDamage: final dice (the loader resolves `scale.per_levels` for the owner's level)
    DamageType type = DamageType::Force;  // ExtraDamage: type, unless same_as_weapon
    bool same_as_weapon = false;          // ExtraDamage: use the attack's damage type
    int16_t value = 0;                    // SetHp / amount rider kinds (9b)
    uint32_t spell_id = 0;                // SaveCondition / AreaSave: synthetic save spell (9b/9c)
    uint32_t pool = 0;                    // Spend: pool id (slot == 0)
    // 9c. Spend: slot 0 = pool `pool`, cost `value` (0 = 1); 1..9 = that slot level; 10 = any slot level (1..9).
    uint8_t slot = 0;
    // ExtraDamage scale.per_slot_level: `slot_dice` per slot level above `slot_base` (Divine Smite: 1d8, base 1).
    DiceExpr slot_dice{0, 0, 0};
    uint8_t slot_base = 0;
    // appended (7.2y `bonus_vs`): ExtraDamage: + vs_dice when the target's creature type is in vs_types (Divine Smite +1d8 vs
    // undead / fiend).
    uint32_t vs_types = 0; DiceExpr vs_dice{0, 0, 0};
};
struct Trigger {
    uint32_t id = 0;                      // spell_id_of("<archetype>#<owner>#<index>"); the trigger_id of FeatureTriggered
    TriggerOn on = TriggerOn::Hit;
    TriggerLimit limit = TriggerLimit::None;
    uint8_t choice = 0;                   // 0 auto (fires), 1 ask (hit/crit triggers: HitRider window, 9c), 2 never
    std::vector<Predicate> when;          // all must hold
    std::vector<RiderEffect> effects;
    // appended (EC deferred monsters, flagged): usage recharge N-6. A registered ActionUse keyed (owner, id) gates it: firing
    // expends it, the owner's turn start rolls a d6 like recharge actions (RechargeRolled). 0 = no recharge.
    uint8_t recharge_min = 0;
    // appended (7.2zb, Open Hand Technique): with choice ask (hit triggers), the HitRider window offers one option per
    // non-Spend effect and only the chosen one applies (ReactionOption::slot_level = 1 + effect index). Auto picks the first.
    bool pick_one = false;
};

// A mechanical reaction compiled from content (row 11): offered in the Damaged / AllyAttacked windows of its owner.
enum class ReactionEffectKind : uint8_t { ReduceDamage, ImposeDisadvantage, Retaliate, Attack, SubtractRoll /* R-G7, AttackRolled */ };
struct Reaction {
    uint32_t id = 0;                         // spell_id_of("<archetype>#<reaction id>")
    ReactionTrigger window = ReactionTrigger::Damaged;   // Damaged or AllyAttacked
    ReactionEffectKind kind = ReactionEffectKind::ReduceDamage;
    bool melee_only = false, ranged_only = false;        // window filter on the attack that caused it
    bool half = false;                       // ReduceDamage: halve the damage (else roll `dice`)
    DiceExpr dice{0, 0, 0};                  // ReduceDamage: amount to subtract (Deflect Missiles: 1d10 + bonus)
    uint32_t spell_id = 0;                   // Retaliate: synthetic Save / AutoHit SpellDef resolved against the damager
    uint8_t attack_index = 0;                // Attack: index into Combatant::attacks (Riposte)
    // appended (R-G7): SubtractRoll rolls `dice` and subtracts it from an enemy's attack roll (Cutting Words: the bardic
    // inspiration die). Offered only while `pool` (0 = none) can pay `pool_cost` (0 = 1); spent when taken (ResourceSpent).
    uint32_t pool = 0; uint16_t pool_cost = 0;
    // appended (7.2y Uncanny Dodge): offered only when the reactor can see the damager (rules5e::sight; no sight hook = sees).
    // A `half` ReduceDamage is offered only for damage from an attack hit (never a save effect such as Fireball).
    bool requires_sight = false;
    // appended (7.2za): SubtractRoll: the roll kinds (kRoll* bits) of the creature's roll it may answer; 0 = attack only (R-G7).
    uint8_t applies = 0;
    // appended (7.2zb Deflect Missiles): ReduceDamage only for damage from an attack hit of these kinds (kAttackKind* bits);
    // 0 = any damage, as before.
    uint8_t attack_kinds = 0;
};
constexpr uint8_t kAttackKindMeleeWeapon = 1, kAttackKindRangedWeapon = 2, kAttackKindMeleeSpell = 4, kAttackKindRangedSpell = 8;
// appended (7.2zb, T&C Patient Defense / Step of the Wind): a bonus-action grant that costs pool points. Combatant::bonus_action_costs:
// taking `kind` as a bonus action needs `cost` from `pool` and spends it (ResourceSpent). The grant bit is in bonus_action_grants.
struct BonusActionCost { ActionKind kind = ActionKind::Dash; uint32_t pool = 0; uint16_t cost = 1; };
// appended (7.2zb, T&C Agonizing / Repelling Blast): a per-spell modification for each attack hit (beam) of spell `spell_id`:
// add the modifier of `damage_ability` (0xFF none) to its damage, push the target `push_ft` (0 none) away (forced movement).
struct SpellMod { uint32_t spell_id = 0; uint8_t damage_ability = 0xFF; uint16_t push_ft = 0; };

// ---- Timed modifiers (version-bump batch, row 12; docs/proposals/VERSION_BUMP_BATCH.md section 6) ----
// A sourced, timed change to one number or roll. Integers only. `bonus_table` is resolved by the loader into `bonus`.
enum class ModStat : uint8_t { AttackRoll, Save, AbilityCheck, Ac, Speed, Damage, TempHp, Resistance, AttacksAgainst,
                               // appended (small leftovers): markers read by rules, no number of their own.
                               // Haste: the restricted extra action (TurnBudget::haste_action), lethargy when it ends;
                               // Slow: no reactions, action or bonus action, one attack per turn;
                               // MaxHp: event-only stat value for Life Drain (ModifierApplied b = MaxHp, c = -reduction).
                               Haste, Slow, MaxHp,
                               // appended (7.2u spell riders): markers. NoReactions: the target can't take reactions (Shocking
                               // Grasp); NoHealing: the target can't regain hit points (Chill Touch; temp HP unaffected).
                               NoReactions, NoHealing,
                               // appended (7.2za Bardic Inspiration): one held die (`die`) the owner may add to one roll of a kind in
                               // ModifierSpec::held_applies, after the roll and before the outcome (ReactionOptionKind::AddHeldDie).
                               // Spent (ModifierExpired reason 9) or lapsed at expiry; at most one per creature (a new one replaces it).
                               HeldDie,
                               // appended (7.2zg Faerie Fire `outlined`, a state): attack rolls against the creature have
                               // advantage when the attacker can see it, and it gains no benefit from being invisible.
                               Outlined,
                               // appended (7.2zi): BonusDash: the creature may take Dash as a bonus action (Expeditious Retreat).
                               // HalveWeaponDamage: weapon attacks using scope_ability (1 Str) deal half damage, round down.
                               // ShareDamage: when this creature takes damage, the caster takes the same amount (Warding Bond).
                               BonusDash, HalveWeaponDamage, ShareDamage };
// appended (7.2za): roll kinds for HeldDie and SubtractRoll (`applies_to` in content: attack, save, check, damage).
inline constexpr uint8_t kRollAttack = 1, kRollSave = 2, kRollCheck = 4, kRollDamage = 8;
enum class ModAppliesTo : uint8_t { Self, Target, AlliesWithin30,
                                    // appended (7.2zd Protection from Evil and Good): an AttackRoll row on the target that applies to
                                    // the attack rolls of creatures attacking it (ModifierSpec::from_types filters their type).
                                    Attackers };
// 7.2zg `exempt_senses` (Blur, Mirror Image; one key, one list): an attacker that perceives the creature through one of these
// senses (within that sense's range), or cannot see it at all, is not fooled by the illusion.
enum : uint8_t { kSenseBlindsight = 1, kSenseTremorsense = 2, kSenseTruesight = 4 };

struct ModifierSpec {
    uint32_t id = 0;                     // stable id of this modifier (the a of ModifierApplied / ModifierExpired)
    uint32_t source = 0;                 // spell / feature id (concentration ends modifiers of the source)
    ModStat stat = ModStat::AttackRoll;
    int16_t bonus = 0;                   // flat; Ac / TempHp use only this
    DiceExpr die{0, 0, 0};               // rolled at the moment of the roll (Bless 1d4); one rng draw per die, only if present
    int8_t die_mult = 1;                 // +1 / -1 (Bane)
    RollMode mode = RollMode::Normal;    // Advantage / Disadvantage (AttackRoll, Save, AbilityCheck, AttacksAgainst)
    uint8_t speed_num = 1, speed_den = 1;// Speed: speed * num / den (half 1/2, double 2/1)
    DamageType damage_type = DamageType::Force;
    bool has_damage_type = false;        // Resistance: required; Damage: the type of the extra damage (else the weapon's)
    Ability ability = Ability::Str;
    bool has_ability = false;            // Save / AbilityCheck scope (else all)
    uint8_t scope = 0;                   // Damage: 0 any weapon attack, 1 melee only (Rage), 2 ranged only
    ModAppliesTo applies_to = ModAppliesTo::Target;
    uint8_t expiry = 0;                  // 0 none (until rest / concentration), 1 rounds, 2 end of the target's next turn,
                                         // 3 end of the caster's next turn, 4 start of the target's next turn (7.2u)
    uint16_t rounds = 0;                 // expiry == 1
    bool until_caster_turn = false;      // ends at the start of the caster's next turn (Shield-like durations)
    bool concentration = false;          // ends when the caster's concentration on `source` ends
    bool stack = false;                  // false: replaces a modifier with the same id on the same target
    // ---- Row 12 follow-ups (appended) ----
    // Consumed early: 0 never; 1 after the target's next attack roll (True Strike); 2 after the next attack roll made against the
    // target (Guiding Bolt); 3 when the target takes damage ("until damaged"). Combines with any expiry: the first one wins.
    uint8_t consume = 0;
    uint8_t speed_sub = 0;               // Speed: feet subtracted after the num/den scaling (Ray of Frost 10)
    int16_t ac_floor = 0;                // Ac: AC is at least this (+ Dex mod with ac_floor_dex), before bonuses (Barkskin 16, Mage Armor 13 + Dex); 0 = none
    bool ac_floor_dex = false;
    bool per_turn = false;               // TempHp: granted again at the start of each of the target's turns while the modifier lasts (Heroism)
    // General `when` scope for AttackRoll, AttacksAgainst, Ac (evaluated before the roll, crit unknown) and Damage (crit and the
    // damage type known): every predicate must hold, owner = the creature the modifier is on, subject = the other party. Predicate
    // kinds that roll (Save) never hold here.
    std::vector<Predicate> when;
    // appended (Slow): the target repeats `repeat_ability` save vs `repeat_dc` at the end of each of its turns (repeat_save 1);
    // a success ends every modifier of this source and caster on it (ModifierExpired reason 6). rules fills repeat_dc from the
    // caster's spell save DC when a spell applies it.
    uint8_t repeat_save = 0;
    Ability repeat_ability = Ability::Wis;
    int16_t repeat_dc = 0;
    // appended (7.2y `scope: {weapon: "wielded"}`; Sacred Weapon): 1 = bound to the weapon wielded when the feature is used
    // (ActionRequest::attack_index on the UseFeature; ActiveModifier::weapon): AttackRoll / Damage apply only to attacks with
    // it, and the modifier ends (ModifierExpired reason 7) when the weapon leaves the hand (the owner drops, dies, falls
    // unconscious, or is commanded to Drop).
    uint8_t weapon_scope = 0;
    // appended (7.2z barbarian hooks; every row of one feature effect carries the same values, rules act on the source):
    // scope_ability: AttackRoll / Damage only for an attack using 1 Str or 2 Dex (ranged: Dex; finesse: the higher; else Str).
    // ends_early: bit 0 at the end of the owner's turn unless it attacked a hostile or took damage since its last turn,
    // bit 1 when it falls unconscious. blocks: bit 0 casting spells is refused, bit 1 concentration breaks when it starts.
    // on_end_exhaustion: exhaustion levels gained when the source ends (with a requires_effect bonus attack: only if used).
    uint8_t scope_ability = 0;
    uint8_t ends_early = 0;
    uint8_t blocks = 0;
    uint8_t on_end_exhaustion = 0;
    // appended (7.2za): HeldDie: the roll kinds (kRoll* bits) the die may be added to. range_ft: Target only, the target must be
    // an ally other than the user, within this range (EncounterHooks::targets_within; no hook = in range) and not deafened.
    uint8_t held_applies = 0;
    uint16_t range_ft = 0;
    // appended (7.2zc generic mark; Hunter's Mark, Hex): mark_scope 1 = `scope: {target: "marked"}`: the row follows the creature
    // the caster marked at cast (ActiveModifier::mark, the spell's one target). Damage rows sit on the caster and add to its
    // attack hits (weapon or spell) on the marked creature only; check rows sit on the caster (applies_to self) or on the marked
    // creature (applies_to target). chosen_ability: AbilityCheck `ability: "chosen"`: the ability is the caster's cast-time
    // choice (ActionRequest::mark_ability), written into `ability` / `has_ability` of the applied row.
    uint8_t mark_scope = 0;
    bool chosen_ability = false;
    // appended (7.2zd `scope: {from_types: [...]}`; Protection from Evil and Good): bitmask over CreatureType of the attackers an
    // `applies_to: attackers` row applies to (0 = any attacker). Derived from content: hashed with the row's spec bytes only via
    // modifiers_digest sub-tag 0x5B when non-zero.
    uint32_t from_types = 0;
    // appended (7.2ze Bless / Bane, `applies_to: [attack, save]`): kRollAttack / kRollSave bits of the rolls the row applies to
    // besides its own stat (0 = its stat only). Hashed via modifiers_digest sub-tag 0x5C only when non-zero.
    uint8_t also_rolls = 0;
    // appended (7.2zg Blur): AttacksAgainst only: kSense* bits; an attacker not fooled (fooled_by_illusion) ignores the row.
    // Hashed in modifiers_digest (sub-byte 0x5E) only when non-zero.
    uint8_t exempt_senses = 0;
    // appended (7.2zi; modifiers_digest sub-byte 0x5F, only when any of these or ActiveModifier::zi_armed is set — not a
    // SIM_EVENTS registry tag). on_next_turn: the row does nothing until the caster's next turn starts (True Strike).
    // save_vs_conditions: a Save mode applies only when the save's SaveSource::conditions overlaps this mask (0 = any save).
    // death_save_adv: advantage on death saving throws. heal_maximum: healing dice are the maximum, and draw no rng.
    // resist_all: Resistance to every damage type (no has_damage_type). end_beyond_ft: end every modifier of this source and
    // caster when the two are farther apart than this (0 = never; needs EncounterHooks::sight_facts). end_caster_at_zero:
    // end that source when the caster drops to 0 HP (SRD Warding Bond: the caster, not the warded creature).
    uint8_t on_next_turn = 0;
    uint32_t save_vs_conditions = 0;
    uint8_t death_save_adv = 0;
    uint8_t heal_maximum = 0;
    uint8_t resist_all = 0;
    uint16_t end_beyond_ft = 0;
    uint8_t end_caster_at_zero = 0;
};

// Row R-G6: a bonus-action attack option (two-weapon fighting, Martial Arts, Flurry of Blows, Frenzy). Requested with
// ActionRequest{Attack, as_bonus_action, bonus_option = 1 + index into Combatant::bonus_attacks}.
struct BonusAttack {
    uint32_t id = 0;                    // content id (opaque to rules)
    uint8_t attack_index = 0;           // Combatant::attacks index the attack uses
    bool any_melee = false;             // Frenzy: any melee attack of the combatant (attack_index is ignored)
    uint8_t count = 1;                  // attacks one use of the bonus action makes (Flurry of Blows: 2)
    uint32_t requires_attacks = 0;      // bitmask over attack indices: the Attack action this turn must have used one (0 = none)
    bool different_weapon = false;      // the one used must not be attack_index itself (two-weapon fighting)
    uint32_t requires_modifier = 0;     // a modifier with this id must be in force on the attacker (Frenzy: the Rage modifier; 0 = none)
    uint32_t pool = 0; uint16_t pool_cost = 0;   // spend from this pool when the bonus action is spent (Flurry: ki, 1); 0 cost = 1
    bool has_damage = false;            // the off-hand attack rolls `damage` instead of the attack's dice
    DiceExpr damage{0, 0, 0};           //   (two-weapon fighting without the style: the dice without the ability modifier)
};

// R-G12 transforms (appended; docs/proposals/SUMMONS_TRANSFORMS.md, VERSION_BUMP_BATCH 7.2q). FormKind: what a creature is
// transformed by. FormStats: the statistics a form replaces; sim's EncounterHooks::build_form fills one from content (the beast's
// monster stat block, or a shapechanger trait's form), and rules saves the creature's own values in the same shape
// (Combatant::base) while the form lasts. Rules owns which fields each kind replaces and which it keeps (apply_form).
enum class FormKind : uint8_t { None = 0, Polymorph = 1, WildShape = 2, Shapechanger = 3 };
struct FormStats {
    AbilityScores abilities{};
    int16_t hp = 0, hp_max = 0;          // build_form: the form's hit points (fixed content HP; hp is ignored: a fresh pool)
    int8_t ac = 10;
    int8_t proficiency_bonus = 2;
    uint8_t speed_ft = 30;
    uint32_t save_proficiencies = 0, skill_proficiencies = 0;
    uint32_t resistances = 0, immunities = 0, vulnerabilities = 0, condition_immunities = 0;
    QualifiedDamageMask resist_qualified{}, immune_qualified{};
    std::vector<Attack> attacks{};       // natural weapons; gear melds into Polymorph / Wild Shape forms (no gear AC or attacks)
    std::vector<AttackSequence> multiattacks{};
    uint8_t attacks_per_action = 1;
    Size size = Size::Medium;
    CreatureType creature_type = CreatureType::Beast;
    std::vector<RollTrait> roll_traits{};
    std::vector<Trigger> triggers{};
    std::vector<Reaction> reactions{};
    std::vector<BonusAttack> bonus_attacks{};
    // appended (SRD 5.1 movement modes, feat/move-modes): the form's fly / swim / climb speeds and hover.
    uint8_t fly_ft = 0, swim_ft = 0, climb_ft = 0;
    bool hover = false;
    // appended (batch 3 Strength Drain): the reduction of these scores (Combatant::ability_reduction); own_stats / put_stats
    // carry it with the block, so a form starts unreduced and the true form's reduction returns with it on revert.
    std::array<uint8_t, 6> ability_reduction{};
    // Wild Shape (D7): the creature's own features (feature ids) the form is not physically capable of using (content:
    // requires_humanoid_form). UseFeature refuses them while the form lasts. Not part of the saved base block.
    std::vector<uint32_t> blocked_features{};
};

// 7.2z Danger Sense: advantage on `ability` saves against effects the creature can see (source_seen: the effect's creature is
// seen, `sight`; no known source counts as seen), not while it has any of `not_conditions` (bitmask over Condition).
struct SaveAdvantage { Ability ability = Ability::Dex; bool source_seen = true; uint32_t not_conditions = 0; };

struct Combatant {
    CombatantId id;
    uint8_t  team;
    AbilityScores abilities;
    int16_t  hp, hp_max, temp_hp;
    int8_t   ac;
    int8_t   proficiency_bonus;
    uint8_t  speed_ft;
    uint32_t save_proficiencies;                   // bitmask over Ability
    uint32_t conditions;                           // bitmask over Condition
    uint8_t  exhaustion_level;
    uint32_t resistances, immunities, vulnerabilities;  // bitmask over DamageType
    std::vector<Attack> attacks;

    // ---- Encounter-tracked state (appended; all default-initialised) ----
    bool     uses_death_saves = false;      // PCs: true. Monsters die at 0 HP (SRD "Monsters and Death").
    DeathSaves death{};
    bool     dead = false;
    uint32_t bonus_action_grants = 0;       // bitmask over ActionKind usable as a bonus action
    bool     reaction_used = false;         // resets at the start of this combatant's turn
    bool     dodging = false;               // Dodge action, until the start of its next turn
    bool     disengaged = false;            // Disengage, for the rest of the turn
    // appended (PR: encounter end / healing / actions):
    bool     is_pc = false;                 // counted by the default end condition
    uint32_t skill_proficiencies = 0;       // bitmask over Skill
    uint8_t  attacks_per_action = 1;        // Extra Attack: 2 (Fighter 5), ...
    std::vector<AttackSequence> multiattacks;   // declared multiattack options (empty = none); see AttackSequence
    int16_t  stealth_total = 0;             // > 0 while hidden (Hide result); sim compares to perception
    CombatantId helped_vs = 0;              // Help: advantage on next attack vs this target
    CombatantId helped_by = 0;              //   ... granted by this helper (expires at its next turn)
    bool     has_readied = false;           // Ready: response stored in the encounter
    CombatantId check_helped_by = 0;        // Help: advantage on next ability check, from this helper
    std::array<ReactionPolicy, kReactionTriggerCount> reaction_policy{};  // per trigger; default Ask
    int8_t   reaction_ac_bonus = 0;         // Shield-style AC bump; until the start of this creature's turn
    // appended (spellcasting core, spells.hpp):
    Ability  spellcasting_ability = Ability::Int;
    uint8_t  caster_level = 0;              // character level for cantrip scaling (monsters: caster level)
    std::array<uint8_t, 10> spell_slots{};      // [1..9] remaining ([0] unused)
    std::array<uint8_t, 10> spell_slots_max{};  // [1..9] restored on a long rest
    uint8_t  pact_slot_level = 0, pact_slots = 0, pact_slots_max = 0;  // Pact Magic: short rest
    std::vector<uint32_t> spells_known;     // spell ids (spell_id_of) it can cast (known or prepared)
    uint32_t concentration_spell = 0;       // spell id concentrated on (0 = none)
    uint32_t reaction_blocks_spell = 0;     // Shield: no damage from this spell until the start of its turn
    CreatureType creature_type = CreatureType::Humanoid;   // spell targeting (Hold Person, Cure Wounds)
    // appended: condition immunities (bitmask over Condition). add_condition() refuses an immune condition.
    uint32_t condition_immunities = 0;
    // appended (damage qualifiers): resistance / immunity that applies only to nonmagical sources.
    QualifiedDamageMask resist_qualified{}, immune_qualified{};
    // appended (regeneration): config + encounter state (suppressed: took a suppressing type since its last turn
    // began; down: at 0 HP waiting for its next turn start instead of dying).
    Regeneration regeneration{};
    bool regen_suppressed = false, regen_down = false;
    std::vector<RollTrait> roll_traits;   // appended: conditional advantage / disadvantage (Pack Tactics, Magic Resistance, ...)
    Size size = Size::Medium;   // appended: Heavy weapons impose disadvantage on Small / Tiny wielders
    // appended (version-bump batch, row 8): innate spellcasting and printed DC / attack overrides.
    std::vector<InnateSpell> innate;   // castable via ActionRequest::innate; uses start full each encounter
    int16_t spell_dc_override = 0, spell_attack_override = 0;   // 0 = derive (8 + prof + mod / prof + mod)
    // appended (row 9): event triggers. `trigger_*` is the once-per-turn / once-per-round bookkeeping (a bit per
    // index in `triggers`, valid only while the stamp matches), kept on the owner because a turn limit also covers
    // reactions on other creatures' turns.
    std::vector<Trigger> triggers;
    std::vector<Reaction> reactions;
    std::vector<BonusAttack> bonus_attacks;   // R-G6: bonus-action attack options; empty for old content
    uint32_t legendary_pool = 0;           // row 13: pool id Legendary Resistance spends from (0 = none); a failed save succeeds instead
    uint32_t mod_resist = 0;               // row 12: damage types resisted through active modifiers (cache, rules-maintained)       // row 11: mechanical reactions (Damaged / AllyAttacked windows)
    uint32_t trigger_turn_mask = 0, trigger_turn_stamp = 0;       // stamp = round << 16 | turn_index
    uint32_t trigger_round_mask = 0; uint16_t trigger_round_stamp = 0;
    bool death_triggers_done = false;   // 9b: this creature's death triggers have been processed
    // R-G5 static feature kinds (appended). Weapon attacks only (never spell attacks): a flat bonus to the attack roll by
    // melee / ranged (Archery +2 ranged), and the lowest natural d20 that is a critical hit when the attack hits (Improved
    // Critical 19, Superior Critical 18; 20 = default). A natural 20 always hits and crits; a lower crit roll must still hit.
    int8_t attack_bonus_melee = 0, attack_bonus_ranged = 0;
    int8_t crit_min = 20;
    // R-G7 (appended). reroll_ones: bitmask 1 attack roll, 2 ability check, 4 saving throw: a natural 1 is rerolled once and the
    // new roll is used (Halfling Lucky; with advantage / disadvantage the die showing 1 is rerolled). great_weapon_fighting:
    // damage dice of a melee weapon attack with a two-handed weapon (or a versatile one held in two hands, a caller fact) showing
    // 1 or 2 are rerolled once. evasion: a Dex save against an effect that deals half on a success deals none on a success and
    // half on a failure. Each draws extra rng only when it applies.
    uint8_t reroll_ones = 0;
    bool great_weapon_fighting = false;
    bool evasion = false;
    uint16_t died_round = 0;   // R-G15 (appended): EncounterState::round when the creature died (0 = alive / unknown); rules-maintained
    // R-G16 (appended). surprised: set by the caller before begin_encounter (or apply_surprise); while set the creature can
    // only end its turn (no move, action, bonus action or other request) and cannot react; rules clear it when its first turn ends.
    bool surprised = false;
    // appended (EC monster predicates #158, flagged): in the encounter's surprised set (apply_surprise); never cleared, not
    // hashed (replay input like the set itself). Read by PredicateKind::TargetSurprised together with round == 1.
    bool surprised_at_start = false;
    // Hit Dice (rest.hpp): die size (6, 8, 10, 12; 0 = none), remaining and maximum (usually the character level).
    uint8_t hit_die = 0, hit_dice = 0, hit_dice_max = 0;
    // R-G14 (appended): Metamagic options known (bitmask, kMeta* in spells.hpp) paid from pool `sorcery_pool` (sorcery points);
    // sculpt_spells: Evocation Wizard's Sculpt Spells (ActionRequest::spared on an evocation spell).
    uint8_t metamagic_known = 0;
    uint32_t sorcery_pool = 0;
    bool sculpt_spells = false;
    // Small leftovers (appended). hp_max_reduction: Life Drain, subtracted by effective_hp_max, cleared by long_rest; a creature
    // whose maximum reaches 0 dies. lethargy: Haste ended (1 = pending, 2 = this turn: no move, action or bonus action; cleared
    // when that turn ends). reckless_attack: Barbarian Reckless Attack known; reckless_active: declared this turn (advantage on
    // its Str melee weapon attacks; attacks against it have advantage until its next turn starts). aggressive: orc Aggressive
    // (bonus-action Dash toward a hostile; sim keeps the movement toward one).
    int16_t hp_max_reduction = 0;
    uint8_t lethargy = 0;
    bool reckless_attack = false, reckless_active = false;
    bool aggressive = false;
    // R-G11 rules PR 2 (appended), content facts for zone rows: no_breath (doesn't need to breathe: Stinking Cloud's automatic
    // success), shapechanger (Moonbeam's disadvantage), evil (an evil caster's Spirit Guardians deals necrotic damage).
    bool no_breath = false, shapechanger = false, evil = false;
    // Petrifying Gaze (appended, VERSION_BUMP_BATCH 7.2p). gazes: this creature's gaze features; each names a synthetic Save
    // SpellDef (is_spell false, fixed_dc, condition + repeat save + escalates / escalate_to / escalate_margin) and a range that
    // only sim reads. gaze_averted: it averted its eyes at its turn start; until the start of its next turn it treats every
    // gazer as unseen (its attacks against a gazer have disadvantage, a gazer's attacks against it advantage).
    struct Gaze { uint32_t spell_id = 0; uint16_t range_ft = 0; };
    std::vector<Gaze> gazes{};
    bool gaze_averted = false;
    // R-G12 transforms (appended, VERSION_BUMP_BATCH 7.2q). form: None = its own statistics. base: its own statistics, saved
    // when the form began (hp / hp_max = its normal-form hit points; temp HP stay on the creature, D8). form_source: the spell
    // (Polymorph) or feature (Wild Shape, shapechanger) id; form_caster: Polymorph's caster (0 otherwise); form_archetype:
    // ActionRequest::form_archetype (1 + index); form_end_round: the form ends at the start of the creature's turn in this
    // round (0 = no timer: Polymorph ends with concentration). form_blocked_features: the form's FormStats::blocked_features.
    FormKind form = FormKind::None;
    uint32_t form_source = 0;
    CombatantId form_caster = 0;
    uint8_t form_archetype = 0;
    uint32_t form_end_round = 0;
    std::optional<FormStats> base{};
    std::vector<uint32_t> form_blocked_features{};
    // appended (EC deferred monsters, flagged): content tags (spell_id_of of monster `tags[]` and a character's race, plus
    // "elf" for elf races), read only by PredicateKind::TargetTag / NotTargetTag. Content-derived: not hashed.
    std::vector<uint32_t> tags{};
    // appended (EC swarm, flagged): SRD swarm "can't regain hit points or gain temporary hit points". apply_healing heals 0
    // and every temp HP grant is skipped. Content-derived (static per archetype): not hashed.
    bool no_regain_hp = false;
    // R-G12 summons (appended, VERSION_BUMP_BATCH 7.2r). summoner: who summoned it (0 = not a summon); summon_source: the spell id;
    // summon_group: initiative group (the first id of its cast); summon_control: SummonControl; summon_vanishes: disappears at
    // 0 HP; summon_concentration: ends with the summoner's concentration on summon_source; commanded: this turn's command (0
    // none, 1 act, 2 defend; reset when its turn ends); gone: ended (SummonEnded): dead, skipped in the turn order, never acts again.
    CombatantId summoner = 0;
    uint32_t summon_source = 0, summon_group = 0;
    uint8_t summon_control = 0;
    bool summon_vanishes = false, summon_concentration = false;
    uint8_t commanded = 0;
    bool gone = false;
    // appended (R-G12 rules PR 3, D6): Pact of the Chain: an Attack can trade one attack for the familiar's reaction attack
    // (ActionRequest::familiar_id). Content-derived: not hashed.
    bool pact_of_chain = false;
    // appended (SRD 5.1 Squeezing, feat/size-space; sim fold Combat::hash 0xC1): it occupies a space one size smaller. Its
    // attack rolls and Dexterity saving throws have disadvantage, attack rolls against it have advantage. Set / cleared by a
    // Move (ActionRequest::squeeze); sim shrinks the footprint and doubles the step cost.
    bool squeezing = false;
    // appended (SRD 5.1 movement modes, feat/move-modes; static content, not folded): fly / swim / climb
    // speeds (0 = none) and hover. A Move with ActionRequest::move_mode uses that speed: SRD "switching speeds" - the
    // feet already spent this turn count against it (TurnBudget::movement_ft is shared; see move_allowance).
    uint8_t fly_ft = 0, swim_ft = 0, climb_ft = 0;
    bool hover = false;
    // appended (grab and contain, 7.2w; content-derived, not hashed): SRD Transparent (gelatinous cube): the DC of the Wisdom
    // (Perception) check to spot it while it has neither moved nor attacked (15; 0 = not transparent). Sim reads it when it
    // sets up surprise (rules5e surprise helper: compare with the observer's Perception); rules does not track "moved".
    int8_t transparent_dc = 0;
    // appended (7.2w; content-derived, not hashed): SRD "formless" (the Net has no effect on it). Content tag "formless".
    bool formless = false;

    // appended (vision 7.2x; static content, not folded): senses, in feet (0 = none). Rules derive can_see / perceives from
    // these, Blinded / Deafened and sim's per-pair SightFacts (per-tile light from any source; LIGHTING.md).
    uint16_t darkvision_ft = 0, blindsight_ft = 0, tremorsense_ft = 0, truesight_ft = 0;
    bool sunlight_sensitive = false;   // SRD Sunlight Sensitivity (drow, kobold...)
    std::vector<SaveAdvantage> save_advantages;   // appended (7.2z, static content, not folded): Danger Sense
    // appended (7.2za, static content, not folded): Jack of All Trades. Bit 0: half the proficiency bonus (rounded down) on
    // ability checks without proficiency; bit 1: on initiative.
    uint8_t half_proficiency = 0;
    bool thieves_tools = false;        // appended (7.2y): proficient with thieves' tools (UseObject 3 adds the proficiency bonus)
    constexpr bool has(Condition c) const { return (conditions & bit(c)) != 0; }
    // appended (control conditions, 7.2v): challenge rating * 8 (CR 1/2 = 4) for Destroy Undead; -1 = unknown (never destroyed).
    int16_t cr8 = -1;
    // appended (7.2zb, static content, not folded): costed bonus-action grants and per-spell modifications (see the structs).
    std::vector<BonusActionCost> bonus_action_costs;
    std::vector<SpellMod> spell_mods;
    // appended (7.2zc Disciple of Life, static content, not folded): every creature a Heal effect of a spell cast with a slot of
    // level 1 or higher heals regains heal_bonus_flat more, plus the slot level when heal_bonus_per_level. Riders, pools and
    // feature heals do not count. has_heal_bonus = the feature is present (flat may be 0).
    bool has_heal_bonus = false;
    int8_t heal_bonus_flat = 0;
    bool heal_bonus_per_level = false;
    // appended (batch 3, shadow Strength Drain; sim Combat::hash 0xDF while any is non-zero): how much each score in `abilities`
    // is currently reduced by. `abilities` holds the EFFECTIVE score (every check, save, modifier and Str requirement reads it);
    // the unreduced score is abilities + ability_reduction. Saturates at 127. A short or long rest restores it (rest.cpp).
    std::array<uint8_t, 6> ability_reduction{};
};

void add_condition(Combatant&, Condition);
void remove_condition(Combatant&, Condition);

} // namespace rules5e
