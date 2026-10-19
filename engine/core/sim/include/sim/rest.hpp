#pragma once
// R-G16 rests (VERSION_BUMP_BATCH 7.2h hand-off (c)): sim's between-encounter rest entry points. Rests run outside any
// encounter on a roster the caller keeps (the combatants and their feature / pool / recharge-action tables, e.g. a copy of
// Combat::state() after the fight). They emit no events, write no command log and touch no Combat, so they never change
// Combat::hash, Combat::log_hash or any golden.
//
// RNG: Hit Dice draw only from the dedicated rest stream (make_rest_rng), never from an encounter's rng (Pcg32(seed, 0) in
// rules5e::begin_encounter), so resting can never perturb combat rolls. Long rests draw nothing.
#include <array>
#include <cstdint>
#include <optional>

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

// Arcane Recovery / Natural Recovery (VERSION_BUMP_BATCH 7.2n hand-off 1 - 3). Like the rests above: no events, no rng, no
// Combat, no hash tags.
struct Archetype;

// Registers the slot_recovery FeatureUses rows (rules5e::slot_recovery_uses) of `archetype` for creature `id` in the
// roster's features table, skipping a row that is already there (idempotent). Returns the number of rows added.
std::size_t register_slot_recovery(rules5e::EncounterState& roster, rules5e::CombatantId id, const Archetype& archetype);

// rules5e::apply_slot_recovery on creature `id` of `roster` with the caller's choice `add` ([1..9] slots per level).
// NoFeature for an unknown id. Only Ok changes the roster (slots added, one use spent).
rules5e::SlotRecoveryResult apply_slot_recovery(rules5e::EncounterState& roster, rules5e::CombatantId id,
                                                const rules5e::SlotRecoverySpec& spec, const std::array<uint8_t, 10>& add);

// A short rest followed by the slot-recovery choice. `recovery` is empty when the rest was refused (rest.ok == false:
// nothing is recovered) or no spec was given; otherwise it is apply_slot_recovery's result (a refusal leaves the rested
// roster as short_rest left it).
struct ShortRestRecovery {
    rules5e::RestResult rest;
    std::optional<rules5e::SlotRecoveryResult> recovery;
};
ShortRestRecovery short_rest_with_recovery(rules5e::EncounterState& roster, rules5e::CombatantId id, rng::Rng& rest_rng,
                                           uint8_t hit_dice_to_spend, const rules5e::SlotRecoverySpec* spec,
                                           const std::array<uint8_t, 10>& add);

}  // namespace eng::sim
