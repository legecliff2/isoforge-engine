#include "rules5e/damage.hpp"

#include "rules5e/conditions.hpp"

namespace rules5e {

void add_condition(Combatant& c, Condition k) { c.conditions |= bit(k); }
void remove_condition(Combatant& c, Condition k) { c.conditions &= ~bit(k); }

DamageApplied apply_damage(const Combatant& c, int16_t amount, DamageType type) {
    int dmg = amount < 0 ? 0 : amount;
    uint32_t b = bit(type);
    if (c.immunities & b) dmg = 0;
    else {
        if (effective_resistances(c) & b) dmg /= 2;  // non-negative: truncation == round down
        if (c.vulnerabilities & b) dmg *= 2;
    }
    int dealt = dmg;
    int absorbed = dmg < c.temp_hp ? dmg : c.temp_hp;
    int temp_after = c.temp_hp - absorbed;
    dmg -= absorbed;
    int hp = c.hp - dmg;
    int leftover = hp < 0 ? -hp : 0;
    if (hp < 0) hp = 0;
    bool dropped = c.hp > 0 && hp == 0;
    bool at_zero_before = c.hp <= 0;
    bool instant = hp == 0 && dmg > 0 &&
                   (at_zero_before ? dmg >= c.hp_max : leftover >= c.hp_max);
    return {int16_t(dealt), int16_t(temp_after), int16_t(hp), dropped, instant};
}

void commit_damage(Combatant& c, const DamageApplied& d) {
    c.hp = d.hp_after;
    c.temp_hp = d.temp_hp_after;
}

} // namespace rules5e
