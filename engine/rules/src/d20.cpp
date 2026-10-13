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

} // namespace rules5e
