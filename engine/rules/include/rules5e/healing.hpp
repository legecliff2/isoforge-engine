#pragma once
#include "rules5e/types.hpp"

namespace rules5e {

struct HealingApplied {
    int16_t healed;        // HP actually restored
    int16_t hp_after;      // capped at effective_hp_max (exhaustion 4+ halves it)
    bool    from_zero;     // was at 0 HP: death saves reset, Unconscious ends (SRD "Healing")
};

// Pure. Dead creatures and amount <= 0 heal nothing.
HealingApplied apply_healing(const Combatant&, int16_t amount);

// Writes hp; if from_zero: resets death saves and removes Unconscious (Prone stays).
void commit_healing(Combatant&, const HealingApplied&);

} // namespace rules5e
