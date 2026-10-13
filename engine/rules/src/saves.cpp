#include "rules5e/saves.hpp"

#include "rules5e/conditions.hpp"

namespace rules5e {

SaveResult saving_throw(rng::Rng& r, const Combatant& c, Ability a, int dc, RollMode mode) {
    RollMode m = save_roll_mode(c, a, mode);
    if (auto_fails_save(c, a)) return {D20Roll{0, 0, 0, m}, false, true};
    D20Roll roll = roll_d20(r, save_modifier(c, a), m);
    return {roll, roll.total >= dc, false};
}

DeathSaveResult roll_death_save(rng::Rng& r, const DeathSaves& in) {
    DeathSaves s = in;
    int nat = r.d(20);
    if (nat == 20) return {int8_t(nat), DeathSaves{}, DeathSaveOutcome::RegainedHp};
    if (nat == 1) s.failures = uint8_t(s.failures + 2);
    else if (nat >= 10) s.successes = uint8_t(s.successes + 1);
    else s.failures = uint8_t(s.failures + 1);
    if (s.failures >= 3) { s.failures = 3; return {int8_t(nat), s, DeathSaveOutcome::Died}; }
    if (s.successes >= 3) return {int8_t(nat), DeathSaves{0, 0, true}, DeathSaveOutcome::Stabilized};
    return {int8_t(nat), s, DeathSaveOutcome::Pending};
}

DeathSaves death_save_failures_from_damage(const DeathSaves& in, bool critical) {
    DeathSaves s = in;
    s.stable = false;
    int f = s.failures + (critical ? 2 : 1);
    s.failures = uint8_t(f > 3 ? 3 : f);
    return s;
}

} // namespace rules5e
