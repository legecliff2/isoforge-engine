#pragma once
#include "rules5e/rules.hpp"

inline rules5e::Combatant make_combatant(rules5e::CombatantId id, int dex = 10, int ac = 12, int hp = 10) {
    rules5e::Combatant c{};
    c.id = id;
    c.abilities.score = {10, int8_t(dex), 10, 10, 10, 10};
    c.ac = int8_t(ac);
    c.hp = c.hp_max = int16_t(hp);
    return c;
}

// Finds a seed whose first d20 (stream 0) is `want`, so tests can force nat 1 / nat 20.
inline uint64_t seed_with_first_d20(int want) {
    for (uint64_t s = 0;; ++s) {
        rng::Rng r(s, 0);
        if (r.d(20) == want) return s;
    }
}
