#include "rules5e/attack.hpp"

#include "rules5e/dice.hpp"

namespace rules5e {

RollMode attack_roll_mode(const Combatant& a, const Combatant& t, const AttackContext& ctx) {
    bool adv = ctx.mode == RollMode::Advantage;
    bool dis = ctx.mode == RollMode::Disadvantage;
    using C = Condition;
    if (a.has(C::Blinded) || a.has(C::Poisoned) || a.has(C::Prone) || a.has(C::Restrained)) dis = true;
    if (a.has(C::Invisible)) adv = true;
    if (t.has(C::Blinded) || t.has(C::Paralyzed) || t.has(C::Petrified) || t.has(C::Restrained) ||
        t.has(C::Stunned) || t.has(C::Unconscious)) adv = true;
    if (t.has(C::Invisible)) dis = true;
    if (t.has(C::Prone)) (ctx.target_within_5ft ? adv : dis) = true;
    return combine_modes(adv, dis);
}

AttackResult resolve_attack(rng::Rng& r, const Combatant& attacker, const Attack& atk,
                            const Combatant& target, const AttackContext& ctx) {
    RollMode mode = attack_roll_mode(attacker, target, ctx);
    D20Roll roll = roll_d20(r, atk.to_hit, mode);
    bool hit, crit = false;
    if (roll.natural == 20) { hit = true; crit = true; }
    else if (roll.natural == 1) { hit = false; }
    else hit = roll.total >= int(target.ac) + int(ctx.cover_ac_bonus);
    if (hit && ctx.target_within_5ft &&
        (target.has(Condition::Paralyzed) || target.has(Condition::Unconscious)))
        crit = true;
    int dmg = 0;
    if (hit) {
        dmg = roll_dice(r, atk.damage, crit);
        if (dmg < 0) dmg = 0;
    }
    return {roll, hit, crit, int16_t(dmg), atk.type};
}

} // namespace rules5e
