#pragma once
#include <span>

#include "rules5e/types.hpp"

namespace rules5e {

// Result of applying damage. Pure: computed from the combatant, never written to it.
struct DamageApplied {
    int16_t dealt;            // after immunity/resistance/vulnerability, before temp HP
    int16_t temp_hp_after;
    int16_t hp_after;
    bool dropped_to_zero;     // was above 0 HP, now at 0
    bool instant_death;       // SRD massive damage: damage left over after reaching 0 HP >= HP max
                              // (also when already at 0 HP and the damage >= HP max)
    uint32_t types_dealt = 0; // bitmask over DamageType: components that were above 0 after mitigation (before temp HP)
};

// SRD 5.1 order: immunity -> 0; resistance halves (round down; Petrified = resist all);
// then vulnerability doubles. Temporary HP absorbs first. HP never goes below 0.
// amount < 0 is treated as 0. Does not touch conditions, death saves, or `dead`;
// the encounter step (or caller) decides what dropping to 0 means.
//
// `qualifiers` (AttackQualifier bits) selects qualified resistances/immunities (Combatant::resist_qualified /
// immune_qualified): they apply only when the source is nonmagical (and, for the silvered / adamantine
// variants, not silvered / not adamantine). The default 0 is a plain nonmagical source; spell damage
// passes kAttackMagical. A qualified immunity counts as immunity, a qualified resistance as resistance
// (resistance never stacks).
DamageApplied apply_damage(const Combatant&, int16_t amount, DamageType, uint8_t qualifiers = 0);

// True if a qualified mask entry is active against a source with these AttackQualifier bits.
constexpr bool qualified_applies(const QualifiedDamageMask& m, DamageType t, uint8_t qualifiers) {
    const uint32_t b = bit(t);
    if (qualifiers & kAttackMagical) return false;
    if (m.nonmagical & b) return true;
    if ((m.nonmagical_not_silvered & b) && !(qualifiers & kAttackSilvered)) return true;
    if ((m.nonmagical_not_adamantine & b) && !(qualifiers & kAttackAdamantine)) return true;
    return false;
}

// One damage component: raw amount (before immunity/resistance/vulnerability) of one type.
struct DamagePiece { int16_t amount; DamageType type; };

// Several components from ONE source (a multi-damage attack hit). Immunity/resistance/vulnerability
// are applied to each component by its own type; the modified amounts are summed and then absorbed
// by temp HP / HP as a single hit (one dropped_to_zero / instant_death outcome). `dealt` is the
// sum. A single piece equals apply_damage().
DamageApplied apply_damage_parts(const Combatant&, std::span<const DamagePiece>, uint8_t qualifiers = 0);

// Writes hp/temp_hp from a result. Convenience for callers; nothing else changes.
void commit_damage(Combatant&, const DamageApplied&);

} // namespace rules5e
