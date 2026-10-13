#include "rules5e/checks.hpp"

#include "rules5e/conditions.hpp"

namespace rules5e {

CheckResult skill_check(rng::Rng& r, const Combatant& c, Skill k, int dc, RollMode mode) {
    D20Roll roll = roll_d20(r, skill_modifier(c, k), check_roll_mode(c, mode));
    return {roll, roll.total >= dc};
}

} // namespace rules5e
