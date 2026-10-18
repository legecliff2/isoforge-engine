#pragma once
#include "rng/rng.hpp"
#include "rules5e/abilities.hpp"
#include "rules5e/d20.hpp"
#include "rules5e/types.hpp"
#include "rules5e/traits.hpp"

namespace rules5e {

struct SaveResult { D20Roll roll; bool success; bool auto_fail; };

// d20 + ability modifier + proficiency bonus (if proficient) vs DC; success if total >= DC.
// Roll mode = `mode` merged with save_roll_mode (Restrained/Dodge/exhaustion).
// Auto-fail (Paralyzed/Petrified/Stunned/Unconscious on Str/Dex): no dice are drawn,
// roll is zeroed, success = false. Natural 20/1 have no special meaning on saves (SRD 5.1).
SaveResult saving_throw(rng::Rng&, const Combatant&, Ability, int dc, RollMode mode = RollMode::Normal);

// Same, with the effect's source so Combatant::roll_traits (Magic Resistance, Fey Ancestry, ...) apply.
SaveResult saving_throw(rng::Rng&, const Combatant&, Ability, int dc, RollMode mode, const SaveSource&);

// Ability modifier + proficiency bonus if proficient in that save.
constexpr int save_modifier(const Combatant& c, Ability a) {
    return modifier(c.abilities, a) + ((c.save_proficiencies & bit(a)) ? c.proficiency_bonus : 0);
}

// ---- Death saving throws (SRD 5.1 "Death Saving Throws") ----
enum class DeathSaveOutcome : uint8_t { Pending, Stabilized, Died, RegainedHp };
struct DeathSaveResult { int8_t natural; DeathSaves after; DeathSaveOutcome outcome; };

// Pure: d20, no modifiers. 10+ success, else failure; nat 1 = two failures; nat 20 = regain
// 1 HP (counters reset). 3 successes -> stable (counters reset); 3 failures -> dead.
DeathSaveResult roll_death_save(rng::Rng&, const DeathSaves&);

// Damage taken while at 0 HP: one failure, two on a critical hit. Ends stability.
DeathSaves death_save_failures_from_damage(const DeathSaves&, bool critical);

} // namespace rules5e

