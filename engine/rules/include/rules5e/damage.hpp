#pragma once
#include "rules5e/types.hpp"

namespace rules5e {

// Result of applying damage. Pure: computed from the combatant, never written to it.
struct DamageApplied {
    int16_t dealt;            // after immunity/resistance/vulnerability, before temp HP
    int16_t temp_hp_after;
    int16_t hp_after;
    bool dropped_to_zero;     // was above 0 HP, now at 0
    bool instant_death;       // SRD massive damage: damage left over after reaching 0 HP >= HP max
                              // (also when already at 0 HP and the damage >= HP max)
};

// SRD 5.1 order: immunity -> 0; resistance halves (round down; Petrified = resist all);
// then vulnerability doubles. Temporary HP absorbs first. HP never goes below 0.
// amount < 0 is treated as 0. Does not touch conditions, death saves, or `dead`;
// the encounter step (or caller) decides what dropping to 0 means.
DamageApplied apply_damage(const Combatant&, int16_t amount, DamageType);

// Writes hp/temp_hp from a result. Convenience for callers; nothing else changes.
void commit_damage(Combatant&, const DamageApplied&);

} // namespace rules5e
