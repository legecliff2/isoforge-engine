#pragma once
// Spellcasting core (SRD 5.1 Chapter 10 "Spellcasting"). Pure data + functions; spell
// definitions come from content (spell@1, loaded by rules5e_content) or are built in code.
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "rules5e/types.hpp"

namespace rules5e {

// Components (SRD "Components"), as flags. Rules only record them; sim decides whether they can
// be provided (hooks.can_provide_components).
inline constexpr uint8_t kComponentV = 1, kComponentS = 2, kComponentM = 4;

enum class CastingTime : uint8_t { Action, BonusAction, Reaction };
// What the spell does. Append-only.
enum class SpellEffectKind : uint8_t {
    SpellAttack,    // spell attack roll vs AC, damage on a hit (Fire Bolt)
    Save,           // saving throw vs spell save DC: damage (full / half) and/or a condition (Hold Person)
    Heal,           // hit points regained (Cure Wounds)
    AutoHit,        // darts that hit automatically (Magic Missile)
    AcBonus,        // reaction: AC bonus until the start of the caster's next turn (Shield)
    Counter,        // reaction: interrupts a spell being cast (Counterspell)
};
enum class OnSave : uint8_t { None, Half };          // damage on a successful save
enum class RepeatSave : uint8_t { None, EndOfTurn };   // condition: target repeats the save

struct SpellDef {
    uint32_t id = 0;                     // spell_id_of(content_id)
    std::string content_id;              // "srd51.spell.fire_bolt"
    uint8_t level = 0;                   // 0 = cantrip
    CastingTime casting_time = CastingTime::Action;
    ReactionTrigger reaction_trigger = ReactionTrigger::BeingHit;   // Reaction spells only
    uint8_t components = 0;              // kComponentV | kComponentS | kComponentM
    bool concentration = false;
    uint16_t range_ft = 0;               // 0 = self; 5 = touch
    bool melee = false;                  // SpellAttack: melee spell attack (default ranged)
    uint8_t targets = 1;                 // creatures affected at base level (Save / Heal / SpellAttack)
    SpellEffectKind kind = SpellEffectKind::SpellAttack;
    DiceExpr dice{0, 0, 0};              // damage / healing / per-dart damage
    DamageType damage_type = DamageType::Force;
    bool add_spellcasting_modifier = false;  // Heal: + spellcasting ability modifier
    Ability save_ability = Ability::Wis; // Save
    OnSave on_save = OnSave::None;       // Save with damage
    bool applies_condition = false;      // Save: condition on a failed save
    Condition condition = Condition::Paralyzed;
    RepeatSave repeat_save = RepeatSave::None;
    uint8_t darts = 0;                   // AutoHit
    int8_t ac_bonus = 0;                 // AcBonus
    uint32_t blocks_spell = 0;           // AcBonus: also takes no damage from this spell (Shield vs Magic Missile)
    uint8_t counter_dc_base = 10;        // Counter: check DC = base + spell level when above the slot level
    // Scaling
    std::array<uint8_t, 3> cantrip_tiers{5, 11, 17};   // cantrip: +base dice at each caster level reached
    bool cantrip_scales = false;
    uint8_t upcast_dice = 0;             // + this many dice per slot level above `level`
    uint8_t upcast_darts = 0;            // AutoHit: + darts per slot level above
    uint8_t upcast_targets = 0;          // + targets per slot level above
};

// Owned by the encounter hooks (shared, immutable). Sorted by id.
class SpellBook {
public:
    // Returns false on a duplicate id (content id hash collision).
    bool add(SpellDef);
    const SpellDef* find(uint32_t id) const;
    const std::vector<SpellDef>& all() const { return defs_; }
private:
    std::vector<SpellDef> defs_;
};

// FNV-1a 32-bit of the content id: the numeric spell_id used in requests, events, and logs.
constexpr uint32_t spell_id_of(std::string_view content_id) {
    uint32_t h = 2166136261u;
    for (char c : content_id) { h ^= uint8_t(c); h *= 16777619u; }
    return h;
}

// SRD: spell save DC = 8 + proficiency bonus + spellcasting ability modifier;
// spell attack bonus = proficiency bonus + spellcasting ability modifier.
int spell_save_dc(const Combatant&);
int spell_attack_bonus(const Combatant&);
// Cantrip damage tier (0..3) for a caster level: number of entries in `tiers` reached.
int cantrip_tier(const SpellDef&, uint8_t caster_level);
// Damage/healing dice when cast with a slot of `slot_level` by a caster of `caster_level`.
DiceExpr scaled_dice(const SpellDef&, uint8_t slot_level, uint8_t caster_level);
int scaled_darts(const SpellDef&, uint8_t slot_level);
int scaled_targets(const SpellDef&, uint8_t slot_level);

// Slots: spell_slots[1..9] (long rest) and Pact Magic slots (short or long rest).
bool has_slot(const Combatant&, uint8_t level);         // level 0 (cantrip) is always true
bool spend_slot(Combatant&, uint8_t level);             // regular slot first, then a pact slot of that level
void recover_slots_short_rest(Combatant&);              // Pact Magic slots
void recover_slots_long_rest(Combatant&);               // all slots
bool knows_spell(const Combatant&, uint32_t spell_id);

} // namespace rules5e
