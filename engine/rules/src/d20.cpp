#include "rules5e/d20.hpp"

namespace rules5e {

D20Roll roll_d20(rng::Rng& r, int mod, RollMode mode) {
    int a = r.d(20);
    int nat = a, other = 0;
    if (mode != RollMode::Normal) {
        int b = r.d(20);
        bool adv = mode == RollMode::Advantage;
        nat = adv ? (a >= b ? a : b) : (a <= b ? a : b);
        other = adv ? (a >= b ? b : a) : (a <= b ? b : a);
    }
    return {int8_t(nat), int8_t(other), int16_t(nat + mod), mode};
}

bool reroll_natural_one(rng::Rng& r, D20Roll& x) {
    if (x.natural != 1) return false;
    const int d = r.d(20);
    int nat = d, other = x.natural_other;
    if (x.mode == RollMode::Advantage) { nat = d >= other ? d : other; other = d >= other ? other : d; }
    else if (x.mode == RollMode::Disadvantage) { nat = d <= other ? d : other; other = d <= other ? other : d; }
    x.total = int16_t(x.total + nat - x.natural);
    x.natural = int8_t(nat); x.natural_other = int8_t(other);
    return true;
}

} // namespace rules5e
