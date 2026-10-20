#pragma once
// Font of Magic outside encounters (VERSION_BUMP_BATCH 7.2o hand-off): Flexible Casting on a kept roster (e.g. a copy of
// Combat::state() after the fight). Like sim/rest.hpp: no events, no rng, no Combat, no hash tags. In an encounter the same
// conversion is rules5e ActionKind::FlexibleCasting (a bonus action, log_hash 0xBA).
#include <cstdint>

#include "rules5e/encounter.hpp"
#include "rules5e/font_of_magic.hpp"

namespace eng::sim {

// rules5e::flexible_casting on creature `id` of `roster` against the roster's pools. flexible 1 = expend a slot of `level`
// for `level` points, 2 = spend flexible_casting_cost(level) points for a slot of `level`. NoPool for an unknown id. Only Ok
// changes the roster.
rules5e::FlexibleOutcome flexible_casting(rules5e::EncounterState& roster, rules5e::CombatantId id, uint8_t flexible,
                                          uint8_t level);

}  // namespace eng::sim
