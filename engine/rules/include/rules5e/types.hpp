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

struct Attack {
    std::string_view id;
    int8_t   to_hit;                               // total bonus
    DiceExpr damage; DamageType type;
    uint8_t  reach_or_range_ft;
    bool     ranged, finesse;
};

enum class ActionKind : uint8_t { Attack, Dash, Disengage, Dodge, Help, Hide, Ready, Move, EndTurn,
                                  // appended (append-only):
                                  StandUp, OpportunityAttack, Stabilize, SpareTheDying,
                                  CastSpell,
                                  // not turn actions: answered/allowed at any time (logged like actions)
                                  ReactionDecision, Heal, AbilityCheck };
constexpr uint32_t bit(ActionKind k) { return uint32_t(1) << uint32_t(k); }

// Reaction windows (encounter.hpp). Append-only.
enum class ReactionTrigger : uint8_t { LeavingReach, BeingHit, SpellCast, ReadiedTrigger };
inline constexpr std::size_t kReactionTriggerCount = 4;
enum class ReactionPolicy : uint8_t { Ask, Never, Auto };

struct DeathSaves { uint8_t successes = 0, failures = 0; bool stable = false; };

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

    constexpr bool has(Condition c) const { return (conditions & bit(c)) != 0; }
};

void add_condition(Combatant&, Condition);
void remove_condition(Combatant&, Condition);

} // namespace rules5e
