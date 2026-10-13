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
enum class RollMode : uint8_t { Normal, Advantage, Disadvantage };

using CombatantId = uint32_t;

constexpr uint32_t bit(Condition c) { return uint32_t(1) << uint32_t(c); }
constexpr uint32_t bit(DamageType t) { return uint32_t(1) << uint32_t(t); }
constexpr uint32_t bit(Ability a) { return uint32_t(1) << uint32_t(a); }

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
                                  StandUp, OpportunityAttack };
constexpr uint32_t bit(ActionKind k) { return uint32_t(1) << uint32_t(k); }

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

    constexpr bool has(Condition c) const { return (conditions & bit(c)) != 0; }
};

void add_condition(Combatant&, Condition);
void remove_condition(Combatant&, Condition);

} // namespace rules5e
