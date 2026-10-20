#include "rules5e/healing.hpp"

#include "rules5e/conditions.hpp"

namespace rules5e {

HealingApplied apply_healing(const Combatant& c, int16_t amount) {
    if (c.dead || amount <= 0 || c.no_regain_hp) return {0, c.hp, false};   // EC swarm: no_regain_hp heals 0
    int cap = effective_hp_max(c);
    int hp = c.hp + amount;
    if (hp > cap) hp = cap;
    if (hp < c.hp) hp = c.hp;   // never lowers (hp above a reduced cap)
    return {int16_t(hp - c.hp), int16_t(hp), c.hp <= 0 && hp > 0};
}

void commit_healing(Combatant& c, const HealingApplied& h) {
    c.hp = h.hp_after;
    if (h.from_zero) {
        c.death = DeathSaves{};
        remove_condition(c, Condition::Unconscious);
    }
}

} // namespace rules5e
