#include "rules5e/saves.hpp"

#include "rules5e/conditions.hpp"
#include "rules5e/traits.hpp"

namespace rules5e {

SaveResult saving_throw(rng::Rng& r, const Combatant& c, Ability a, int dc, RollMode mode) {
    RollMode m = save_roll_mode(c, a, mode);
    if (auto_fails_save(c, a)) return {D20Roll{0, 0, 0, m}, false, true};
    D20Roll roll = roll_d20(r, save_modifier(c, a), m);
    return {roll, roll.total >= dc, false};
}

TraitModes save_trait_modes(const Combatant& c, Ability a, const SaveSource& src) {
    TraitModes m;
    for (const auto& r : c.roll_traits) {
        if (r.roll != TraitRoll::Save) continue;
        if ((r.when & kWhenSourceMagical) && !src.magical) continue;
        if (r.save_abilities && !(r.save_abilities & bit(a))) continue;
        if (r.save_vs_conditions || r.save_vs_damage_types) {
            if (!((r.save_vs_conditions & src.conditions) || (r.save_vs_damage_types & src.damage_types))) continue;
        }
        if (r.mode == RollMode::Advantage) m.advantage = true; else if (r.mode == RollMode::Disadvantage) m.disadvantage = true;
    }
    return m;
}

SaveResult saving_throw(rng::Rng& r, const Combatant& c, Ability a, int dc, RollMode mode, const SaveSource& src) {
    const TraitModes tm = save_trait_modes(c, a, src);
    const bool adv = mode == RollMode::Advantage || tm.advantage, dis = mode == RollMode::Disadvantage || tm.disadvantage;
    return saving_throw(r, c, a, dc, combine_modes(adv, dis));
}

namespace {
DeathSaveResult death_save_from_natural(const DeathSaves& in, int nat) {
    DeathSaves s = in;
    if (nat == 20) return {int8_t(nat), DeathSaves{}, DeathSaveOutcome::RegainedHp};
    if (nat == 1) s.failures = uint8_t(s.failures + 2);
    else if (nat >= 10) s.successes = uint8_t(s.successes + 1);
    else s.failures = uint8_t(s.failures + 1);
    if (s.failures >= 3) { s.failures = 3; return {int8_t(nat), s, DeathSaveOutcome::Died}; }
    if (s.successes >= 3) return {int8_t(nat), DeathSaves{0, 0, true}, DeathSaveOutcome::Stabilized};
    return {int8_t(nat), s, DeathSaveOutcome::Pending};
}
}  // namespace

DeathSaveResult roll_death_save(rng::Rng& r, const DeathSaves& in) {
    return death_save_from_natural(in, r.d(20));
}

DeathSaveResult roll_death_save(rng::Rng& r, const DeathSaves& in, RollMode mode) {
    if (mode == RollMode::Normal) return roll_death_save(r, in);
    return death_save_from_natural(in, roll_d20(r, 0, mode).natural);
}

DeathSaves death_save_failures_from_damage(const DeathSaves& in, bool critical) {
    DeathSaves s = in;
    s.stable = false;
    int f = s.failures + (critical ? 2 : 1);
    s.failures = uint8_t(f > 3 ? 3 : f);
    return s;
}

} // namespace rules5e
