#pragma once
#include <span>
#include <vector>
#include "rng/rng.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

struct InitiativeEntry { CombatantId id; int16_t total; int8_t dex_mod; };

// Each combatant rolls d20 + Dex modifier, in input order. Sorted by total (desc),
// then Dex mod (desc), then an rng tiebreak drawn after all rolls, then id (asc) for a
// total order. Deterministic given rng state and input order.
std::vector<InitiativeEntry> roll_initiative(rng::Rng&, std::span<const Combatant>);

} // namespace rules5e
