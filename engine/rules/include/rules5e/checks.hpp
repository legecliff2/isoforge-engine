#pragma once
#include "rng/rng.hpp"
#include "rules5e/abilities.hpp"
#include "rules5e/d20.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

struct CheckResult { D20Roll roll; bool success; };

// Ability modifier + proficiency bonus if proficient in the skill.
constexpr int skill_modifier(const Combatant& c, Skill k) {
    return modifier(c.abilities, skill_ability(k)) +
           ((c.skill_proficiencies & bit(k)) ? c.proficiency_bonus : 0);
}

// R-G16: passive score = 10 + skill modifier + `bonus` (the caller passes +5 for advantage, -5 for disadvantage, SRD 5.1).
constexpr int passive_score(const Combatant& c, Skill k, int bonus = 0) { return 10 + skill_modifier(c, k) + bonus; }
constexpr int passive_perception(const Combatant& c, int bonus = 0) { return passive_score(c, Skill::Perception, bonus); }

// Skill check vs DC (success if total >= DC). Mode merged with check_roll_mode
// (Poisoned/Frightened/exhaustion). Natural 20/1 have no special meaning (SRD 5.1).
CheckResult skill_check(rng::Rng&, const Combatant&, Skill, int dc, RollMode = RollMode::Normal);

} // namespace rules5e
