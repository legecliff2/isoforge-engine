#pragma once
// SRD spells built in code, mirroring content/packs/sample.vertical_slice/spells/*.json
// (rules5e_content_tests checks the JSON loads to the same definitions).
#include <memory>
#include "encounter_util.hpp"

namespace enc {

inline const uint32_t kFireBolt = spell_id_of("srd51.spell.fire_bolt");
inline const uint32_t kMagicMissile = spell_id_of("srd51.spell.magic_missile");
inline const uint32_t kCureWounds = spell_id_of("srd51.spell.cure_wounds");
inline const uint32_t kShield = spell_id_of("srd51.spell.shield");
inline const uint32_t kHoldPerson = spell_id_of("srd51.spell.hold_person");
inline const uint32_t kCounterspell = spell_id_of("srd51.spell.counterspell");

inline std::shared_ptr<SpellBook> srd_spellbook() {
    auto b = std::make_shared<SpellBook>();
    SpellDef fb; fb.content_id = "srd51.spell.fire_bolt"; fb.id = kFireBolt; fb.level = 0;
    fb.components = kComponentV | kComponentS; fb.range_ft = 120; fb.kind = SpellEffectKind::SpellAttack;
    fb.dice = {1, 10, 0}; fb.damage_type = DamageType::Fire; fb.cantrip_scales = true;
    b->add(fb);
    SpellDef mm; mm.content_id = "srd51.spell.magic_missile"; mm.id = kMagicMissile; mm.level = 1;
    mm.components = kComponentV | kComponentS; mm.range_ft = 120; mm.kind = SpellEffectKind::AutoHit;
    mm.darts = 3; mm.dice = {1, 4, 1}; mm.damage_type = DamageType::Force; mm.upcast_darts = 1;
    mm.distribution = DartDistribution::Any; mm.simultaneous = true;
    b->add(mm);
    SpellDef cw; cw.content_id = "srd51.spell.cure_wounds"; cw.id = kCureWounds; cw.level = 1;
    cw.components = kComponentV | kComponentS; cw.range_ft = 5; cw.kind = SpellEffectKind::Heal;
    cw.dice = {1, 8, 0}; cw.add_spellcasting_modifier = true; cw.upcast_dice = 1;
    cw.excluded_types = bit(CreatureType::Undead) | bit(CreatureType::Construct);
    b->add(cw);
    SpellDef sh; sh.content_id = "srd51.spell.shield"; sh.id = kShield; sh.level = 1;
    sh.casting_time = CastingTime::Reaction; sh.reaction_trigger = ReactionTrigger::BeingHit;
    sh.components = kComponentV | kComponentS; sh.kind = SpellEffectKind::AcBonus; sh.ac_bonus = 5;
    sh.blocks_spell = kMagicMissile; sh.or_targeted_by = {kMagicMissile};
    sh.duration = SpellDuration::UntilStartOfCasterTurn;
    b->add(sh);
    SpellDef hp; hp.content_id = "srd51.spell.hold_person"; hp.id = kHoldPerson; hp.level = 2;
    hp.components = kComponentV | kComponentS | kComponentM; hp.concentration = true; hp.range_ft = 60;
    hp.kind = SpellEffectKind::Save; hp.save_ability = Ability::Wis; hp.applies_condition = true;
    hp.condition = Condition::Paralyzed; hp.repeat_save = RepeatSave::EndOfTurn; hp.upcast_targets = 1;
    hp.target_types = bit(CreatureType::Humanoid); hp.max_spread_ft = 30;
    hp.duration = SpellDuration::Minutes; hp.duration_amount = 1;
    b->add(hp);
    SpellDef cs; cs.content_id = "srd51.spell.counterspell"; cs.id = kCounterspell; cs.level = 3;
    cs.casting_time = CastingTime::Reaction; cs.reaction_trigger = ReactionTrigger::SpellCast;
    cs.components = kComponentS; cs.range_ft = 60; cs.kind = SpellEffectKind::Counter; cs.requires_sight = true;
    b->add(cs);
    return b;
}

// Level-5 wizard: INT 16 (+3), proficiency +3 -> save DC 14, spell attack +6. Slots 4/3/2.
inline Combatant wizard(CombatantId id, uint8_t team = 0) {
    Combatant c = pc(id, 30);
    c.team = team;
    c.abilities.score[std::size_t(Ability::Int)] = 16;
    c.proficiency_bonus = 3;
    c.spellcasting_ability = Ability::Int;
    c.caster_level = 5;
    c.spell_slots_max = {0, 4, 3, 2, 0, 0, 0, 0, 0, 0};
    c.spell_slots = c.spell_slots_max;
    c.spells_known = {kFireBolt, kMagicMissile, kShield, kHoldPerson, kCounterspell};
    return c;
}

inline Combatant cleric(CombatantId id) {
    Combatant c = pc(id, 30);
    c.abilities.score[std::size_t(Ability::Wis)] = 16;
    c.spellcasting_ability = Ability::Wis;
    c.caster_level = 1;
    c.spell_slots_max = {0, 2, 0, 0, 0, 0, 0, 0, 0, 0};
    c.spell_slots = c.spell_slots_max;
    c.spells_known = {kCureWounds};
    return c;
}

inline EncounterHooks spell_hooks() {
    EncounterHooks h;
    h.spells = srd_spellbook();
    h.in_range = [](const EncounterState&, const ReactionWindow&, const Combatant&) { return true; };
    return h;
}

inline ActionRequest cast(CombatantId a, uint32_t spell, CombatantId target, uint8_t level = 0) {
    ActionRequest r{a, ActionKind::CastSpell, target};
    r.spell_id = spell; r.spell_level = level;
    return r;
}

inline void set_policy(Combatant& c, ReactionTrigger t, ReactionPolicy p) { c.reaction_policy[std::size_t(t)] = p; }
inline void never_react(Combatant& c) { c.reaction_policy.fill(ReactionPolicy::Never); }

} // namespace enc
