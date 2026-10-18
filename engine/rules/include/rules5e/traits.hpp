#pragma once
// Conditional advantage / disadvantage traits (T&C-G7, the roll-mode part of the generic effect set).
// A RollTrait is data: "this creature has <mode> on <roll> when <predicate>". Examples:
//   Pack Tactics     {Attack, Advantage, when = kWhenAllyNearTarget}
//   Blood Frenzy     {Attack, Advantage, when = kWhenTargetDamaged}
//   Reckless (monster) two traits: {Attack, Advantage, kWhenMeleeAttack} and {AttackedBy, Advantage, kWhenMeleeAttack}
//                    (the "until the start of its next turn" gate is the sim/AI adding/removing the traits)
//   Magic Resistance {Save, Advantage, when = kWhenSourceMagical}
//   Gnome Cunning    {Save, Advantage, when = kWhenSourceMagical, save_abilities = Int|Wis|Cha}
//   Fey Ancestry     {Save, Advantage, save_vs_conditions = Charmed}
//   Brave            {Save, Advantage, save_vs_conditions = Frightened}
//   Dwarven Resilience {Save, Advantage, save_vs_conditions = Poisoned, save_vs_damage_types = Poison}
//   Sunlight Sensitivity (if a lighting model ever exists) {Attack, Disadvantage, when = <sim fact>}
// Predicates are ANDed. The facts rules cannot know (an ally next to the target) are supplied by the caller
// (AttackContext::ally_near_target / ActionRequest::ally_near_target); everything else is read from state.
// Any advantage source plus any disadvantage source cancels to a normal roll, as everywhere else.
#include <cstdint>
#include "rules5e/d20.hpp"
#include "rules5e/types.hpp"

namespace rules5e {

// RollTrait / TraitRoll / TraitWhen live in types.hpp (Combatant::roll_traits).

// What a saving throw is against (supplied by the caller that knows the effect).
struct SaveSource {
    bool magical = false;            // a spell or magical effect
    uint32_t conditions = 0;         // Conditions the effect would impose (bitmask)
    uint32_t damage_types = 0;       // damage types the effect deals (bitmask)
};

// Does at least one trait of this kind match? Split by mode so callers can merge with other sources.
struct TraitModes { bool advantage = false, disadvantage = false; };
TraitModes attack_trait_modes(const Combatant& attacker, const Combatant& target, bool ally_near_target, bool ranged);
TraitModes save_trait_modes(const Combatant& c, Ability a, const SaveSource& src);

} // namespace rules5e
