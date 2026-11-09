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

// Batch 3 shadow Strength Drain: "the reduction lasts until the target finishes a short or long rest".
void restore_ability_reduction(Combatant& c) {
    for (std::size_t i = 0; i < 6; ++i) {
        c.abilities.score[i] = int8_t(std::min(127, int(c.abilities.score[i]) + int(c.ability_reduction[i])));
        c.ability_reduction[i] = 0;
    }
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
    restore_ability_reduction(c);
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
    c.hp_max_reduction = 0;   // Life Drain lasts until a long rest. A bite disease's reduction does not (CONTENT_CONVENTIONS §36).
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
    restore_ability_reduction(c);
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

FeatureUses slot_recovery_uses(CombatantId actor, const SlotRecoverySpec& s) {
    FeatureUses f;
    f.actor = actor;
    f.feature_id = s.feature_id;
    f.max = s.uses;
    f.recharge = FeatureRecharge::LongRest;
    return f;
}

SlotRecoveryResult apply_slot_recovery(Combatant& c, const SlotRecoverySpec& s, std::vector<FeatureUses>& features,
                                       const std::array<uint8_t, 10>& add) {
    auto row = std::find_if(features.begin(), features.end(),
                            [&](const FeatureUses& f) { return f.actor == c.id && f.feature_id == s.feature_id; });
    if (row == features.end() || s.class_level == 0) return SlotRecoveryResult::NoFeature;
    if (row->max != 0 && row->used >= row->max) return SlotRecoveryResult::NoUsesLeft;
    int total = 0;
    for (std::size_t i = 1; i <= 9; ++i) {
        if (add[i] == 0) continue;
        if (int(i) > int(s.max_slot_level)) return SlotRecoveryResult::LevelTooHigh;
        if (int(c.spell_slots[i]) + int(add[i]) > int(c.spell_slots_max[i])) return SlotRecoveryResult::AboveMax;
        total += int(i) * int(add[i]);
    }
    if (total == 0) return SlotRecoveryResult::Empty;
    if (total > slot_recovery_budget(s)) return SlotRecoveryResult::OverBudget;
    if (!recover_spell_slots(c, add, slot_recovery_budget(s), int(s.max_slot_level))) return SlotRecoveryResult::AboveMax;
    row->used = uint8_t(row->used + 1);
    return SlotRecoveryResult::Ok;
}

}  // namespace rules5e
