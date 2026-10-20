#include <algorithm>
#include "rules5e/attack.hpp"

#include "rules5e/conditions.hpp"
#include "rules5e/abilities.hpp"
#include "rules5e/weapon.hpp"
#include "rules5e/dice.hpp"
#include "rules5e/traits.hpp"

namespace rules5e {

RollMode attack_roll_mode(const Combatant& a, const Combatant& t, const AttackContext& ctx) {
    bool adv = ctx.mode == RollMode::Advantage;
    bool dis = ctx.mode == RollMode::Disadvantage || ctx.extra_disadvantage;
    using C = Condition;
    if (a.has(C::Blinded) || a.has(C::Poisoned) || a.has(C::Prone) || a.has(C::Restrained) ||
        a.has(C::Frightened) || a.exhaustion_level >= 3) dis = true;
    if (a.has(C::Invisible)) adv = true;
    if (t.dodging && !is_incapacitated(t) && effective_speed(t) > 0) dis = true;  // Dodge
    if (t.has(C::Blinded) || t.has(C::Paralyzed) || t.has(C::Petrified) || t.has(C::Restrained) ||
        t.has(C::Stunned) || t.has(C::Unconscious)) adv = true;
    if (t.has(C::Invisible)) dis = true;
    const TraitModes tm = attack_trait_modes(a, t, ctx.ally_near_target, ctx.ranged);
    adv = adv || tm.advantage; dis = dis || tm.disadvantage;
    if (t.has(C::Prone)) (ctx.target_within_5ft ? adv : dis) = true;
    return combine_modes(adv, dis);
}

TraitModes attack_trait_modes(const Combatant& a, const Combatant& t, bool ally_near_target, bool ranged) {
    TraitModes m;
    auto test = [&](const RollTrait& r) {
        if ((r.when & kWhenAllyNearTarget) && !ally_near_target) return false;
        if ((r.when & kWhenTargetDamaged) && t.hp >= t.hp_max) return false;
        if ((r.when & kWhenMeleeAttack) && ranged) return false;
        if ((r.when & kWhenRangedAttack) && !ranged) return false;
        if ((r.when & kWhenSourceMagical)) return false;   // a save predicate; never true for attack rolls
        if (r.target_conditions && !(t.conditions & r.target_conditions)) return false;
        if (r.target_types && !(r.target_types & bit(t.creature_type))) return false;
        return true;
    };
    auto note = [&](const RollTrait& r) {
        if (r.mode == RollMode::Advantage) m.advantage = true; else if (r.mode == RollMode::Disadvantage) m.disadvantage = true;
    };
    for (const auto& r : a.roll_traits) if (r.roll == TraitRoll::Attack && test(r)) note(r);
    for (const auto& r : t.roll_traits) if (r.roll == TraitRoll::AttackedBy && test(r)) note(r);
    return m;
}

Ability attack_ability(const Combatant& c, const Attack& a) {
    if (is_finesse(a)) return modifier(c.abilities, Ability::Dex) > modifier(c.abilities, Ability::Str) ? Ability::Dex : Ability::Str;
    return a.ranged ? Ability::Dex : Ability::Str;
}

bool weapon_disadvantage(const Combatant& c, const Attack& a, bool within_5ft) {
    if (has_property(a, kWeaponHeavy) && (c.size == Size::Small || c.size == Size::Tiny)) return true;
    return has_property(a, kWeaponLance) && within_5ft;
}

AttackResult resolve_attack(rng::Rng& r, const Combatant& attacker, const Attack& atk,
                            const Combatant& target, const AttackContext& ctx) {
    AttackContext wctx = ctx;
    wctx.ranged = atk.ranged;
    wctx.extra_disadvantage = ctx.extra_disadvantage || weapon_disadvantage(attacker, atk, ctx.target_within_5ft);
    RollMode mode = attack_roll_mode(attacker, target, wctx);
    D20Roll roll = roll_d20(r, atk.to_hit + (atk.ranged ? attacker.attack_bonus_ranged : attacker.attack_bonus_melee), mode);
    bool hit, crit = false;
    if (roll.natural == 20) { hit = true; crit = true; }
    else if (roll.natural == 1) { hit = false; }
    else {
        hit = roll.total >= int(target.ac) + int(ctx.cover_ac_bonus) + int(target.reaction_ac_bonus);
        crit = hit && roll.natural >= std::max(2, int(attacker.crit_min));
    }
    if (hit && ctx.target_within_5ft &&
        (target.has(Condition::Paralyzed) || target.has(Condition::Unconscious)))
        crit = true;
    int dmg = 0;
    if (hit) {
        // Uses base max HP, not exhaustion-reduced max (Director ruling).
        dmg = roll_dice(r, (atk.half_hp_damage.count > 0 && 2 * int(attacker.hp) <= int(attacker.hp_max)) ? atk.half_hp_damage : atk.damage, crit);   // EC swarm
        if (dmg < 0) dmg = 0;
    }
    return {roll, hit, crit, int16_t(dmg), atk.type, atk.qualifiers};
}

} // namespace rules5e
