#pragma once
// Encounter state machine (ARCHITECTURE.md §5.3). Pure; the engine drives it.
// Positions/distances live in `sim`; requests carry the facts rules needs (e.g. within 5 ft).
#include <optional>
#include <string_view>
#include <vector>
#include "rng/rng.hpp"
#include "rules5e/attack.hpp"
#include "rules5e/initiative.hpp"
#include "rules5e/saves.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

struct TurnBudget { bool action = true, bonus_action = true, reaction = true; int16_t movement_ft = 0; };

struct ActionRequest {
    CombatantId actor; ActionKind kind; CombatantId target = 0;
    uint8_t attack_index = 0; int16_t move_ft = 0;
    // appended:
    bool as_bonus_action = false;      // Dash/Disengage/Hide via bonus_action_grants
    bool target_within_5ft = false;    // supplied by sim
};

enum class EventKind : uint8_t {
    InitiativeRolled, TurnStarted, AttackResolved, DamageDealt, ConditionAdded, CombatantDowned,
    DeathSaveRolled, Stabilized, RegainedHp, CombatantDied, Moved, Dashed, Disengaged, Dodged,
    StoodUp, TurnEnded, EncounterEnded };

// Flat tagged event; meaning of a/b/c per kind:
//   InitiativeRolled a=total; TurnStarted a=round; AttackResolved a=natural b=total c=hit|crit<<1;
//   DamageDealt a=dealt b=hp_after; ConditionAdded a=Condition; DeathSaveRolled a=natural
//   b=successes c=failures; Moved a=feet b=movement left; Dashed a=movement left;
//   EncounterEnded a=winning team.
struct Event { EventKind kind; CombatantId actor = 0, target = 0; int32_t a = 0, b = 0, c = 0; };

struct EncounterState {
    std::vector<Combatant>       combatants;
    std::vector<InitiativeEntry> order;
    uint16_t round = 1; uint16_t turn_index = 0;
    TurnBudget budget;
    rng::Rng rng{0, 0};                  // owned -> state fully serializable & replayable
    bool ended = false;
};

struct StepResult { bool ok; std::string_view error; std::vector<Event> events; };

// Rolls initiative from Pcg32(seed, 0) and starts the first turn. Events from that start are
// returned through `opening_events` when non-null.
EncounterState begin_encounter(std::vector<Combatant>, uint64_t seed, std::vector<Event>* opening_events = nullptr);

// Validates and applies one action. Invalid requests change nothing (ok = false).
StepResult step(EncounterState&, const ActionRequest&);

std::vector<ActionRequest> legal_actions(const EncounterState&);   // for AI + UI (current actor)
std::optional<uint8_t>     winner_team(const EncounterState&);
CombatantId                current_actor(const EncounterState&);
const Combatant*           find(const EncounterState&, CombatantId);

} // namespace rules5e
