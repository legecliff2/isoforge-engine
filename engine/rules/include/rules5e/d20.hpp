#pragma once
#include "rng/rng.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

// natural: the die that counts. natural_other: the discarded die (0 for Normal).
struct D20Roll { int8_t natural; int8_t natural_other; int16_t total; RollMode mode; };

// Draws one d20 (Normal) or two d20s in order (Advantage/Disadvantage).
D20Roll roll_d20(rng::Rng&, int modifier, RollMode);
// R-G7 (Lucky): a natural 1 is rerolled once (one more d20 draw, only then); the die showing 1 is replaced and the mode
// re-applied; total moves with the natural. Returns true if it rerolled.
bool reroll_natural_one(rng::Rng&, D20Roll&);

// SRD 5.1: any number of advantage and disadvantage sources cancel to Normal.
constexpr RollMode combine_modes(bool any_advantage, bool any_disadvantage) {
    if (any_advantage == any_disadvantage) return RollMode::Normal;
    return any_advantage ? RollMode::Advantage : RollMode::Disadvantage;
}

} // namespace rules5e
