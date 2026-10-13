#pragma once
#include "rules5e/types.hpp"

namespace rules5e {

struct DamageApplied { int16_t dealt; int16_t hp_after; bool dropped_to_zero, killed_outright; };

// SRD 5.1 order: immunity -> 0; resistance halves (round down); then vulnerability doubles.
// Temporary HP absorbs first. HP never goes below 0. killed_outright = massive damage
// (damage remaining after reaching 0 HP >= hp_max). Conditions (e.g. Unconscious) and death
// saves are left to the caller/encounter step. amount < 0 is treated as 0.
DamageApplied apply_damage(Combatant&, int16_t amount, DamageType);

} // namespace rules5e
