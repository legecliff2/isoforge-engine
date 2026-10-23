#pragma once
#include "rng/rng.hpp"
#include "rules5e/d20.hpp"
#include "rules5e/types.hpp"
#include "rules5e/traits.hpp"

namespace rules5e {

struct AttackContext {
    RollMode mode = RollMode::Normal;     // extra caller-supplied source; merged with conditions
    bool target_within_5ft = false;       // Prone / Paralyzed / Unconscious rules
    int8_t cover_ac_bonus = 0;            // +2 half, +5 three-quarters
    // appended: independent disadvantage source (ranged: long range / hostile within 5 ft). Kept
    // separate from `mode` so it is combined with every other source at once (any adv + any dis = normal).
    bool extra_disadvantage = false;
    // appended (T&C-G7): facts for Combatant::roll_traits. ally_near_target is supplied by sim (Pack Tactics);
    // `ranged` is set from Attack::ranged by do_attack / resolve_attack.
    bool ally_near_target = false;
    bool ranged = false;
    // appended (7.2zg `outlined`): the target / attacker is outlined, so its Invisible condition gives no benefit.
    bool target_outlined = false, attacker_outlined = false;
};

struct AttackResult { D20Roll roll; bool hit, critical; int16_t damage; DamageType type; uint8_t qualifiers = 0; };

// Combines ctx.mode with condition-based sources (SRD 5.1 Appendix A):
//   attacker Blinded/Frightened/Poisoned/Prone/Restrained or exhaustion >= 3 -> disadvantage;
//   target Dodging (and not incapacitated / speed 0) -> disadvantage;
//   attacker Invisible -> advantage;
//   target Blinded/Paralyzed/Petrified/Restrained/Stunned/Unconscious -> advantage;
//   target Invisible -> disadvantage; target Prone -> advantage within 5 ft, else disadvantage.
// Any advantage + any disadvantage -> Normal.
RollMode attack_roll_mode(const Combatant& attacker, const Combatant& target, const AttackContext&);

// Natural 20: always hits and is a critical hit. Natural 1: always misses.
// Otherwise hits if total >= target AC + cover. A hit on a Paralyzed or Unconscious target from
// within 5 ft is a critical hit. Damage = max(0, roll_dice(damage, critical)); 0 on a miss.
// Does not apply damage (see apply_damage).
AttackResult resolve_attack(rng::Rng&, const Combatant& attacker, const Attack&,
                            const Combatant& target, const AttackContext&);

} // namespace rules5e
