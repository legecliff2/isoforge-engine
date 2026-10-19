#pragma once
// Core SRD 5.1 value types (ARCHITECTURE.md §5.3). Pure data; no engine headers.
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rules5e {

enum class Ability : uint8_t { Str, Dex, Con, Int, Wis, Cha };
enum class DamageType : uint8_t { Acid, Bludgeoning, Cold, Fire, Force, Lightning,
                                  Necrotic, Piercing, Poison, Psychic, Radiant, Slashing, Thunder };
enum class Condition : uint8_t { Blinded, Charmed, Deafened, Frightened, Grappled, Incapacitated,
                                 Invisible, Paralyzed, Petrified, Poisoned, Prone, Restrained,
                                 Stunned, Unconscious, Exhaustion };
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
struct DamagePart { DiceExpr dice; DamageType type; };

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
};
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
                                  UseFeature };
constexpr uint32_t bit(ActionKind k) { return uint32_t(1) << uint32_t(k); }

// Reaction windows (encounter.hpp). Append-only.
// HitRider (row 9c): the attacker is the reactor; opened after a hit for each `choice: ask` trigger (Divine Smite).
enum class ReactionTrigger : uint8_t { LeavingReach, BeingHit, SpellCast, ReadiedTrigger, HitRider };
inline constexpr std::size_t kReactionTriggerCount = 5;
enum class ReactionPolicy : uint8_t { Ask, Never, Auto };

struct DeathSaves { uint8_t successes = 0, failures = 0; bool stable = false; };

// A declared multiattack option (SRD "Multiattack"; also Thirsting Blade, ...): an ordered list of
// indices into Combatant::attacks, all made with ONE Attack action. A creature may declare several
// (a dragon's "Bite, Claw, Claw" vs "Tail"). Each attack resolves as its own step, so reaction windows,
// Ready triggers and movement can happen between attacks (see ActionRequest::multiattack).
struct AttackSequence {
    std::string_view id;                  // option id; storage owned by the caller (like Attack::id)
    std::vector<uint8_t> attacks;         // indices into Combatant::attacks, in order (1..255)
};

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
enum class TriggerOn : uint8_t { Hit, Crit, DamageDealt, ReduceToZero, DropToZero, Death, TurnStart, TurnEnd };
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
};
enum class WeaponIs : uint8_t { Melee, Ranged, Finesse, FinesseOrRanged };
struct Predicate { PredicateKind kind = PredicateKind::NotCrit; int16_t a = 0, b = 0; uint32_t mask = 0; };
enum class RiderKind : uint8_t { ExtraDamage, SaveCondition, ApplyCondition, HealSelf, TempHp, SetHp, BonusAttack, AreaSave, Spend };
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
};
struct Trigger {
    uint32_t id = 0;                      // spell_id_of("<archetype>#<owner>#<index>"); the trigger_id of FeatureTriggered
    TriggerOn on = TriggerOn::Hit;
    TriggerLimit limit = TriggerLimit::None;
    uint8_t choice = 0;                   // 0 auto (fires), 1 ask (hit/crit triggers: HitRider window, 9c), 2 never
    std::vector<Predicate> when;          // all must hold
    std::vector<RiderEffect> effects;
};

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
    uint32_t trigger_turn_mask = 0, trigger_turn_stamp = 0;       // stamp = round << 16 | turn_index
    uint32_t trigger_round_mask = 0; uint16_t trigger_round_stamp = 0;
    bool death_triggers_done = false;   // 9b: this creature's death triggers have been processed

    constexpr bool has(Condition c) const { return (conditions & bit(c)) != 0; }
};

void add_condition(Combatant&, Condition);
void remove_condition(Combatant&, Condition);

} // namespace rules5e
