#include "rules5e/rest.hpp"

#include <algorithm>
#include "rules5e/abilities.hpp"
#include "rules5e/conditions.hpp"
#include "rules5e/dice.hpp"
#include "rules5e/healing.hpp"
#include "rules5e/spells.hpp"

namespace rules5e {

HitDiceSpent spend_hit_dice(Combatant& c, rng::Rng& rng, uint8_t n) {
    HitDiceSpent out;
    if (c.dead || c.hit_die == 0) return out;
    const int con = modifier(c.abilities, Ability::Con);
    while (out.spent < n && c.hit_dice > 0 && c.hp < effective_hp_max(c)) {
        const int gain = std::max(0, roll_dice(rng, DiceExpr{1, c.hit_die, 0}) + con);
        c.hit_dice = uint8_t(c.hit_dice - 1);
        out.spent = uint8_t(out.spent + 1);
        out.gains.push_back(int16_t(gain));
        const HealingApplied h = apply_healing(c, int16_t(gain));
        commit_healing(c, h);
        out.healed = int16_t(out.healed + h.healed);
    }
    return out;
}

void recharge_tables(CombatantId actor, bool long_rest, const RestTables& t) {
    auto due = [&](FeatureRecharge r) { return r == FeatureRecharge::ShortRest || (long_rest && r == FeatureRecharge::LongRest); };
    if (t.features)
        for (auto& f : *t.features) if ((actor == 0 || f.actor == actor) && due(f.recharge)) f.used = 0;
    if (t.pools)
        for (auto& p : *t.pools) if ((actor == 0 || p.actor == actor) && due(p.recharge)) p.cur = p.max;
    if (t.action_uses)
        for (auto& u : *t.action_uses) if (actor == 0 || u.actor == actor) u.expended = false;
}

RestResult short_rest(Combatant& c, rng::Rng& rng, uint8_t hit_dice_to_spend, const RestTables& t) {
    RestResult r;
    if (c.dead) return r;
    r.ok = true;
    r.hit_dice = spend_hit_dice(c, rng, hit_dice_to_spend);
    recover_slots_short_rest(c);
    recharge_tables(c.id, false, t);
    return r;
}

RestResult long_rest(Combatant& c, const RestTables& t, bool food_and_drink) {
    RestResult r;
    if (c.dead || c.hp < 1) return r;
    r.ok = true;
    if (food_and_drink && c.exhaustion_level > 0) {
        c.exhaustion_level = uint8_t(c.exhaustion_level - 1);
        if (c.exhaustion_level == 0) remove_condition(c, Condition::Exhaustion);
        r.exhaustion_reduced = true;
    }
    c.hp_max_reduction = 0;   // Life Drain lasts until a long rest
    const int cap = effective_hp_max(c);
    if (c.hp < cap) { r.hp_regained = int16_t(cap - c.hp); c.hp = int16_t(cap); }
    c.temp_hp = 0;
    c.death = DeathSaves{};
    if (c.hit_dice_max > 0) {
        const int regain = std::max(1, c.hit_dice_max / 2);
        const int now = std::min(int(c.hit_dice_max), c.hit_dice + regain);
        r.hit_dice_regained = uint8_t(now - c.hit_dice);
        c.hit_dice = uint8_t(now);
    }
    recover_slots_long_rest(c);
    for (auto& is : c.innate) is.uses_left = is.uses_max;
    recharge_tables(c.id, true, t);
    return r;
}

bool recover_spell_slots(Combatant& c, const std::array<uint8_t, 10>& add, int max_total_levels, int max_slot_level) {
    int total = 0;
    for (int lvl = 1; lvl <= 9; ++lvl) {
        const std::size_t i = std::size_t(lvl);
        if (add[i] == 0) continue;
        if (lvl > max_slot_level) return false;
        if (int(c.spell_slots[i]) + add[i] > int(c.spell_slots_max[i])) return false;
        total += lvl * add[i];
    }
    if (total > max_total_levels) return false;
    for (std::size_t i = 1; i <= 9; ++i) c.spell_slots[i] = uint8_t(c.spell_slots[i] + add[i]);
    return true;
}

}  // namespace rules5e
