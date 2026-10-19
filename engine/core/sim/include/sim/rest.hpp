#pragma once
// R-G16 rests (VERSION_BUMP_BATCH 7.2h hand-off (c)): sim's between-encounter rest entry points. Rests run outside any
// encounter on a roster the caller keeps (the combatants and their feature / pool / recharge-action tables, e.g. a copy of
// Combat::state() after the fight). They emit no events, write no command log and touch no Combat, so they never change
// Combat::hash, Combat::log_hash or any golden.
//
// RNG: Hit Dice draw only from the dedicated rest stream (make_rest_rng), never from an encounter's rng (Pcg32(seed, 0) in
// rules5e::begin_encounter), so resting can never perturb combat rolls. Long rests draw nothing.
#include <cstdint>

#include "rng/rng.hpp"
#include "rules5e/encounter.hpp"
#include "rules5e/rest.hpp"

namespace eng::sim {

inline constexpr int64_t kRestSeedKey = 0x52455354;   // "REST": derive_seed key of the rest stream
inline constexpr uint64_t kRestStream = 2;            // Pcg32 stream id (encounters use 0, Luau policies 1)

// The rest stream for a campaign / party seed: Pcg32(derive_seed(seed, {kRestSeedKey}), kRestStream). Keep it with the
// roster and pass it to every short rest; its position is the caller's state.
rng::Rng make_rest_rng(uint64_t seed) noexcept;

// The tables of `roster` (features, pools, recharge actions) as rules5e::RestTables.
rules5e::RestTables rest_tables(rules5e::EncounterState& roster) noexcept;

// Short / long rest of one creature of `roster` (rules5e::short_rest / long_rest with the roster's tables; only that
// creature's rows change). ok == false for an unknown id, or when rules refuse the rest (dead; long rest below 1 HP).
rules5e::RestResult short_rest(rules5e::EncounterState& roster, rules5e::CombatantId id, rng::Rng& rest_rng,
                               uint8_t hit_dice_to_spend);
rules5e::RestResult long_rest(rules5e::EncounterState& roster, rules5e::CombatantId id, bool food_and_drink = true);

}  // namespace eng::sim
