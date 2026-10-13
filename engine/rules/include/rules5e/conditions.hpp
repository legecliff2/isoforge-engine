#pragma once
// SRD 5.1 Appendix A (Conditions) as pure queries. Effects that need positions or a
// "source" (Charmed, Frightened line of sight, Grappled escape) are resolved by `sim`;
// see notes per function.
#include "rules5e/types.hpp"

namespace rules5e {

// Incapacitated, or a condition that includes it (Paralyzed, Petrified, Stunned, Unconscious):
// no actions, bonus actions, or reactions.
bool is_incapacitated(const Combatant&);

// Speed after conditions and exhaustion: 0 for Grappled, Restrained, Paralyzed, Petrified,
// Stunned, Unconscious, or exhaustion >= 5; halved (round down) at exhaustion >= 2.
int effective_speed(const Combatant&);

// Exhaustion level 4+: hit point maximum halved (round down).
int effective_hp_max(const Combatant&);

// Paralyzed, Petrified, Stunned, Unconscious: automatically fail Str and Dex saves.
bool auto_fails_save(const Combatant&, Ability);

// Roll mode contributions for the creature's own saving throws (before caller sources):
// Restrained -> disadvantage on Dex; Dodging -> advantage on Dex; exhaustion >= 3 -> disadvantage.
RollMode save_roll_mode(const Combatant&, Ability, RollMode extra = RollMode::Normal);

// Roll mode for ability checks: Poisoned, Frightened, exhaustion >= 1 -> disadvantage.
RollMode check_roll_mode(const Combatant&, RollMode extra = RollMode::Normal);

// Petrified: resistance to all damage. Merged into apply_damage.
uint32_t effective_resistances(const Combatant&);

// Exhaustion level 6: death.
constexpr bool dies_of_exhaustion(const Combatant& c) { return c.exhaustion_level >= 6; }

} // namespace rules5e
