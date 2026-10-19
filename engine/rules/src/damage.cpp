#include "rules5e/damage.hpp"

#include "rules5e/conditions.hpp"

namespace rules5e {

// Condition immunity (stat block `condition_immunities`): the condition is never added.
void add_condition(Combatant& c, Condition k) {
    if (c.condition_immunities & bit(k)) return;
    c.conditions |= bit(k);
}
void remove_condition(Combatant& c, Condition k) { c.conditions &= ~bit(k); }

namespace {
// SRD order: immunity -> 0; resistance halves (round down); vulnerability doubles.
int modified(const Combatant& c, int16_t amount, DamageType type, uint8_t qualifiers) {
    int dmg = amount < 0 ? 0 : amount;
    uint32_t b = bit(type);
    if ((c.immunities & b) || qualified_applies(c.immune_qualified, type, qualifiers)) return 0;
    if ((effective_resistances(c) & b) || qualified_applies(c.resist_qualified, type, qualifiers))
        dmg /= 2;  // non-negative: truncation == round down
    if (c.vulnerabilities & b) dmg *= 2;
    return dmg;
}

DamageApplied absorb(const Combatant& c, int dmg) {
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
} // namespace

DamageApplied apply_damage(const Combatant& c, int16_t amount, DamageType type, uint8_t qualifiers) {
    const int m = modified(c, amount, type, qualifiers);
    DamageApplied d = absorb(c, m);
    if (m > 0) d.types_dealt = bit(type);
    return d;
}

DamageApplied apply_damage_parts(const Combatant& c, std::span<const DamagePiece> parts, uint8_t qualifiers) {
    int total = 0;
    uint32_t types = 0;
    for (const DamagePiece& p : parts) {
        const int m = modified(c, p.amount, p.type, qualifiers);
        total += m;
        if (m > 0) types |= bit(p.type);
    }
    DamageApplied d = absorb(c, total);
    d.types_dealt = types;
    return d;
}

DamageApplied reduce_applied(const Combatant& c, const DamageApplied& d, int reduction) {
    const int r = reduction < 0 ? 0 : (reduction > d.dealt ? int(d.dealt) : reduction);
    DamageApplied out = absorb(c, d.dealt - r);
    out.types_dealt = out.dealt > 0 ? d.types_dealt : 0u;
    return out;
}

void commit_damage(Combatant& c, const DamageApplied& d) {
    c.hp = d.hp_after;
    c.temp_hp = d.temp_hp_after;
}

} // namespace rules5e
