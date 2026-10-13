#include "rules5e/damage.hpp"

namespace rules5e {

void add_condition(Combatant& c, Condition k) { c.conditions |= bit(k); }
void remove_condition(Combatant& c, Condition k) { c.conditions &= ~bit(k); }

DamageApplied apply_damage(Combatant& c, int16_t amount, DamageType type) {
    int dmg = amount < 0 ? 0 : amount;
    uint32_t b = bit(type);
    if (c.immunities & b) dmg = 0;
    else {
        if (c.resistances & b) dmg /= 2;          // non-negative: truncation == round down
        if (c.vulnerabilities & b) dmg *= 2;
    }
    int dealt = dmg;
    int absorbed = dmg < c.temp_hp ? dmg : c.temp_hp;
    c.temp_hp = int16_t(c.temp_hp - absorbed);
    dmg -= absorbed;
    bool was_up = c.hp > 0;
    int hp = c.hp - dmg;
    int overflow = hp < 0 ? -hp : 0;
    if (hp < 0) hp = 0;
    c.hp = int16_t(hp);
    bool dropped = was_up && hp == 0;
    bool killed = hp == 0 && dmg > 0 && overflow >= c.hp_max;
    return {int16_t(dealt), c.hp, dropped, killed};
}

} // namespace rules5e
