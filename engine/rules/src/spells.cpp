#include "rules5e/spells.hpp"

#include <algorithm>
#include "rules5e/abilities.hpp"

namespace rules5e {

bool SpellBook::add(SpellDef d) {
    auto it = std::lower_bound(defs_.begin(), defs_.end(), d.id,
                               [](const SpellDef& a, uint32_t id) { return a.id < id; });
    if (it != defs_.end() && it->id == d.id) return false;
    defs_.insert(it, std::move(d));
    return true;
}

bool SpellBook::erase(uint32_t id) {
    auto it = std::lower_bound(defs_.begin(), defs_.end(), id,
                               [](const SpellDef& a, uint32_t v) { return a.id < v; });
    if (it == defs_.end() || it->id != id) return false;
    defs_.erase(it);
    return true;
}

const SpellDef* SpellBook::find(uint32_t id) const {
    auto it = std::lower_bound(defs_.begin(), defs_.end(), id,
                               [](const SpellDef& a, uint32_t v) { return a.id < v; });
    return it != defs_.end() && it->id == id ? &*it : nullptr;
}

int spell_attack_bonus(const Combatant& c) {
    return c.spell_attack_override != 0 ? int(c.spell_attack_override) : c.proficiency_bonus + modifier(c.abilities, c.spellcasting_ability);
}
int spell_save_dc(const Combatant& c) {
    return c.spell_dc_override != 0 ? int(c.spell_dc_override) : 8 + c.proficiency_bonus + modifier(c.abilities, c.spellcasting_ability);
}
int spell_save_dc(const Combatant& c, const SpellDef& d) { return d.fixed_dc != 0 ? int(d.fixed_dc) : spell_save_dc(c); }
int spell_attack_bonus(const Combatant& c, const SpellDef& d) { return d.fixed_attack != 0 ? int(d.fixed_attack) : spell_attack_bonus(c); }

int cantrip_tier(const SpellDef& d, uint8_t caster_level) {
    if (d.level != 0 || !d.cantrip_scales) return 0;
    int n = 0;
    for (uint8_t t : d.cantrip_tiers) if (t != 0 && caster_level >= t) ++n;
    return n;
}

SpellDef effect_view(const SpellDef& base, const SpellEffect& e) {
    SpellDef v = base;
    v.extra_effects.clear();
    v.kind = e.kind; v.melee = e.melee; v.beams = e.beams;
    v.dice = e.dice; v.damage_type = e.damage_type;
    v.add_spellcasting_modifier = e.add_spellcasting_modifier; v.excluded_types = e.excluded_types;
    v.save_ability = e.save_ability; v.on_save = e.on_save;
    v.applies_condition = e.applies_condition; v.condition = e.condition; v.repeat_save = e.repeat_save;
    v.expiry = e.expiry; v.duration_rounds = e.duration_rounds; v.modifier = e.modifier;
    v.darts = e.darts; v.distribution = e.distribution; v.simultaneous = e.simultaneous;
    v.forced = e.forced; v.teleport = e.teleport;   // R-G9
    v.condition_untracked = e.condition_untracked;
    v.escape_dc = e.escape_dc;   // EC deferred monsters (flagged)
    v.contain = e.contain; v.contain_dice = e.contain_dice; v.contain_damage_type = e.contain_damage_type;   // 7.2w
    v.condition_max_size = e.condition_max_size;
    v.regurgitate_dc = e.regurgitate_dc; v.regurgitate_damage = e.regurgitate_damage; v.excludes_formless = e.excludes_formless;
    v.ends_on = e.ends_on;   // 7.2zc
    v.has_ward = e.has_ward; v.ward_save = e.ward_save; v.ward_on_fail = e.ward_on_fail;   // 7.2zd
    v.immune_conditions = e.immune_conditions; v.immune_from_types = e.immune_from_types;
    v.hp_bonus_flat = e.hp_bonus_flat; v.hp_bonus_per_slot = e.hp_bonus_per_slot;
    v.has_modifier_save = false;   // 7.2zg: the save gate is the first effect's (Slow's rows ride on the creatures that failed)
    v.object_ac = e.object_ac; v.object_hp = e.object_hp;   // EC spider follow-up (flagged)
    v.object_vulnerable = e.object_vulnerable; v.object_immune = e.object_immune;
    v.hit_only = e.hit_only;   // 7.2zi: a rider does not inherit the primary attack's hit_only
    v.dash_on_cast = e.dash_on_cast;   // 7.2zi: a rider does not inherit the primary effect's cast-turn Dash
    v.hostile_save_advantage = false; v.ends_on_ally_harm = false; v.suppress_conditions = 0;   // 7.2zj: primary only
    v.has_also_damage = false; v.also_dice = {}; v.also_damage_type = DamageType::Cold; v.difficult_until = 0;
    v.fly_ft = 0; v.restore_options.clear(); v.ends_on_restoration = false;
    v.charm_immune = false; v.caster_sees_target = false; v.requires_hearing = false;
    v.drop_to_zero = false; v.has_target_condition = false;   // B15: Death Glare is the primary save only
    v.charm_command = false; v.save_drain_max_hp = false;    // B16: Charm and Draining Kiss are the primary save only
    v.age_margin = 0; v.age_dice = {}; v.age_years_per = 0; v.immune_on_effect_end = false;   // B17: the visage is the primary save only
    v.has_while = false;   // B3b: while is the primary save's, not a rider effect's
    v.rider_only = false;
    v.requires_jumped_ft = 0; v.has_save_choice = false; v.leap_push_ft = 0;   // B8: the leap is the primary save only
    v.whirl_ft = 0; v.impact_per_ft = 0; v.impact_die = {}; v.impact_dc = 0;   // B10: the fling is the primary save only
    v.whelm = false; v.whelm_escape_dc = 0; v.whelm_max_size = -1; v.whelm_crush = {};   // B11: the grapple is the primary save only
    v.cantrip_scales = false; v.cantrip_adds_beams = false;
    v.upcast_beams = 0; v.upcast_dice = e.upcast_dice; v.upcast_darts = 0; v.upcast_targets = 0;   // upcast_dice: delayed_damage only
    return v;
}

DiceExpr scaled_dice(const SpellDef& d, uint8_t slot_level, uint8_t caster_level) {
    DiceExpr e = d.dice;
    if (e.count == 0) return e;
    int count = e.count;
    if (d.level == 0 && !d.cantrip_adds_beams) count += d.dice.count * cantrip_tier(d, caster_level);
    else if (slot_level > d.level) count += d.upcast_dice * (slot_level - d.level);
    e.count = uint8_t(std::min(count, 255));
    return e;
}

int scaled_darts(const SpellDef& d, uint8_t slot_level) {
    return d.darts + (slot_level > d.level ? d.upcast_darts * (slot_level - d.level) : 0);
}

int scaled_targets(const SpellDef& d, uint8_t slot_level) {
    return d.targets + (d.level > 0 && slot_level > d.level ? d.upcast_targets * (slot_level - d.level) : 0);
}

int scaled_beams(const SpellDef& d, uint8_t slot_level, uint8_t caster_level) {
    int n = d.beams;
    if (d.level == 0 && d.cantrip_adds_beams) n += cantrip_tier(d, caster_level);
    if (d.level > 0 && slot_level > d.level) n += d.upcast_beams * (slot_level - d.level);
    return n;
}

bool valid_target_type(const SpellDef& d, const Combatant& t) {
    return d.target_types == 0 || (d.target_types & bit(t.creature_type)) != 0;
}

bool has_slot(const Combatant& c, uint8_t level) {
    if (level == 0) return true;
    if (level > 9) return false;
    return c.spell_slots[level] > 0 || (c.pact_slot_level == level && c.pact_slots > 0);
}

bool spend_slot(Combatant& c, uint8_t level) {
    if (level == 0) return true;
    if (level > 9) return false;
    if (c.spell_slots[level] > 0) { --c.spell_slots[level]; return true; }
    if (c.pact_slot_level == level && c.pact_slots > 0) { --c.pact_slots; return true; }
    return false;
}

void recover_slots_short_rest(Combatant& c) { c.pact_slots = c.pact_slots_max; }
void recover_slots_long_rest(Combatant& c) { c.spell_slots = c.spell_slots_max; c.pact_slots = c.pact_slots_max; }

bool knows_spell(const Combatant& c, uint32_t id) {
    return std::find(c.spells_known.begin(), c.spells_known.end(), id) != c.spells_known.end();
}

} // namespace rules5e
