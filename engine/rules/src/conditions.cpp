#include "rules5e/conditions.hpp"

#include "rules5e/d20.hpp"

namespace rules5e {

using C = Condition;

bool is_incapacitated(const Combatant& c) {
    return c.has(C::Incapacitated) || c.has(C::Paralyzed) || c.has(C::Petrified) ||
           c.has(C::Stunned) || c.has(C::Unconscious) || c.dead;
}

int effective_speed(const Combatant& c) {
    if (c.dead || c.has(C::Grappled) || c.has(C::Restrained) || c.has(C::Paralyzed) ||
        c.has(C::Petrified) || c.has(C::Stunned) || c.has(C::Unconscious) || c.exhaustion_level >= 5)
        return 0;
    int s = c.speed_ft;
    if (c.exhaustion_level >= 2) s /= 2;
    return s;
}

int effective_hp_max(const Combatant& c) {
    return c.exhaustion_level >= 4 ? c.hp_max / 2 : c.hp_max;
}

bool auto_fails_save(const Combatant& c, Ability a) {
    if (a != Ability::Str && a != Ability::Dex) return false;
    return c.has(C::Paralyzed) || c.has(C::Petrified) || c.has(C::Stunned) || c.has(C::Unconscious);
}

RollMode save_roll_mode(const Combatant& c, Ability a, RollMode extra) {
    bool adv = extra == RollMode::Advantage, dis = extra == RollMode::Disadvantage;
    if (a == Ability::Dex && c.has(C::Restrained)) dis = true;
    if (a == Ability::Dex && c.dodging) adv = true;
    if (c.exhaustion_level >= 3) dis = true;
    return combine_modes(adv, dis);
}

RollMode check_roll_mode(const Combatant& c, RollMode extra) {
    bool adv = extra == RollMode::Advantage, dis = extra == RollMode::Disadvantage;
    if (c.has(C::Poisoned) || c.has(C::Frightened) || c.exhaustion_level >= 1) dis = true;
    return combine_modes(adv, dis);
}

uint32_t effective_resistances(const Combatant& c) {
    return c.has(C::Petrified) ? 0xFFFFFFFFu : c.resistances;
}

} // namespace rules5e
